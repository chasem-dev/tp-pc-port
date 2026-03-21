/* pc_os.cpp - Dolphin OS replacement: arena, timers, threads, message queues */
#include "pc_platform.h"
#include <time.h>
#include <pthread.h>
#include <dolphin/os/OSContext.h>
#include <dolphin/os/OSThread.h>
#include <dolphin/os/OSMessage.h>
#include <dolphin/os/OSMutex.h>

extern "C" {

/* --- Memory arena --- */
static u8* arena_memory = NULL;
static u8* arena_lo = NULL;
static u8* arena_hi = NULL;

void* OSGetArenaLo(void) { return arena_lo; }
void* OSGetArenaHi(void) { return arena_hi; }
void  OSSetArenaLo(void* lo) { arena_lo = (u8*)lo; }
void  OSSetArenaHi(void* hi) { arena_hi = (u8*)hi; }

void* OSAllocFromArenaLo(u32 size, u32 align) {
    u8* ptr = (u8*)(((uintptr_t)arena_lo + align - 1) & ~(uintptr_t)(align - 1));
    arena_lo = ptr + size;
    return ptr;
}

void pc_os_init_arena(void) {
    arena_memory = (u8*)malloc(PC_MAIN_MEMORY_SIZE);
    if (!arena_memory) {
        fprintf(stderr, "Failed to allocate %d MB arena\n", PC_MAIN_MEMORY_SIZE / (1024*1024));
        exit(1);
    }
    memset(arena_memory, 0, PC_MAIN_MEMORY_SIZE);
    arena_lo = arena_memory;
    arena_hi = arena_memory + PC_MAIN_MEMORY_SIZE;
    printf("[PC] Arena allocated: %d MB at %p\n", PC_MAIN_MEMORY_SIZE / (1024*1024), arena_memory);
}

/* --- Time/clock --- */
static u64 time_base_start = 0;
static s64 gc_epoch_offset_ticks = 0;

#define GC_UNIX_EPOCH_DIFF 946684800LL

void pc_os_init_time(void) {
    time_base_start = SDL_GetPerformanceCounter();
    time_t now = time(NULL);
    s64 gc_secs = (s64)now - GC_UNIX_EPOCH_DIFF;
    gc_epoch_offset_ticks = gc_secs * (s64)GC_TIMER_CLOCK;
}

s64 OSGetTime(void) {
    u64 now = SDL_GetPerformanceCounter();
    u64 freq = SDL_GetPerformanceFrequency();
    s64 elapsed = (s64)((now - time_base_start) * (u64)GC_TIMER_CLOCK / freq);
    return gc_epoch_offset_ticks + elapsed;
}

s64 __OSGetSystemTime(void) { return OSGetTime(); }

u32 OSGetTick(void) { return (u32)OSGetTime(); }

/* --- Calendar time --- */
#define OS_TIMER_CLOCK          GC_TIMER_CLOCK
#define OSSecondsToTicks(sec)   ((s64)(sec) * (s64)OS_TIMER_CLOCK)
#define OSTicksToSeconds(ticks) ((s64)(ticks) / (s64)OS_TIMER_CLOCK)

typedef struct {
    int sec, min, hour, mday, mon, year, wday, yday, msec, usec;
} OSCalendarTime;

void OSTicksToCalendarTime(s64 ticks, OSCalendarTime* td) {
    s64 secs = ticks / (s64)OS_TIMER_CLOCK;
    s64 remainder = ticks % (s64)OS_TIMER_CLOCK;
    if (remainder < 0) { secs--; remainder += OS_TIMER_CLOCK; }

    td->usec = (int)((remainder * 1000000LL) / OS_TIMER_CLOCK % 1000);
    td->msec = (int)((remainder * 1000LL) / OS_TIMER_CLOCK % 1000);

    /* Convert GC epoch seconds to calendar */
    time_t unix_time = (time_t)(secs + GC_UNIX_EPOCH_DIFF);
    struct tm* t = localtime(&unix_time);
    if (t) {
        td->sec = t->tm_sec;
        td->min = t->tm_min;
        td->hour = t->tm_hour;
        td->mday = t->tm_mday;
        td->mon = t->tm_mon + 1;
        td->year = t->tm_year + 1900;
        td->wday = t->tm_wday;
        td->yday = t->tm_yday;
    }
}

s64 OSCalendarTimeToTicks(OSCalendarTime* td) {
    struct tm t = {};
    t.tm_sec = td->sec;
    t.tm_min = td->min;
    t.tm_hour = td->hour;
    t.tm_mday = td->mday;
    t.tm_mon = td->mon - 1;
    t.tm_year = td->year - 1900;
    time_t unix_time = mktime(&t);
    s64 gc_secs = (s64)unix_time - GC_UNIX_EPOCH_DIFF;
    return gc_secs * (s64)OS_TIMER_CLOCK;
}

/* --- Thread implementation (pthreads) --- */
typedef void* (*OSThreadFunc)(void*);

/* Side-table mapping OSThread* to pthread info (GC thread struct is too small
 * to store a pthread_t, and we can't change its layout). */
struct PCThreadEntry {
    OSThread* os_thread;
    OSThreadFunc func;
    void* param;
    pthread_t pthread;
    int created;   /* pthread_create called */
    int terminated;
};
#define PC_MAX_THREADS 16
static PCThreadEntry pc_threads[PC_MAX_THREADS];
static pthread_mutex_t pc_thread_table_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Dummy OSThread for the main thread */
static OSThread pc_main_thread;

static PCThreadEntry* pc_thread_find(OSThread* t) {
    for (int i = 0; i < PC_MAX_THREADS; i++)
        if (pc_threads[i].os_thread == t) return &pc_threads[i];
    return NULL;
}

static PCThreadEntry* pc_thread_alloc(OSThread* t) {
    for (int i = 0; i < PC_MAX_THREADS; i++) {
        if (pc_threads[i].os_thread == NULL) {
            memset(&pc_threads[i], 0, sizeof(PCThreadEntry));
            pc_threads[i].os_thread = t;
            return &pc_threads[i];
        }
    }
    fprintf(stderr, "[PC] ERROR: pc_threads table full!\n");
    return NULL;
}

/* Wrapper so we can mark the thread as terminated when it exits */
struct PCThreadStartArg {
    OSThreadFunc func;
    void* param;
    PCThreadEntry* entry;
};

static void* pc_thread_wrapper(void* arg) {
    PCThreadStartArg* sa = (PCThreadStartArg*)arg;
    OSThreadFunc func = sa->func;
    void* param = sa->param;
    PCThreadEntry* entry = sa->entry;
    free(sa);

    func(param);

    entry->terminated = 1;
    return NULL;
}

int OSCreateThread(OSThread* thread, OSThreadFunc func, void* param,
                   void* stack, u32 stackSize, s32 priority, u16 attr) {
    (void)stack; (void)stackSize; (void)priority; (void)attr;
    pthread_mutex_lock(&pc_thread_table_mtx);
    PCThreadEntry* e = pc_thread_alloc(thread);
    if (e) {
        e->func = func;
        e->param = param;
        e->terminated = 0;
        e->created = 0;
    }
    pthread_mutex_unlock(&pc_thread_table_mtx);
    if (g_pc_verbose) fprintf(stderr, "[PC] OSCreateThread %p (func=%p)\n", (void*)thread, (void*)(uintptr_t)func);
    return 1;
}

s32 OSResumeThread(OSThread* thread) {
    pthread_mutex_lock(&pc_thread_table_mtx);
    PCThreadEntry* e = pc_thread_find(thread);
    if (e && !e->created) {
        e->created = 1;
        PCThreadStartArg* sa = (PCThreadStartArg*)malloc(sizeof(PCThreadStartArg));
        sa->func = e->func;
        sa->param = e->param;
        sa->entry = e;
        pthread_create(&e->pthread, NULL, pc_thread_wrapper, sa);
        pthread_detach(e->pthread);
        if (g_pc_verbose) fprintf(stderr, "[PC] OSResumeThread %p — launched pthread\n", (void*)thread);
    }
    pthread_mutex_unlock(&pc_thread_table_mtx);
    return 0;
}

s32 OSSuspendThread(OSThread* thread) {
    (void)thread;
    /* On GC, OSSuspendThread suspends the specified thread. If a thread suspends
     * itself, it blocks until resumed. Approximate with a long sleep. */
    SDL_Delay(100);
    return 0;
}
void OSCancelThread(OSThread* thread) { (void)thread; }
void OSDetachThread(OSThread* thread) { (void)thread; }

int OSIsThreadTerminated(OSThread* thread) {
    pthread_mutex_lock(&pc_thread_table_mtx);
    PCThreadEntry* e = pc_thread_find(thread);
    int ret = e ? e->terminated : 1;
    pthread_mutex_unlock(&pc_thread_table_mtx);
    return ret;
}

int OSIsThreadSuspended(OSThread* thread) {
    pthread_mutex_lock(&pc_thread_table_mtx);
    PCThreadEntry* e = pc_thread_find(thread);
    int ret = (e && !e->created) ? 1 : 0;
    pthread_mutex_unlock(&pc_thread_table_mtx);
    return ret;
}

static thread_local OSThread pc_tls_thread;
static thread_local int pc_tls_thread_init = 0;

OSThread* OSGetCurrentThread(void) {
    if (!pc_tls_thread_init) {
        memset(&pc_tls_thread, 0, sizeof(OSThread));
        pc_tls_thread_init = 1;
    }
    return &pc_tls_thread;
}
s32 OSGetThreadPriority(OSThread* thread) { (void)thread; return 16; }
int OSSetThreadPriority(OSThread* thread, s32 priority) { (void)thread; (void)priority; return 0; }
s32 OSEnableScheduler(void) { return 0; }
s32 OSDisableScheduler(void) { return 0; }
void OSYieldThread(void) { sched_yield(); }
void OSSleepThread(OSThreadQueue* queue) { (void)queue; }
void OSWakeupThread(OSThreadQueue* queue) { (void)queue; }
s32 OSCheckActiveThreads(void) { return 1; }
void OSFillFPUContext(OSContext* context) { (void)context; }

/* --- Mutex (pthreads) --- */
/* Side-table for real pthread mutexes keyed by OSMutex* */
struct PCMutexEntry {
    OSMutex* os_mutex;
    pthread_mutex_t pmtx;
    int active;
};
#define PC_MAX_MUTEXES 32
static PCMutexEntry pc_mutexes[PC_MAX_MUTEXES];
static pthread_mutex_t pc_mutex_table_mtx = PTHREAD_MUTEX_INITIALIZER;

static PCMutexEntry* pc_mutex_find(OSMutex* m) {
    for (int i = 0; i < PC_MAX_MUTEXES; i++)
        if (pc_mutexes[i].active && pc_mutexes[i].os_mutex == m) return &pc_mutexes[i];
    return NULL;
}

static PCMutexEntry* pc_mutex_find_or_create(OSMutex* m) {
    PCMutexEntry* e = pc_mutex_find(m);
    if (e) return e;
    for (int i = 0; i < PC_MAX_MUTEXES; i++) {
        if (!pc_mutexes[i].active) {
            pc_mutexes[i].os_mutex = m;
            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&pc_mutexes[i].pmtx, &attr);
            pthread_mutexattr_destroy(&attr);
            pc_mutexes[i].active = 1;
            return &pc_mutexes[i];
        }
    }
    return NULL;
}

