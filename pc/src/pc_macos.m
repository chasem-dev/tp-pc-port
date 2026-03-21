/* pc_macos.m - Early SDL/GL initialization on macOS.
 *
 * Problem: TP has 447 C++ static constructors that run before main().
 * These fragment the heap in a way that causes QuartzCore's CA::Fence::Observer
 * thread to crash in __hash_table::__rehash (NULL bucket pointer).
 * AC (which works) has only 11 constructors.
 *
 * Solution: Initialize SDL and create the GL context BEFORE the game's
 * constructors run. This way, Metal's background threads and QuartzCore's
 * data structures are allocated in a clean heap state.
 *
 * __attribute__((constructor(101))) runs before C++ global constructors
 * (which default to priority 65535 on clang). */
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

/* These are defined in pc_main.cpp — we set them up early */
extern SDL_Window*   g_pc_window;
extern SDL_GLContext  g_pc_gl_context;
extern int           g_pc_verbose;

__attribute__((constructor(101)))
static void pc_early_sdl_init(void) {
    /* Set up NSApplication first */
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    /* Initialize SDL video subsystem */
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "[EARLY] SDL_Init failed: %s\n", SDL_GetError());
        return;
    }

    /* Create OpenGL context early — before game constructors fragment heap */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    g_pc_window = SDL_CreateWindow(
        "Twilight Princess",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1216, 896,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!g_pc_window) {
        fprintf(stderr, "[EARLY] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return;
    }

    g_pc_gl_context = SDL_GL_CreateContext(g_pc_window);
    if (!g_pc_gl_context) {
        fprintf(stderr, "[EARLY] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return;
    }

    /* Pump events to process the activation/window-shown events immediately */
    SDL_PumpEvents();
    SDL_PumpEvents();

    /* Activate the app */
    [NSApp activateIgnoringOtherApps:YES];

    fprintf(stderr, "[EARLY] SDL/GL context created before game constructors\n");
}

void pc_macos_init(void) {
    /* Already done in early constructor */
}

void pc_macos_pump_events(void) {
    /* unused */
}
#endif
