// ui/ui.c — Module 9: minimal HUD + auto-targeting.
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "net/net_transport.h" // lobby roster + loadout sync
#include "entities/entities.h"
#include "combat/combat.h"
#include "boss/boss_system.h"
#include "skills/skill_manager.h" // slot names + CanCast shading + element colors
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Meter-scaled search ranges.
static const float AIM_PROJECTILE_RANGE = 12.0f; // đối-đòn detection radius
static const float AIM_BOSS_RANGE       = 30.0f;

static const Camera3D *s_camera = NULL;
// Cached per-frame result so DrawOverlay reuses UI_Update's computation.
static bool    s_hasTarget = false;
static Vector3 s_aimPoint = { 0 };
static int     s_aimAgentId = -1;
static int     s_manualTargetId = -1;

// Loadout panel state.
static bool s_loadoutOpen = false;
static int  s_selSlot = 0;

void UI_Init(void) {
    WuxiaUI_Init();
    s_camera = NULL;
    s_hasTarget = false;
    s_aimAgentId = -1;
    s_manualTargetId = -1;
    s_loadoutOpen = false;
    s_selSlot = 0;
}

void UI_SetManualTarget(int agentId) {
    s_manualTargetId = agentId;
}

int UI_GetManualTarget(void) {
    return s_manualTargetId;
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
    if (strcmp(n, "INFERNO_SURGE") == 0 || strcmp(n, "FIRE") == 0) return 2; // legacy ORANGE
    if (strcmp(n, "TIDAL_STREAM") == 0 || strcmp(n, "TUBE") == 0 || strcmp(n, "WATER") == 0 ||
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

    // Priority 0: Player's manual tap-locked target (if alive & within 32m).
    if (s_manualTargetId >= 0 && s_manualTargetId != agentId) {
        const Agent *mt = Entity_GetAgent(s_manualTargetId);
        if (mt && Entity_CanDamageTarget(agentId, s_manualTargetId)) {
            float dx = mt->position.x - self->position.x;
            float dz = mt->position.z - self->position.z;
            if (dx * dx + dz * dz <= 32.0f * 32.0f) {
                if (hasTarget) *hasTarget = true;
                return mt->position;
            }
        } else {
            s_manualTargetId = -1;
        }
    }

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

    // Priority 3: nearest hostile enemy (monster, boss, or hostile/sparring hero) within 24m.
    int bestHostile = -1;
    float bestHostileSq = 24.0f * 24.0f;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (i == agentId) continue;
        const Agent *cand = Entity_GetAgent(i);
        bool isPvpHero = (cand && cand->archetype == ARCH_HERO) &&
                         (self->pkMode != PK_MODE_PEACE || cand->pkMode != PK_MODE_PEACE);
        if (!cand || (cand->archetype != ARCH_MINION && cand->archetype != ARCH_BOSS && !isPvpHero)) continue;
        if (!Entity_CanDamageTarget(agentId, i)) continue;
        float dx = cand->position.x - self->position.x;
        float dz = cand->position.z - self->position.z;
        float dSq = dx * dx + dz * dz;
        if (dSq < bestHostileSq) {
            bestHostileSq = dSq;
            bestHostile = i;
        }
    }
    if (bestHostile >= 0) {
        if (hasTarget) *hasTarget = true;
        return Entity_GetAgent(bestHostile)->position;
    }

    return (Vector3){ 0 };
}

