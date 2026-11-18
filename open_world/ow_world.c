// open_world/ow_world.c
#include "open_world/ow_world.h"
#include "open_world/ow_loot.h"
#include "open_world/ow_quest_npc.h"
#include "character/player_profile.h"
#include "combat/combat.h"
#include "core/composition/visual_composer.h"
#include "entities/entities.h"
#include "environment/wind/wind_system.h"
#include "game/quest_datau.h"
#include "maps/map_manager.h"
#include "net/net_transport.h"
#include "skills/skill_manager.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_EMOTE_BUBBLES     8
#define GUIDING_WIND_DURATION 2.4f

typedef struct {
    Vector3 pos;
    float   cooldown;
} OWWindSurge;

typedef struct {
    int   agentId;
    int   emoteId;
    float timer;
} EmoteBubble;

static OWRealmDef   s_realm = { 0 };
static bool         s_hasRealm = false;
static OWWindSurge  s_windSurges[OW_MAX_WIND_SURGES];
static int          s_windSurgeCount = 0;
static EmoteBubble  s_emotes[MAX_EMOTE_BUBBLES];

static float s_mightTimer     = 0.0f;
static float s_xpBoostTimer   = 0.0f;
static float s_sandstormTimer = 80.0f;
static float s_bannerTimer    = 0.0f;
static char  s_bannerText[128] = { 0 };
static bool  s_emoteMenuOpen  = false;
static bool  s_tradeMenuOpen  = false;
static float s_clock          = 0.0f;

static bool    s_windActive    = false;
static float   s_windElapsed   = 0.0f;
static float   s_windAutoTimer = 6.0f;
static Vector3 s_windStartPos  = { 0 };
static Vector3 s_windEndPos    = { 0 };
static Vector3 s_lastPlayerPos = { 0 };

static int   s_killStreak      = 0;
static float s_killStreakTimer = 0.0f;

