// ui/ui.c — Module 9: Unified PC & Mobile HUD + Auto-Targeting ("Where Winds Meet" Style).
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "net/net_transport.h"
#include "entities/entities.h"
#include "combat/combat.h"
#include "boss/boss_system.h"
#include "skills/skill_manager.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static const float AIM_PROJECTILE_RANGE = 12.0f;
static const float AIM_BOSS_RANGE       = 30.0f;

static const Camera3D *s_camera = NULL;
static bool    s_hasTarget = false;
static Vector3 s_aimPoint = { 0 };
static int     s_aimAgentId = -1;
static int     s_manualTargetId = -1;

// Loadout panel state
static bool s_loadoutOpen = false;
static int  s_selSlot = 0;

// Smooth damage trail for bottom-center HP bar
static float s_hpTrailRatio = 1.0f;

void UI_Init(void) {
    WuxiaUI_Init();
    s_camera = NULL;
    s_hasTarget = false;
    s_aimAgentId = -1;
    s_manualTargetId = -1;
    s_loadoutOpen = false;
    s_selSlot = 0;
    s_hpTrailRatio = 1.0f;
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

static int SkillElement(int skillIdx) {
    Color c = GetRegisteredSkillColor(skillIdx);
    for (int e = 0; e <= 4; e++) {
        Color ec = ElemColor(e);
        if (c.r == ec.r && c.g == ec.g && c.b == ec.b) return e;
    }
    const char *n = GetRegisteredSkillName(skillIdx);
    if (strcmp(n, "INFERNO_SURGE") == 0 || strcmp(n, "FIRE") == 0) return 2;
    if (strcmp(n, "TIDAL_STREAM") == 0 || strcmp(n, "TUBE") == 0 || strcmp(n, "WATER") == 0 ||
        strcmp(n, "SHIELD") == 0) return 0;
    if (strcmp(n, "WOOD") == 0) return 1;
    if (strcmp(n, "METAL") == 0) return 4;
    return -1;
}

static bool SkillEquippable(int skillIdx) {
    const char *n = GetRegisteredSkillName(skillIdx);
    if (strncmp(n, "TAIJI_", 6) == 0) return false;
    if (strcmp(n, "CORE_TEST") == 0) return false;
    return SkillElement(skillIdx) >= 0;
}

static const char *SkillDisplayNameVN(int skillIdx) {
    if (skillIdx < 0) return "-";
    const char *raw = GetRegisteredSkillName(skillIdx);
    const VoHocSkillDef *def = VoHoc_FindByName(raw);
    if (def && def->displayName && def->displayName[0]) return def->displayName;
    return raw;
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

    // Priority 0: Manual tap-locked target
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

    // Priority 1: Nearest enemy projectile in flight (đối-đòn)
    CombatProjectileInfo projs[MAX_COMBAT_PROJECTILES];
    int n = Combat_QueryProjectiles(projs, MAX_COMBAT_PROJECTILES);
    int bestProj = -1;
    float bestSq = AIM_PROJECTILE_RANGE * AIM_PROJECTILE_RANGE;
    for (int i = 0; i < n; i++) {
        if (!Entity_CanDamageTarget(agentId, projs[i].ownerAgentId)) continue;
        float dx = projs[i].pos.x - self->position.x;
        float dz = projs[i].pos.z - self->position.z;
        float dSq = dx * dx + dz * dz;
        if (dSq < bestSq) { bestSq = dSq; bestProj = i; }
    }
    if (bestProj >= 0) {
        if (hasTarget) *hasTarget = true;
        return projs[bestProj].pos;
    }

    // Priority 2: Opposing boss
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

    // Priority 3: Nearest hostile enemy within 24m
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

// --- Loadout panel layout ---
static Rectangle LoadoutPanel(void) {
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float w = fminf(700.0f, sw - 40.0f);
    float h = fminf(540.0f, sh - 60.0f);
    return (Rectangle){ sw * 0.5f - w * 0.5f, sh * 0.5f - h * 0.5f, w, h };
}
static Rectangle LoadoutSlotRect(Rectangle p, int slot) {
    return (Rectangle){ p.x + 18, p.y + 68 + slot * 64, 240, 56 };
}
static Rectangle LoadoutSkillRect(Rectangle p, int visIdx) {
    int col = visIdx % 2, row = visIdx / 2;
    return (Rectangle){ p.x + 276 + col * 204, p.y + 68 + row * 54, 194, 46 };
}
static Rectangle LoadoutCloseRect(Rectangle p) {
    return (Rectangle){ p.x + p.width - 96, p.y + 14, 82, 32 };
}

void UI_Update(float dt) {
    s_hasTarget = false;
    if (s_aimAgentId >= 0) {
        s_aimPoint = UI_GetAutoAimPoint(s_aimAgentId, &s_hasTarget);
        const Agent *self = Entity_GetAgent(s_aimAgentId);
        if (self && self->maxHealth > 0.0f) {
            float r = self->health / self->maxHealth;
            if (r < 0.0f) r = 0.0f;
            if (r > 1.0f) r = 1.0f;
            if (s_hpTrailRatio > r) {
                s_hpTrailRatio -= dt * 0.32f;
                if (s_hpTrailRatio < r) s_hpTrailRatio = r;
            } else {
                s_hpTrailRatio = r;
            }
        }
    }

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
                Net_ClientSendLoadout(s_selSlot, i, SkillElement(i));
                return;
            }
            vis++;
        }
    }
}