void OSInitMutex(OSMutex* mutex) {
    pthread_mutex_lock(&pc_mutex_table_mtx);
    pc_mutex_find_or_create(mutex);
    pthread_mutex_unlock(&pc_mutex_table_mtx);
}

void OSLockMutex(OSMutex* mutex) {
    pthread_mutex_lock(&pc_mutex_table_mtx);
    PCMutexEntry* e = pc_mutex_find_or_create(mutex);
    pthread_mutex_unlock(&pc_mutex_table_mtx);
    if (e) pthread_mutex_lock(&e->pmtx);
}

void OSUnlockMutex(OSMutex* mutex) {
    pthread_mutex_lock(&pc_mutex_table_mtx);
    PCMutexEntry* e = pc_mutex_find(mutex);
    pthread_mutex_unlock(&pc_mutex_table_mtx);
    if (e) pthread_mutex_unlock(&e->pmtx);
}

int OSTryLockMutex(OSMutex* mutex) {
    pthread_mutex_lock(&pc_mutex_table_mtx);
    PCMutexEntry* e = pc_mutex_find_or_create(mutex);
    pthread_mutex_unlock(&pc_mutex_table_mtx);
    if (e) return pthread_mutex_trylock(&e->pmtx) == 0 ? 1 : 0;
    return 1;
}

