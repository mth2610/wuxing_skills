// ui/ui.c — Module 9: minimal HUD + auto-targeting.
#include "ui/ui.h"
#include "entities/entities.h"
#include "combat/combat.h"
#include "boss/boss_system.h"
#include "core/skill_manager.h" // slot names + CanCast shading + element colors
#include <math.h>
#include <stddef.h>
#include <string.h>

// Meter-scaled search ranges.
static const float AIM_PROJECTILE_RANGE = 12.0f; // đối-đòn detection radius
static const float AIM_BOSS_RANGE       = 30.0f;

static const Camera3D *s_camera = NULL;
// Cached per-frame result so DrawOverlay reuses UI_Update's computation.
static bool    s_hasTarget = false;
static Vector3 s_aimPoint = { 0 };
static int     s_aimAgentId = -1;

// Loadout panel state.
static bool s_loadoutOpen = false;
static int  s_selSlot = 0;

void UI_Init(void) {
    s_camera = NULL;
    s_hasTarget = false;
    s_aimAgentId = -1;
    s_loadoutOpen = false;
    s_selSlot = 0;
}

static Color ElemColor(int elem) {
    switch (elem) {
        case 0:  return ELEMENT_COLOR_WATER;
        case 1:  return ELEMENT_COLOR_WOOD;
        case 2:  return ELEMENT_COLOR_FIRE;
        case 3:  return ELEMENT_COLOR_EARTH;
        case 4:  return ELEMENT_COLOR_METAL;
        default: return ELEMENT_COLOR_TAIJI;
    }
}

// Element of a registered skill: generated skills carry their element as
// the registry color (scripts/generate_registry.py assigns ELEMENT_COLOR_*
// by directory); the two legacy names get a lookup. -1 = no element.
static int SkillElement(int skillIdx) {
    Color c = GetRegisteredSkillColor(skillIdx);
    for (int e = 0; e <= 4; e++) {
        Color ec = ElemColor(e);
        if (c.r == ec.r && c.g == ec.g && c.b == ec.b) return e;
    }
    const char *n = GetRegisteredSkillName(skillIdx);
    if (strcmp(n, "FIRE") == 0) return 2;                                // legacy ORANGE
    if (strcmp(n, "TUBE") == 0 || strcmp(n, "WATER") == 0 ||
        strcmp(n, "SHIELD") == 0) return 0;                              // legacy BLUE/SKYBLUE
    if (strcmp(n, "WOOD") == 0) return 1;
    if (strcmp(n, "METAL") == 0) return 4;
    return -1;
}

// Player-equippable: has an element and isn't an internal/Thái Cực skill
// (PHONG/LÔI are granted BY the state, never equipped into it).
static bool SkillEquippable(int skillIdx) {
    const char *n = GetRegisteredSkillName(skillIdx);
    if (strncmp(n, "TAIJI_", 6) == 0) return false;
    if (strcmp(n, "CORE_TEST") == 0) return false;
    return SkillElement(skillIdx) >= 0;
}

void UI_ToggleLoadout(void) {
    s_loadoutOpen = !s_loadoutOpen;
}

bool UI_IsLoadoutOpen(void) {
    return s_loadoutOpen;
}

void UI_SetCamera(const Camera3D *camera) {
    s_camera = camera;
}

Vector3 UI_GetAutoAimPoint(int agentId, bool *hasTarget) {
    if (hasTarget) *hasTarget = false;
    const Agent *self = Entity_GetAgent(agentId);
    if (self == NULL) return (Vector3){ 0 };

    // Priority 1: nearest ENEMY projectile in flight — aiming at it lines up
    // the đối-đòn cast that triggers Đấu Pháp in combat/.
    CombatProjectileInfo projs[MAX_COMBAT_PROJECTILES];
    int n = Combat_QueryProjectiles(projs, MAX_COMBAT_PROJECTILES);
    int bestProj = -1;
    float bestSq = AIM_PROJECTILE_RANGE * AIM_PROJECTILE_RANGE;
    for (int i = 0; i < n; i++) {
        if (projs[i].team == self->team) continue;
        float dx = projs[i].pos.x - self->position.x;
        float dz = projs[i].pos.z - self->position.z;
        float dSq = dx * dx + dz * dz;
        if (dSq < bestSq) { bestSq = dSq; bestProj = i; }
    }
    if (bestProj >= 0) {
        if (hasTarget) *hasTarget = true;
        return projs[bestProj].pos;
    }

    // Priority 2: the opposing boss's core.
    if (Boss_IsAlive()) {
        const Agent *boss = Entity_GetAgent(Boss_GetAgentId());
        if (boss && boss->team != self->team) {
            float dx = boss->position.x - self->position.x;
            float dz = boss->position.z - self->position.z;
            if (dx * dx + dz * dz <= AIM_BOSS_RANGE * AIM_BOSS_RANGE) {
                if (hasTarget) *hasTarget = true;
                return boss->position;
            }
        }
    }

    return (Vector3){ 0 };
}