// --- Loadout panel layout (shared by Update's hit-testing and Draw) ---
static Rectangle LoadoutPanel(void) {
    float w = 640, h = 460;
    return (Rectangle){ GetScreenWidth() / 2.0f - w / 2, GetScreenHeight() / 2.0f - h / 2, w, h };
}
static Rectangle LoadoutSlotRect(Rectangle p, int slot) {
    return (Rectangle){ p.x + 18, p.y + 64 + slot * 74, 220, 62 };
}
static Rectangle LoadoutSkillRect(Rectangle p, int visIdx) {
    int col = visIdx % 2, row = visIdx / 2;
    return (Rectangle){ p.x + 262 + col * 186, p.y + 64 + row * 54, 176, 44 };
}
static Rectangle LoadoutCloseRect(Rectangle p) {
    return (Rectangle){ p.x + p.width - 96, p.y + 12, 82, 34 };
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
        if (CheckCollisionPointRec(m, LoadoutCloseRect(p))) {
            s_loadoutOpen = false;
            return;
        }
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
                // Connected client: the pool is a mirror — the swap must
                // happen on the HOST's copy of our hero too (Đợt A5).
                Net_ClientSendLoadout(s_selSlot, i, SkillElement(i));
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

    // --- Skill slot chips (keys 1-5 Strike | 6-7 Buff) — hidden on touch screens ---
    if (self != NULL && !UI_IsTouchControlsEnabled()) {
        int chipW = sw / 10;
        if (chipW < 104) chipW = 104;
        if (chipW > 136) chipW = 136;
        const int chipH = 42, gap = 8, groupGap = 20;
        int totalW = chipW * AGENT_SKILL_SLOTS + gap * (AGENT_SKILL_SLOTS - 2) + groupGap;
        int x0 = sw / 2 - totalW / 2;
        int y = sh - chipH - 14;
        for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
            bool isBuff = (slot >= AGENT_STRIKE_SLOTS);
            int x = x0 + slot * (chipW + gap) + (isBuff ? (groupGap - gap) : 0);
            if (slot == AGENT_STRIKE_SLOTS) {
                int divX = x - groupGap / 2;
                DrawLine(divX, y + 4, divX, y + chipH - 4, (Color){ 110, 210, 200, 140 });
            }
            int skillIdx = self->equippedSkills[slot];
            bool ready = (skillIdx >= 0) && SkillManager_CanCast(skillIdx, agentId);
            Color back = ready
                ? (isBuff ? (Color){ 16, 30, 38, 225 } : (Color){ 20, 24, 34, 225 })
                : (Color){ 14, 15, 20, 215 };
            Rectangle chipRec = { (float)x, (float)y, (float)chipW, (float)chipH };
            DrawRectangleRec(chipRec, back);

            Color c = (skillIdx >= 0) ? GetRegisteredSkillColor(skillIdx) : (Color){ 110, 115, 130, 255 };
            if (!ready) { c.r = (unsigned char)(c.r / 2); c.g = (unsigned char)(c.g / 2); c.b = (unsigned char)(c.b / 2); }

            // Subtle element bar at bottom of chip
            if (skillIdx >= 0) {
                DrawRectangle(x + 2, y + chipH - 4, chipW - 4, 3, c);
            }
            Color borderCol = ready
                ? (isBuff ? (Color){ 95, 220, 205, 225 } : (Color){ 195, 170, 105, 215 })
                : (Color){ 75, 80, 95, 170 };
            DrawRectangleLinesEx(chipRec, 1.5f, borderCol);

            const char *tag = isBuff ? TextFormat("[%d-B%d]", slot + 1, slot - AGENT_STRIKE_SLOTS + 1)
                                     : TextFormat("[%d]", slot + 1);
            DrawText(tag, x + 6, y + 5, 10,
                     ready ? (isBuff ? (Color){ 125, 235, 220, 240 } : (Color){ 210, 195, 145, 235 })
                           : (Color){ 120, 120, 130, 200 });
            if (skillIdx >= 0) {
                const char *sname = GetRegisteredSkillName(skillIdx);
                int fontSize = 11;
                int maxTextW = chipW - 14;
                if (MeasureText(sname, fontSize) > maxTextW) fontSize = 10;
                if (MeasureText(sname, fontSize) > maxTextW) fontSize = 9;
                char fitted[32];
                snprintf(fitted, sizeof(fitted), "%s", sname);
                int len = (int)strlen(fitted);
                while (len > 3 && MeasureText(fitted, fontSize) > maxTextW) {
                    fitted[--len] = '\0';
                    if (len >= 2) { fitted[len - 1] = '.'; fitted[len - 2] = '.'; }
                }
                DrawText(fitted, x + 7, y + 19, fontSize, c);
            } else {
                DrawText("-", x + chipW / 2 - 3, y + 18, 12, (Color){ 90, 95, 110, 200 });
            }
        }
    }

    // --- Loadout panel (TAB) ---
    if (s_loadoutOpen && self != NULL) {
        DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 155 }); // dim the fight
        Rectangle p = LoadoutPanel();
        WuxiaUI_DrawPanel(p, WUXIA_GOLD, true);
        WuxiaUI_DrawText("TRANG BI VO HOC", p.x + 20.0f, p.y + 15.0f, 22.0f, WUXIA_GOLD);
        const char *elemNames[5] = { "THUY", "MOC", "HOA", "THO", "KIM" };
        int ce = self->currentElement;
        if (ce >= 0 && ce <= 4) {
            DrawText(TextFormat("HE: %s", elemNames[ce]),
                     (int)(p.x + p.width) - 210, (int)p.y + 20, 18, ElemColor(ce));
        }
        Rectangle closeRec = LoadoutCloseRect(p);
        DrawRectangleRec(closeRec, (Color){ 95, 32, 32, 240 });
        DrawRectangleLinesEx(closeRec, 1.5f, (Color){ 235, 130, 120, 235 });
        DrawText("DONG", (int)closeRec.x + 19, (int)closeRec.y + 9, 15, WHITE);

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