void UI_DrawCompassMinimap(Vector3 playerPos, float camYaw,
                           Vector3 questTarget, bool hasQuestTarget, bool inSafeZone) {
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    float s = ctx->scale;
    float r = 42.0f * s;
    Vector2 c = { ctx->safeRect.x + r + 4.0f, ctx->safeRect.y + r + 2.0f };

    // Translucent circular ink-wash radar
    DrawCircleV(c, r, (Color){ 14, 18, 26, 175 });
    DrawRing(c, r - 1.5f, r, 0.0f, 360.0f, 36,
             inSafeZone ? ColorAlpha(WUXIA_JADE, 0.72f) : ColorAlpha(WUXIA_GOLD, 0.65f));
    DrawRing(c, r * 0.52f - 0.5f, r * 0.52f + 0.5f, 0.0f, 360.0f, 24, ColorAlpha(WHITE, 0.10f));

    // North cardinal tick rotated by camera yaw
    float nAng = -camYaw - PI * 0.5f;
    Vector2 nPt = { c.x + cosf(nAng) * (r - 8.0f * s), c.y + sinf(nAng) * (r - 8.0f * s) };
    WuxiaUI_DrawText("B", nPt.x - 4.0f, nPt.y - 6.0f, 10.0f, ColorAlpha(WUXIA_GOLD_BRIGHT, 0.82f));

    // Quest / Objective direction dot
    if (hasQuestTarget) {
        float dx = questTarget.x - playerPos.x;
        float dz = questTarget.z - playerPos.z;
        float dist = sqrtf(dx * dx + dz * dz);
        float worldAng = atan2f(dx, dz) - camYaw;
        float normDist = fminf(1.0f, dist / 60.0f) * (r - 9.0f * s);
        Vector2 qPt = { c.x + sinf(worldAng) * normDist, c.y - cosf(worldAng) * normDist };
        WuxiaUI_DrawDiamond(qPt, 4.5f * s, WUXIA_GOLD_BRIGHT, WUXIA_GOLD);
    }

    // Center player chevron
    WuxiaUI_DrawDiamond(c, 3.8f * s, inSafeZone ? WUXIA_JADE : WUXIA_TEXT_PRIMARY, WUXIA_GOLD_BRIGHT);
}

