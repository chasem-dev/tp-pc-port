/* pc_main.cpp - PC entry point: SDL2/GL init, boot sequence */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "pc_platform.h"
#include "pc_gx_internal.h"
#ifndef _WIN32
#include <unistd.h>
#include <pthread.h>
#include <execinfo.h>
#include <sys/ucontext.h>
#include <sys/time.h>
#endif

#ifdef __APPLE__
extern "C" void pc_macos_init(void);
extern "C" void pc_macos_pump_events(void);
#endif

/* prefer discrete GPU on laptops */
#ifdef _WIN32
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif


SDL_Window*   g_pc_window = NULL;
SDL_GLContext  g_pc_gl_context = NULL;
int           g_pc_running = 1;
int           g_pc_no_framelimit = 0;
int           g_pc_fast_forward = 0;
int           g_pc_verbose = 0;
int           g_pc_window_w = PC_SCREEN_WIDTH;
int           g_pc_window_h = PC_SCREEN_HEIGHT;
static SDL_threadID  g_pc_main_thread_id = 0;
static SDL_threadID  g_pc_gl_owner_thread_id = 0;
static SDL_threadID  g_pc_render_thread_id = 0;
static SDL_mutex* g_pc_gl_owner_mutex = NULL;
#ifndef _WIN32
static pthread_t g_pc_main_pthread = (pthread_t)0;
static volatile int g_pc_main_pthread_valid = 0;  /* async-signal-safe flag */
#endif

void pc_platform_update_window_size(void) {
    SDL_GL_GetDrawableSize(g_pc_window, &g_pc_window_w, &g_pc_window_h);
    if (g_pc_window_w <= 0) g_pc_window_w = PC_SCREEN_WIDTH;
    if (g_pc_window_h <= 0) g_pc_window_h = PC_SCREEN_HEIGHT;
}

int pc_platform_is_main_thread(void) {
    return g_pc_main_thread_id != 0 && SDL_ThreadID() == g_pc_main_thread_id;
}

int pc_platform_is_gl_thread(void) {
    return g_pc_gl_owner_thread_id != 0 && SDL_ThreadID() == g_pc_gl_owner_thread_id;
}

int pc_platform_is_render_thread(void) {
    return g_pc_render_thread_id != 0 && SDL_ThreadID() == g_pc_render_thread_id;
}

void pc_platform_mark_render_thread(void) {
    const SDL_threadID current_thread = SDL_ThreadID();
    if (g_pc_render_thread_id == current_thread) {
        return;
    }

    if (g_pc_verbose) {
        fprintf(stderr, "[PC] render thread set to %u (previous=%u)\n",
                current_thread, g_pc_render_thread_id);
    }
    g_pc_render_thread_id = current_thread;
}

void pc_platform_ensure_gl_context_current(void) {
    /* Like AC's approach: GL context stays on the main thread forever.
     * Never call SDL_GL_MakeCurrent after init — doing so triggers Metal's
     * pipeline cache invalidation on macOS ARM64, causing MemoryPoolDecay
     * thread crashes (EXC_ARM_DA_ALIGN in AGXMetalG16G_B0). */
    if (!g_pc_window || !g_pc_gl_context) {
        return;
    }
    /* If context was released somehow, re-acquire it once */
    if (g_pc_gl_owner_thread_id == 0) {
        SDL_GL_MakeCurrent(g_pc_window, g_pc_gl_context);
        g_pc_gl_owner_thread_id = SDL_ThreadID();
        fprintf(stderr, "[PC] GL context acquired by thread %u\n", g_pc_gl_owner_thread_id);
    }
}

/* --- Crash protection --- */
/* Single-threaded mode: simple global jmpbuf */
static jmp_buf* pc_active_jmpbuf = NULL;
static volatile uintptr_t pc_last_crash_addr = 0;

