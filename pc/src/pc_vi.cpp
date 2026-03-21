/* pc_vi.cpp - Video interface stubs with frame pacing */
#include "pc_platform.h"
#include "JSystem/JUtility/JUTVideo.h"
#ifndef _WIN32
#include <unistd.h>
#endif

extern "C" {

static u32 retrace_count = 0;
static u64 frame_start_time = 0;
static u64 perf_freq = 0;
static int swap_enabled = 0; /* Don't swap until game is ready */
static SDL_mutex* retrace_mutex = NULL;
static SDL_cond* retrace_cond = NULL;
static void (*vi_pre_callback)(u32) = NULL;
static void (*vi_post_callback)(u32) = NULL;
static void* vi_next_frame_buffer = NULL;
static void* vi_current_frame_buffer = NULL;
static int vi_black = 0;

static void ensure_retrace_sync_primitives(void) {
    if (retrace_mutex == NULL) {
        retrace_mutex = SDL_CreateMutex();
    }
    if (retrace_cond == NULL) {
        retrace_cond = SDL_CreateCond();
    }
}

void VIInit(void) {
    perf_freq = SDL_GetPerformanceFrequency();
    frame_start_time = SDL_GetPerformanceCounter();
    ensure_retrace_sync_primitives();
}

void VIConfigure(void* rm) { (void)rm; }
void VIConfigurePan(u16 x, u16 y, u16 w, u16 h) { (void)x; (void)y; (void)w; (void)h; }
void VIFlush(void) {}

void VISetNextFrameBuffer(void* fb) { vi_next_frame_buffer = fb; }

void VIEnableSwap(void) { swap_enabled = 1; }

void VIWaitForRetrace(void) {
    if (!perf_freq) {
        perf_freq = SDL_GetPerformanceFrequency();
    }
    ensure_retrace_sync_primitives();

    static int s_vi_count = 0;
    s_vi_count++;
    int is_render = pc_platform_is_render_thread();
    if (s_vi_count <= 10) {
        fprintf(stderr, "[VI] WaitForRetrace #%d: is_render=%d swap_enabled=%d retrace=%u\n",
                s_vi_count, is_render, swap_enabled, retrace_count);
    }

    if (!is_render) {
        u32 observed = retrace_count;
        if (retrace_mutex != NULL && retrace_cond != NULL) {
            SDL_LockMutex(retrace_mutex);
            while (observed == retrace_count) {
                SDL_CondWaitTimeout(retrace_cond, retrace_mutex, 20);
                if (observed == 0) {
                    break;
                }
            }
            SDL_UnlockMutex(retrace_mutex);
        } else {
            SDL_Delay(1);
        }
        return;
    }

    u32 next_retrace = retrace_count + 1;

    if (vi_pre_callback != NULL) {
        if (g_pc_verbose && next_retrace <= 8) {
            fprintf(stderr, "[VI] pre-callback #%u (black=%d next_fb=%p)\n",
                    next_retrace, vi_black, vi_next_frame_buffer);
        }
        vi_pre_callback(next_retrace);
        if (g_pc_verbose && next_retrace <= 8) {
            fprintf(stderr, "[VI] pre-callback #%u returned OK\n", next_retrace);
        }
    }

    u64 vi_enter = SDL_GetPerformanceCounter();
    u64 frame_ms = 0;
    if (frame_start_time != 0) {
        frame_ms = (vi_enter - frame_start_time) * 1000 / perf_freq;
    }

    if (g_pc_verbose && next_retrace <= 8) {
        fprintf(stderr, "[VI] about to poll_events #%u\n", next_retrace);
    }
    if (!pc_platform_poll_events()) {
        g_pc_running = 0;
        return;
    }

    u64 t_before_swap = SDL_GetPerformanceCounter();
    if (swap_enabled) {
        pc_platform_swap_buffers();
        vi_current_frame_buffer = vi_next_frame_buffer;
    }
    u64 t_after_swap = SDL_GetPerformanceCounter();

    if (!g_pc_no_framelimit && frame_start_time != 0) {
        const u64 target_us = g_pc_fast_forward ? 8333 : 16667;
        u64 now = SDL_GetPerformanceCounter();
        u64 elapsed_us = (now - frame_start_time) * 1000000 / perf_freq;
        while (elapsed_us < target_us) {
            u64 remain_us = target_us - elapsed_us;
            /* Use usleep instead of SDL_Delay — SDL_Delay hangs indefinitely
             * when Metal background threads are in error state on macOS ARM64 */
            if (remain_us > 2000) {
                usleep(1000);
            }
            now = SDL_GetPerformanceCounter();
            elapsed_us = (now - frame_start_time) * 1000000 / perf_freq;
        }
    }

    if (g_pc_verbose && frame_ms > 20) {
        u64 swap_ms = (t_after_swap - t_before_swap) * 1000 / perf_freq;
        fprintf(stderr, "[PC] slow retrace %u: frame=%llums swap=%llums\n",
                retrace_count, frame_ms, swap_ms);
    }

    frame_start_time = SDL_GetPerformanceCounter();
    if (retrace_mutex != NULL) {
        SDL_LockMutex(retrace_mutex);
    }
    retrace_count = next_retrace;
    if (retrace_cond != NULL) {
        SDL_CondBroadcast(retrace_cond);
    }
    if (retrace_mutex != NULL) {
        SDL_UnlockMutex(retrace_mutex);
    }

    if (vi_post_callback != NULL) {
        if (g_pc_verbose && retrace_count <= 8) {
            fprintf(stderr, "[VI] post-callback #%u\n", retrace_count);
        }
        vi_post_callback(retrace_count);
    }
}

u32 VIGetRetraceCount(void) { return retrace_count; }

void VISetBlack(int black) {
    vi_black = black != 0;
}
u32 VIGetTvFormat(void) { return 0; /* NTSC */ }
u32 VIGetDTVStatus(void) { return 0; }
u32 VIGetNextField(void) { return 0; }
u32 VIGetCurrentLine(void) { return 0; }

void* VISetPreRetraceCallback(void* callback) {
    void* old = (void*)vi_pre_callback;
    vi_pre_callback = (void (*)(u32))callback;
    return old;
}

void* VISetPostRetraceCallback(void* callback) {
    void* old = (void*)vi_post_callback;
    vi_post_callback = (void (*)(u32))callback;
    return old;
}

void* VIGetCurrentFrameBuffer(void) { return vi_current_frame_buffer; }
void* VIGetNextFrameBuffer(void) { return vi_next_frame_buffer; }

} /* extern "C" */