// --- Shared Mobile Touch Combat Controls ---
#if defined(PLATFORM_ANDROID) || defined(__ANDROID__)
static bool s_touchControlsEnabled = true;
#else
static bool s_touchControlsEnabled = false;
#endif

static int     s_touchJoyPointerId = -1;
static Vector2 s_touchJoyKnob      = { 0, 0 };
static bool    s_prevTouchAttack   = false;
static bool    s_prevTouchKhinh    = false;
static bool    s_prevTouchDodge    = false;
static bool    s_prevTouchSkill[7] = { false, false, false, false, false, false, false };

void UI_SetTouchControlsEnabled(bool enabled) {
    s_touchControlsEnabled = enabled;
    if (!enabled) {
        s_touchJoyPointerId = -1;
        s_touchJoyKnob = (Vector2){ 0, 0 };
    }
}

bool UI_IsTouchControlsEnabled(void) {
    return s_touchControlsEnabled;
}

void UI_FormatShortSkillName(const char *fullName, char *outBuf, int bufSize) {
    if (!outBuf || bufSize <= 0) return;
    outBuf[0] = '\0';
    if (!fullName || !fullName[0]) {
        snprintf(outBuf, (size_t)bufSize, "-");
        return;
    }
    int maxChars = bufSize - 1;
    if (maxChars > 6) maxChars = 6;
    int len = 0;
    for (int i = 0; fullName[i] != '\0' && len < maxChars; i++) {
        if (fullName[i] == '_') {
            if (len >= 3) break; // keep first word if >= 3 chars (e.g. GLACIAL, STONE, LEAF)
            continue;
        }
        outBuf[len++] = fullName[i];
    }
    outBuf[len] = '\0';
}

typedef struct {
    float   scale;
    Vector2 joyCenter;
    float   joyBaseR;
    float   joyKnobR;
    Vector2 attackCenter;
    float   attackR;
    Vector2 khinhCenter;
    float   khinhR;
    Vector2 dodgeCenter;
    float   dodgeR;
    Vector2 meditateCenter;
    float   meditateR;
    Vector2 skillCenter[7];
    float   skillR;
    float   buffR;
} TouchCombatLayout;

static TouchCombatLayout ComputeTouchCombatLayout(void) {
    TouchCombatLayout L;
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float s = sh / 700.0f;
    if (s < 0.75f) s = 0.75f;
    if (s > 1.45f) s = 1.45f;
    L.scale = s;

    L.joyCenter = (Vector2){ 130.0f * s, sh - 130.0f * s };
    L.joyBaseR  = 62.0f * s;
    L.joyKnobR  = 26.0f * s;

    L.attackCenter = (Vector2){ sw - 105.0f * s, sh - 102.0f * s };
    L.attackR      = 46.0f * s;

    L.khinhCenter  = (Vector2){ sw - 208.0f * s, sh - 44.0f * s };
    L.khinhR       = 25.0f * s;

    L.dodgeCenter  = (Vector2){ sw - 282.0f * s, sh - 54.0f * s };
    L.dodgeR       = 25.0f * s;

    /* Move Meditate above the left joystick to keep the upper-right free for Buffs */
    L.meditateCenter = (Vector2){ 62.0f * s, sh - 232.0f * s };
    L.meditateR      = 22.0f * s;

    /* 5 Strike skills [0..4] on a clean arc around Basic Attack (178°..78°, step 25°) */
    float arcR = 136.0f * s;
    L.skillR   = 25.0f * s;
    for (int k = 0; k < 5; k++) {
        float angleRad = (178.0f - (float)k * 25.0f) * (PI / 180.0f);
        L.skillCenter[k].x = L.attackCenter.x + cosf(angleRad) * arcR;
        L.skillCenter[k].y = L.attackCenter.y - sinf(angleRad) * arcR;
    }

    /* 2 Buff/Support slots [5..6] separated into a dedicated upper-right vertical column */
    L.buffR = 24.0f * s;
    L.skillCenter[5] = (Vector2){ sw - 46.0f * s, sh - 308.0f * s }; /* B1 (Key 6) */
    L.skillCenter[6] = (Vector2){ sw - 46.0f * s, sh - 372.0f * s }; /* B2 (Key 7) */
    return L;
}