void UI_DrawPlayerVitals(int agentId, int healPotions, int manaPotions,
                         bool *outUseHeal, bool *outUseMana) {
    if (outUseHeal) *outUseHeal = false;
    if (outUseMana) *outUseMana = false;
    const Agent *agent = Entity_GetAgent(agentId);
    if (!agent) return;

    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    float s = ctx->scale;
    float sw = ctx->screenW;
    float sh = ctx->screenH;

    float hp = agent->health, maxHp = (agent->maxHealth > 0.0f) ? agent->maxHealth : 1.0f;
    float mana = agent->mana, maxMana = (agent->maxMana > 0.0f) ? agent->maxMana : 1.0f;
    float stam = agent->stamina, maxStam = (agent->maxStamina > 0.0f) ? agent->maxStamina : 1.0f;

    float hpRatio   = fmaxf(0.0f, fminf(1.0f, hp / maxHp));
    float manaRatio = fmaxf(0.0f, fminf(1.0f, mana / maxMana));
    float stamRatio = fmaxf(0.0f, fminf(1.0f, stam / maxStam));

    // Bottom-center low-profile Where Winds Meet vitals bar
    float barW = ctx->isTouchMode ? (250.0f * s) : (310.0f * s);
    float totalH = 38.0f * s;
    float startX = sw * 0.5f - barW * 0.5f;
    float startY = sh - totalH - 16.0f * s;

    // Element Crest on left of the bars
    Color elemCol = ElemColor(agent->currentElement);
    Vector2 crestC = { startX - 22.0f * s, startY + 14.0f * s };
    WuxiaUI_DrawDiamond(crestC, 12.0f * s, ColorAlpha(elemCol, 0.35f),
                        agent->taijiActive ? WUXIA_GOLD_BRIGHT : elemCol);
    WuxiaUI_DrawDiamond(crestC, 6.0f * s, elemCol, WHITE);

    // Numeric HP & Level/Taiji tag above the bar
    WuxiaUI_DrawText(TextFormat("Sinh Lực %d/%d", (int)hp, (int)maxHp),
                     startX, startY - 15.0f * s, 11.5f * s, WUXIA_TEXT_PRIMARY);
    if (agent->taijiActive) {
        WuxiaUI_DrawTextRight("THÁI CỰC", startX + barW, startY - 15.0f * s, 11.0f * s, WUXIA_GOLD_BRIGHT);
    }

    // Main HP bar (with white damage trail)
    Rectangle hpRec = { startX, startY, barW, 10.0f * s };
    WuxiaUI_DrawSleekBar(hpRec, hpRatio, s_hpTrailRatio,
                         (Color){ 198, 52, 52, 255 }, (Color){ 238, 102, 82, 255 }, true);

    // Sub-bar: Chân Khí (MP)
    Rectangle mpRec = { startX, startY + 13.0f * s, barW * 0.86f, 5.0f * s };
    Color mpA = agent->taijiActive ? WUXIA_GOLD : (Color){ 58, 138, 232, 255 };
    Color mpB = agent->taijiActive ? WUXIA_GOLD_BRIGHT : (Color){ 108, 198, 252, 255 };
    WuxiaUI_DrawSleekBar(mpRec, manaRatio, manaRatio, mpA, mpB, true);

    // Sub-bar: Thể Lực (Stamina)
    Rectangle stRec = { startX, startY + 20.5f * s, barW * 0.72f, 4.0f * s };
    Color stA = agent->staminaExhausted ? (Color){ 185, 95, 45, 255 } : (Color){ 72, 188, 128, 255 };
    Color stB = agent->staminaExhausted ? (Color){ 225, 135, 65, 255 } : (Color){ 126, 238, 178, 255 };
    WuxiaUI_DrawSleekBar(stRec, stamRatio, stamRatio, stA, stB, true);

    // Quick Potion slots right of the vitals bar (if any)
    if (healPotions >= 0 && manaPotions >= 0) {
        float px = startX + barW + 14.0f * s;
        float py = startY + 10.0f * s;
        float pr = 15.0f * s;
        if (WuxiaUI_DrawSkillNode((Vector2){ px + pr, py }, pr, WUXIA_ICON_POTION,
                                  TextFormat("%d", healPotions), "R",
                                  WUXIA_CRIMSON, healPotions > 0, false, 0.0f, 0.0f, false)) {
            if (outUseHeal) *outUseHeal = true;
        }
        if (WuxiaUI_DrawSkillNode((Vector2){ px + pr * 3.3f, py }, pr, WUXIA_ICON_POTION,
                                  TextFormat("%d", manaPotions), "F1",
                                  WUXIA_AZURE, manaPotions > 0, false, 0.0f, 0.0f, false)) {
            if (outUseMana) *outUseMana = true;
        }
    }
}

