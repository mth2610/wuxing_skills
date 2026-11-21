// open_world/ow_world.c
#include "open_world/ow_world.h"
#include "open_world/ow_loot.h"
#include "open_world/ow_quest_npc.h"
#include "open_world/ow_sandbox_modes.h"
#include "open_world/ow_weather.h"
#include "character/player_profile.h"
#include "combat/combat.h"
#include "core/composition/visual_composer.h"
#include "entities/entities.h"
#include "environment/wind/wind_system.h"
#include "game/quest_datau.h"
#include "maps/map_manager.h"
#include "net/net_transport.h"
#include "skills/skill_manager.h"
#include "ui/ui.h"
#include "ui/ui_theme.h"
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

static bool  s_touchInteractReq = false;
static bool  s_touchWindReq     = false;
static bool  s_touchHealReq     = false;
static bool  s_touchManaReq     = false;
static bool  s_touchDuelReq     = false;

// P2P Spar (Tỉ Thí) handshake: Ask -> Agree -> 3..2..1 -> Fight
#define OW_EMOTE_SPAR_ASK     10
#define OW_EMOTE_SPAR_ACCEPT  11
#define OW_EMOTE_SPAR_DECLINE 12
static bool  s_sparInvitePending = false;
static int   s_sparInviteFromId  = -1;
static int   s_sparPartnerId     = -1;
static float s_sparCountdown     = 0.0f;
static bool  s_sparStarting      = false;
static float s_prevHeroHp[MAX_AGENTS];
static bool  s_prevHeroHpInit[MAX_AGENTS];

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
void  OW_RecordKills(int count) {
    if (count <= 0) return;
    s_killStreak += count;
    s_killStreakTimer = 6.5f;
}

Vector3 OW_GetDaTauNpcPos(void) {
    Vector3 p = s_realm.daTauNpcPos;
    p.y = MapManager_GetGroundHeightAt(p.x, p.z);
    return p;
}

int OW_GetActiveNodeCount(void) { return OWLoot_GetActiveNodeCount(); }
void OW_TriggerSandstormEvent(void) {
    if (OWWeather_GetProfileCount() > 1) {
        OWWeather_SetWeather(1, 0.0f);
    } else {
        OWLoot_TriggerSandstormReveal();
    }
}
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
    xp = (uint32_t)((float)xp * OWWeather_GetXpMult());

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

static void ScaleRealmXZ(OWRealmDef *r, float s) {
    if (!r || fabsf(s - 1.0f) < 0.01f) return;
    r->safeCenter.x *= s; r->safeCenter.z *= s;
    r->safeRadius   *= s;
    r->daTauNpcPos.x *= s; r->daTauNpcPos.z *= s;
    r->alchemistPos.x *= s; r->alchemistPos.z *= s;
    r->stelePos.x *= s; r->stelePos.z *= s;
    r->altarPos.x *= s; r->altarPos.z *= s;
    r->duelBannerPos.x *= s; r->duelBannerPos.z *= s;
    r->defaultStartPos.x *= s; r->defaultStartPos.z *= s;
    r->dunesCenter.x *= s; r->dunesCenter.z *= s;
    r->canyonCenter.x *= s; r->canyonCenter.z *= s;
    r->badlandsCenter.x *= s; r->badlandsCenter.z *= s;
    for (int i = 0; i < r->zoneCount && i < OW_MAX_ZONES; i++) {
        r->zones[i].center.x *= s; r->zones[i].center.z *= s;
        r->zones[i].radius   *= s;
    }
    for (int i = 0; i < r->nodeCount && i < OW_MAX_NODE_SPAWNS; i++) {
        r->nodes[i].x *= s; r->nodes[i].z *= s;
    }
    for (int i = 0; i < r->windSurgeCount && i < OW_MAX_WIND_SURGES; i++) {
        r->windSurges[i].x *= s; r->windSurges[i].z *= s;
    }
    for (int i = 0; i < r->campCount && i < OW_MAX_CAMPS; i++) {
        r->camps[i].center.x *= s; r->camps[i].center.z *= s;
    }
    for (int i = 0; i < r->hordeZoneCount && i < OW_MAX_CAMPS; i++) {
        r->hordeZones[i].center.x *= s; r->hordeZones[i].center.z *= s;
        r->hordeZones[i].radius   *= s;
    }
}