// --- Loadout panel layout (shared by Update's hit-testing and Draw) ---
static Rectangle LoadoutPanel(void) {
    float w = 640, h = 400;
    return (Rectangle){ GetScreenWidth() / 2.0f - w / 2, GetScreenHeight() / 2.0f - h / 2, w, h };
}
static Rectangle LoadoutSlotRect(Rectangle p, int slot) {
    return (Rectangle){ p.x + 18, p.y + 64 + slot * 74, 220, 62 };
}
static Rectangle LoadoutSkillRect(Rectangle p, int visIdx) {
    int col = visIdx % 2, row = visIdx / 2;
    return (Rectangle){ p.x + 262 + col * 186, p.y + 64 + row * 54, 176, 44 };
}

void UI_Update(float dt) {
    (void)dt;
    s_hasTarget = false;
    if (s_aimAgentId >= 0) {
        s_aimPoint = UI_GetAutoAimPoint(s_aimAgentId, &s_hasTarget);
    }

    // Loadout interaction: click a slot to select, click a skill to equip.
    if (s_loadoutOpen && s_aimAgentId >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        Rectangle p = LoadoutPanel();
        for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
            if (CheckCollisionPointRec(m, LoadoutSlotRect(p, slot))) {
                s_selSlot = slot;
                return;
            }
        }
        int vis = 0;
        for (int i = 0; i < GetRegisteredSkillCount(); i++) {
            if (!SkillEquippable(i)) continue;
            if (CheckCollisionPointRec(m, LoadoutSkillRect(p, vis))) {
                Entity_SetEquippedSkill(s_aimAgentId, s_selSlot, i, SkillElement(i));
                return;
            }
            vis++;
        }
    }
}

