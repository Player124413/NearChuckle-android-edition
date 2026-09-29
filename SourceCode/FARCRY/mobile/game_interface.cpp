// Far Cry's Portable* implementation. See docs/engines/farcry.md in Sigma Touch.
// Keys and the menu pointer go in as real SDL events, which CryInput's SDLKeyboard/SDLMouse
// poll. Gameplay goes through farcry_bridge.h: queued here on the UI thread, drained by
// CryGame once per frame and applied as CXClient actions by name, so in-game rebinding
// cannot break the touch controls.

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <android/log.h>

#include "SDL3/SDL.h"

#include "game_interface.h"
#include "farcry_bridge.h"
#include "LogWritter.h"

#define LOG_TAG "FarCry"
#define FC_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))

extern int FarCry_AndroidMain(int argc, char **argv);

// Exported by the OpenTouch SDL3 fork; the shared touch layer uses it too.
extern "C" bool SDL_SendKeyboardKey(Uint64 timestamp, SDL_KeyboardID keyboardID, int rawcode, SDL_Scancode scancode, bool down);
#define SDL_DEFAULT_KEYBOARD_ID 1

// The shared touch_interface_base.cpp expects every SDL3 engine to define this.
// The renderer module creates the window, so TouchInterface::newFrame fills it in.
extern "C" SDL_Window *window = NULL;

// stdout and stderr (the engine log, SDL, OpenAL, printf) go to logcat and the launcher's log file.
#define STDIO_PUMP_LINE_MAX 1008

static void *stdio_pump(void *arg)
{
    int fd = (int) (long) arg;
    char line[STDIO_PUMP_LINE_MAX + 1];
    int len = 0;
    char buf[512];
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        for (ssize_t i = 0; i < n; i++)
        {
            if (buf[i] == '\n' || len == STDIO_PUMP_LINE_MAX)
            {
                if (len > 0)
                {
                    line[len] = 0;
                    FC_LOGI("%s", line);
                    LogWritter_Write(line);
                    len = 0;
                }

                if (buf[i] == '\n')
                    continue;
            }

            if (buf[i] != '\r')
                line[len++] = buf[i];
        }
    }

    return NULL;
}

static void redirect_stdio_to_logcat()
{
    int pipes[2];
    pthread_t thread;

    if (pipe(pipes) != 0)
        return;

    dup2(pipes[1], STDOUT_FILENO);
    dup2(pipes[1], STDERR_FILENO);
    close(pipes[1]);

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (pthread_create(&thread, NULL, stdio_pump, (void *) (long) pipes[0]) == 0)
        pthread_detach(thread);
}

void PortableInit(int argc, const char **argv)
{
    redirect_stdio_to_logcat();

    // The engine's development switches are environment variables (FARCRY_GLES_DEBUG
    // and friends); the launcher passes them as -ENV:NAME=VALUE.
    static const char *engineArgv[128];
    int engineArgc = 0;

    for (int i = 0; i < argc && engineArgc < 127; i++)
    {
        // -MAP:name becomes the quoted console command "map name" the engine's
        // own command line takes; the app's tokenizer would split the quoted form.
        if (strncmp(argv[i], "-MAP:", 5) == 0)
        {
            char *cmd = (char *) malloc(strlen(argv[i]) + 8);
            sprintf(cmd, "\"map %s\"", argv[i] + 5);
            engineArgv[engineArgc++] = cmd;
            continue;
        }

        // -CVAR:name=value becomes the quoted console command "name value" (run after the configs load).
        if (strncmp(argv[i], "-CVAR:", 6) == 0)
        {
            char *cmd = (char *) malloc(strlen(argv[i]) + 8);
            sprintf(cmd, "\"%s\"", argv[i] + 6);
            char *eq = strchr(cmd, '=');
            if (eq) *eq = ' ';
            engineArgv[engineArgc++] = cmd;
            continue;
        }

        // -GAMEPATH <dir>: the game folder, absolute (on secondary storage a SAF path nothing can chdir() into).
        // -SHADERPAK <file>: the shader cache pak the launcher stages on primary storage.
        // -CACHEPATH <dir>: where generated caches go.
        if ((!strcmp(argv[i], "-GAMEPATH") || !strcmp(argv[i], "-SHADERPAK") || !strcmp(argv[i], "-CACHEPATH")) && i + 1 < argc)
        {
            const char *name = argv[i][1] == 'G' ? "FARCRY_GAME_PATH" : argv[i][1] == 'S' ? "FARCRY_SHADER_PAK" : "FARCRY_CACHE_PATH";
            setenv(name, argv[++i], 1);
            FC_LOGI("env %s=%s", name, argv[i]);
            continue;
        }

        if (strncmp(argv[i], "-ENV:", 5) == 0)
        {
            char *nameValue = strdup(argv[i] + 5);
            char *eq = strchr(nameValue, '=');
            if (eq)
            {
                *eq = 0;
                setenv(nameValue, eq + 1, 1);
                FC_LOGI("env %s=%s", nameValue, eq + 1);
            }
            free(nameValue);
            continue;
        }
        engineArgv[engineArgc++] = argv[i];
    }
    engineArgv[engineArgc] = NULL;

    // Every game-data read is built on -GAMEPATH, so the cwd only catches files the engine writes by bare
    // name (reports, dumps, logs): make it the user folder so nothing lands in the game data.
    if (const char *userFiles = getenv("USER_FILES"))
    {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/farcry", userFiles);
        mkdir(dir, 0755);
        if (chdir(dir) == 0)
            FC_LOGI("cwd %s", dir);
    }

    FC_LOGI("PortableInit, starting engine");

    FarCry_AndroidMain(engineArgc, (char **) engineArgv);

    FC_LOGI("engine returned, exiting");
    exit(0);
}