// --- Unified Bottom-Right Skill Cluster Layout (shared PC & Mobile) ---
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

static TouchCombatLayout ComputeCombatClusterLayout(bool touchMode) {
    TouchCombatLayout L;
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float s = sh / 720.0f;
    if (s < 0.78f) s = 0.78f;
    if (s > 1.45f) s = 1.45f;
    if (!touchMode) s *= 0.84f; // compact on PC, spacious on touch
    L.scale = s;

    L.joyCenter = (Vector2){ 126.0f * s, sh - 126.0f * s };
    L.joyBaseR  = 60.0f * s;
    L.joyKnobR  = 25.0f * s;

    L.attackCenter = (Vector2){ sw - 96.0f * s, sh - 92.0f * s };
    L.attackR      = 42.0f * s;

    L.khinhCenter  = (Vector2){ sw - 196.0f * s, sh - 44.0f * s };
    L.khinhR       = 24.0f * s;

    L.dodgeCenter  = (Vector2){ sw - 266.0f * s, sh - 54.0f * s };
    L.dodgeR       = 24.0f * s;

    L.meditateCenter = touchMode
        ? (Vector2){ 62.0f * s, sh - 228.0f * s }
        : (Vector2){ sw - 332.0f * s, sh - 62.0f * s };
    L.meditateR      = 21.0f * s;

    // 5 Strike skills [0..4] on clean arc around Basic Attack
    float arcR = 132.0f * s;
    L.skillR   = 25.0f * s;
    for (int k = 0; k < 5; k++) {
        float angleRad = (176.0f - (float)k * 25.0f) * (PI / 180.0f);
        L.skillCenter[k].x = L.attackCenter.x + cosf(angleRad) * arcR;
        L.skillCenter[k].y = L.attackCenter.y - sinf(angleRad) * arcR;
    }

    // 2 Buff/Support slots [5..6] in dedicated upper-right vertical column
    L.buffR = 23.0f * s;
    L.skillCenter[5] = (Vector2){ sw - 46.0f * s, sh - 292.0f * s };
    L.skillCenter[6] = (Vector2){ sw - 46.0f * s, sh - 354.0f * s };
    return L;
}

static void BuildSlotsFromAgent(int agentId, UITouchSkillSlot outSlots[7]) {
    const Agent *self = Entity_GetAgent(agentId);
    for (int k = 0; k < 7; k++) {
        outSlots[k].skillIndex = -1;
        outSlots[k].shortLabel[0] = '\0';
        outSlots[k].color = (Color){ 120, 180, 230, 255 };
        outSlots[k].ready = false;
        outSlots[k].cooldownRemain = 0.0f;
        outSlots[k].cooldownTotal = 0.0f;
        if (self && k < AGENT_SKILL_SLOTS) {
            int idx = self->equippedSkills[k];
            if (idx >= 0) {
                outSlots[k].skillIndex = idx;
                UI_FormatShortSkillName(SkillDisplayNameVN(idx), outSlots[k].shortLabel, sizeof(outSlots[k].shortLabel));
                outSlots[k].color = GetRegisteredSkillColor(idx);
                outSlots[k].ready = SkillManager_CanCast(idx, agentId);
                outSlots[k].cooldownRemain = SkillManager_GetCooldownRemaining(idx, agentId);
                outSlots[k].cooldownTotal  = Skill_GetCooldown(idx);
            }
        }
    }
}