#ifdef _WIN32
static LONG WINAPI pc_veh_handler(PEXCEPTION_POINTERS ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (pc_active_jmpbuf != NULL &&
        (code == EXCEPTION_ACCESS_VIOLATION ||
         code == EXCEPTION_ILLEGAL_INSTRUCTION ||
         code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
         code == EXCEPTION_PRIV_INSTRUCTION)) {
        pc_last_crash_addr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
        jmp_buf* buf = pc_active_jmpbuf;
        pc_active_jmpbuf = NULL;
        longjmp(*buf, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void pc_signal_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)ucontext;
    /* longjmp if crash protection is active */
    if (pc_active_jmpbuf != NULL) {
        pc_last_crash_addr = (uintptr_t)info->si_addr;
        jmp_buf* buf = pc_active_jmpbuf;
        pc_active_jmpbuf = NULL;
        longjmp(*buf, 1);
    }
    /* No BG crash absorption — VI callback fix prevents the Metal crash.
     * Let BG crashes be fatal (matches AC). */
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

void pc_crash_protection_init(void) {
    static int installed = 0;
    if (!installed) {
#ifdef _WIN32
        AddVectoredExceptionHandler(1, pc_veh_handler);
#else
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = pc_signal_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
        /* Also handle SIGALRM for GL draw call timeouts */
        sigaction(SIGALRM, &sa, NULL);
#endif
        installed = 1;
    }
}

void pc_crash_set_jmpbuf(jmp_buf* buf) { pc_active_jmpbuf = buf; }
uintptr_t pc_crash_get_addr(void) { return pc_last_crash_addr; }

/* GL draw call timeout — set an itimer that fires SIGALRM if a GL call hangs */
void pc_gl_timeout_start(jmp_buf* buf, int timeout_ms) {
    pc_active_jmpbuf = buf;
#ifndef _WIN32
    struct itimerval timer;
    timer.it_value.tv_sec = timeout_ms / 1000;
    timer.it_value.tv_usec = (timeout_ms % 1000) * 1000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
#endif
}
void pc_gl_timeout_stop(void) {
#ifndef _WIN32
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_REAL, &timer, NULL);
#endif
    pc_active_jmpbuf = NULL;
}

/* --- Platform init --- */
void pc_platform_init(void) {
#ifdef _WIN32
    SetProcessDPIAware();
#endif
#ifdef __APPLE__
    /* On macOS, SDL/GL context was already created by the early constructor
     * in pc_macos.m (priority 101, runs before game's 447 C++ constructors).
     * This prevents the CA::Fence::Observer crash caused by heap fragmentation.
     * Just init the remaining subsystems. */
    if (g_pc_window && g_pc_gl_context) {
        fprintf(stderr, "[PC] Using early-init SDL/GL context\n");
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
        SDL_InitSubSystem(SDL_INIT_AUDIO);
    } else
#endif
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            exit(1);
        }
    }

    if (!g_pc_window || !g_pc_gl_context) {
        /* Normal path — create window and GL context */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
        g_pc_window = SDL_CreateWindow(
            PC_WINDOW_TITLE,
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            PC_SCREEN_WIDTH * 2, PC_SCREEN_HEIGHT * 2, flags
        );
        if (!g_pc_window) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            SDL_Quit();
            exit(1);
        }

        g_pc_gl_context = SDL_GL_CreateContext(g_pc_window);
        if (!g_pc_gl_context) {
            fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(g_pc_window);
            SDL_Quit();
            exit(1);
        }
    }

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGL failed\n");
        SDL_GL_DeleteContext(g_pc_gl_context);
        SDL_DestroyWindow(g_pc_window);
        SDL_Quit();
        exit(1);
    }

    SDL_GL_SetSwapInterval(0); /* Manual frame pacing in VIWaitForRetrace */
    pc_platform_update_window_size();
    g_pc_gl_owner_thread_id = SDL_ThreadID();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    SDL_GL_SwapWindow(g_pc_window);
    fprintf(stderr, "[PC] Window ready\n");

    pc_gx_init();

    /* Pump events during init and keep pumping every frame — macOS invalidates
     * the window server connection after ~1 second without pumping, causing
     * CA::Fence::Observer to crash in __hash_table::__rehash. */
    SDL_PumpEvents();
    fprintf(stderr, "[PC] Platform initialized: %dx%d\n", g_pc_window_w, g_pc_window_h);
}

