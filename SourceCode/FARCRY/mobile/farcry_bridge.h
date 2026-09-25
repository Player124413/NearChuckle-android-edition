// Touch input crossing from libfarcry (Android UI thread) into CryGame's frame (engine thread).
// libfarcry queues; CryGame drains once per frame and turns the result into CXClient actions,
// so rebinding keys in the game's own options cannot break the touch controls.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Held while the button is down; CryGame re-sends the action every frame (aamOnHold actions).
enum FarCryHeld
{
    FC_HELD_ATTACK,
    FC_HELD_JUMP,
    FC_HELD_CROUCH,
    FC_HELD_SPRINT,
    FC_HELD_WALK,
    FC_HELD_LEAN_LEFT,
    FC_HELD_LEAN_RIGHT,
    FC_HELD_GRENADE,
    FC_HELD_COUNT
};

// One-shot presses (aamOnPress actions), delivered once in the order they were queued.
enum FarCryImpulse
{
    FC_IMP_USE = 1,
    FC_IMP_RELOAD,
    FC_IMP_FLASHLIGHT,
    FC_IMP_NEXT_WEAPON,
    FC_IMP_PREV_WEAPON,
    FC_IMP_CROUCH_TOGGLE,
    FC_IMP_PRONE,
    FC_IMP_ZOOM_TOGGLE,
    FC_IMP_BINOCULARS,
    FC_IMP_FIREMODE,
    FC_IMP_CYCLE_GRENADE,
    FC_IMP_DROP_WEAPON,
    FC_IMP_CHANGE_VIEW,
    FC_IMP_QUICKSAVE,
    FC_IMP_QUICKLOAD,
    FC_IMP_WEAPON_0 = 32 // + slot, matches ACTION_WEAPON_0 + slot
};

#define FC_MAX_IMPULSES 16

typedef struct FarCryTouchInput
{
    float moveFwd;   // -1..1, positive forward
    float moveSide;  // -1..1, positive right
    unsigned held;   // bit per FarCryHeld
    int impulses[FC_MAX_IMPULSES];
    int impulseCount;
} FarCryTouchInput;

// Engine thread, once per frame. Snapshots the sticks and held buttons, drains the impulses,
// and pushes the accumulated look as an SDL mouse-motion event (the real mouse's path, so the
// game's sensitivity and inversion settings apply). frameTime scales the joystick look rate.
void FarCry_DrainTouchInput(FarCryTouchInput *out, float frameTime);

// CryGame reports each frame which overlay the touch layer should show.
void FarCry_ReportScreenMode(int inGame, int consoleOpen);

#ifdef __cplusplus
}
#endif