void UI_DrawOverlay(int agentId) {
    s_aimAgentId = agentId; // remembered for UI_Update's cached recompute
    const Agent *self = Entity_GetAgent(agentId);
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    // --- Skill slot chips (keys 1-4), cooldown-shaded. No Tutorial: the
    // only text is the key number + the skill's registered name. ---
    if (self != NULL) {
        const int chipW = 92, chipH = 34, gap = 8;
        int x0 = sw / 2 - (chipW * AGENT_SKILL_SLOTS + gap * (AGENT_SKILL_SLOTS - 1)) / 2;
        int y = sh - chipH - 14;
        for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
            int x = x0 + slot * (chipW + gap);
            int skillIdx = self->equippedSkills[slot];
            bool ready = (skillIdx >= 0) && SkillManager_CanCast(skillIdx, agentId);
            Color back = ready ? (Color){ 35, 35, 45, 230 } : (Color){ 18, 18, 22, 230 };
            DrawRectangle(x, y, chipW, chipH, back);
            DrawRectangleLines(x, y, chipW, chipH,
                               ready ? (Color){ 220, 220, 230, 200 } : (Color){ 90, 90, 100, 160 });
            DrawText(TextFormat("%d", slot + 1), x + 6, y + 4, 10, (Color){ 160, 160, 170, 255 });
            if (skillIdx >= 0) {
                Color c = GetRegisteredSkillColor(skillIdx);
                if (!ready) { c.r /= 2; c.g /= 2; c.b /= 2; }
                DrawText(GetRegisteredSkillName(skillIdx), x + 8, y + 16, 10, c);
            }
        }
    }

    // --- Loadout panel (TAB) ---
    if (s_loadoutOpen && self != NULL) {
        DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 140 }); // dim the fight
        Rectangle p = LoadoutPanel();
        DrawRectangleRec(p, (Color){ 16, 16, 22, 245 });
        DrawRectangleLinesEx(p, 2, (Color){ 200, 200, 215, 200 });
        DrawText("TRANG BI", (int)p.x + 18, (int)p.y + 14, 24, (Color){ 235, 230, 245, 255 });
        const char *elemNames[5] = { "THUY", "MOC", "HOA", "THO", "KIM" };
        int ce = self->currentElement;
        if (ce >= 0 && ce <= 4) {
            DrawText(TextFormat("HE: %s", elemNames[ce]),
                     (int)(p.x + p.width) - 130, (int)p.y + 20, 18, ElemColor(ce));
        }

        Vector2 m = GetMousePosition();

        // Left: the 4 equip slots.
        for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
            Rectangle r = LoadoutSlotRect(p, slot);
            bool sel = (slot == s_selSlot);
            bool hov = CheckCollisionPointRec(m, r);
            DrawRectangleRec(r, hov ? (Color){ 45, 45, 58, 255 } : (Color){ 30, 30, 40, 255 });
            DrawRectangleLinesEx(r, sel ? 3.0f : 1.0f,
                                 sel ? (Color){ 240, 220, 130, 255 } : (Color){ 120, 120, 135, 180 });
            DrawText(TextFormat("%d", slot + 1), (int)r.x + 8, (int)r.y + 6, 12, (Color){ 150, 150, 160, 255 });
            int idx = self->equippedSkills[slot];
            if (idx >= 0) {
                int e = self->equippedElements[slot];
                DrawRectangle((int)r.x + 8, (int)r.y + r.height - 12, 42, 5, ElemColor(e));
                DrawText(GetRegisteredSkillName(idx), (int)r.x + 26, (int)r.y + 22, 14,
                         GetRegisteredSkillColor(idx));
            } else {
                DrawText("-", (int)r.x + 26, (int)r.y + 22, 14, (Color){ 110, 110, 120, 255 });
            }
        }

        // Right: every equippable skill in the registry.
        int vis = 0;
        for (int i = 0; i < GetRegisteredSkillCount(); i++) {
            if (!SkillEquippable(i)) continue;
            Rectangle r = LoadoutSkillRect(p, vis);
            bool hov = CheckCollisionPointRec(m, r);
            DrawRectangleRec(r, hov ? (Color){ 48, 48, 62, 255 } : (Color){ 26, 26, 34, 255 });
            DrawRectangleLinesEx(r, 1, (Color){ 110, 110, 125, 170 });
            DrawRectangle((int)r.x + 6, (int)r.y + r.height - 9, 30, 4, ElemColor(SkillElement(i)));
            DrawText(GetRegisteredSkillName(i), (int)r.x + 8, (int)r.y + 8, 13,
                     GetRegisteredSkillColor(i));
            vis++;
        }
    }

    // --- Auto-aim reticle: pulsing double diamond over the current target
    // (đối-đòn/boss lock — casts fly here). ---
    if (s_hasTarget && s_camera != NULL) {
        Vector2 p = GetWorldToScreen((Vector3){ s_aimPoint.x, s_aimPoint.y + 0.4f, s_aimPoint.z }, *s_camera);
        if (p.x > -50 && p.x < sw + 50 && p.y > -50 && p.y < sh + 50) {
            float pulse = 1.0f + 0.25f * sinf((float)GetTime() * 8.0f);
            Color c = (Color){ 250, 235, 140, 255 };
            for (int layer = 0; layer < 2; layer++) {
                float r = (layer == 0 ? 12.0f : 7.0f) * pulse;
                DrawLineV((Vector2){ p.x, p.y - r }, (Vector2){ p.x + r, p.y }, c);
                DrawLineV((Vector2){ p.x + r, p.y }, (Vector2){ p.x, p.y + r }, c);
                DrawLineV((Vector2){ p.x, p.y + r }, (Vector2){ p.x - r, p.y }, c);
                DrawLineV((Vector2){ p.x - r, p.y }, (Vector2){ p.x, p.y - r }, c);
            }
            DrawCircleV(p, 2.0f, c);
        }
    }
}
