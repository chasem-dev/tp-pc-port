/* pc_pad.cpp - Controller pad stubs */
#include "pc_platform.h"
#ifdef __APPLE__
#include <Carbon/Carbon.h>  /* for kVK_ key codes */
#include <CoreGraphics/CoreGraphics.h>  /* for CGEventSourceKeyState */

static bool pc_key_down(int keyCode) {
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, (CGKeyCode)keyCode);
}
#endif

extern "C" {

#define PAD_MAX_CONTROLLERS 4

typedef struct {
    u16 button;
    s8 stickX;
    s8 stickY;
    s8 substickX;
    s8 substickY;
    u8 triggerL;
    u8 triggerR;
    u8 analogA;
    u8 analogB;
    s8 err;
} PADStatus;

static SDL_GameController* pad_controllers[PAD_MAX_CONTROLLERS] = {};
static int pad_initialized = 0;

void PADInit(void) {
    pad_initialized = 1;
    for (int i = 0; i < SDL_NumJoysticks() && i < PAD_MAX_CONTROLLERS; i++) {
        if (SDL_IsGameController(i)) {
            pad_controllers[i] = SDL_GameControllerOpen(i);
        }
    }
}

u32 PADRead(PADStatus* status) {
    memset(status, 0, sizeof(PADStatus) * PAD_MAX_CONTROLLERS);

#ifdef __APPLE__
    /* Read keyboard directly via CoreGraphics — bypasses SDL event pump
     * which deadlocks with spinning QuartzCore CA::Fence threads. */
    if (pc_key_down(kVK_Return) || pc_key_down(kVK_Space))  status[0].button |= 0x0100; /* A */
    if (pc_key_down(kVK_Delete) || pc_key_down(kVK_ANSI_X)) status[0].button |= 0x0200; /* B */
    if (pc_key_down(kVK_ANSI_Z))                             status[0].button |= 0x0400; /* X */
    if (pc_key_down(kVK_ANSI_C))                             status[0].button |= 0x0800; /* Y */
    if (pc_key_down(kVK_Shift))                              status[0].button |= 0x0040; /* L */
    if (pc_key_down(kVK_RightShift))                         status[0].button |= 0x0020; /* R */
    if (pc_key_down(kVK_Tab))                                status[0].button |= 0x0010; /* Z */
    if (pc_key_down(kVK_Escape))                             status[0].button |= 0x1000; /* Start */
    if (pc_key_down(kVK_UpArrow))                            status[0].button |= 0x0008; /* D-Up */
    if (pc_key_down(kVK_DownArrow))                          status[0].button |= 0x0004; /* D-Down */
    if (pc_key_down(kVK_LeftArrow))                          status[0].button |= 0x0001; /* D-Left */
    if (pc_key_down(kVK_RightArrow))                         status[0].button |= 0x0002; /* D-Right */

    /* Main stick via WASD */
    if (pc_key_down(kVK_ANSI_W)) status[0].stickY =  72;
    if (pc_key_down(kVK_ANSI_S)) status[0].stickY = -72;
    if (pc_key_down(kVK_ANSI_A)) status[0].stickX = -72;
    if (pc_key_down(kVK_ANSI_D)) status[0].stickX =  72;
#else
    /* Non-macOS: use SDL keyboard state (requires SDL_PumpEvents) */
    const u8* keys = SDL_GetKeyboardState(NULL);
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE]) status[0].button |= 0x0100;
    if (keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_X]) status[0].button |= 0x0200;
    if (keys[SDL_SCANCODE_Z]) status[0].button |= 0x0400;
    if (keys[SDL_SCANCODE_C]) status[0].button |= 0x0800;
    if (keys[SDL_SCANCODE_LSHIFT]) status[0].button |= 0x0040;
    if (keys[SDL_SCANCODE_RSHIFT]) status[0].button |= 0x0020;
    if (keys[SDL_SCANCODE_TAB]) status[0].button |= 0x0010;
    if (keys[SDL_SCANCODE_ESCAPE]) status[0].button |= 0x1000;
    if (keys[SDL_SCANCODE_UP]) status[0].button |= 0x0008;
    if (keys[SDL_SCANCODE_DOWN]) status[0].button |= 0x0004;
    if (keys[SDL_SCANCODE_LEFT]) status[0].button |= 0x0001;
    if (keys[SDL_SCANCODE_RIGHT]) status[0].button |= 0x0002;
    if (keys[SDL_SCANCODE_W]) status[0].stickY = 72;
    if (keys[SDL_SCANCODE_S]) status[0].stickY = -72;
    if (keys[SDL_SCANCODE_A]) status[0].stickX = -72;
    if (keys[SDL_SCANCODE_D]) status[0].stickX = 72;
#endif

    /* Gamepad input (SDL controller API doesn't need pump) */
    if (pad_controllers[0]) {
        SDL_GameController* gc = pad_controllers[0];
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) status[0].button |= 0x0100;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)) status[0].button |= 0x0200;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X)) status[0].button |= 0x0400;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y)) status[0].button |= 0x0800;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) status[0].button |= 0x1000;

        s16 lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        s16 ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        if (lx > 4000 || lx < -4000) status[0].stickX = (s8)(lx * 72 / 32767);
        if (ly > 4000 || ly < -4000) status[0].stickY = (s8)(-ly * 72 / 32767);
    }

    return 0x000F0000; /* all controllers connected mask */
}

void PADClamp(PADStatus* status) { (void)status; }
void PADClampCircle(PADStatus* status) { (void)status; }

void PADControlMotor(s32 chan, u32 cmd) { (void)chan; (void)cmd; }
void PADControlAllMotors(const u32* cmdArr) { (void)cmdArr; }

BOOL PADReset(u32 mask) { (void)mask; return TRUE; }
BOOL PADRecalibrate(u32 mask) { (void)mask; return TRUE; }
void PADSetSpec(u32 spec) { (void)spec; }
u32 PADGetSpec(void) { return 5; }

void PADSetAnalogMode(u32 mode) { (void)mode; }

void PADCleanup(void) {
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (pad_controllers[i]) {
            SDL_GameControllerClose(pad_controllers[i]);
            pad_controllers[i] = NULL;
        }
    }
}

u32 SIGetType(s32 chan) { (void)chan; return 0x09000000; }
u32 SIGetTypeAsync(s32 chan, void* callback) { (void)callback; return SIGetType(chan); }

} /* extern "C" */