/* --- Message queue (pthreads condvar) --- */
struct PCMsgQueueEntry {
    OSMessageQueue* os_mq;
    pthread_mutex_t mtx;
    pthread_cond_t cond_send;
    pthread_cond_t cond_recv;
    int active;
};
#define PC_MAX_MSG_QUEUES 32
static PCMsgQueueEntry pc_msg_queues[PC_MAX_MSG_QUEUES];
static pthread_mutex_t pc_mq_table_mtx = PTHREAD_MUTEX_INITIALIZER;

static PCMsgQueueEntry* pc_mq_find(OSMessageQueue* mq) {
    for (int i = 0; i < PC_MAX_MSG_QUEUES; i++)
        if (pc_msg_queues[i].active && pc_msg_queues[i].os_mq == mq) return &pc_msg_queues[i];
    return NULL;
}

static PCMsgQueueEntry* pc_mq_find_or_create(OSMessageQueue* mq) {
    PCMsgQueueEntry* e = pc_mq_find(mq);
    if (e) return e;
    for (int i = 0; i < PC_MAX_MSG_QUEUES; i++) {
        if (!pc_msg_queues[i].active) {
            pc_msg_queues[i].os_mq = mq;
            pthread_mutex_init(&pc_msg_queues[i].mtx, NULL);
            pthread_cond_init(&pc_msg_queues[i].cond_send, NULL);
            pthread_cond_init(&pc_msg_queues[i].cond_recv, NULL);
            pc_msg_queues[i].active = 1;
            return &pc_msg_queues[i];
        }
    }
    fprintf(stderr, "[PC] ERROR: pc_msg_queues table full!\n");
    return NULL;
}