// Shared glue: blocks the calling (UI) thread until the engine has drawn n frames.
extern "C" void androidWaitFrames(int frames);

static void sendKey(SDL_Scancode sc, int down)
{
    SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, 0, sc, down != 0);
}

// CryInput samples key state once per frame, so a press and release in the same
// frame reads as never pressed. Hold it across a frame.
static void tapKey(SDL_Scancode sc)
{
    sendKey(sc, 1);
    androidWaitFrames(1);
    sendKey(sc, 0);
}

// -------------------------------------------------------------------------
// Touch thread -> engine thread
//
// Every Portable* call runs on Android's UI thread. Gameplay input only lands in
// these plain variables and the impulse ring; FarCry_DrainTouchInput, called by
// CryGame once per frame, is what reaches the engine.
// -------------------------------------------------------------------------

#define IMPULSE_QUEUE_SIZE 32
static int impulseQueue[IMPULSE_QUEUE_SIZE];
static volatile int impulseHead; // written by the touch thread
static volatile int impulseTail; // written by the engine thread

static volatile unsigned heldMask;
static volatile bool sprintToggle;            // run button toggle, on top of a held sprint
static volatile float stickFwd, stickSide;
static volatile int digitalFwd, digitalSide;   // dpad, -1/0/+1
static volatile float yawMouse, pitchMouse;    // pending look, in mouse pixels
static volatile float yawJoy, pitchJoy;        // held look rate, -1..1

// The touch layer's mouse-mode look is a screen fraction; CryInput turns pixels into
// degrees at 0.2 per pixel, so 900 makes a full-width swipe about half a turn.
#define LOOK_MOUSE_YAW_SCALE   1500.0f
#define LOOK_MOUSE_PITCH_SCALE 600.0f
#define LOOK_JOY_PIXELS_PER_SEC 900.0f

static int farcry_screen_mode = TS_MENU;

static void queueImpulse(int impulse)
{
    int next = (impulseHead + 1) % IMPULSE_QUEUE_SIZE;
    if (next == impulseTail)
        return; // full, drop
    impulseQueue[impulseHead] = impulse;
    impulseHead = next;
}

static void setHeld(int which, int state)
{
    if (state)
        heldMask |= 1u << which;
    else
        heldMask &= ~(1u << which);
}

