# UI MODULE API SPECIFICATION

> Module: `ui/` (`ui.h` / `ui.c`) — MODULES_ROADMAP.md Module 9 (HUD +
> Auto-Targeting). Owner agent: **UI Agent** (see `ui/CLAUDE.md`).

## 1. Scope & Design

Minimal by philosophy (No Tutorial): skill slot chips + an auto-aim reticle.
Auto-target priority (thiết kế §XI, mobile-first):
1. **Nearest ENEMY projectile in flight** within 12 m — aiming at it lines
   up the đối-đòn cast that triggers Đấu Pháp. Read from combat/'s
   last-frame snapshot (`Combat_QueryProjectiles`).
2. **The enemy boss** within 30 m (`boss/` singleton).
3. Nothing — the caller keeps its own aim (mouse ray).

The result feeds `PlayerIntent.aimPoint` in `game/game_screen.c`; control/
never knows ui/ exists.

## 2. API

```c
void UI_Init(void);
void UI_SetCamera(const Camera3D *camera); // per frame (reticle projection)
void UI_Update(float dt);                  // recompute the cached aim
Vector3 UI_GetAutoAimPoint(int agentId, bool *hasTarget);
void UI_DrawOverlay(int agentId);          // 2D pass: slot chips + reticle
```

- Slot chips: keys 1-4, registered skill name in its element color,
  dimmed + darkened while `SkillManager_CanCast` is false (cooldown).
- Reticle: yellow diamond projected over the target via `GetWorldToScreen`.
- All reads, no `Entity_*` mutations.

## 3. Explicitly NOT in this version

- Mobile virtual buttons / touch input surface (Phase 1 follow-up alongside
  control/'s touch ReadIntent).
- Projectile *lead* prediction (aims at current snapshot position).
- HP/mana/boss bars stay in `game/game_screen.c` (predate this module).

## Autotest

`ui_auto_target` in `main.c`: boss targeted when the field is clear;
projectile beats boss when one is in flight; no target when neither exists.