void OSInitMessageQueue(OSMessageQueue* mq, void* msgArray, s32 msgCount) {
    mq->msgArray = msgArray;
    mq->msgCount = msgCount;
    mq->firstIndex = 0;
    mq->usedCount = 0;
    pthread_mutex_lock(&pc_mq_table_mtx);
    pc_mq_find_or_create(mq);
    pthread_mutex_unlock(&pc_mq_table_mtx);
}

int OSSendMessage(OSMessageQueue* mq, void* msg, s32 flags) {
    pthread_mutex_lock(&pc_mq_table_mtx);
    PCMsgQueueEntry* e = pc_mq_find_or_create(mq);
    pthread_mutex_unlock(&pc_mq_table_mtx);
    if (!e) return 0;

    pthread_mutex_lock(&e->mtx);
    if (flags == OS_MESSAGE_BLOCK) {
        while (mq->usedCount >= mq->msgCount)
            pthread_cond_wait(&e->cond_send, &e->mtx);
    } else if (mq->usedCount >= mq->msgCount) {
        pthread_mutex_unlock(&e->mtx);
        return 0;
    }

    s32 idx = (mq->firstIndex + mq->usedCount) % mq->msgCount;
    ((OSMessage*)mq->msgArray)[idx] = msg;
    mq->usedCount++;

    pthread_cond_signal(&e->cond_recv);
    pthread_mutex_unlock(&e->mtx);
    return 1;
}

int OSReceiveMessage(OSMessageQueue* mq, void* msg, s32 flags) {
    void** pmsg = (void**)msg;
    pthread_mutex_lock(&pc_mq_table_mtx);
    PCMsgQueueEntry* e = pc_mq_find_or_create(mq);
    pthread_mutex_unlock(&pc_mq_table_mtx);
    if (!e) return 0;

    pthread_mutex_lock(&e->mtx);
    if (flags == OS_MESSAGE_BLOCK) {
        while (mq->usedCount <= 0)
            pthread_cond_wait(&e->cond_recv, &e->mtx);
    } else if (mq->usedCount <= 0) {
        pthread_mutex_unlock(&e->mtx);
        return 0;
    }

    if (pmsg) *pmsg = ((OSMessage*)mq->msgArray)[mq->firstIndex];
    mq->firstIndex = (mq->firstIndex + 1) % mq->msgCount;
    mq->usedCount--;

    pthread_cond_signal(&e->cond_send);
    pthread_mutex_unlock(&e->mtx);
    return 1;
}