void OW_Init(const OWRealmDef *realm, bool spawnCamps) {
    if (!realm) return;
    s_realm = *realm;
    if (s_realm.scaleWithMap) {
        ScaleRealmXZ(&s_realm, MapManager_GetWorldScaleXZ());
    }
    s_hasRealm = true;

    memset(s_emotes, 0, sizeof(s_emotes));
    memset(s_windSurges, 0, sizeof(s_windSurges));
    s_mightTimer = 0.0f;
    s_xpBoostTimer = 0.0f;
    s_bannerTimer = 0.0f;
    s_emoteMenuOpen = false;
    s_tradeMenuOpen = false;
    s_clock = 0.0f;
    s_lastPlayerPos = s_realm.defaultStartPos;
    s_windActive = false;
    s_windElapsed = 0.0f;
    s_windAutoTimer = 1.5f;
    s_killStreak = 0;
    s_killStreakTimer = 0.0f;

    DaTau_Init(s_realm.realmId);
    DaTau_SetNpcFlowEnabled(true);

    OWLoot_Init(&s_realm);
    OWQuestNpc_Init(&s_realm, spawnCamps);
    OWWeather_Init(&s_realm);
    OWSandbox_Init(&s_realm);

    s_windSurgeCount = s_realm.windSurgeCount;
    if (s_windSurgeCount > OW_MAX_WIND_SURGES) s_windSurgeCount = OW_MAX_WIND_SURGES;
    for (int i = 0; i < s_windSurgeCount; i++) {
        Vector3 wp = s_realm.windSurges[i];
        wp.y = MapManager_GetGroundHeightAt(wp.x, wp.z);
        s_windSurges[i] = (OWWindSurge){ .pos = wp, .cooldown = 0.0f };
    }

    AI_ClearHordeZones();
    AI_SetSafeZone(s_realm.safeCenter, s_realm.safeRadius);
    if (spawnCamps) {
        for (int i = 0; i < s_realm.campCount && i < OW_MAX_CAMPS; i++) {
            const OWCampSpawn *c = &s_realm.camps[i];
            AI_SpawnMonsterCamp(c->center, c->count, c->element, c->kind);
        }
        for (int i = 0; i < s_realm.hordeZoneCount && i < OW_MAX_CAMPS; i++) {
            AI_RegisterHordeZone(&s_realm.hordeZones[i]);
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

    OWWeather_Update(player, dt);
    OWSandbox_Update(player, dt);

    NetSocialEvent sev[8];
    int nsev = Net_PollSocialEvents(sev, 8);
    for (int i = 0; i < nsev; i++) {
        if (sev[i].emoteId == OW_EMOTE_SPAR_ASK) {
            if (player && sev[i].agentId != player->agentId) {
                s_sparInvitePending = true;
                s_sparInviteFromId  = sev[i].agentId;
            }
        } else if (sev[i].emoteId == OW_EMOTE_SPAR_ACCEPT) {
            if (player && sev[i].agentId != player->agentId) {
                s_sparInvitePending = false;
                s_sparPartnerId     = sev[i].agentId;
                s_sparCountdown     = 3.0f;
                s_sparStarting      = true;
                OW_SetBanner("DOI PHUONG DA DONG Y! CHUAN BI TY THI...", 2.0f);
            }
        } else if (sev[i].emoteId == OW_EMOTE_SPAR_DECLINE) {
            if (player && sev[i].agentId != player->agentId) {
                OW_SetBanner("DOI PHUONG TU CHOI TY THI.", 2.4f);
            }
        } else {
            RecordEmote(sev[i].agentId, sev[i].emoteId);
        }
    }

    // Countdown 3..2..1 -> Activate Tỉ Thí (PK_MODE_SPAR) for BOTH players
    if (s_sparStarting) {
        s_sparCountdown -= dt;
        if (s_sparCountdown <= 0.0f) {
            s_sparStarting  = false;
            s_sparCountdown = 0.0f;
            if (player) {
                OWSandbox_StartSparWith(player, s_sparPartnerId);
            }
            s_sparPartnerId = -1;
        }
    }

    // Floating damage numbers & hit flash for all heroes (works on both Host & Client!)
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *ha = Entity_GetAgent(i);
        if (!ha || ha->archetype != ARCH_HERO) {
            s_prevHeroHpInit[i] = false;
            continue;
        }
        if (!s_prevHeroHpInit[i]) {
            s_prevHeroHp[i] = ha->health;
            s_prevHeroHpInit[i] = true;
        } else {
            float delta = s_prevHeroHp[i] - ha->health;
            if (delta >= 0.8f) {
                static float s_lastHeroHitDust = 0.0f;
                float nowT = (float)GetTime();
                Vector3 txtPos = { ha->position.x, ha->position.y + 1.9f, ha->position.z };
                AddFloatingText(txtPos, TextFormat("-%.0f", delta),
                                (Color){ 255, 95, 70, 255 }, 20.0f, 0.9f);
                if (nowT - s_lastHeroHitDust >= 0.18f) {
                    VFX_ComposeImpactDust(ha->position, VC_MAT_EARTH, 0.65f, 0.45f);
                    s_lastHeroHitDust = nowT;
                }
            }
            s_prevHeroHp[i] = ha->health;
        }
        if (player && i == player->agentId) {
            static AgentPkMode s_prevPlayerPkMode = PK_MODE_PEACE;
            if (s_prevPlayerPkMode == PK_MODE_SPAR && ha->pkMode == PK_MODE_PEACE && ha->health > 1.2f) {
                OW_SetBanner("TY THI KET THUC! HOI PHUC SINH LUC BAN DAU!", 3.2f);
            }
            s_prevPlayerPkMode = ha->pkMode;
        }
    }

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
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *ha = Entity_GetAgent(i);
        if (ha && ha->archetype == ARCH_HERO) {
            Entity_SetSafeZone(i, OW_IsInSafeZone(ha->position));
        }
    }
    if (!Net_ClientDrivesWorld()) {
        if (inSafe && pa->pkMode != PK_MODE_SPAR) {
            if (pa->health < pa->maxHealth)
                Entity_ApplyDamage(player->agentId, -15.0f * dt, (Vector3){ 0 });
            Entity_RestoreMana(player->agentId, 20.0f * dt);
        }
    }

    // Vampire-Survivors Frenzy Tiers based on Kill Streak
    if (s_killStreak >= 80)      Entity_AddModifier(player->agentId, 1.35f, 0.4f);
    else if (s_killStreak >= 40) Entity_AddModifier(player->agentId, 1.22f, 0.4f);
    else if (s_killStreak >= 15) Entity_AddModifier(player->agentId, 1.10f, 0.4f);

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

    if (IsKeyPressed(KEY_F) || s_touchInteractReq) {
        s_touchInteractReq = false;
        if (!OWSandbox_TryInteract(player)) {
            OW_TryInteractNearest(pa->position);
        }
    }
    if (IsKeyPressed(KEY_C) || s_touchWindReq)     { s_touchWindReq = false; OW_TriggerGuidingWind(); }
    if (IsKeyPressed(KEY_R) || s_touchHealReq)     { s_touchHealReq = false; UsePotionKey(player, POTION_HEAL_HP); }
    if (IsKeyPressed(KEY_F1) || s_touchManaReq)    { s_touchManaReq = false; UsePotionKey(player, POTION_RESTORE_MANA); }
    if (IsKeyPressed(KEY_F2)) UsePotionKey(player, POTION_EARTH_MIGHT);
    if (IsKeyPressed(KEY_F3)) UsePotionKey(player, POTION_SKILL_XP_BOOST);

    if (IsKeyPressed(KEY_V) || s_touchDuelReq) {
        s_touchDuelReq = false;
        OWSandbox_CyclePkMode(player);
    }

    if (IsKeyPressed(KEY_M)) {
        if (!Net_VoiceIsAvailable()) {
            OW_SetBanner("MIC CHI HOAT DONG KHI KET NOI PHONG ONLINE (EOS)!", 2.8f);
        } else {
            bool nextMic = !Net_VoiceIsMicEnabled();
            Net_VoiceSetMicEnabled(nextMic);
            OW_SetBanner(nextMic ? "DA BAT MIC VOICE CHAT! DONG DAO CO THE NGHE BAN!"
                                 : "DA TAT MIC VOICE CHAT", 2.4f);
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

static void DrawSafeZoneRingCached(void) {
    static Vector3 s_ringPts[49];
    static Vector3 s_cachedCenter = { -99999.0f, 0.0f, -99999.0f };
    static float   s_cachedRadius = -1.0f;
    const int segs = 48;
    if (fabsf(s_realm.safeCenter.x - s_cachedCenter.x) > 0.05f ||
        fabsf(s_realm.safeCenter.z - s_cachedCenter.z) > 0.05f ||
        fabsf(s_realm.safeRadius - s_cachedRadius) > 0.05f) {
        s_cachedCenter = s_realm.safeCenter;
        s_cachedRadius = s_realm.safeRadius;
        float step = (2.0f * PI) / (float)segs;
        for (int i = 0; i <= segs; i++) {
            float a = (float)i * step;
            float x = s_realm.safeCenter.x + cosf(a) * s_realm.safeRadius;
            float z = s_realm.safeCenter.z + sinf(a) * s_realm.safeRadius;
            s_ringPts[i] = (Vector3){ x, MapManager_GetGroundHeightAt(x, z) + 0.14f, z };
        }
    }
    Color ringCol = (Color){ 90, 235, 175, 135 };
    for (int i = 1; i <= segs; i++) {
        DrawLine3D(s_ringPts[i - 1], s_ringPts[i], ringCol);
    }
}

void OW_Draw3D(const PlayerEntity *player, Camera3D cam) {
    if (!s_hasRealm) return;

    DrawSafeZoneRingCached();

    OWQuestNpc_Draw3D();

    for (int i = 0; i < s_windSurgeCount; i++) {
        Vector3 wp = s_windSurges[i].pos;
        float dx = wp.x - cam.position.x;
        float dz = wp.z - cam.position.z;
        if (dx * dx + dz * dz > 95.0f * 95.0f) continue;
        Color wc = (s_windSurges[i].cooldown > 0.0f) ? (Color){ 100, 160, 150, 70 }
                                                     : (Color){ 110, 255, 220, 190 };
        DrawCircle3D((Vector3){ wp.x, wp.y + 0.25f, wp.z }, 1.8f, (Vector3){ 1, 0, 0 }, 90.0f, wc);
        DrawCircle3D((Vector3){ wp.x, wp.y + 0.85f + 0.25f * sinf(s_clock * 4.0f), wp.z },
                     1.2f, (Vector3){ 1, 0, 0 }, 90.0f, wc);
    }

    OWLoot_Draw3D(cam);
    OWWeather_Draw3D(player, cam);
    OWSandbox_Draw3D(player, cam);

    if (s_windActive) {
        float progress = s_windElapsed / GUIDING_WIND_DURATION;
        if (progress > 0.0f && progress < 1.0f) {
            VFX_ComposeGuidingWind(s_windStartPos, s_windEndPos, progress, cam);
        }
    }
}

void OW_DrawHUD(const PlayerEntity *player, Camera3D cam) {
    if (!s_hasRealm) return;
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    float s = ctx->scale;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    const Agent *pa = player ? Entity_GetAgent(player->agentId) : NULL;
    PlayerProfile *prof = PlayerProfile_GetGlobal();

    OWQuestNpc_DrawHUD(pa, cam);
    OWWeather_DrawHUD();
    OWSandbox_DrawHUD(player, cam);

    static bool s_toolboxOpen = false;
    bool touchMode = UI_IsTouchControlsEnabled();
    if (pa) {
        int z = OW_GetZoneAt(pa->position);
        float zw = touchMode ? 360.0f : 480.0f;
        float zy = ctx->safeRect.y + 42.0f;
        Rectangle zr = { (float)sw * 0.5f - zw * 0.5f, zy, zw, 28.0f };
        Color pkRibCol = (pa->pkMode == PK_MODE_SLAUGHTER) ? WUXIA_CRIMSON
                       : (pa->pkMode == PK_MODE_SPAR)      ? WUXIA_GOLD
                                                           : WUXIA_JADE;
        const char *pkStr = OWSandbox_GetPkModeLabel(pa->pkMode);
        WuxiaUI_DrawRibbon(zr, pkRibCol);
        const char *hudLine = touchMode
            ? TextFormat("%s  ·  %s", OW_GetZoneName(z), pkStr)
            : TextFormat("%s  ·  [B] Kỳ Ngộ  ·  [V] %s", OW_GetZoneName(z), pkStr);
        WuxiaUI_DrawTextCentered(hudLine, zr, 13.0f,
                                 (pa->pkMode == PK_MODE_SLAUGHTER) ? (Color){ 255, 145, 135, 255 } : WUXIA_GOLD_BRIGHT);

        int liveSwarm = AI_GetActiveCampMonsterCount();
        if (liveSwarm > 0 || s_killStreak >= 3) {
            Rectangle hr = { (float)sw * 0.5f - 150.0f, zy + 32.0f, 300.0f, 22.0f };
            WuxiaUI_DrawPanel(hr, (s_killStreak >= 40) ? WUXIA_CRIMSON : WUXIA_GOLD, true);
            const char *hTxt = (s_killStreak >= 3)
                ? TextFormat("BẦY QUÁI: %d YÊU  ·  LIÊN TRẢM x%d", liveSwarm, s_killStreak)
                : TextFormat("BẦY QUÁI QUANH ĐÂY: %d YÊU", liveSwarm);
            WuxiaUI_DrawTextCentered(hTxt, hr, 11.5f, (s_killStreak >= 40) ? (Color){ 255, 195, 110, 255 } : WUXIA_GOLD_BRIGHT);
        }

        const char *prompt = OWSandbox_GetNearbyPrompt(pa->position);
        if (!prompt) prompt = OWQuestNpc_GetNearbyPrompt(pa->position);
        if (!prompt) prompt = OWLoot_GetNearbyPrompt(pa->position);
        if (prompt) {
            if (touchMode) {
                const char *clean = (strncmp(prompt, "[F] ", 4) == 0) ? prompt + 4 : prompt;
                float lw = WuxiaUI_MeasureText(clean, 15.0f);
                float bw = fmaxf(250.0f, lw + 48.0f);
                Rectangle ib = { (float)sw * 0.5f - bw * 0.5f, (float)sh - 118.0f * s, bw, 44.0f };
                if (WuxiaUI_DrawButton(ib, clean, WUXIA_GOLD, true, 15.0f)) {
                    s_touchInteractReq = true;
                }
            } else {
                float lw = WuxiaUI_MeasureText(prompt, 14.5f);
                Rectangle ib = { (float)sw * 0.5f - lw * 0.5f - 18.0f, (float)sh - 134.0f * s, lw + 36.0f, 32.0f };
                WuxiaUI_DrawPanel(ib, WUXIA_GOLD, true);
                WuxiaUI_DrawTextCentered(prompt, ib, 14.5f, WUXIA_GOLD_BRIGHT);
            }
        }
    }

    // --- Left Edge Collapsible 3x3 Quick Toolbox (Where Winds Meet Image 2 style) ---
    {
        float toggleX = ctx->safeRect.x;
        float toggleY = ctx->safeRect.y + 102.0f * s;
        if (WuxiaUI_DrawIconPill((Rectangle){ toggleX, toggleY, 96.0f * s, 28.0f * s },
                                 WUXIA_ICON_GEAR, s_toolboxOpen ? "Thu Gọn" : "Tiện Ích",
                                 NULL, WUXIA_GOLD, s_toolboxOpen)) {
            s_toolboxOpen = !s_toolboxOpen;
        }

        if (s_toolboxOpen && pa) {
            float cellW = 86.0f * s;
            float cellH = 62.0f * s;
            float gap = 6.0f * s;
            float boxW = cellW * 3.0f + gap * 2.0f + 20.0f * s;
            float boxH = cellH * 3.0f + gap * 2.0f + 46.0f * s;
            Rectangle tb = { toggleX, toggleY + 34.0f * s, boxW, boxH };
            WuxiaUI_DrawPanel(tb, WUXIA_GOLD, true);
            WuxiaUI_DrawText("TIỆN ÍCH GIANG HỒ", tb.x + 12.0f, tb.y + 10.0f, 13.0f, WUXIA_GOLD_BRIGHT);

            bool voiceOk = Net_VoiceIsAvailable();
            bool micOn   = Net_VoiceIsMicEnabled();
            bool kyNgoOn = OWSandbox_IsPanelOpen();

            struct { WuxiaIconId icon; const char *lbl; const char *hk; Color col; bool act; } items[9] = {
                { WUXIA_ICON_COMPASS,     "Gió Dẫn",  "C",  WUXIA_JADE,    false },
                { WUXIA_ICON_SHIELD_PK,   "Đổi PK",   "V",  (pa->pkMode == PK_MODE_SLAUGHTER) ? WUXIA_CRIMSON : WUXIA_GOLD, pa->pkMode != PK_MODE_PEACE },
                { WUXIA_ICON_SPARK_EVENT, "Kỳ Ngộ",   "B",  WUXIA_GOLD,    kyNgoOn },
                { WUXIA_ICON_CHAT,        "Giao Lưu", "T",  WUXIA_AZURE,   s_emoteMenuOpen },
                { WUXIA_ICON_MIC,         micOn ? "Mic: Bật" : "Mic: Tắt", "M", micOn ? WUXIA_JADE : WUXIA_AZURE, micOn },
                { WUXIA_ICON_POTION,      TextFormat("Hộ Thể:%d", prof->potions[POTION_EARTH_MIGHT]), "F2", WUXIA_GOLD, s_mightTimer > 0.0f },
                { WUXIA_ICON_POTION,      TextFormat("x2 XP:%d", prof->potions[POTION_SKILL_XP_BOOST]), "F3", WUXIA_JADE, s_xpBoostTimer > 0.0f },
                { WUXIA_ICON_CROSSED_SWORDS, "Tỷ Võ", NULL, WUXIA_CRIMSON, pa->duelFlag },
                { WUXIA_ICON_CLOSE,       "Đóng",     NULL, WUXIA_TEXT_MUTED, false }
            };

            for (int idx = 0; idx < 9; idx++) {
                int col = idx % 3, row = idx / 3;
                Rectangle cr = {
                    tb.x + 10.0f * s + col * (cellW + gap),
                    tb.y + 34.0f * s + row * (cellH + gap),
                    cellW, cellH
                };
                if (WuxiaUI_DrawFeatureTile(cr, items[idx].icon, items[idx].lbl, NULL,
                                            touchMode ? NULL : items[idx].hk,
                                            items[idx].col, items[idx].act, false)) {
                    if (idx == 0) s_touchWindReq = true;
                    else if (idx == 1) OWSandbox_CyclePkMode((PlayerEntity *)player);
                    else if (idx == 2) OWSandbox_TogglePanel();
                    else if (idx == 3) s_emoteMenuOpen = !s_emoteMenuOpen;
                    else if (idx == 4) {
                        if (!voiceOk) OW_SetBanner("MIC CHỈ HOẠT ĐỘNG KHI KẾT NỐI PHÒNG ONLINE!", 2.8f);
                        else {
                            bool nextMic = !micOn;
                            Net_VoiceSetMicEnabled(nextMic);
                            OW_SetBanner(nextMic ? "ĐÃ BẬT MIC ĐÀM THOẠI!" : "ĐÃ TẮT MIC ĐÀM THOẠI", 2.2f);
                        }
                    } else if (idx == 5 && PlayerProfile_ConsumePotion(POTION_EARTH_MIGHT)) {
                        s_mightTimer = 30.0f;
                        OW_SetBanner("HOÀNG THỔ HỘ THỂ ĐAN: +35% CÔNG & -30% SÁT THƯƠNG!", 2.5f);
                    } else if (idx == 6 && PlayerProfile_ConsumePotion(POTION_SKILL_XP_BOOST)) {
                        s_xpBoostTimer = 60.0f;
                        OW_SetBanner("NGỘ ĐẠO LINH TRÀ: x2 EXP VÕ HỌC TRONG 60 GIÂY!", 2.5f);
                    } else if (idx == 7) {
                        s_touchDuelReq = true;
                    } else if (idx == 8) {
                        s_toolboxOpen = false;
                    }
                }
            }
        }
    }

    if (s_mightTimer > 0.0f)
        WuxiaUI_DrawText(TextFormat("Hoàng Thổ Hộ Thể: %.0fs", s_mightTimer), ctx->safeRect.x, ctx->safeRect.y + 136.0f * s, 12.0f, WUXIA_GOLD);
    if (s_xpBoostTimer > 0.0f)
        WuxiaUI_DrawText(TextFormat("Ngộ Đạo x2 EXP: %.0fs", s_xpBoostTimer), ctx->safeRect.x, ctx->safeRect.y + 152.0f * s, 12.0f, WUXIA_JADE);
    if (s_killStreak >= 2)
        WuxiaUI_DrawTextRight(TextFormat("LIÊN TRẢM x%d (+25%% EXP)", s_killStreak), (float)sw - 16.0f, ctx->safeRect.y + 100.0f * s, 14.0f, WUXIA_GOLD_BRIGHT);

    if (s_bannerTimer > 0.0f && s_bannerText[0] != '\0') {
        float bw = fmaxf(320.0f, WuxiaUI_MeasureText(s_bannerText, 16.5f) + 56.0f);
        Rectangle br = { (float)sw * 0.5f - bw * 0.5f, ctx->safeRect.y + 78.0f, bw, 32.0f };
        WuxiaUI_DrawRibbon(br, WUXIA_GOLD);
        WuxiaUI_DrawTextCentered(s_bannerText, br, 16.5f, WUXIA_GOLD_BRIGHT);
    }

    static const char *kEmoteTexts[5] = {
        "", "Chào các vị đạo hữu!", "Mau tới hợp sức săn Boss!",
        "Xin lĩnh giáo cao chiêu! [V]", "Cần trao đổi vật phẩm! [H]"
    };
    for (int i = 0; i < MAX_EMOTE_BUBBLES; i++) {
        if (s_emotes[i].timer <= 0.0f || s_emotes[i].emoteId <= 0 || s_emotes[i].emoteId > 4) continue;
        const Agent *ea = Entity_GetAgent(s_emotes[i].agentId);
        if (!ea) continue;
        Vector2 sp = GetWorldToScreen((Vector3){ ea->position.x, ea->position.y + 2.3f, ea->position.z }, cam);
        const char *txt = kEmoteTexts[s_emotes[i].emoteId];
        float tw = WuxiaUI_MeasureText(txt, 13.0f);
        Rectangle er = { sp.x - tw * 0.5f - 8.0f, sp.y - 12.0f, tw + 16.0f, 24.0f };
        WuxiaUI_DrawPanel(er, WUXIA_AZURE, false);
        WuxiaUI_DrawTextCentered(txt, er, 13.0f, WUXIA_TEXT_PRIMARY);
    }

    if (s_emoteMenuOpen && player) {
        Rectangle eb = { (float)sw / 2 - 245.0f, 125.0f, 490.0f, 118.0f };
        WuxiaUI_DrawPanel(eb, WUXIA_AZURE, true);
        WuxiaUI_DrawText("GIAO LƯU & TỶ VÕ", eb.x + 16.0f, eb.y + 12.0f, 15.0f, WUXIA_GOLD);

        Rectangle eClose = { eb.x + eb.width - 78.0f, eb.y + 8.0f, 64.0f, 26.0f };
        if (WuxiaUI_DrawButton(eClose, "ĐÓNG", WUXIA_CRIMSON, false, 12.0f))
            s_emoteMenuOpen = false;

        const char *eLbls[5] = { "Chào Hỏi", "Gọi Boss", "Mời Tỷ Võ", "Giao Dịch",
                                 (pa && pa->duelFlag) ? "Tắt PK" : "Bật PK" };
        for (int i = 0; i < 5; i++) {
            float bx = eb.x + 14.0f + i * 93.0f;
            Rectangle br = { bx, eb.y + 46.0f, 87.0f, 52.0f };
            Color bc = (i == 4 && pa && pa->duelFlag) ? WUXIA_CRIMSON : WUXIA_AZURE;
            if (WuxiaUI_DrawButton(br, eLbls[i], bc, (i == 4 && pa && pa->duelFlag), 12.5f)) {
                if (i == 2) {
                    Net_SendSocialState(player->agentId, OW_EMOTE_SPAR_ASK, false);
                    OW_SetBanner("ĐÃ GỬI LỜI MỜI TỶ THÍ! CHỜ ĐỒNG ĐẠO ĐỒNG Ý...", 2.8f);
                } else if (i < 4) {
                    RecordEmote(player->agentId, i + 1);
                    Net_SendSocialState(player->agentId, i + 1, Entity_GetDuelFlag(player->agentId));
                    if (i == 3) s_tradeMenuOpen = true;
                } else {
                    s_touchDuelReq = true;
                }
                s_emoteMenuOpen = false;
            }
        }
    }

    // Spar invite popup (Ask -> Agree -> 1, 2, 3)
    if (s_sparInvitePending && player) {
        (void)s_sparInviteFromId;
        Rectangle sb = { (float)sw / 2 - 195.0f, 130.0f, 390.0f, 110.0f };
        WuxiaUI_DrawPanel(sb, WUXIA_GOLD, true);
        WuxiaUI_DrawText("ĐỒNG ĐẠO MỜI BẠN TỶ THÍ (1v1)!", sb.x + 22.0f, sb.y + 16.0f, 15.5f, WUXIA_GOLD);
        Rectangle btnYes = { sb.x + 24.0f, sb.y + 52.0f, 158.0f, 40.0f };
        Rectangle btnNo  = { sb.x + 208.0f, sb.y + 52.0f, 158.0f, 40.0f };
        if (WuxiaUI_DrawButton(btnYes, "ĐỒNG Ý", WUXIA_JADE, false, 15.0f)) {
            s_sparInvitePending = false;
            s_sparPartnerId     = s_sparInviteFromId;
            s_sparCountdown     = 3.0f;
            s_sparStarting      = true;
            Net_SendSocialState(player->agentId, OW_EMOTE_SPAR_ACCEPT, false);
        }
        if (WuxiaUI_DrawButton(btnNo, "TỪ CHỐI", WUXIA_CRIMSON, false, 15.0f)) {
            s_sparInvitePending = false;
            Net_SendSocialState(player->agentId, OW_EMOTE_SPAR_DECLINE, false);
        }
    }

    // 3..2..1 Spar Countdown overlay
    if (s_sparStarting && s_sparCountdown > 0.0f) {
        int sec = (int)ceilf(s_sparCountdown);
        if (sec < 1) sec = 1;
        const char *cdTxt = TextFormat("TỶ THÍ BẮT ĐẦU SAU: %d", sec);
        WuxiaUI_DrawRibbon((Rectangle){ (float)sw * 0.5f - 200.0f, (float)sh * 0.5f - 48.0f, 400.0f, 64.0f }, WUXIA_GOLD);
        WuxiaUI_DrawTextCentered(cdTxt, (Rectangle){ (float)sw * 0.5f - 200.0f, (float)sh * 0.5f - 48.0f, 400.0f, 64.0f },
                                 26.0f, WUXIA_GOLD_BRIGHT);
    }

    if (s_tradeMenuOpen && pa) {
        Rectangle tb = { (float)sw / 2 - 205.0f, 195.0f, 410.0f, 226.0f };
        WuxiaUI_DrawPanel(tb, WUXIA_GOLD, true);
        WuxiaUI_DrawText("GIAO DỊCH / TẶNG VẬT PHẨM", tb.x + 16.0f, tb.y + 12.0f, 15.0f, WUXIA_GOLD);

        Rectangle tClose = { tb.x + tb.width - 78.0f, tb.y + 8.0f, 64.0f, 26.0f };
        if (WuxiaUI_DrawButton(tClose, "ĐÓNG", WUXIA_CRIMSON, false, 12.0f))
            s_tradeMenuOpen = false;

        WuxiaUI_DrawText("Chạm để gửi quà cho đồng đạo trong phòng:", tb.x + 16.0f, tb.y + 38.0f, 12.5f, WUXIA_TEXT_MUTED);

        struct { const char *lbl; int kind; int amt; } opts[4] = {
            { "Gửi 100 Vàng", 0, 100 },
            { "Gửi 1 Hoàng Thổ Khoáng", 1, 1 },
            { "Gửi 2 Linh Thảo", 2, 2 },
            { "Gửi 1 Hồi Sinh Đan", 3, 1 }
        };
        for (int i = 0; i < 4; i++) {
            Rectangle br = { tb.x + 16.0f, tb.y + 62.0f + i * 38.0f, tb.width - 32.0f, 32.0f };
            if (WuxiaUI_DrawButton(br, opts[i].lbl, WUXIA_GOLD, false, 13.0f)) {
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
                    OW_SetBanner("ĐÃ GỬI VẬT PHẨM GIAO DỊCH!", 2.5f);
                } else {
                    OW_SetBanner("KHÔNG ĐỦ VẬT PHẨM ĐỂ GỬI!", 2.0f);
                }
            }
        }
    }
}