void pc_platform_shutdown(void) {
    pc_platform_ensure_gl_context_current();
    pc_audio_shutdown();
    pc_gx_shutdown();
    if (g_pc_gl_context) { SDL_GL_DeleteContext(g_pc_gl_context); g_pc_gl_context = NULL; }
    if (g_pc_window) { SDL_DestroyWindow(g_pc_window); g_pc_window = NULL; }
    if (g_pc_gl_owner_mutex) { SDL_DestroyMutex(g_pc_gl_owner_mutex); g_pc_gl_owner_mutex = NULL; }
    SDL_Quit();
}

extern "C" void pc_platform_swap_buffers(void) {
    SDL_GL_SwapWindow(g_pc_window);
}

static int s_sdl_events_ready = 0;
static u32 s_poll_events_only_calls = 0;
static u64 s_last_event_pump_time = 0;
static u32 s_event_pump_skip_logs = 0;
static u32 s_event_frame_token = 0;
static u32 s_last_pumped_frame_token = UINT32_MAX;

static int pc_platform_drain_event_queue(void) {
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 1) {
        switch (event.type) {
            case SDL_QUIT:
                g_pc_running = 0;
                return 0;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    pc_platform_update_window_size();
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    g_pc_running = 0;
                    return 0;
                }
                if (event.key.keysym.sym == SDLK_F3 && !event.key.repeat) {
                    g_pc_no_framelimit ^= 1;
                    printf("[PC] Frame limiter %s\n", g_pc_no_framelimit ? "OFF" : "ON");
                }
                break;
        }
    }
    return 1;
}

void pc_platform_warm_up_events(void) {
    SDL_PumpEvents();
    if (g_pc_verbose) {
        fprintf(stderr, "[PC] Event system ready\n");
    }
    s_sdl_events_ready = 1;
    s_last_event_pump_time = SDL_GetPerformanceCounter();
}

extern "C" void pc_platform_begin_frame(void) {
    s_event_frame_token++;
}

extern "C" void pc_platform_pump_events_safe(void) {
    /* Pump events — safe now that we don't call VI pre/post retrace
     * callbacks (which interleaved GL ops with Cocoa event processing). */
    if (!g_pc_window) return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT: g_pc_running = 0; break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    pc_platform_update_window_size();
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) g_pc_running = 0;
                if (ev.key.keysym.sym == SDLK_F3 && !ev.key.repeat) g_pc_no_framelimit ^= 1;
                break;
        }
    }
}

extern "C" void pc_platform_poll_events_only(void) {
    /* Minimal event drain — do NOT pump here during loading.
     * Pumping is done after SDL_GL_SwapWindow in swap_buffers. */
    if (!g_pc_window) return;
    u64 t0 = SDL_GetPerformanceCounter();
    s_last_event_pump_time = SDL_GetPerformanceCounter();
    pc_platform_drain_event_queue();
    if (g_pc_verbose) {
        u64 dt = SDL_GetPerformanceCounter() - t0;
        u64 freq = SDL_GetPerformanceFrequency();
        if (dt > freq / 20) {
            fprintf(stderr, "[PC] poll_events_only slow: %llums\n", dt * 1000 / freq);
        }
    }
}

int pc_platform_poll_events(void) {
    if (!g_pc_window) return 1;
    /* Use SDL_PeepEvents to drain without pumping.
     * SDL_PollEvent/SDL_PumpEvents trigger Cocoa CA::Transaction::commit
     * which deadlocks with the spinning CA::Fence::Observer thread
     * (macOS QuartzCore bug with OpenGL-to-Metal translation). */
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 1) {
        switch (event.type) {
            case SDL_QUIT:
                g_pc_running = 0;
                return 0;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    pc_platform_update_window_size();
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    g_pc_running = 0;
                    return 0;
                }
                if (event.key.keysym.sym == SDLK_F3 && !event.key.repeat) {
                    g_pc_no_framelimit ^= 1;
                }
                break;
        }
    }
    return 1;
}