static float Dist2D(Vector2 a, Vector2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

void UI_PollTouchCombat(const UITouchSkillSlot slots[7], UITouchCombatInput *outInput) {
    if (!outInput) return;
    memset(outInput, 0, sizeof(*outInput));
    if (!s_touchControlsEnabled) {
        s_prevTouchAttack = false;
        s_prevTouchKhinh  = false;
        s_prevTouchDodge  = false;
        for (int k = 0; k < 7; k++) s_prevTouchSkill[k] = false;
        return;
    }

    TouchCombatLayout L = ComputeTouchCombatLayout();
    Vector2 pts[10];
    int     ids[10];
    int count = GetTouchPointCount();
    if (count > 10) count = 10;
    for (int i = 0; i < count; i++) {
        pts[i] = GetTouchPosition(i);
        ids[i] = GetTouchPointId(i);
    }
    if (count == 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        pts[0] = GetMousePosition();
        ids[0] = -2;
        count = 1;
    }

    // Keep joystick locked to its pointer ID even when right-hand fingers lift/press.
    int joyIdx = -1;
    if (s_touchJoyPointerId != -1) {
        for (int i = 0; i < count; i++) {
            if (ids[i] == s_touchJoyPointerId) { joyIdx = i; break; }
        }
        if (joyIdx < 0) s_touchJoyPointerId = -1;
    }
    if (s_touchJoyPointerId == -1) {
        for (int i = 0; i < count; i++) {
            if (Dist2D(pts[i], L.joyCenter) <= L.joyBaseR * 1.95f) {
                s_touchJoyPointerId = ids[i];
                joyIdx = i;
                break;
            }
        }
    }

    s_touchJoyKnob = (Vector2){ 0, 0 };
    if (joyIdx >= 0) {
        Vector2 dv = { pts[joyIdx].x - L.joyCenter.x, pts[joyIdx].y - L.joyCenter.y };
        float len = sqrtf(dv.x * dv.x + dv.y * dv.y);
        if (len > L.joyBaseR) {
            dv.x = (dv.x / len) * L.joyBaseR;
            dv.y = (dv.y / len) * L.joyBaseR;
        }
        s_touchJoyKnob = dv;
        outInput->joyX = dv.x / L.joyBaseR;
        outInput->joyZ = dv.y / L.joyBaseR;
        outInput->joyActive = true;
        outInput->pointerOverUI = true;
    }

    bool attackNow   = false;
    bool khinhNow    = false;
    bool dodgeNow    = false;
    bool meditateNow = false;
    bool skillNow[7] = { false, false, false, false, false, false, false };

    for (int i = 0; i < count; i++) {
        if (i == joyIdx) continue;
        Vector2 pt = pts[i];
        if (Dist2D(pt, L.attackCenter) <= L.attackR * 1.25f) {
            attackNow = true;
            outInput->pointerOverUI = true;
            continue;
        }
        if (Dist2D(pt, L.khinhCenter) <= L.khinhR * 1.35f) {
            khinhNow = true;
            outInput->pointerOverUI = true;
            continue;
        }
        if (Dist2D(pt, L.dodgeCenter) <= L.dodgeR * 1.35f) {
            dodgeNow = true;
            outInput->pointerOverUI = true;
            continue;
        }
        if (Dist2D(pt, L.meditateCenter) <= L.meditateR * 1.35f) {
            meditateNow = true;
            outInput->pointerOverUI = true;
            continue;
        }
        for (int k = 0; k < 7; k++) {
            bool hasSkill = (slots == NULL) || (slots[k].skillIndex >= 0);
            float btnR = (k < 5) ? L.skillR : L.buffR;
            if (hasSkill && Dist2D(pt, L.skillCenter[k]) <= btnR * 1.35f) {
                skillNow[k] = true;
                outInput->pointerOverUI = true;
                break;
            }
        }
    }

    outInput->attackHeld    = attackNow;
    outInput->attackPressed = attackNow && !s_prevTouchAttack;
    s_prevTouchAttack       = attackNow;

    outInput->khinhCongHeld    = khinhNow;
    outInput->khinhCongPressed = khinhNow && !s_prevTouchKhinh;
    s_prevTouchKhinh           = khinhNow;

    outInput->dodgeHeld    = dodgeNow;
    outInput->dodgePressed = dodgeNow && !s_prevTouchDodge;
    s_prevTouchDodge       = dodgeNow;

    outInput->meditateHeld = meditateNow;

    for (int k = 0; k < 7; k++) {
        outInput->skillHeld[k]    = skillNow[k];
        outInput->skillPressed[k] = skillNow[k] && !s_prevTouchSkill[k];
        s_prevTouchSkill[k]       = skillNow[k];
    }
}

static void DrawWuxiaTouchCircleBtn(Vector2 center, float radius, Color accent, bool active,
                                    bool ready, const char *topTag, const char *mainLabel,
                                    float scale) {
    Color inner = active
        ? (Color){ (unsigned char)(accent.r / 3 + 35), (unsigned char)(accent.g / 3 + 35), (unsigned char)(accent.b / 3 + 35), 225 }
        : (ready ? (Color){ 16, 20, 30, 205 } : (Color){ 10, 12, 16, 185 });
    Color outer = (Color){ (unsigned char)(inner.r * 0.65f), (unsigned char)(inner.g * 0.65f), (unsigned char)(inner.b * 0.65f), inner.a };
    Color ring = active ? WUXIA_GOLD_BRIGHT : (ready ? accent : (Color){ 78, 84, 96, 160 });

    DrawCircleGradient(center, radius, inner, outer);
    DrawRing(center, radius - 2.2f * scale, radius, 0.0f, 360.0f, 36, ring);
    DrawRing(center, radius - 5.2f * scale, radius - 4.2f * scale, 0.0f, 360.0f, 36, ColorAlpha(WUXIA_GOLD, ready ? 0.35f : 0.14f));

    if (topTag && topTag[0]) {
        float tagSize = 11.0f * scale;
        if (tagSize < 10.0f) tagSize = 10.0f;
        float tw = WuxiaUI_MeasureText(topTag, tagSize);
        WuxiaUI_DrawText(topTag, center.x - tw * 0.5f, center.y - radius * 0.68f,
                         tagSize, ColorAlpha(ready ? WUXIA_GOLD_BRIGHT : GRAY, 0.92f));
    }
    if (mainLabel && mainLabel[0]) {
        float fontSize = 13.5f * scale;
        if (fontSize < 11.0f) fontSize = 11.0f;
        float lw = WuxiaUI_MeasureText(mainLabel, fontSize);
        while (lw > radius * 1.72f && fontSize > 9.0f) {
            fontSize -= 0.5f;
            lw = WuxiaUI_MeasureText(mainLabel, fontSize);
        }
        float ty = (topTag && topTag[0]) ? (center.y - fontSize * 0.25f) : (center.y - fontSize * 0.5f);
        WuxiaUI_DrawText(mainLabel, center.x - lw * 0.5f, ty, fontSize,
                         ready ? WUXIA_TEXT_PRIMARY : (Color){ 155, 160, 172, 220 });
    }
}

void UI_DrawTouchCombat(const UITouchSkillSlot slots[7], const UITouchCombatInput *input) {
    if (!s_touchControlsEnabled) return;

    rlDrawRenderBatchActive();
    EndShaderMode();
    BeginBlendMode(BLEND_ALPHA);

    Camera2D cam2d = { 0 };
    cam2d.zoom = 1.0f;
    BeginMode2D(cam2d);
    rlSetTexture(0);

    TouchCombatLayout L = ComputeTouchCombatLayout();
    float s = L.scale;

    // 1. Left Movement Joystick
    bool joyActive = input ? input->joyActive : (s_touchJoyPointerId != -1);
    DrawCircleV(L.joyCenter, L.joyBaseR, (Color){ 14, 18, 26, 125 });
    DrawRing(L.joyCenter, L.joyBaseR - 2.0f * s, L.joyBaseR, 0.0f, 360.0f, 36,
             joyActive ? (Color){ 130, 225, 210, 210 } : (Color){ 185, 170, 120, 130 });
    // Cardinal tick dots
    for (int d = 0; d < 4; d++) {
        float a = (float)d * (PI * 0.5f);
        Vector2 tick = { L.joyCenter.x + cosf(a) * (L.joyBaseR - 8.0f * s),
                         L.joyCenter.y + sinf(a) * (L.joyBaseR - 8.0f * s) };
        DrawCircleV(tick, 2.2f * s, ColorAlpha(WHITE, 0.35f));
    }
    Vector2 knobPos = { L.joyCenter.x + s_touchJoyKnob.x, L.joyCenter.y + s_touchJoyKnob.y };
    DrawCircleV(knobPos, L.joyKnobR, joyActive ? (Color){ 90, 205, 190, 215 } : (Color){ 175, 185, 200, 155 });
    DrawRing(knobPos, L.joyKnobR - 2.0f * s, L.joyKnobR, 0.0f, 360.0f, 24, WHITE);

    // 2. Large Basic Attack Button ("DANH")
    bool atkActive = input ? input->attackHeld : s_prevTouchAttack;
    Color atkAccent = (Color){ 235, 175, 75, 240 };
    DrawWuxiaTouchCircleBtn(L.attackCenter, L.attackR, atkAccent, atkActive, true, NULL, "DANH", s * 1.18f);

    // 3. Khinh Công ("KHINH"), Dodge ("NE"), and Meditate ("THIEN")
    bool khinhActive = input ? input->khinhCongHeld : s_prevTouchKhinh;
    Color khinhAccent = (Color){ 95, 220, 215, 235 };
    DrawWuxiaTouchCircleBtn(L.khinhCenter, L.khinhR, khinhAccent, khinhActive, true, "^", "KHINH", s * 0.92f);

    bool dodgeActive = input ? input->dodgeHeld : s_prevTouchDodge;
    Color dodgeAccent = (Color){ 240, 205, 110, 235 };
    DrawWuxiaTouchCircleBtn(L.dodgeCenter, L.dodgeR, dodgeAccent, dodgeActive, true, NULL, "NE", s * 0.96f);

    bool medActive = input ? input->meditateHeld : false;
    Color medAccent = (Color){ 130, 195, 255, 235 };
    DrawWuxiaTouchCircleBtn(L.meditateCenter, L.meditateR, medAccent, medActive, true, NULL, "THIEN", s * 0.85f);

    // 4. 5 Strike Buttons [0..4] on arc + 2 Buff Buttons [5..6] in dedicated upper-right column
    for (int k = 0; k < 7; k++) {
        if (slots && slots[k].skillIndex < 0) continue;
        bool isBuff = (k >= 5);
        bool held = input ? input->skillHeld[k] : s_prevTouchSkill[k];
        bool ready = slots ? slots[k].ready : true;
        float cd = slots ? slots[k].cooldownRemain : 0.0f;
        Color col = isBuff
            ? (Color){ 85, 225, 205, 240 }
            : (slots ? slots[k].color : (Color){ 120, 190, 255, 230 });
        const char *lbl = (!ready && cd > 0.05f)
            ? TextFormat("%.1fs", cd)
            : ((slots && slots[k].shortLabel[0]) ? slots[k].shortLabel
                                                 : (isBuff ? TextFormat("B%d", k - 4) : TextFormat("S%d", k + 1)));
        const char *topTag = isBuff ? TextFormat("B%d", k - 4) : TextFormat("%d", k + 1);
        float btnR = isBuff ? L.buffR : L.skillR;
        if (isBuff) {
            /* Subtle outer jade aura halo to visually separate Buff slots from Strike arc */
            DrawRing(L.skillCenter[k], btnR + 1.5f * s, btnR + 3.2f * s, 0.0f, 360.0f, 32,
                     ColorAlpha((Color){ 95, 235, 215, 255 }, ready ? 0.55f : 0.16f));
        }
        DrawWuxiaTouchCircleBtn(L.skillCenter[k], btnR, col, held, ready, topTag, lbl, s * 0.90f);
    }

    EndMode2D();
    EndBlendMode();
}