int OSJamMessage(OSMessageQueue* mq, void* msg, s32 flags) {
    pthread_mutex_lock(&pc_mq_table_mtx);
    PCMsgQueueEntry* e = pc_mq_find_or_create(mq);
    pthread_mutex_unlock(&pc_mq_table_mtx);
    if (!e) return 0;

    pthread_mutex_lock(&e->mtx);
    if (flags == OS_MESSAGE_BLOCK) {
        while (mq->usedCount >= mq->msgCount)
            pthread_cond_wait(&e->cond_send, &e->mtx);
    } else if (mq->usedCount >= mq->msgCount) {
        pthread_mutex_unlock(&e->mtx);
        return 0;
    }

    /* Jam inserts at front */
    mq->firstIndex = (mq->firstIndex - 1 + mq->msgCount) % mq->msgCount;
    ((OSMessage*)mq->msgArray)[mq->firstIndex] = msg;
    mq->usedCount++;

    pthread_cond_signal(&e->cond_recv);
    pthread_mutex_unlock(&e->mtx);
    return 1;
}

/* --- Interrupt stubs --- */
int OSDisableInterrupts(void) { return 0; }
int OSEnableInterrupts(void) { return 0; }
int OSRestoreInterrupts(int level) { (void)level; return 0; }

/* --- Cache stubs --- */
void DCFlushRange(void* addr, u32 nBytes) { (void)addr; (void)nBytes; }
void DCStoreRange(void* addr, u32 nBytes) { (void)addr; (void)nBytes; }
void DCInvalidateRange(void* addr, u32 nBytes) { (void)addr; (void)nBytes; }
void DCFlushRangeNoSync(void* addr, u32 nBytes) { (void)addr; (void)nBytes; }
void DCStoreRangeNoSync(void* addr, u32 nBytes) { (void)addr; (void)nBytes; }
void DCZeroRange(void* addr, u32 nBytes) { if (addr && nBytes) memset(addr, 0, nBytes); }
void ICInvalidateRange(void* addr, u32 nBytes) { (void)addr; (void)nBytes; }
void LCEnable(void) {}
void LCDisable(void) {}
void LCStoreBlocks(void* dst, void* src, u32 nBlocks) { (void)dst; (void)src; (void)nBlocks; }
void LCLoadBlocks(void* dst, void* src, u32 nBlocks) { (void)dst; (void)src; (void)nBlocks; }
u32 LCStoreData(void* dst, void* src, u32 nBytes) { (void)dst; (void)src; (void)nBytes; return 0; }
void LCQueueWait(u32 len) { (void)len; }

/* --- Heap API --- */
s32 OSSetCurrentHeap(s32 heap) { (void)heap; return 0; }
void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps) {
    (void)arenaStart; (void)arenaEnd; (void)maxHeaps;
    return arenaStart;
}
s32 OSCreateHeap(void* start, void* end) { (void)start; (void)end; return 0; }
void* OSAllocFromHeap(s32 heap, u32 size) { (void)heap; return malloc(size); }
void OSFreeToHeap(s32 heap, void* ptr) { (void)heap; free(ptr); }

