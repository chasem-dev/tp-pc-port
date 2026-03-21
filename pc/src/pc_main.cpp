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

void pc_platform_update_window_size(void) {
    SDL_GL_GetDrawableSize(g_pc_window, &g_pc_window_w, &g_pc_window_h);
    if (g_pc_window_w <= 0) g_pc_window_w = PC_SCREEN_WIDTH;
    if (g_pc_window_h <= 0) g_pc_window_h = PC_SCREEN_HEIGHT;
}

/* --- Crash protection --- */
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
    if (pc_active_jmpbuf != NULL) {
        pc_last_crash_addr = (uintptr_t)info->si_addr;
        jmp_buf* buf = pc_active_jmpbuf;
        pc_active_jmpbuf = NULL;
        longjmp(*buf, 1);
    }
    int is_main = pthread_main_np();
    if (!is_main) {
        /* Non-main thread crash — likely macOS system framework.
         * Return to let macOS handle it internally. */
        return;
    }
    /* Main thread — check if crash is in system code (Metal/QuartzCore/AGX).
     * SIGBUS from Metal shader compilation should not be fatal.
     * Use dladdr to check if the faulting address is in a system library. */
    if (sig == 10 /* SIGBUS */) {
        Dl_info dli;
        void* bt2[8];
        int bt2_count = backtrace(bt2, 8);
        /* Check if any frame in the backtrace is outside our binary */
        for (int i = 1; i < bt2_count; i++) {
            if (dladdr(bt2[i], &dli) && dli.dli_fname) {
                /* If the frame is in a system library (not our executable), tolerate it */
                const char* name = dli.dli_fname;
                int len = strlen(name);
                if (len > 4 && (
                    strstr(name, "AGX") ||
                    strstr(name, "Metal") ||
                    strstr(name, "AppleMetal") ||
                    strstr(name, "libsystem") ||
                    strstr(name, "QuartzCore") ||
                    strstr(name, "AppKit") ||
                    strstr(name, "CoreGraphics") ||
                    strstr(name, "/usr/lib/") ||
                    strstr(name, "/System/"))) {
                    return; /* System framework crash — let it retry internally */
                }
            }
        }
    }
    /* Real game crash — fatal */
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "\n[CRASH] sig=%d addr=%p\n",
                     sig, info->si_addr);
    (void)!write(2, buf, n);
    void* bt[16];
    int bt_count = backtrace(bt, 16);
    backtrace_symbols_fd(bt, bt_count, 2);
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
        sigaction(SIGTRAP, &sa, NULL);
#endif
        installed = 1;
    }
}

void pc_crash_set_jmpbuf(jmp_buf* buf) { pc_active_jmpbuf = buf; }
uintptr_t pc_crash_get_addr(void) { return pc_last_crash_addr; }

