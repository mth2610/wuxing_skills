# CONTROL MODULE API SPECIFICATION

> Module: `control/` (`control.h` / `control.c`) — ../../ROADMAP.md Module 4.
> Player controller: INPUT (device) split from INTENT (gameplay).
> Owner agent: **Control Agent** (see `control/CLAUDE.md`).

## 1. Scope & Design

`PlayerIntent` is a POD struct describing *what the player wants this frame*;
`Control_ReadIntent` fills it from the keyboard/mouse, `Control_Apply`
executes it through entities/skill_manager APIs. Touch (Phase 1) and
networking (`net/` serializes `PlayerIntent` verbatim) replace only the
ReadIntent side. Control never draws; camera + character animation + basic
attack stay in `game/` (they couple to VFX/anim).

```c
typedef struct {
    Vector2 moveDir;       // camera-relative XZ direction (unnormalized ok)
    bool    jump, dash, meditate;
    int     castSkillSlot; // -1 none; 0..3 = Agent.equippedSkills slot
    Vector3 aimPoint;      // mouse-ray ground point (future: auto-target)
} PlayerIntent;
```

## 2. API

```c
void Control_Init(int agentId);            // bind to an entities pool agent
int  Control_GetAgentId(void);
void Control_SetCamera(float yawRadians, const Camera3D *camera); // every frame, before ReadIntent
PlayerIntent Control_ReadIntent(void);     // WASD, SPACE jump, LSHIFT dash, G meditate, 1-4 cast
void Control_Apply(const PlayerIntent *in, float dt);
float Control_GetYaw(void);                // facing for character rendering
void Control_FaceTowards(Vector3 point);   // snap facing (game/'s attack turn)
void Control_SetCastCooldownMult(float mult); // zone rule hook — game/ sets per frame
```

## 3. Apply semantics

- **Movement**: 3.5 m/s × `Entity_GetSpeedMult`, via `Entity_SetPosition`
  (which breaks Thiền Định on a real move). Skipped while airborne,
  crowd-controlled, or mid-dash-burst — entities owns the position then.
- **Jump/Dash/Meditate**: direct `Entity_Jump(4.0)`, `Entity_Dash(dir, 10.0)`
  (cooldown gated inside entities), `Entity_StartMeditate`.
- **Cast**: slot → `Agent.equippedSkills[slot]` → `SkillManager_CanCast`
  (cooldown) → `CastSkill` (mana gate lives inside skill_manager) →
  `SkillManager_TriggerCooldown(1.0s × cooldownMult)` only on real success.
- **Thái Cực**: while `Agent.taijiActive`, the 4 normal slots are LOCKED;
  slot 0 casts `TAIJI_PHONG`, slot 1 `TAIJI_LOI`, slots 2-3 inert.

## 4. Explicitly NOT in this version

- No touch/gamepad layer (Phase 1) — keyboard/mouse only.
- No per-skill cooldown data (flat 1.0s × zone mult until game/ wires real
  per-skill params).
- No basic attack (owned by `game/`), no camera (owned by `game/`).
- Single local player (module-static state).

## Autotest

`control_intent` in `main.c`: movement distance & speedMult, meditate via
intent + cancel-on-move, dash burst arming, real cast charging mana and
engaging the cooldown.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-07-20 | Claude | Doc migration: moved from root `control/docs/API.md` to `control/docs/API.md` per `DOC_ARCHITECTURE.md` (no content changes needed — already pure interface) | root `control/docs/API.md` (read directly) | Ground-truth |