static float DistXZ(Vector3 a, Vector3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

void OW_SetBanner(const char *msg, float seconds) {
    snprintf(s_bannerText, sizeof(s_bannerText), "%s", msg ? msg : "");
    s_bannerTimer = seconds;
}

int OW_GetZoneAt(Vector3 pos) {
    if (!s_hasRealm) return 0;
    if (DistXZ(pos, s_realm.safeCenter) <= s_realm.safeRadius) return 0;
    for (int i = s_realm.zoneCount - 1; i >= 1; i--) {
        if (DistXZ(pos, s_realm.zones[i].center) <= s_realm.zones[i].radius) return i;
    }
    return (s_realm.zoneCount > 2) ? 2 : 0;
}

const char *OW_GetZoneName(int zoneIndex) {
    if (s_hasRealm && zoneIndex >= 0 && zoneIndex < s_realm.zoneCount && s_realm.zones[zoneIndex].label) {
        return s_realm.zones[zoneIndex].label;
    }
    return "GIANG HO NGU HANH";
}

bool OW_IsInSafeZone(Vector3 pos) {
    return s_hasRealm && (DistXZ(pos, s_realm.safeCenter) <= s_realm.safeRadius);
}

float OW_GetMightTimer(void) { return s_mightTimer; }
float OW_GetXpBoostTimer(void) { return s_xpBoostTimer; }
int   OW_GetKillStreak(void) { return s_killStreak; }

Vector3 OW_GetDaTauNpcPos(void) {
    Vector3 p = s_realm.daTauNpcPos;
    p.y = MapManager_GetGroundHeightAt(p.x, p.z);
    return p;
}

int OW_GetActiveNodeCount(void) { return OWLoot_GetActiveNodeCount(); }
void OW_TriggerSandstormEvent(void) { OWLoot_TriggerSandstormReveal(); }
void OW_SyncQuestEncounter(void) { OWQuestNpc_SyncEncounter(); }

Vector3 OW_GetGuideTarget(Vector3 playerPos) {
    const DaTauQuest *q = DaTau_GetCurrent();
    if (!q || q->phase == DATAU_PHASE_WAIT_ACCEPT || q->phase == DATAU_PHASE_READY_RETURN) {
        return OW_GetDaTauNpcPos();
    }
    Vector3 encPos = { 0 };
    if (OWQuestNpc_GetEncounterGuideTarget(playerPos, &encPos)) return encPos;

    if (q->type == DATAU_FORAGE_COLLECT) {
        Vector3 nodePos = { 0 };
        if (OWLoot_GetNearestAvailableNodePos(playerPos, &nodePos)) return nodePos;
    }
    if (q->type == DATAU_BOSS_TRIAL || q->type == DATAU_SLAY_ELITE) return s_realm.badlandsCenter;
    if (q->type == DATAU_CLASH_MASTERY || q->type == DATAU_SLAY_RANGED) return s_realm.canyonCenter;
    return s_realm.dunesCenter;
}

void OW_TriggerGuidingWind(void) {
    if (!s_hasRealm) return;
    Vector3 tgt = OW_GetGuideTarget(s_lastPlayerPos);
    float dx = tgt.x - s_lastPlayerPos.x;
    float dz = tgt.z - s_lastPlayerPos.z;
    float d = sqrtf(dx * dx + dz * dz);
    if (d < 1.5f) return;

    float nx = dx / d, nz = dz / d;
    float reach = (d > 24.0f) ? 24.0f : d;
    s_windStartPos = (Vector3){
        s_lastPlayerPos.x - nx * 2.5f,
        MapManager_GetGroundHeightAt(s_lastPlayerPos.x - nx * 2.5f, s_lastPlayerPos.z - nz * 2.5f),
        s_lastPlayerPos.z - nz * 2.5f
    };
    s_windEndPos = (Vector3){
        s_lastPlayerPos.x + nx * reach,
        MapManager_GetGroundHeightAt(s_lastPlayerPos.x + nx * reach, s_lastPlayerPos.z + nz * reach),
        s_lastPlayerPos.z + nz * reach
    };
    Wind_TriggerGuidingWind(s_lastPlayerPos, s_windEndPos, 18.0f, GUIDING_WIND_DURATION);
    s_windActive = true;
    s_windElapsed = 0.0f;
    s_windAutoTimer = 8.0f;
}

bool OW_TryInteractNearest(Vector3 playerPos) {
    if (OWQuestNpc_TryInteract(playerPos)) return true;
    return OWLoot_TryHarvestNearest(playerPos);
}

static void GrantSkillXpWithFeedback(int skillIndex, uint32_t baseXp, Vector3 worldPos) {
    if (skillIndex < 0) return;
    uint32_t xp = baseXp;
    if (s_killStreak >= 5) xp = (uint32_t)((float)xp * 1.25f);
    if (s_xpBoostTimer > 0.0f) xp *= 2u;

    bool leveledUp = PlayerProfile_AddSkillXp(skillIndex, xp);
    const char *skName = GetRegisteredSkillName(skillIndex);
    AddFloatingText(worldPos, TextFormat("+%u XP %s", xp, skName ? skName : ""),
                    (Color){ 130, 235, 255, 255 }, 14.0f, 1.3f);
    if (leveledUp) {
        int newLv = PlayerProfile_GetEffectiveSkillLevel(skillIndex, false);
        OW_SetBanner(TextFormat("DOT PHA VO HOC! %s DAT CAP %d!", skName ? skName : "CHIEU", newLv), 3.5f);
        AddFloatingText((Vector3){ worldPos.x, worldPos.y + 1.1f, worldPos.z },
                        TextFormat("THANG CAP LV.%d!", newLv), GOLD, 20.0f, 2.2f);
    }
}

static void RecordEmote(int agentId, int emoteId) {
    if (emoteId <= 0) return;
    for (int i = 0; i < MAX_EMOTE_BUBBLES; i++) {
        if (s_emotes[i].agentId == agentId || s_emotes[i].timer <= 0.0f) {
            s_emotes[i] = (EmoteBubble){ .agentId = agentId, .emoteId = emoteId, .timer = 4.0f };
            return;
        }
    }
}

static void UsePotionKey(PlayerEntity *player, PotionType type) {
    const Agent *pa = player ? Entity_GetAgent(player->agentId) : NULL;
    if (!pa) return;
    if (!PlayerProfile_ConsumePotion(type)) {
        OW_SetBanner("HET DAN DUOC! GAP LUYEN DAN SU TAI OC DAO DE DOI!", 2.4f);
        return;
    }
    if (type == POTION_HEAL_HP) {
        Entity_ApplyDamage(player->agentId, -45.0f, (Vector3){ 0 });
        AddFloatingText(pa->position, "+45 HP (HOI SINH DAN)", GREEN, 16.0f, 1.5f);
    } else if (type == POTION_RESTORE_MANA) {
        Entity_RestoreMana(player->agentId, 60.0f);
        AddFloatingText(pa->position, "+60 MANA (TU KHI DICH)", SKYBLUE, 16.0f, 1.5f);
    } else if (type == POTION_EARTH_MIGHT) {
        s_mightTimer = 60.0f;
        AddFloatingText(pa->position, "HOANG THO HO THE (+35% DMG, 60s)", GOLD, 16.0f, 1.8f);
    } else if (type == POTION_SKILL_XP_BOOST) {
        s_xpBoostTimer = 90.0f;
        AddFloatingText(pa->position, "NGO DAO LINH TRA (x2 XP SKILL, 90s)", (Color){ 140, 255, 220, 255 }, 16.0f, 1.8f);
    }
    PlayerProfile_SaveGlobal();
}

void OW_Init(const OWRealmDef *realm, bool spawnCamps) {
    if (!realm) return;
    s_realm = *realm;
    s_hasRealm = true;

    memset(s_emotes, 0, sizeof(s_emotes));
    memset(s_windSurges, 0, sizeof(s_windSurges));
    s_mightTimer = 0.0f;
    s_xpBoostTimer = 0.0f;
    s_sandstormTimer = 80.0f;
    s_bannerTimer = 0.0f;
    s_emoteMenuOpen = false;
    s_tradeMenuOpen = false;
    s_clock = 0.0f;
    s_lastPlayerPos = realm->defaultStartPos;
    s_windActive = false;
    s_windElapsed = 0.0f;
    s_windAutoTimer = 1.5f;
    s_killStreak = 0;
    s_killStreakTimer = 0.0f;

    DaTau_Init(realm->realmId);
    DaTau_SetNpcFlowEnabled(true);

    OWLoot_Init(realm);
    OWQuestNpc_Init(realm, spawnCamps);

    s_windSurgeCount = realm->windSurgeCount;
    if (s_windSurgeCount > OW_MAX_WIND_SURGES) s_windSurgeCount = OW_MAX_WIND_SURGES;
    for (int i = 0; i < s_windSurgeCount; i++) {
        Vector3 wp = realm->windSurges[i];
        wp.y = MapManager_GetGroundHeightAt(wp.x, wp.z);
        s_windSurges[i] = (OWWindSurge){ .pos = wp, .cooldown = 0.0f };
    }

    if (spawnCamps) {
        for (int i = 0; i < realm->campCount && i < OW_MAX_CAMPS; i++) {
            const OWCampSpawn *c = &realm->camps[i];
            AI_SpawnMonsterCamp(c->center, c->count, c->element, c->kind);
        }
    }

    OW_SetBanner("THEO LUONG GIO DAN LOI DEN GAP DA TAU [!] NHAN NHIEM VU!", 4.5f);
}

void OW_Update(PlayerEntity *player, float dt) {
    if (!s_hasRealm) return;
    s_clock += dt;
    if (s_bannerTimer > 0.0f) s_bannerTimer -= dt;
    if (s_mightTimer > 0.0f) s_mightTimer = fmaxf(0.0f, s_mightTimer - dt);
    if (s_xpBoostTimer > 0.0f) s_xpBoostTimer = fmaxf(0.0f, s_xpBoostTimer - dt);

    if (s_windActive) {
        s_windElapsed += dt;
        if (s_windElapsed >= GUIDING_WIND_DURATION) {
            s_windActive = false;
            s_windElapsed = 0.0f;
        }
    } else {
        s_windAutoTimer -= dt;
        if (s_windAutoTimer <= 0.0f) OW_TriggerGuidingWind();
    }

    if (s_killStreakTimer > 0.0f) {
        s_killStreakTimer -= dt;
        if (s_killStreakTimer <= 0.0f) s_killStreak = 0;
    }

    s_sandstormTimer -= dt;
    if (s_sandstormTimer <= 0.0f) {
        s_sandstormTimer = 80.0f;
        OW_TriggerSandstormEvent();
    }

    NetSocialEvent sev[8];
    int nsev = Net_PollSocialEvents(sev, 8);
    for (int i = 0; i < nsev; i++) RecordEmote(sev[i].agentId, sev[i].emoteId);

    NetTradeGift tgv[8];
    int ntg = Net_PollTradeGifts(tgv, 8);
    if (ntg > 0) {
        PlayerProfile *prof = PlayerProfile_GetGlobal();
        for (int i = 0; i < ntg; i++) {
            if (tgv[i].itemKind == 0) prof->gold += tgv[i].amount;
            else if (tgv[i].itemKind == 1) prof->elementalStones[s_realm.realmId % 5] += tgv[i].amount;
            else if (tgv[i].itemKind == 2) prof->herbs += tgv[i].amount;
            else if (tgv[i].itemKind == 3) PlayerProfile_AddPotion(POTION_HEAL_HP, tgv[i].amount);
            else if (tgv[i].itemKind == 4) PlayerProfile_AddPotion(POTION_RESTORE_MANA, tgv[i].amount);
            else if (tgv[i].itemKind == 5) prof->daTauTokens += tgv[i].amount;
        }
        PlayerProfile_SaveGlobal();
        OW_SetBanner("NHAN DUOC VAT PHAM GIAO DICH TU DONG DAO!", 3.2f);
    }

    for (int i = 0; i < MAX_EMOTE_BUBBLES; i++) {
        if (s_emotes[i].timer > 0.0f) s_emotes[i].timer -= dt;
    }

    const Agent *pa = player ? Entity_GetAgent(player->agentId) : NULL;
    OWLoot_Update(pa, dt);
    OWQuestNpc_Update(pa, dt);
    if (!pa) return;

    s_lastPlayerPos = pa->position;

    bool inSafe = OW_IsInSafeZone(pa->position);
    Entity_SetSafeZone(player->agentId, inSafe);
    if (inSafe) {
        if (pa->health < pa->maxHealth)
            Entity_ApplyDamage(player->agentId, -15.0f * dt, (Vector3){ 0 });
        Entity_RestoreMana(player->agentId, 20.0f * dt);
    }

    for (int i = 0; i < s_windSurgeCount; i++) {
        if (s_windSurges[i].cooldown > 0.0f) {
            s_windSurges[i].cooldown -= dt;
            continue;
        }
        if (DistXZ(pa->position, s_windSurges[i].pos) <= 2.3f) {
            s_windSurges[i].cooldown = 5.0f;
            Entity_AddModifier(player->agentId, 1.55f, 8.0f);
            Entity_RestoreMana(player->agentId, 40.0f);
            if (pa->vState == AGENT_GROUNDED) Entity_ApplyLaunch(player->agentId, 6.2f, (Vector3){ 0 });
            AddFloatingText(pa->position, "LINH PHONG KHINH CONG (+55% TOC DO!)",
                            (Color){ 120, 255, 220, 255 }, 16.0f, 1.5f);
        }
    }

    const ClashEvent *evs = NULL;
    int nec = Combat_PeekEvents(&evs);
    int steleId = OWQuestNpc_GetSteleAgentId();
    for (int i = 0; i < nec; i++) {
        if (evs[i].outcome == CLASH_HIT_AGENT && evs[i].otherAgentId >= 0) {
            const Agent *vic = Entity_GetAgent(evs[i].otherAgentId);
            if (vic && vic->archetype != ARCH_HERO && pa->lastCastSkillIndex >= 0) {
                uint32_t hitXp = (vic->archetype == ARCH_BOSS) ? 15u : 8u;
                GrantSkillXpWithFeedback(pa->lastCastSkillIndex, hitXp, evs[i].clashPoint);
            } else if (!vic && evs[i].otherAgentId != steleId) {
                s_killStreak++;
                s_killStreakTimer = 6.0f;
                if (s_killStreak >= 3) {
                    AddFloatingText(evs[i].clashPoint, TextFormat("LIEN TRAM x%d!", s_killStreak),
                                    GOLD, 18.0f, 1.4f);
                }
            }
        } else if (evs[i].outcome == CLASH_A_WINS && pa->lastCastSkillIndex >= 0) {
            GrantSkillXpWithFeedback(pa->lastCastSkillIndex, 12u, evs[i].clashPoint);
        }
    }

    if (IsKeyPressed(KEY_F)) OW_TryInteractNearest(pa->position);
    if (IsKeyPressed(KEY_C)) OW_TriggerGuidingWind();
    if (IsKeyPressed(KEY_R))  UsePotionKey(player, POTION_HEAL_HP);
    if (IsKeyPressed(KEY_F1)) UsePotionKey(player, POTION_RESTORE_MANA);
    if (IsKeyPressed(KEY_F2)) UsePotionKey(player, POTION_EARTH_MIGHT);
    if (IsKeyPressed(KEY_F3)) UsePotionKey(player, POTION_SKILL_XP_BOOST);

    if (IsKeyPressed(KEY_V)) {
        if (inSafe) {
            OW_SetBanner("TRONG OC DAO AN TOAN KHONG THE BAT CO TY VO PK!", 2.5f);
        } else {
            bool nextDuel = !Entity_GetDuelFlag(player->agentId);
            Entity_SetDuelFlag(player->agentId, nextDuel);
            Net_SendSocialState(player->agentId, 0, nextDuel);
            OW_SetBanner(nextDuel ? "DA BAT CO TY VO PK! CO THE SO TAI VOI NGUOI CHOI KHAC!"
                                  : "DA TAT CO PK - TRO VE CHE DO HOA BINH", 3.0f);
        }
    }

    if (IsKeyPressed(KEY_T)) s_emoteMenuOpen = !s_emoteMenuOpen;
    if (s_emoteMenuOpen) {
        int chosen = 0;
        if (IsKeyPressed(KEY_F5)) chosen = 1;
        else if (IsKeyPressed(KEY_F6)) chosen = 2;
        else if (IsKeyPressed(KEY_F7)) chosen = 3;
        else if (IsKeyPressed(KEY_F8)) chosen = 4;
        if (chosen > 0) {
            RecordEmote(player->agentId, chosen);
            Net_SendSocialState(player->agentId, chosen, Entity_GetDuelFlag(player->agentId));
            s_emoteMenuOpen = false;
        }
    }
    if (IsKeyPressed(KEY_H)) s_tradeMenuOpen = !s_tradeMenuOpen;
}

void OW_Draw3D(const PlayerEntity *player, Camera3D cam) {
    if (!s_hasRealm) return;

    Vector3 safeRing = s_realm.safeCenter;
    safeRing.y = MapManager_GetGroundHeightAt(safeRing.x, safeRing.z) + 0.12f;
    DrawCircle3D(safeRing, s_realm.safeRadius, (Vector3){ 1, 0, 0 }, 90.0f, (Color){ 90, 235, 175, 145 });

    OWQuestNpc_Draw3D();

    for (int i = 0; i < s_windSurgeCount; i++) {
        Vector3 wp = s_windSurges[i].pos;
        Color wc = (s_windSurges[i].cooldown > 0.0f) ? (Color){ 100, 160, 150, 70 }
                                                     : (Color){ 110, 255, 220, 190 };
        DrawCircle3D((Vector3){ wp.x, wp.y + 0.25f, wp.z }, 1.8f, (Vector3){ 1, 0, 0 }, 90.0f, wc);
        DrawCircle3D((Vector3){ wp.x, wp.y + 0.85f + 0.25f * sinf(s_clock * 4.0f), wp.z },
                     1.2f, (Vector3){ 1, 0, 0 }, 90.0f, wc);
    }

    OWLoot_Draw3D();

    if (s_windActive) {
        float progress = s_windElapsed / GUIDING_WIND_DURATION;
        if (progress > 0.0f && progress < 1.0f) {
            VFX_ComposeGuidingWind(s_windStartPos, s_windEndPos, progress, cam);
        }
    }

    const Agent *pa = player ? Entity_GetAgent(player->agentId) : NULL;
    if (pa && pa->duelFlag) {
        DrawCircle3D((Vector3){ pa->position.x, pa->position.y + 0.14f, pa->position.z },
                     1.1f, (Vector3){ 1, 0, 0 }, 90.0f, (Color){ 255, 85, 85, 220 });
    }
}

void OW_DrawHUD(const PlayerEntity *player, Camera3D cam) {
    if (!s_hasRealm) return;
    int sw = GetScreenWidth();
    const Agent *pa = player ? Entity_GetAgent(player->agentId) : NULL;
    PlayerProfile *prof = PlayerProfile_GetGlobal();

    OWQuestNpc_DrawHUD(pa, cam);

    if (pa) {
        int z = OW_GetZoneAt(pa->position);
        DrawRectangle(sw / 2 - 225, 44, 450, 28, (Color){ 16, 18, 26, 210 });
        DrawRectangleLines(sw / 2 - 225, 44, 450, 28, (Color){ 215, 178, 95, 190 });
        const char *hudLine = TextFormat("%s  |  [C] Gio Dan Loi  |  %s",
                                         OW_GetZoneName(z),
                                         pa->duelFlag ? "PK: BAT" : "HOA BINH");
        int tw = MeasureText(hudLine, 12);
        DrawText(hudLine, sw / 2 - tw / 2, 52, 12, pa->duelFlag ? (Color){ 255, 130, 120, 255 } : GOLD);

        const char *prompt = OWQuestNpc_GetNearbyPrompt(pa->position);
        if (!prompt) prompt = OWLoot_GetNearbyPrompt(pa->position);
        if (prompt) {
            int lw = MeasureText(prompt, 15);
            DrawRectangle(sw / 2 - lw / 2 - 14, GetScreenHeight() - 148, lw + 28, 30, (Color){ 20, 24, 34, 230 });
            DrawRectangleLines(sw / 2 - lw / 2 - 14, GetScreenHeight() - 148, lw + 28, 30, GOLD);
            DrawText(prompt, sw / 2 - lw / 2, GetScreenHeight() - 141, 15, GOLD);
        }
    }

    int py = GetScreenHeight() - 118;
    DrawRectangle(16, py, 370, 44, (Color){ 16, 18, 28, 200 });
    DrawText(TextFormat("[R] Mau:%d  [F1] Mana:%d  [F2] HoThe:%d  [F3] x2XP:%d",
                        prof->potions[POTION_HEAL_HP], prof->potions[POTION_RESTORE_MANA],
                        prof->potions[POTION_EARTH_MIGHT], prof->potions[POTION_SKILL_XP_BOOST]),
             24, py + 7, 12, (Color){ 225, 235, 210, 255 });
    DrawText(TextFormat("Khoang:%d  Thao:%d | [V] PK  [T] BieuCam  [H] GiaoDich  [C] Gio",
                        prof->elementalStones[s_realm.realmId % 5], prof->herbs),
             24, py + 24, 11, (Color){ 195, 205, 225, 255 });

    if (s_mightTimer > 0.0f)
        DrawText(TextFormat("HOANG THO HO THE: %.0fs", s_mightTimer), 20, 125, 13, ORANGE);
    if (s_xpBoostTimer > 0.0f)
        DrawText(TextFormat("NGO DAO x2 SKILL XP: %.0fs", s_xpBoostTimer), 20, 142, 13, (Color){ 130, 255, 210, 255 });
    if (s_killStreak >= 2)
        DrawText(TextFormat("LIEN TRAM x%d (+25%% EXP)", s_killStreak), sw - 220, 210, 16, GOLD);

    if (s_bannerTimer > 0.0f && s_bannerText[0] != '\0') {
        int bw = MeasureText(s_bannerText, 18);
        DrawRectangle(sw / 2 - bw / 2 - 18, 84, bw + 36, 32, (Color){ 24, 20, 14, 225 });
        DrawRectangleLines(sw / 2 - bw / 2 - 18, 84, bw + 36, 32, GOLD);
        DrawText(s_bannerText, sw / 2 - bw / 2, 91, 18, GOLD);
    }

    static const char *kEmoteTexts[5] = {
        "", "Chao cac vi dao huu!", "Mau toi hop suc san Boss!",
        "Xin linh giao cao chieu! [V]", "Can trao doi vat pham! [H]"
    };
    for (int i = 0; i < MAX_EMOTE_BUBBLES; i++) {
        if (s_emotes[i].timer <= 0.0f || s_emotes[i].emoteId <= 0 || s_emotes[i].emoteId > 4) continue;
        const Agent *ea = Entity_GetAgent(s_emotes[i].agentId);
        if (!ea) continue;
        Vector2 sp = GetWorldToScreen((Vector3){ ea->position.x, ea->position.y + 2.3f, ea->position.z }, cam);
        const char *txt = kEmoteTexts[s_emotes[i].emoteId];
        int tw = MeasureText(txt, 13);
        DrawRectangle((int)sp.x - tw / 2 - 8, (int)sp.y - 12, tw + 16, 24, (Color){ 18, 24, 36, 230 });
        DrawRectangleLines((int)sp.x - tw / 2 - 8, (int)sp.y - 12, tw + 16, 24, SKYBLUE);
        DrawText(txt, (int)sp.x - tw / 2, (int)sp.y - 7, 13, WHITE);
    }

    if (s_emoteMenuOpen) {
        DrawRectangle(sw / 2 - 200, 130, 400, 56, (Color){ 16, 20, 30, 235 });
        DrawRectangleLines(sw / 2 - 200, 130, 400, 56, SKYBLUE);
        DrawText("BIEU CAM GIAO LUU (BAM PHIM):", sw / 2 - 185, 138, 12, SKYBLUE);
        DrawText("[F5] Chao Hoi   [F6] Goi Boss   [F7] Moi Ty Vo   [F8] Giao Dich",
                 sw / 2 - 185, 158, 12, WHITE);
    }

    if (s_tradeMenuOpen && pa) {
        Rectangle tb = { (float)sw / 2 - 180.0f, 195.0f, 360.0f, 190.0f };
        DrawRectangleRec(tb, (Color){ 18, 22, 32, 242 });
        DrawRectangleLinesEx(tb, 1.5f, GOLD);
        DrawText("GIAO DICH / TANG VAT PHAM (P2P)", (int)tb.x + 14, (int)tb.y + 12, 14, GOLD);
        DrawText("Bam phim de gui qua cho dong dao trong phong:", (int)tb.x + 14, (int)tb.y + 34, 11, LIGHTGRAY);

        struct { const char *lbl; int kind; int amt; } opts[4] = {
            { "Gui 100 Vang", 0, 100 },
            { "Gui 1 Hoang Tho Khoang", 1, 1 },
            { "Gui 2 Linh Thao", 2, 2 },
            { "Gui 1 Hoi Sinh Dan", 3, 1 }
        };
        for (int i = 0; i < 4; i++) {
            Rectangle br = { tb.x + 14, tb.y + 56 + i * 30, tb.width - 28, 24 };
            bool hov = CheckCollisionPointRec(GetMousePosition(), br);
            DrawRectangleRec(br, hov ? (Color){ 48, 44, 30, 255 } : (Color){ 28, 32, 44, 255 });
            DrawRectangleLinesEx(br, 1.0f, hov ? GOLD : GRAY);
            DrawText(opts[i].lbl, (int)br.x + 10, (int)br.y + 5, 12, WHITE);
            if (hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                bool ok = false;
                if (opts[i].kind == 0 && prof->gold >= 100) { prof->gold -= 100; ok = true; }
                else if (opts[i].kind == 1 && prof->elementalStones[s_realm.realmId % 5] >= 1) {
                    prof->elementalStones[s_realm.realmId % 5] -= 1; ok = true;
                }
                else if (opts[i].kind == 2 && prof->herbs >= 2) { prof->herbs -= 2; ok = true; }
                else if (opts[i].kind == 3 && PlayerProfile_ConsumePotion(POTION_HEAL_HP)) { ok = true; }
                if (ok) {
                    Net_SendTradeGift(player->agentId, opts[i].kind, opts[i].amt);
                    PlayerProfile_SaveGlobal();
                    OW_SetBanner("DA GUI VAT PHAM GIAO DICH!", 2.5f);
                } else {
                    OW_SetBanner("KHONG DU VAT PHAM DE GUI!", 2.0f);
                }
            }
        }
    }
}