/* --- Platform init --- */
void pc_platform_init(void) {
#ifdef _WIN32
    SetProcessDPIAware();
#endif
    /* Init non-video subsystems first (safe with GCC static constructors) */
    if (SDL_Init(SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init(TIMER) failed: %s\n", SDL_GetError());
        exit(1);
    }
    /* Video init triggers Cocoa/AppKit which can crash with many GCC static
     * constructors. Defer and retry if it fails. */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_InitSubSystem(SDL_INIT_AUDIO);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
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

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGL failed\n");
        SDL_GL_DeleteContext(g_pc_gl_context);
        SDL_DestroyWindow(g_pc_window);
        SDL_Quit();
        exit(1);
    }

    SDL_GL_SetSwapInterval(0); /* No vsync — our VIWaitForRetrace handles frame pacing */
    pc_platform_update_window_size();

    /* Show window and do initial clear+swap to establish GL framebuffer.
     * Process events to let macOS complete window server setup. */
    SDL_ShowWindow(g_pc_window);
    SDL_PumpEvents();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    SDL_GL_SwapWindow(g_pc_window);
    SDL_PumpEvents();
    fprintf(stderr, "[PC] Window ready\n");

    pc_gx_init();

    /* Force Metal shader compilation by drawing with multiple GL state
     * combinations BEFORE any game threads start. Metal compiles shader
     * pipelines lazily on first use, which can SIGBUS on dispatch threads.
     * Pre-compiling the common variants avoids this. */
    {
        extern PCGXState g_gx;
        glBindVertexArray(g_gx.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_gx.vbo);
        PCGXVertex dummy[4];
        memset(dummy, 0, sizeof(dummy));
        /* Give vertices some position so they're not degenerate */
        dummy[0].position[0] = -1; dummy[0].position[1] = -1;
        dummy[1].position[0] =  1; dummy[1].position[1] = -1;
        dummy[2].position[0] =  1; dummy[2].position[1] =  1;
        dummy[3].position[0] = -1; dummy[3].position[1] =  1;
        for (int i = 0; i < 4; i++) { dummy[i].color0[0] = dummy[i].color0[1] = dummy[i].color0[2] = dummy[i].color0[3] = 255; }
        glBufferData(GL_ARRAY_BUFFER, sizeof(dummy), dummy, GL_STREAM_DRAW);

        /* Variant 1: no blend, no texture, depth test */
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();

        /* Variant 2: alpha blend (used by logo scene) */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();

        /* Variant 3: blend + no depth (2D draws) */
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();

        /* Variant 4: with texture bound */
        GLuint warmup_tex;
        glGenTextures(1, &warmup_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, warmup_tex);
        unsigned char white[] = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();

        /* Variant 5: indexed draw (quads use EBO) */
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
        glFinish();

        glDeleteTextures(1, &warmup_tex);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        SDL_GL_SwapWindow(g_pc_window);
        SDL_PumpEvents();
        fprintf(stderr, "[PC] Shader pipeline compiled (5 variants)\n");
    }

    fprintf(stderr, "[PC] Platform initialized: %dx%d\n", g_pc_window_w, g_pc_window_h);
}

void pc_platform_shutdown(void) {
    pc_audio_shutdown();
    pc_gx_shutdown();
    if (g_pc_gl_context) { SDL_GL_DeleteContext(g_pc_gl_context); g_pc_gl_context = NULL; }
    if (g_pc_window) { SDL_DestroyWindow(g_pc_window); g_pc_window = NULL; }
    SDL_Quit();
}

extern "C" void pc_platform_swap_buffers(void) {
    SDL_GL_SwapWindow(g_pc_window);
}

static int s_sdl_events_ready = 0;

void pc_platform_warm_up_events(void) {
    /* On macOS, the first SDL_PollEvent call triggers Cocoa/AppKit
     * initialization. Must happen before any game threads run. */
    SDL_PumpEvents();
    s_sdl_events_ready = 1;
}

int pc_platform_poll_events(void) {
    if (!g_pc_window) return 1;
    SDL_Event event;
    while (SDL_WaitEventTimeout(&event, 0)) {
        switch (event.type) {
            case SDL_QUIT:
                g_pc_running = 0;
                return 0;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    pc_platform_update_window_size();
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) { g_pc_running = 0; return 0; }
                if (event.key.keysym.sym == SDLK_F3 && !event.key.repeat) {
                    g_pc_no_framelimit ^= 1;
                    printf("[PC] Frame limiter %s\n", g_pc_no_framelimit ? "OFF" : "ON");
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
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            g_pc_verbose = 1;
        } else if (strcmp(argv[i], "--no-framelimit") == 0) {
            g_pc_no_framelimit = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: TwilightPrincess [options] [rom.iso]\n");
            printf("  --verbose, -v       Enable diagnostic output\n");
            printf("  --no-framelimit     Disable frame limiter\n");
            printf("  --help, -h          Show this help\n");
            return 0;
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
    pc_disc_init();

    fprintf(stderr, "[PC] Entering game code...\n");
    tp_main_entry(argc, (const char**)argv);

    fprintf(stderr, "[PC] Game exited, shutting down.\n");
    pc_disc_shutdown();
    pc_platform_shutdown();
    return 0;
}