void UI_DrawOverlay(int agentId) {
    s_aimAgentId = agentId;
    const Agent *self = Entity_GetAgent(agentId);
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    // --- PC Bottom-Right Martial Skill Cluster (same layout as Mobile, with keycaps) ---
    if (self != NULL && !UI_IsTouchControlsEnabled()) {
        UITouchSkillSlot slots[7];
        BuildSlotsFromAgent(agentId, slots);
        TouchCombatLayout L = ComputeCombatClusterLayout(false);

        WuxiaUI_DrawSkillNode(L.attackCenter, L.attackR, WUXIA_ICON_SWORD,
                              "Đánh", "RMB", WUXIA_GOLD, true,
                              IsMouseButtonDown(MOUSE_BUTTON_RIGHT), 0.0f, 0.0f, false);

        WuxiaUI_DrawSkillNode(L.khinhCenter, L.khinhR, WUXIA_ICON_WIND_BOOT,
                              "Nhảy", "Space", WUXIA_JADE, true,
                              IsKeyDown(KEY_SPACE), 0.0f, 0.0f, false);

        WuxiaUI_DrawSkillNode(L.dodgeCenter, L.dodgeR, WUXIA_ICON_WIND_BOOT,
                              "Né", "Shift", WUXIA_GOLD, true,
                              IsKeyDown(KEY_LEFT_SHIFT), 0.0f, 0.0f, false);

        WuxiaUI_DrawSkillNode(L.meditateCenter, L.meditateR, WUXIA_ICON_LOTUS,
                              "Thiền", "G", WUXIA_AZURE, true,
                              self->isMeditating, 0.0f, 0.0f, false);

        static const char *kHotkeys[7] = { "1", "2", "3", "4", "5", "6", "7" };
        for (int k = 0; k < 7; k++) {
            bool isBuff = (k >= 5);
            float r = isBuff ? L.buffR : L.skillR;
            bool hasSkill = (slots[k].skillIndex >= 0);
            Color col = isBuff ? WUXIA_JADE : (hasSkill ? slots[k].color : WUXIA_TEXT_MUTED);
            if (isBuff && hasSkill) {
                DrawRing(L.skillCenter[k], r + 2.0f, r + 3.5f, 0.0f, 360.0f, 28,
                         ColorAlpha(WUXIA_JADE, slots[k].ready ? 0.45f : 0.14f));
            }
            WuxiaUI_DrawSkillNode(L.skillCenter[k], r, WUXIA_ICON_NONE,
                                  hasSkill ? slots[k].shortLabel : "-", kHotkeys[k],
                                  col, hasSkill && slots[k].ready,
                                  IsKeyDown(KEY_ONE + k),
                                  slots[k].cooldownRemain, slots[k].cooldownTotal, !isBuff);
        }
    }

    // --- Loadout panel (TAB in PvP) ---
    if (s_loadoutOpen && self != NULL) {
        DrawRectangle(0, 0, sw, sh, (Color){ 6, 8, 14, 175 });
        Rectangle p = LoadoutPanel();
        WuxiaUI_DrawPanel(p, WUXIA_GOLD, true);
        WuxiaUI_DrawDiamond((Vector2){ p.x + 22.0f, p.y + 26.0f }, 5.5f, WUXIA_GOLD, WUXIA_GOLD_BRIGHT);
        WuxiaUI_DrawTextTitle("TRANG BỊ VÕ HỌC", p.x + 36.0f, p.y + 13.0f, 22.0f, WUXIA_GOLD_BRIGHT);

        static const char *elemNames[5] = { "THỦY", "MỘC", "HỎA", "THỔ", "KIM" };
        int ce = self->currentElement;
        if (ce >= 0 && ce <= 4) {
            WuxiaUI_DrawTextRight(TextFormat("HỆ HIỆN TẠI: %s", elemNames[ce]),
                                  p.x + p.width - 112.0f, p.y + 19.0f, 15.0f, ElemColor(ce));
        }
        Rectangle closeRec = LoadoutCloseRect(p);
        if (WuxiaUI_DrawButton(closeRec, "ĐÓNG", WUXIA_CRIMSON, false, 13.0f)) {
            s_loadoutOpen = false;
            return;
        }
        WuxiaUI_DrawSpecularRule(p.x + 16.0f, p.y + 54.0f, p.width - 32.0f, WUXIA_GOLD, true);

        const WuxiaUIContext *ctx = WuxiaUI_GetContext();
        Vector2 m = ctx->pointerPos;

        for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
            Rectangle r = LoadoutSlotRect(p, slot);
            bool sel = (slot == s_selSlot);
            bool hov = CheckCollisionPointRec(m, r);
            bool isBuff = (slot >= AGENT_STRIKE_SLOTS);
            Color brd = sel ? WUXIA_GOLD_BRIGHT : (hov ? WUXIA_GOLD : (isBuff ? ColorAlpha(WUXIA_JADE, 0.55f) : WUXIA_BORDER_SUBTLE));
            DrawRectangleRec(r, sel ? (Color){ 44, 40, 26, 245 } : (hov ? WUXIA_BG_CARD_HOV : WUXIA_BG_CARD));
            DrawRectangleLinesEx(r, sel ? 2.0f : 1.0f, brd);

            WuxiaUI_DrawText(isBuff ? TextFormat("Ô TRỢ [%d]", slot + 1) : TextFormat("Ô ĐẢ [%d]", slot + 1),
                             r.x + 10.0f, r.y + 6.0f, 11.5f, isBuff ? WUXIA_JADE : WUXIA_TEXT_MUTED);
            int idx = self->equippedSkills[slot];
            if (idx >= 0) {
                int e = self->equippedElements[slot];
                DrawRectangle((int)r.x, (int)r.y, 4, (int)r.height, ElemColor(e));
                WuxiaUI_DrawText(SkillDisplayNameVN(idx), r.x + 12.0f, r.y + 24.0f, 14.5f,
                                 GetRegisteredSkillColor(idx));
            } else {
                WuxiaUI_DrawText("<Trống>", r.x + 12.0f, r.y + 24.0f, 13.0f, WUXIA_TEXT_MUTED);
            }
        }

        int vis = 0;
        for (int i = 0; i < GetRegisteredSkillCount(); i++) {
            if (!SkillEquippable(i)) continue;
            Rectangle r = LoadoutSkillRect(p, vis);
            if (r.y + r.height > p.y + p.height - 14.0f) break;
            bool hov = CheckCollisionPointRec(m, r);
            Color ec = ElemColor(SkillElement(i));
            DrawRectangleRec(r, hov ? WUXIA_BG_CARD_HOV : WUXIA_BG_CARD);
            DrawRectangleLinesEx(r, 1.0f, hov ? WUXIA_GOLD_BRIGHT : ColorAlpha(ec, 0.55f));
            DrawRectangle((int)r.x, (int)r.y + 4, 3, (int)r.height - 8, ec);
            WuxiaUI_DrawText(SkillDisplayNameVN(i), r.x + 10.0f, r.y + 13.0f, 13.5f,
                             GetRegisteredSkillColor(i));
            vis++;
        }
    }

    // --- Auto-aim reticle ---
    if (s_hasTarget && s_camera != NULL) {
        Vector2 p = GetWorldToScreen((Vector3){ s_aimPoint.x, s_aimPoint.y + 0.4f, s_aimPoint.z }, *s_camera);
        if (p.x > -50 && p.x < sw + 50 && p.y > -50 && p.y < sh + 50) {
            float pulse = 1.0f + 0.25f * sinf((float)GetTime() * 8.0f);
            Color c = (Color){ 250, 235, 140, 255 };
            for (int layer = 0; layer < 2; layer++) {
                float r = (layer == 0 ? 12.0f : 7.0f) * pulse;
                DrawLineEx((Vector2){ p.x, p.y - r }, (Vector2){ p.x + r, p.y }, 1.4f, c);
                DrawLineEx((Vector2){ p.x + r, p.y }, (Vector2){ p.x, p.y + r }, 1.4f, c);
                DrawLineEx((Vector2){ p.x, p.y + r }, (Vector2){ p.x - r, p.y }, 1.4f, c);
                DrawLineEx((Vector2){ p.x - r, p.y }, (Vector2){ p.x, p.y - r }, 1.4f, c);
            }
            DrawCircleV(p, 2.2f, c);
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

// Preserves UTF-8 multi-byte Vietnamese characters cleanly when abbreviating to 2 words / <= 14 bytes
void UI_FormatShortSkillName(const char *fullName, char *outBuf, int bufSize) {
    if (!outBuf || bufSize <= 0) return;
    outBuf[0] = '\0';
    if (!fullName || !fullName[0]) {
        snprintf(outBuf, (size_t)bufSize, "-");
        return;
    }

    const VoHocSkillDef *def = VoHoc_FindByName(fullName);
    const char *src = (def && def->displayName && def->displayName[0]) ? def->displayName : fullName;

    // Take up to first 2 words (or 1st segment if underscore-delimited)
    int words = 0;
    int outLen = 0;
    int i = 0;
    while (src[i] != '\0' && outLen < bufSize - 1) {
        unsigned char c = (unsigned char)src[i];
        int charBytes = 1;
        if ((c & 0xE0) == 0xC0) charBytes = 2;
        else if ((c & 0xF0) == 0xE0) charBytes = 3;
        else if ((c & 0xF8) == 0xF0) charBytes = 4;

        if (c == ' ' || c == '_') {
            words++;
            if (words >= 2 || (c == '_' && outLen >= 3)) break;
            if (outLen + 1 >= bufSize - 1) break;
            outBuf[outLen++] = ' ';
            i++;
            continue;
        }
        if (outLen + charBytes >= bufSize) break;
        for (int b = 0; b < charBytes && src[i] != '\0'; b++) {
            outBuf[outLen++] = src[i++];
        }
    }
    outBuf[outLen] = '\0';
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

    TouchCombatLayout L = ComputeCombatClusterLayout(true);
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

    outInput->attackHeld       = attackNow;
    outInput->attackPressed    = attackNow && !s_prevTouchAttack;
    s_prevTouchAttack          = attackNow;

    outInput->khinhCongHeld    = khinhNow;
    outInput->khinhCongPressed = khinhNow && !s_prevTouchKhinh;
    s_prevTouchKhinh           = khinhNow;

    outInput->dodgeHeld        = dodgeNow;
    outInput->dodgePressed     = dodgeNow && !s_prevTouchDodge;
    s_prevTouchDodge           = dodgeNow;

    outInput->meditateHeld     = meditateNow;

    for (int k = 0; k < 7; k++) {
        outInput->skillHeld[k]    = skillNow[k];
        outInput->skillPressed[k] = skillNow[k] && !s_prevTouchSkill[k];
        s_prevTouchSkill[k]       = skillNow[k];
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

    TouchCombatLayout L = ComputeCombatClusterLayout(true);
    float s = L.scale;

    // 1. Left Movement Joystick
    bool joyActive = input ? input->joyActive : (s_touchJoyPointerId != -1);
    DrawCircleV(L.joyCenter, L.joyBaseR, (Color){ 14, 18, 26, 125 });
    DrawRing(L.joyCenter, L.joyBaseR - 2.0f * s, L.joyBaseR, 0.0f, 360.0f, 36,
             joyActive ? WUXIA_JADE : ColorAlpha(WUXIA_GOLD, 0.55f));
    for (int d = 0; d < 4; d++) {
        float a = (float)d * (PI * 0.5f);
        Vector2 tick = { L.joyCenter.x + cosf(a) * (L.joyBaseR - 8.0f * s),
                         L.joyCenter.y + sinf(a) * (L.joyBaseR - 8.0f * s) };
        DrawCircleV(tick, 2.2f * s, ColorAlpha(WHITE, 0.35f));
    }
    Vector2 knobPos = { L.joyCenter.x + s_touchJoyKnob.x, L.joyCenter.y + s_touchJoyKnob.y };
    DrawCircleV(knobPos, L.joyKnobR, joyActive ? ColorAlpha(WUXIA_JADE, 0.85f) : (Color){ 175, 185, 200, 155 });
    DrawRing(knobPos, L.joyKnobR - 2.0f * s, L.joyKnobR, 0.0f, 360.0f, 24, WHITE);

    // 2. Basic Attack
    bool atkActive = input ? input->attackHeld : s_prevTouchAttack;
    WuxiaUI_DrawSkillNode(L.attackCenter, L.attackR, WUXIA_ICON_SWORD, "Đánh", NULL,
                          WUXIA_GOLD, true, atkActive, 0.0f, 0.0f, false);

    // 3. Khinh Công, Né, Thiền
    bool khinhActive = input ? input->khinhCongHeld : s_prevTouchKhinh;
    WuxiaUI_DrawSkillNode(L.khinhCenter, L.khinhR, WUXIA_ICON_WIND_BOOT, "Khinh", NULL,
                          WUXIA_JADE, true, khinhActive, 0.0f, 0.0f, false);

    bool dodgeActive = input ? input->dodgeHeld : s_prevTouchDodge;
    WuxiaUI_DrawSkillNode(L.dodgeCenter, L.dodgeR, WUXIA_ICON_WIND_BOOT, "Né", NULL,
                          WUXIA_GOLD, true, dodgeActive, 0.0f, 0.0f, false);

    bool medActive = input ? input->meditateHeld : false;
    WuxiaUI_DrawSkillNode(L.meditateCenter, L.meditateR, WUXIA_ICON_LOTUS, "Thiền", NULL,
                          WUXIA_AZURE, true, medActive, 0.0f, 0.0f, false);

    // 4. 5 Strike Buttons [0..4] + 2 Buff Buttons [5..6]
    for (int k = 0; k < 7; k++) {
        if (slots && slots[k].skillIndex < 0) continue;
        bool isBuff = (k >= 5);
        bool held = input ? input->skillHeld[k] : s_prevTouchSkill[k];
        bool ready = slots ? slots[k].ready : true;
        float cd = slots ? slots[k].cooldownRemain : 0.0f;
        float cdTot = slots ? slots[k].cooldownTotal : 0.0f;
        Color col = isBuff ? WUXIA_JADE : (slots ? slots[k].color : WUXIA_AZURE);
        const char *lbl = (slots && slots[k].shortLabel[0])
            ? slots[k].shortLabel
            : (isBuff ? TextFormat("Trợ %d", k - 4) : TextFormat("Chiêu %d", k + 1));
        float btnR = isBuff ? L.buffR : L.skillR;
        if (isBuff) {
            DrawRing(L.skillCenter[k], btnR + 1.5f * s, btnR + 3.2f * s, 0.0f, 360.0f, 32,
                     ColorAlpha(WUXIA_JADE, ready ? 0.55f : 0.16f));
        }
        WuxiaUI_DrawSkillNode(L.skillCenter[k], btnR, WUXIA_ICON_NONE, lbl, NULL,
                              col, ready, held, cd, cdTot, false);
    }

    EndMode2D();
    EndBlendMode();
}
