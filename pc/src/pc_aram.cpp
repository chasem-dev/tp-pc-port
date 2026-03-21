/* pc_aram.cpp - ARAM (Auxiliary RAM) stubs */
#include "pc_platform.h"

extern "C" {

static u8* aram_memory = NULL;
static u32 aram_alloc_ptr = 0;

void ARInit(u32* stackIndexAddr, u32 numEntries) {
    (void)stackIndexAddr; (void)numEntries;
    if (!aram_memory) {
        aram_memory = (u8*)malloc(PC_ARAM_SIZE);
        if (aram_memory) memset(aram_memory, 0, PC_ARAM_SIZE);
    }
    aram_alloc_ptr = 0;
    printf("[PC] ARAM initialized: %d MB\n", PC_ARAM_SIZE / (1024*1024));
}

u32 ARAlloc(u32 length) {
    u32 ptr = aram_alloc_ptr;
    aram_alloc_ptr += (length + 31) & ~31; /* 32-byte align */
    return ptr;
}

u32 ARFree(u32* length) { (void)length; return 0; }

u32 ARGetSize(void) { return PC_ARAM_SIZE; }
u32 ARGetBaseAddress(void) { return 0; }
u32 ARGetInternalSize(void) { return PC_ARAM_SIZE; }

void ARStartDMA(u32 type, u32 mainmem_addr, u32 aram_addr, u32 length) {
    (void)type; (void)mainmem_addr; (void)aram_addr; (void)length;
    /* Will implement ARAM<->main memory DMA when needed */
}

int ARCheckInit(void) { return 1; }
void ARReset(void) {}
int ARGetDMAStatus(void) { return 0; /* idle */ }

/* ARQ - queued ARAM requests */
void ARQInit(void) {}
void ARQReset(void) {}
void ARQPostRequest(void* task, u32 owner, u32 type, u32 priority,
                    u32 source, u32 dest, u32 length, void* callback) {
    (void)task; (void)owner; (void)type; (void)priority;
    (void)source; (void)dest; (void)length; (void)callback;
}
void ARQFlushQueue(void) {}
u32 ARQGetChunkSize(void) { return 4096; }
void ARQSetChunkSize(u32 size) { (void)size; }

} /* extern "C" */