static float clampUnit(float v)
{
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

extern "C" void FarCry_DrainTouchInput(FarCryTouchInput *out, float frameTime)
{
    out->moveFwd = clampUnit(stickFwd + (float) digitalFwd);
    out->moveSide = clampUnit(stickSide + (float) digitalSide);
    out->held = heldMask | (sprintToggle ? 1u << FC_HELD_SPRINT : 0);

    out->impulseCount = 0;
    while (impulseTail != impulseHead && out->impulseCount < FC_MAX_IMPULSES)
    {
        out->impulses[out->impulseCount++] = impulseQueue[impulseTail];
        impulseTail = (impulseTail + 1) % IMPULSE_QUEUE_SIZE;
    }

    // Look: take what the touch thread accumulated, leaving anything it adds meanwhile.
    float yaw = yawMouse;
    yawMouse -= yaw;
    float pitch = pitchMouse;
    pitchMouse -= pitch;
    yaw += yawJoy * LOOK_JOY_PIXELS_PER_SEC * frameTime;
    pitch += pitchJoy * LOOK_JOY_PIXELS_PER_SEC * frameTime;

    // CryInput truncates the motion to whole pixels; carry the fraction over.
    static float remX, remY;
    remX += yaw;
    remY += pitch;
    int dx = (int) remX;
    int dy = (int) remY;
    remX -= (float) dx;
    remY -= (float) dy;
    if (dx || dy)
    {
        SDL_Event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = SDL_EVENT_MOUSE_MOTION;
        // The touch layer's axes run opposite to a mouse's (see MouseMove in the shared glue).
        ev.motion.xrel = (float) -dx;
        ev.motion.yrel = (float) -dy;
        SDL_PushEvent(&ev);
    }
}

extern "C" void FarCry_ReportScreenMode(int inGame, int consoleOpen)
{
    farcry_screen_mode = consoleOpen ? TS_CONSOLE : (inGame ? TS_GAME : TS_MENU);
}

// -------------------------------------------------------------------------
// Portable* API
// -------------------------------------------------------------------------

void PortableBackButton(void)
{
    tapKey(SDL_SCANCODE_ESCAPE);
}

int PortableKeyEvent(int state, int code, int unitcode)
{
    sendKey((SDL_Scancode) code, state);
    return 0;
}

void PortableAction(int state, int action)
{
    // Custom buttons are keys the player binds in Far Cry's own controls screen, so
    // they must reach the engine in either mode. Same scheme as Quake 4: KP1-KP0 for
    // the ten buttons, A-P for the quad slides.
    if (action >= PORT_ACT_CUSTOM_0 && action <= PORT_ACT_CUSTOM_25)
    {
        if (action <= PORT_ACT_CUSTOM_9)
            sendKey((SDL_Scancode) (SDL_SCANCODE_KP_1 + action - PORT_ACT_CUSTOM_0), state);
        else
            sendKey((SDL_Scancode) (SDL_SCANCODE_A + action - PORT_ACT_CUSTOM_10), state);
        return;
    }

    const bool menuUp = PortableGetScreenMode() != TS_GAME;

    // Present in both modes.
    switch (action)
    {
        case PORT_ACT_CONSOLE:   sendKey(SDL_SCANCODE_GRAVE, state); return;
        case PORT_ACT_QUICKSAVE: if (state) queueImpulse(FC_IMP_QUICKSAVE); return;
        case PORT_ACT_QUICKLOAD: if (state) queueImpulse(FC_IMP_QUICKLOAD); return;
        default: break;
    }

    // Menu navigation reads raw keys, so synthetic presses are right there. Releases
    // pass whatever the mode, so a menu closing on a press leaves nothing stuck.
    if (menuUp || !state)
    {
        switch (action)
        {
            case PORT_ACT_MENU_UP:      sendKey(SDL_SCANCODE_UP, state); return;
            case PORT_ACT_MENU_DOWN:    sendKey(SDL_SCANCODE_DOWN, state); return;
            case PORT_ACT_MENU_LEFT:    sendKey(SDL_SCANCODE_LEFT, state); return;
            case PORT_ACT_MENU_RIGHT:   sendKey(SDL_SCANCODE_RIGHT, state); return;
            case PORT_ACT_MENU_SELECT:
            case PORT_ACT_MENU_CONFIRM: sendKey(SDL_SCANCODE_RETURN, state); return;
            case PORT_ACT_MENU_BACK:
            case PORT_ACT_MENU_ABORT:   sendKey(SDL_SCANCODE_ESCAPE, state); return;
            case PORT_ACT_MOUSE_LEFT:   MouseButton(state, BUTTON_PRIMARY); return;
            default: break;
        }
    }

    // Nothing below starts behind a menu; releases still fall through because the
    // touch layer emits its button-ups as the game controls fade out.
    if (menuUp && state)
        return;

    switch (action)
    {
        case PORT_ACT_FWD:        digitalFwd = state ? 1 : 0; return;
        case PORT_ACT_BACK:       digitalFwd = state ? -1 : 0; return;
        case PORT_ACT_MOVE_RIGHT: digitalSide = state ? 1 : 0; return;
        case PORT_ACT_MOVE_LEFT:  digitalSide = state ? -1 : 0; return;

        case PORT_ACT_RIGHT: yawJoy = state ? 1.0f : 0.0f; return;
        case PORT_ACT_LEFT:  yawJoy = state ? -1.0f : 0.0f; return;

        case PORT_ACT_ATTACK:     setHeld(FC_HELD_ATTACK, state); return;
        case PORT_ACT_ALT_FIRE:   setHeld(FC_HELD_GRENADE, state); return;
        case PORT_ACT_JUMP:
        case PORT_ACT_UP:         setHeld(FC_HELD_JUMP, state); return;
        case PORT_ACT_DOWN:       setHeld(FC_HELD_CROUCH, state); return;
        case PORT_ACT_SPEED:
        case PORT_ACT_SPRINT:     setHeld(FC_HELD_SPRINT, state); return;
        case PORT_ACT_LEAN_LEFT:  setHeld(FC_HELD_LEAN_LEFT, state); return;
        case PORT_ACT_LEAN_RIGHT: setHeld(FC_HELD_LEAN_RIGHT, state); return;

        // Toggles and one-shots: the game's aamOnPress actions.
        case PORT_ACT_CROUCH:
        case PORT_ACT_TOGGLE_CROUCH: if (state) queueImpulse(FC_IMP_CROUCH_TOGGLE); return;
        case PORT_ACT_USE:           if (state) queueImpulse(FC_IMP_USE); return;
        case PORT_ACT_RELOAD:        if (state) queueImpulse(FC_IMP_RELOAD); return;
        case PORT_ACT_FLASH_LIGHT:   if (state) queueImpulse(FC_IMP_FLASHLIGHT); return;
        case PORT_ACT_NEXT_WEP:      if (state) queueImpulse(FC_IMP_NEXT_WEAPON); return;
        case PORT_ACT_PREV_WEP:      if (state) queueImpulse(FC_IMP_PREV_WEAPON); return;
        case PORT_ACT_ALT_ATTACK:
        case PORT_ACT_ZOOM_IN:       if (state) queueImpulse(FC_IMP_ZOOM_TOGGLE); return;
        case PORT_ACT_HELPCOMP:      if (state) queueImpulse(FC_IMP_BINOCULARS); return;
        case PORT_ACT_TOGGLE_ALT_ATTACK: if (state) queueImpulse(FC_IMP_FIREMODE); return;
        case PORT_ACT_INVNEXT:       if (state) queueImpulse(FC_IMP_CYCLE_GRENADE); return;
        case PORT_ACT_INVDROP:       if (state) queueImpulse(FC_IMP_DROP_WEAPON); return;
        case PORT_ACT_THIRD_PERSON:  if (state) queueImpulse(FC_IMP_CHANGE_VIEW); return;

        default:
            // Weapon number grid: key n selects slot n, Far Cry's own 0-4 binds.
            if (action >= PORT_ACT_WEAP0 && action <= PORT_ACT_WEAP10)
            {
                if (state)
                    queueImpulse(FC_IMP_WEAPON_0 + action - PORT_ACT_WEAP0);
                return;
            }
            break;
    }
}

void PortableMove(float fwd, float strafe)
{
    PortableMoveFwd(fwd);
    PortableMoveSide(strafe);
}

void PortableMoveFwd(float fwd)
{
    stickFwd = clampUnit(fwd);
}

void PortableMoveSide(float strafe)
{
    stickSide = clampUnit(strafe);
}

void PortableLookPitch(int mode, float pitch)
{
    if (mode == LOOK_MODE_JOYSTICK)
        pitchJoy = clampUnit(pitch);
    else
        pitchMouse += pitch * LOOK_MOUSE_PITCH_SCALE;
}

void PortableLookYaw(int mode, float yaw)
{
    if (mode == LOOK_MODE_JOYSTICK)
        yawJoy = clampUnit(yaw);
    else
        yawMouse += yaw * LOOK_MOUSE_YAW_SCALE;
}

void PortableMouse(float dx, float dy)
{
    yawMouse += dx * LOOK_MOUSE_YAW_SCALE;
    pitchMouse += dy * LOOK_MOUSE_PITCH_SCALE;
}

void PortableMouseAbs(float x, float y)
{
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
    MouseButton(state, button);
}

void PortableCommand(const char *cmd)
{
}

void PortableAutomapControl(float zoom, float x, float y)
{
}

int PortableShowKeyboard(void)
{
    return 0;
}

bool PortableSetAlwaysRun(bool run)
{
    sprintToggle = run;
    return run;
}

touchscreemode_t PortableGetScreenMode()
{
    return (touchscreemode_t) farcry_screen_mode;
}
