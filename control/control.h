// control/control.h
// Player controller (MODULES_ROADMAP.md Module 4) — the INPUT (device) layer
// is separated from the INTENT (gameplay) layer so touch/gamepad/network can
// later replace Control_ReadIntent without touching Control_Apply (net/ will
// serialize PlayerIntent as-is). CONTROL_API.md will document the contract.
//
// Owns: movement (khinh công), jump, dash, thiền định, and casting the 4
// equipped skills through core/skill_manager (cooldown gate here, mana gate
// inside CastSkill). Does NOT own: basic attack + camera + all rendering —
// those stay in game/ (they drive character animation and VFX, which pure
// logic must not touch).
#ifndef CONTROL_H
#define CONTROL_H

#include "raylib.h"
#include <stdbool.h>

typedef struct {
    Vector2 moveDir;       // camera-relative XZ move direction (unnormalized ok)
    bool    jump;
    bool    dash;
    bool    meditate;
    int     castSkillSlot; // -1 = none; 0..3 = Agent.equippedSkills slot
    Vector3 aimPoint;      // ground point being aimed at (mouse ray / auto-target)
} PlayerIntent;

// Attach the controller to an agent in the entities pool.
void Control_Init(int agentId);
int  Control_GetAgentId(void);

// Keyboard mapping needs the isometric camera yaw to turn WASD into world
// XZ; the camera pointer (optional, may be NULL) enables the mouse-ray
// aimPoint. Call every frame before Control_ReadIntent.
void Control_SetCamera(float yawRadians, const Camera3D *camera);

// Device → intent. Keyboard/mouse today: WASD move, SPACE jump, LEFT SHIFT
// dash, G meditate, keys 1-4 cast equipped slots 0-3.
PlayerIntent Control_ReadIntent(void);

// Intent → entities/skill_manager calls. Movement respects
// Entity_GetSpeedMult and is skipped while airborne/crowd-controlled (the
// entities module owns position during those). Casting checks
// SkillManager_CanCast, then CastSkill (which spends mana), then triggers
// the cooldown.
void Control_Apply(const PlayerIntent *in, float dt);

// Facing yaw (radians) — updated while moving, held otherwise. For the
// rendering side (character model orientation).
float Control_GetYaw(void);

// Snap facing toward a world point (e.g. game/'s basic attack turning the
// character toward its auto-found target).
void Control_FaceTowards(Vector3 point);

// Zone-rule hook (game/ computes the rule, control just consumes a number):
// multiplies the cooldown armed after a successful cast. 1.0 = normal;
// e.g. Thủy trong Sông → 0.5. Reset it every frame from game/.
void Control_SetCastCooldownMult(float mult);

// Cast feedback for the render side: returns the skillIndex of the cast
// that actually fired since the last call (mana+cooldown gates passed),
// or -1. game/ consumes it to play the character's cast animation —
// control is pure logic and cannot touch character/VFX itself.
int Control_ConsumeCastFired(void);

#endif // CONTROL_H