/* --- OSReport --- */
void OSReport(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void OSReport_Error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void OSReport_Warning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void OSReportInit(void) {}
void OSReportDisable(void) {}
void OSReportEnable(void) {}

void OSPanic(const char* file, int line, const char* fmt, ...) {
    fprintf(stderr, "PANIC at %s:%d: ", file, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(1);
}

/* --- OS info --- */
typedef struct {
    char gameName[4];
    char company[2];
    u8 diskNumber;
    u8 gameVersion;
    u8 streaming;
    u8 streamingBufSize;
    u8 padding[22];
} DVDDiskID;

/* DVDGetCurrentDiskID moved to pc_dvd.cpp — reads from disc image */

u32 OSGetConsoleType(void) { return 0x10000006; /* retail GC */ }
u32 OSGetResetCode(void) { return 0; }

/* --- Additional OS stubs --- */
void OSInit(void) {
    pc_os_init_arena();
    pc_os_init_time();
    printf("[PC] OSInit complete\n");
}

typedef struct OSAlarm { u8 data[48]; } OSAlarm;
void OSCreateAlarm(OSAlarm* alarm) { (void)alarm; }
void OSSetAlarm(OSAlarm* alarm, s64 tick, void* handler) { (void)alarm; (void)tick; (void)handler; }
void OSSetPeriodicAlarm(OSAlarm* alarm, s64 start, s64 period, void* handler) {
    (void)alarm; (void)start; (void)period; (void)handler;
}
void OSCancelAlarm(OSAlarm* alarm) { (void)alarm; }

void OSExitThread(void* val) {
    (void)val;
    /* Mark thread as terminated in side-table, then exit the pthread */
    pthread_mutex_lock(&pc_thread_table_mtx);
    pthread_t self = pthread_self();
    for (int i = 0; i < PC_MAX_THREADS; i++) {
        if (pc_threads[i].created && pthread_equal(pc_threads[i].pthread, self)) {
            pc_threads[i].terminated = 1;
            break;
        }
    }
    pthread_mutex_unlock(&pc_thread_table_mtx);
    pthread_exit(NULL);
}

void OSInitThreadQueue(OSThreadQueue* queue) {
    if (queue) { queue->head = NULL; queue->tail = NULL; }
}
u32 OSGetStackPointer(void) { return 0; /* not meaningful on PC */ }
static OSContext pc_dummy_context;
OSContext* OSGetCurrentContext(void) { return &pc_dummy_context; }
void OSSetCurrentContext(OSContext* ctx) { (void)ctx; }
void OSClearContext(OSContext* ctx) { if (ctx) memset(ctx, 0, sizeof(OSContext)); }
u32 OSGetResetButtonState(void) { return 0; }
u32 OSGetResetSwitchState(void) { return 0; }
void OSResetSystem(u32 reset, u32 resetCode, u32 forceMenu) { (void)reset; (void)resetCode; (void)forceMenu; exit(0); }
void OSSetSaveRegion(void* start, u32 size) { (void)start; (void)size; }
void OSSetStringTable(const void* table) { (void)table; }
OSSwitchThreadCallback OSSetSwitchThreadCallback(OSSwitchThreadCallback cb) { (void)cb; return NULL; }
/* OS condition variables — backed by pthread condvars */
struct PCCondEntry {
    OSCond* os_cond;
    pthread_cond_t pcond;
    int active;
};
#define PC_MAX_CONDS 16
static PCCondEntry pc_conds[PC_MAX_CONDS];
static pthread_mutex_t pc_cond_table_mtx = PTHREAD_MUTEX_INITIALIZER;

static PCCondEntry* pc_cond_find_or_create(OSCond* c) {
    for (int i = 0; i < PC_MAX_CONDS; i++)
        if (pc_conds[i].active && pc_conds[i].os_cond == c) return &pc_conds[i];
    for (int i = 0; i < PC_MAX_CONDS; i++) {
        if (!pc_conds[i].active) {
            pc_conds[i].os_cond = c;
            pthread_cond_init(&pc_conds[i].pcond, NULL);
            pc_conds[i].active = 1;
            return &pc_conds[i];
        }
    }
    return NULL;
}

void OSInitCond(OSCond* cond) {
    pthread_mutex_lock(&pc_cond_table_mtx);
    pc_cond_find_or_create(cond);
    pthread_mutex_unlock(&pc_cond_table_mtx);
}

void OSSignalCond(OSCond* cond) {
    pthread_mutex_lock(&pc_cond_table_mtx);
    PCCondEntry* e = pc_cond_find_or_create(cond);
    pthread_mutex_unlock(&pc_cond_table_mtx);
    if (e) pthread_cond_signal(&e->pcond);
}

void OSWaitCond(OSCond* cond, OSMutex* mutex) {
    pthread_mutex_lock(&pc_cond_table_mtx);
    PCCondEntry* ce = pc_cond_find_or_create(cond);
    pthread_mutex_unlock(&pc_cond_table_mtx);
    pthread_mutex_lock(&pc_mutex_table_mtx);
    PCMutexEntry* me = pc_mutex_find_or_create(mutex);
    pthread_mutex_unlock(&pc_mutex_table_mtx);
    if (ce && me) pthread_cond_wait(&ce->pcond, &me->pmtx);
}
u8 __OSFpscrEnableBits = 0;
} /* extern "C" */

/* OSSwitchFiberEx is declared with C++ linkage in dolphin/os.h
 * (before the extern "C" block). Provide a C++ linkage definition. */
void OSSwitchFiberEx(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