/* game's main() renamed to tp_main_entry via -Dmain=tp_main_entry.
 * Since it's defined in m_Do_main.cpp (C++ file) it has C++ linkage. */
void tp_main_entry(int argc, const char* argv[]);

/* OS init that must happen before game code */
extern "C" void pc_os_init_arena(void);
extern "C" void pc_os_init_time(void);

int main(int argc, char* argv[]) {
    const char* disc_path = NULL;
    char** game_argv = (char**)malloc(sizeof(char*) * (size_t)argc);
    int game_argc = 1;
    game_argv[0] = argv[0];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            g_pc_verbose = 1;
        } else if (strcmp(argv[i], "--no-framelimit") == 0) {
            g_pc_no_framelimit = 1;
        } else if (strcmp(argv[i], "--headless") == 0) {
            /* Handled later; keep out of game argv. */
        } else if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc) {
            disc_path = argv[++i];
        } else if (strncmp(argv[i], "--disc=", sizeof("--disc=") - 1) == 0) {
            disc_path = argv[i] + sizeof("--disc=") - 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: TwilightPrincess [options] [rom.iso]\n");
            printf("  --verbose, -v       Enable diagnostic output\n");
            printf("  --no-framelimit     Disable frame limiter\n");
            printf("  --headless          Skip video init for boot testing\n");
            printf("  --disc PATH         Use an explicit .iso/.gcm/.ciso path\n");
            printf("  --help, -h          Show this help\n");
            free(game_argv);
            return 0;
        } else if (argv[i][0] != '-' && disc_path == NULL) {
            disc_path = argv[i];
        } else {
            game_argv[game_argc++] = argv[i];
        }
    }

    if (!g_pc_verbose) {
        /* Silence game's OSReport spam (goes to stdout).
         * Keep stderr open for PC platform messages. */
#ifdef _WIN32
        freopen("NUL", "w", stdout);
#else
        freopen("/dev/null", "w", stdout);
#endif
    } else {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }

    /* Check for --headless (skip video, useful for boot testing) */
    int headless = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) headless = 1;
    }

    SDL_SetMainReady();
    g_pc_main_thread_id = SDL_ThreadID();
#ifndef _WIN32
    g_pc_main_pthread = pthread_self();
    g_pc_main_pthread_valid = 1;
#endif
    pc_crash_protection_init();

    /* Initialize SDL/GL FIRST — before arena allocation.
     * Static constructors in tp_game.a may affect global state;
     * SDL_Init must run in clean environment. */
    if (!headless) {
        fprintf(stderr, "[PC] Initializing platform (SDL2 + OpenGL 3.3)...\n");
        pc_platform_init();
        pc_platform_warm_up_events();
    } else {
        fprintf(stderr, "[PC] Headless mode — skipping video init\n");
        if (SDL_Init(SDL_INIT_TIMER) < 0) {
            fprintf(stderr, "SDL_Init(TIMER) failed: %s\n", SDL_GetError());
        }
    }

    fprintf(stderr, "[PC] Initializing arena (%d MB)...\n",
            PC_MAIN_MEMORY_SIZE / (1024 * 1024));
    pc_os_init_arena();
    pc_os_init_time();

    fprintf(stderr, "[PC] Initializing disc I/O...\n");
    if (disc_path != NULL) {
        pc_disc_set_path(disc_path);
    }
    pc_disc_init();

    fprintf(stderr, "[PC] Entering game code...\n");
    tp_main_entry(game_argc, (const char**)game_argv);

    fprintf(stderr, "[PC] Game exited, shutting down.\n");
    pc_disc_shutdown();
    pc_platform_shutdown();
    free(game_argv);
    return 0;
}
