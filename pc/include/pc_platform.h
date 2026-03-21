/* pc_platform.h - SDL2/OpenGL platform layer for TP PC port */
#ifndef PC_PLATFORM_H
#define PC_PLATFORM_H

#include <stdint.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <glad/gl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "pc_types.h"

/* --- Configuration --- */
#define PC_GC_WIDTH       608
#define PC_GC_HEIGHT      448
#define PC_SCREEN_WIDTH   PC_GC_WIDTH
#define PC_SCREEN_HEIGHT  PC_GC_HEIGHT
#define PC_WINDOW_TITLE   "Twilight Princess"

#if UINTPTR_MAX > 0xFFFFFFFFu
/* 64-bit: structs with pointer fields are larger, need more arena space.
 * Also need extra room since ARAM archives are loaded into main memory. */
#define PC_MAIN_MEMORY_SIZE   (512 * 1024 * 1024)
#else
#define PC_MAIN_MEMORY_SIZE   (128 * 1024 * 1024)
#endif
#define PC_ARAM_SIZE          (16 * 1024 * 1024)
#define PC_FIFO_SIZE          (256 * 1024)

#define PC_PI  3.14159265358979323846
#define PC_PIf 3.14159265358979323846f
#define PC_DEG_TO_RAD (PC_PI / 180.0)
#define PC_DEG_TO_RADf (PC_PIf / 180.0f)

/* GC hardware clocks */
#define GC_BUS_CLOCK          162000000u
#define GC_CORE_CLOCK         486000000u
#define GC_TIMER_CLOCK        (GC_BUS_CLOCK / 4)

/* --- Platform headers --- */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef near
#undef far
#elif defined(__APPLE__)
#include <signal.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#else
#include <signal.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <elf.h>
#endif
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Global state --- */
extern SDL_Window*   g_pc_window;
extern SDL_GLContext  g_pc_gl_context;
extern int           g_pc_running;
extern int           g_pc_verbose;
extern int           g_pc_no_framelimit;
extern int           g_pc_fast_forward;

extern int g_pc_window_w;
extern int g_pc_window_h;
void pc_platform_update_window_size(void);

/* --- Functions --- */
void pc_platform_init(void);
void pc_platform_shutdown(void);
void pc_platform_swap_buffers(void);
int  pc_platform_poll_events(void);
void pc_platform_begin_frame(void);
int  pc_platform_is_main_thread(void);
int  pc_platform_is_gl_thread(void);
int  pc_platform_is_render_thread(void);
void pc_platform_mark_render_thread(void);
void pc_platform_ensure_gl_context_current(void);

/* --- Crash protection --- */
void pc_crash_protection_init(void);
void pc_crash_set_jmpbuf(jmp_buf* buf);
uintptr_t pc_crash_get_addr(void);

/* Compile-time type validation */
#ifdef __cplusplus
static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
static_assert(sizeof(uintptr_t) == sizeof(void*), "uintptr_t must match pointer size");
#else
_Static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
_Static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
_Static_assert(sizeof(uintptr_t) == sizeof(void*), "uintptr_t must match pointer size");
#endif

/* --- Audio --- */
void pc_audio_shutdown(void);

/* --- Disc I/O --- */
void pc_disc_set_path(const char* path);
void pc_disc_init(void);
void pc_disc_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_PLATFORM_H */
