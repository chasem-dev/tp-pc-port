/* pc_vi.cpp - Video interface stubs with frame pacing */
#include "pc_platform.h"
#include "JSystem/JUtility/JUTVideo.h"
#ifndef _WIN32
#include <unistd.h>
#endif

extern "C" {

static u32 retrace_count = 0;
static u64 last_retrace_time = 0;
static int swap_enabled = 0; /* Don't swap until game is ready */
#define FRAME_TIME_US 16667  /* ~60fps */

void VIInit(void) {
    last_retrace_time = SDL_GetPerformanceCounter();
}

void VIConfigure(void* rm) { (void)rm; }
void VIConfigurePan(u16 x, u16 y, u16 w, u16 h) { (void)x; (void)y; (void)w; (void)h; }
void VIFlush(void) {}

void VISetNextFrameBuffer(void* fb) { (void)fb; }

void VIEnableSwap(void) { swap_enabled = 1; }

static int s_vi_call = 0;
void VIWaitForRetrace(void) {
    s_vi_call++;
    if (s_vi_call <= 10) fprintf(stderr, "[PC] VIWaitForRetrace #%d: start\n", s_vi_call);

    /* Frame pacing: target 60 FPS */
    if (!g_pc_no_framelimit) {
        u64 freq = SDL_GetPerformanceFrequency();
        u64 target = last_retrace_time + (freq / 60);
        u64 now;
        while ((now = SDL_GetPerformanceCounter()) < target) {
            u64 remaining_us = (target - now) * 1000000 / freq;
            if (remaining_us > 2000) SDL_Delay(1);
        }
        last_retrace_time = now;
    } else {
        last_retrace_time = SDL_GetPerformanceCounter();
    }
    retrace_count++;

    if (s_vi_call <= 10) fprintf(stderr, "[PC] VIWaitForRetrace: postRetrace\n");
    /* Simulate VI retrace interrupt */
    if (JUTVideo::getManager()) {
        JUTVideo::postRetraceProc(retrace_count);
    }
    if (s_vi_call <= 10) fprintf(stderr, "[PC] VIWaitForRetrace: done\n");
}

u32 VIGetRetraceCount(void) { return retrace_count; }

void VISetBlack(int black) { (void)black; }
u32 VIGetTvFormat(void) { return 0; /* NTSC */ }
u32 VIGetDTVStatus(void) { return 0; }
u32 VIGetNextField(void) { return 0; }
u32 VIGetCurrentLine(void) { return 0; }

void* VISetPreRetraceCallback(void* callback) { (void)callback; return NULL; }
void* VISetPostRetraceCallback(void* callback) { (void)callback; return NULL; }

void* VIGetCurrentFrameBuffer(void) { return NULL; }
void* VIGetNextFrameBuffer(void) { return NULL; }

} /* extern "C" */
