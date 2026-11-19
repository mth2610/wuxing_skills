// open_world/ow_sandbox_modes.c
#include "open_world/ow_sandbox_modes.h"
#include "open_world/ow_loot.h"
#include "open_world/ow_weather.h"
#include "open_world/ow_world.h"
#include "ai/ai.h"
#include "boss/boss_system.h"
#include "character/player_profile.h"
#include "entities/entities.h"
#include "maps/map_manager.h"
#include "net/net_transport.h"
#include "skills/skill_manager.h"
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OW_MAX_CUSTOM_SPAWNED 128

typedef struct {
    bool              used;
    int               agentId;
    OWSpawnActionKind kind;
    bool              isBountyEnforcer;
} OWSpawnedEntry;

static OWRealmDef      s_realm = { 0 };
static bool            s_hasRealm = false;
static OWPvPConfig     s_pvpCfg = { 0 };
static OWSpawnedEntry  s_spawned[OW_MAX_CUSTOM_SPAWNED];

static bool    s_panelOpen      = false;
static int     s_panelTab       = 0; // 0 = Nhả Quái/Boss, 1 = Thời Tiết & Tỉ Thí
static float   s_clock          = 0.0f;
static bool    s_sparActive     = false;
static Vector3 s_sparCenter     = { 0 };
static int     s_sparOpponentId = -1;
static bool    s_bountyActive   = false;
static int     s_bountyAgentId  = -1;
static bool    s_customBossLive = false;

static float DistXZ(Vector3 a, Vector3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

static OWPvPConfig DefaultPvPConfig(const OWPvPConfig *in) {
    OWPvPConfig c = {
        .sparRingRadius        = 12.0f,
        .sparRewardGold        = 60,
        .sparRewardSkillXp     = 60,
        .slaughterDmgBonus     = 0.08f,
        .slaughterSpeedBonus   = 0.15f,
        .bountyInfamyThreshold = 2
    };
    if (!in) return c;
    if (in->sparRingRadius > 1.0f)        c.sparRingRadius = in->sparRingRadius;
    if (in->sparRewardGold > 0)           c.sparRewardGold = in->sparRewardGold;
    if (in->sparRewardSkillXp > 0)        c.sparRewardSkillXp = in->sparRewardSkillXp;
    if (in->slaughterDmgBonus > 0.0f)     c.slaughterDmgBonus = in->slaughterDmgBonus;
    if (in->slaughterSpeedBonus > 0.0f)   c.slaughterSpeedBonus = in->slaughterSpeedBonus;
    if (in->bountyInfamyThreshold > 0)    c.bountyInfamyThreshold = in->bountyInfamyThreshold;
    return c;
}

static void RegisterSpawned(int agentId, OWSpawnActionKind kind, bool isBounty) {
    if (agentId < 0) return;
    for (int i = 0; i < OW_MAX_CUSTOM_SPAWNED; i++) {
        if (!s_spawned[i].used) {
            s_spawned[i] = (OWSpawnedEntry){
                .used = true,
                .agentId = agentId,
                .kind = kind,
                .isBountyEnforcer = isBounty
            };
            return;
        }
    }
}

static void EquipRivalLoadout(int botId, int elemHint) {
    static const struct { const char *name; int elem; } kSkills[AGENT_SKILL_SLOTS] = {
        { "EARTH_DISC_VOLLEY", 3 },
        { "FIRE", 2 },
        { "GLACIAL_CANNON", 0 },
        { "METAL_BLADE_STORM", 4 },
        { "LEAF_WHIRLWIND", 1 }
    };
    for (int s = 0; s < AGENT_SKILL_SLOTS; s++) {
        int idx = Skill_GetIndexByName(kSkills[s].name);
        if (idx >= 0) {
            int el = (s == 0 && elemHint >= 0 && elemHint <= 4) ? elemHint : kSkills[s].elem;
            Entity_SetEquippedSkill(botId, s, idx, el);
        }
    }
}

static Vector3 FindSafeCombatPos(Vector3 playerPos, float dist) {
    float ang = ((float)(rand() % 360)) * DEG2RAD;
    Vector3 p = {
        playerPos.x + cosf(ang) * dist,
        0.0f,
        playerPos.z + sinf(ang) * dist
    };
    if (DistXZ(p, s_realm.safeCenter) <= s_realm.safeRadius + 3.0f) {
        p = s_realm.canyonCenter;
        p.x += cosf(ang) * 6.0f;
        p.z += sinf(ang) * 6.0f;
    }
    p.y = MapManager_GetGroundHeightAt(p.x, p.z);
    return p;
}

void OWSandbox_Init(const OWRealmDef *realm) {
    memset(s_spawned, 0, sizeof(s_spawned));
    s_panelOpen      = false;
    s_panelTab       = 0;
    s_clock          = 0.0f;
    s_sparActive     = false;
    s_sparOpponentId = -1;
    s_bountyActive   = false;
    s_bountyAgentId  = -1;
    s_customBossLive = false;
    s_hasRealm       = (realm != NULL);
    if (realm) {
        s_realm = *realm;
        s_pvpCfg = DefaultPvPConfig(&realm->pvpConfig);
    } else {
        memset(&s_realm, 0, sizeof(s_realm));
        s_pvpCfg = DefaultPvPConfig(NULL);
    }
}

void OWSandbox_ClearCustomSpawns(void) {
    for (int i = 0; i < OW_MAX_CUSTOM_SPAWNED; i++) {
        if (!s_spawned[i].used) continue;
        int aid = s_spawned[i].agentId;
        if (aid >= 0 && Entity_GetAgent(aid) != NULL) {
            Entity_ApplyDamage(aid, 1e9f, (Vector3){ 0 });
        }
        s_spawned[i].used = false;
    }
    s_sparActive     = false;
    s_sparOpponentId = -1;
    s_bountyActive   = false;
    s_bountyAgentId  = -1;
    s_customBossLive = false;
    OW_SetBanner("DA TINH HOA SA MAC! THU HOI TOAN BO QUAI & BOSS TRIEU HOI.", 2.8f);
}

int OWSandbox_GetActiveCustomSpawnCount(void) {
    int c = 0;
    for (int i = 0; i < OW_MAX_CUSTOM_SPAWNED; i++) {
        if (s_spawned[i].used && Entity_GetAgent(s_spawned[i].agentId) != NULL) {
            c++;
        }
    }
    return c;
}

const char *OWSandbox_GetPkModeLabel(AgentPkMode mode) {
    switch (mode) {
        case PK_MODE_SPAR:      return "TY THI (1v1)";
        case PK_MODE_SLAUGHTER: return "DO SAT (PK)";
        default:                return "HOA BINH";
    }
}

bool OWSandbox_SetPkMode(PlayerEntity *player, AgentPkMode mode) {
    if (!player) return false;
    const Agent *pa = Entity_GetAgent(player->agentId);
    if (!pa) return false;

    if (mode != PK_MODE_PEACE && OW_IsInSafeZone(pa->position)) {
        OW_SetBanner("TRONG VUNG AN TOAN KHONG THE BAT TY THI HOAC DO SAT!", 2.6f);
        return false;
    }

    Entity_SetPkMode(player->agentId, mode);
    Net_SendSocialState(player->agentId, 0, mode != PK_MODE_PEACE);

    if (mode == PK_MODE_PEACE) {
        s_sparActive = false;
        s_sparOpponentId = -1;
        OW_SetBanner("CHE DO: HOA BINH (AN TOAN TU DO DAO CANH)", 2.5f);
    } else if (mode == PK_MODE_SPAR) {
        OW_SetBanner("CHE DO: TY THI (SO TAI DUNG O 1 HP - KHONG MAT MAT)", 3.0f);
    } else {
        OW_SetBanner("CHE DO: DO SAT HUYET CHIEN! CO THE TAN CONG NGOAI OC DAO!", 3.2f);
    }
    return true;
}

void OWSandbox_CyclePkMode(PlayerEntity *player) {
    if (!player) return;
    AgentPkMode cur = Entity_GetPkMode(player->agentId);
    AgentPkMode next = (AgentPkMode)(((int)cur + 1) % 3);
    OWSandbox_SetPkMode(player, next);
}

bool OWSandbox_IsPanelOpen(void) { return s_panelOpen; }
void OWSandbox_TogglePanel(void) { s_panelOpen = !s_panelOpen; }

bool OWSandbox_TriggerPreset(PlayerEntity *player, int presetIndex) {
    if (!s_hasRealm || !player) return false;
    if (presetIndex < 0 || presetIndex >= s_realm.spawnerPresetCount) return false;

    const Agent *pa = Entity_GetAgent(player->agentId);
    if (!pa) return false;

    const OWSpawnerPresetDef *pr = &s_realm.spawnerPresets[presetIndex];
    int count = (pr->count > 0) ? pr->count : 1;

    if (pr->actionKind == OW_SPAWN_MONSTER_PACK) {
        for (int i = 0; i < count; i++) {
            float ang = ((float)i / (float)count) * 2.0f * PI;
            float r = (count > 20 ? 12.0f : 6.5f) + (float)(i % 5) * 2.4f;
            Vector3 sp = {
                pa->position.x + cosf(ang) * r,
                0.0f,
                pa->position.z + sinf(ang) * r
            };
            if (DistXZ(sp, s_realm.safeCenter) <= s_realm.safeRadius + 1.5f) {
                sp = FindSafeCombatPos(pa->position, 10.0f);
            }
            sp.y = MapManager_GetGroundHeightAt(sp.x, sp.z);
            MonsterKind k = pr->monsterKind;
            if (count >= 25 && (i % 7 == 0)) k = MONSTER_LEAPER;
            else if (count >= 25 && (i % 11 == 0)) k = MONSTER_RANGED_CASTER;
            int id = AI_SpawnOneShotMonster(sp, pr->element, k);
            RegisterSpawned(id, pr->actionKind, false);
        }
    } else if (pr->actionKind == OW_SPAWN_WORLD_BOSS) {
        if (Boss_IsSlotAlive(0)) {
            OW_SetBanner("DA CO DAI BOSS TREN SAN! HA GUC HOAC TINH HOA TRUOC.", 2.6f);
            return false;
        }
        Vector3 bp = FindSafeCombatPos(pa->position, 10.5f);
        const BossDef *def = (pr->bossVariant == 1) ? &BOSS_MOC_LINH : &BOSS_HAC_DIEN_TON_GIA;
        int bid = Boss_SpawnSlot(0, def, bp, TEAM_ENEMY);
        if (bid >= 0) {
            s_customBossLive = true;
            RegisterSpawned(bid, pr->actionKind, false);
        }
    } else if (pr->actionKind == OW_SPAWN_AI_RIVAL_SPAR) {
        if (OW_IsInSafeZone(pa->position)) {
            OW_SetBanner("HAY BUOC RA NGOAI OC DAO AN TOAN DE TY THI VOI CAO THU!", 2.8f);
            return false;
        }
        OWSandbox_SetPkMode(player, PK_MODE_SPAR);
        Vector3 rp = FindSafeCombatPos(pa->position, 7.5f);
        int botId = AI_SpawnHeroBot(rp, TEAM_ALLY);
        if (botId >= 0) {
            EquipRivalLoadout(botId, pr->element);
            Entity_SetPkMode(botId, PK_MODE_SPAR);
            RegisterSpawned(botId, pr->actionKind, false);
            s_sparActive = true;
            s_sparCenter = (Vector3){
                (pa->position.x + rp.x) * 0.5f,
                MapManager_GetGroundHeightAt((pa->position.x + rp.x) * 0.5f, (pa->position.z + rp.z) * 0.5f),
                (pa->position.z + rp.z) * 0.5f
            };
            s_sparOpponentId = botId;
        }
    } else if (pr->actionKind == OW_SPAWN_AI_RIVAL_PK) {
        if (OW_IsInSafeZone(pa->position)) {
            OW_SetBanner("HAY RA NGOAI OC DAO AN TOAN DE GIAO CHIEN SINH TU!", 2.8f);
            return false;
        }
        OWSandbox_SetPkMode(player, PK_MODE_SLAUGHTER);
        for (int i = 0; i < count; i++) {
            Vector3 rp = FindSafeCombatPos(pa->position, 8.5f + (float)i * 2.0f);
            int botId = AI_SpawnHeroBot(rp, TEAM_ALLY);
            if (botId >= 0) {
                EquipRivalLoadout(botId, pr->element);
                Entity_SetPkMode(botId, PK_MODE_SLAUGHTER);
                RegisterSpawned(botId, pr->actionKind, false);
            }
        }
    }

    if (pr->bannerText && pr->bannerText[0] != '\0') {
        OW_SetBanner(pr->bannerText, 3.6f);
    }
    return true;
}

const char *OWSandbox_GetNearbyPrompt(Vector3 playerPos) {
    if (!s_hasRealm) return NULL;
    if (s_realm.altarPos.x != 0.0f && DistXZ(playerPos, s_realm.altarPos) <= 3.6f) {
        return "[F] MO TE DAN VAN YEU (NHA QUAI / GOI BOSS)";
    }
    if (s_realm.duelBannerPos.x != 0.0f && DistXZ(playerPos, s_realm.duelBannerPos) <= 3.4f) {
        return "[F] CAM CO TY THI / THACH DAU CAO THU";
    }
    return NULL;
}

bool OWSandbox_TryInteract(PlayerEntity *player) {
    if (!s_hasRealm || !player) return false;
    const Agent *pa = Entity_GetAgent(player->agentId);
    if (!pa) return false;

    if (s_realm.altarPos.x != 0.0f && DistXZ(pa->position, s_realm.altarPos) <= 3.6f) {
        s_panelOpen = true;
        s_panelTab = 0;
        return true;
    }
    if (s_realm.duelBannerPos.x != 0.0f && DistXZ(pa->position, s_realm.duelBannerPos) <= 3.4f) {
        for (int i = 0; i < s_realm.spawnerPresetCount; i++) {
            if (s_realm.spawnerPresets[i].actionKind == OW_SPAWN_AI_RIVAL_SPAR) {
                return OWSandbox_TriggerPreset(player, i);
            }
        }
        OWSandbox_CyclePkMode(player);
        return true;
    }
    return false;
}

float OWSandbox_GetSlaughterDamageMult(const Agent *pa) {
    if (!pa || pa->pkMode != PK_MODE_SLAUGHTER) return 1.0f;
    return 1.0f + (float)pa->infamyLevel * s_pvpCfg.slaughterDmgBonus;
}

float OWSandbox_GetSlaughterSpeedMult(const Agent *pa) {
    if (!pa || pa->pkMode != PK_MODE_SLAUGHTER) return 1.0f;
    return 1.0f + s_pvpCfg.slaughterSpeedBonus;
}

static void FinishSparMatch(PlayerEntity *player, int winnerId, int loserId) {
    s_sparActive = false;
    s_sparOpponentId = -1;

    const Agent *wa = Entity_GetAgent(winnerId);
    const Agent *la = Entity_GetAgent(loserId);
    if (wa) {
        Entity_ApplyDamage(winnerId, -wa->maxHealth * 0.5f, (Vector3){ 0 });
        Entity_RestoreMana(winnerId, wa->maxMana * 0.5f);
    }
    if (la) {
        Entity_ApplyDamage(loserId, -la->maxHealth * 0.5f, (Vector3){ 0 });
        Entity_RestoreMana(loserId, la->maxMana * 0.5f);
    }

    if (player && winnerId == player->agentId) {
        PlayerProfile *prof = PlayerProfile_GetGlobal();
        prof->gold += s_pvpCfg.sparRewardGold;
        if (wa && wa->equippedSkills[0] >= 0) {
            PlayerProfile_AddSkillXp(wa->equippedSkills[0], (uint32_t)s_pvpCfg.sparRewardSkillXp);
        }
        PlayerProfile_SaveGlobal();
        OW_SetBanner(TextFormat("TY THI TOAN THANG! +%d VANG +%d EXP SKILL!",
                                s_pvpCfg.sparRewardGold, s_pvpCfg.sparRewardSkillXp), 4.0f);
    } else {
        OW_SetBanner("TY THI KET THUC! DIEM DAO VI CHI — HOI PHUC SINH LUC!", 3.5f);
    }

    // Despawn AI spar rival cleanly after salute
    for (int i = 0; i < OW_MAX_CUSTOM_SPAWNED; i++) {
        if (s_spawned[i].used && s_spawned[i].kind == OW_SPAWN_AI_RIVAL_SPAR) {
            int aid = s_spawned[i].agentId;
            if (aid >= 0 && Entity_GetAgent(aid) != NULL) {
                Entity_ApplyDamage(aid, 1e9f, (Vector3){ 0 });
            }
            s_spawned[i].used = false;
        }
    }
}

void OWSandbox_Update(PlayerEntity *player, float dt) {
    s_clock += dt;
    if (!s_hasRealm || !player) return;

    if (IsKeyPressed(KEY_B)) {
        s_panelOpen = !s_panelOpen;
    }

    const Agent *pa = Entity_GetAgent(player->agentId);
    if (!pa) return;

    // 1. Check custom Boss defeat
    if (s_customBossLive && !Boss_IsSlotAlive(0)) {
        s_customBossLive = false;
        OWLoot_SpawnOrb(pa->position, OW_LOOT_GOLD, 320);
        OWLoot_SpawnOrb(pa->position, OW_LOOT_STONE, 2);
        OWLoot_SpawnOrb(pa->position, OW_LOOT_DATAU_TOKEN, 1);
    }

    // 2. Sweep custom-spawned entries & detect PK / Bounty kills
    for (int i = 0; i < OW_MAX_CUSTOM_SPAWNED; i++) {
        if (!s_spawned[i].used) continue;
        const Agent *sa = Entity_GetAgent(s_spawned[i].agentId);
        if (sa == NULL) {
            if (s_spawned[i].kind == OW_SPAWN_AI_RIVAL_PK) {
                if (s_spawned[i].isBountyEnforcer) {
                    s_bountyActive = false;
                    s_bountyAgentId = -1;
                    OWLoot_SpawnOrb(pa->position, OW_LOOT_GOLD, 250);
                    OWLoot_SpawnOrb(pa->position, OW_LOOT_STONE, 2);
                    OWLoot_SpawnOrb(pa->position, OW_LOOT_POTION_HEAL, 1);
                    OW_SetBanner("HA GUC BO KHOAI TRUY NA! RUONG KHO BAU ROI RA!", 3.8f);
                } else if (pa->pkMode == PK_MODE_SLAUGHTER) {
                    Entity_AddInfamy(player->agentId, 1);
                    OWLoot_SpawnOrb(pa->position, OW_LOOT_GOLD, 110);
                    OW_SetBanner(TextFormat("DO SAT THANH CONG! SAT KHI TANG LEN CAP %d!",
                                            Entity_GetInfamyLevel(player->agentId)), 3.2f);
                }
            }
            s_spawned[i].used = false;
        }
    }

    // 3. Bounty Enforcer pursuit when Infamy >= threshold
    if (pa->pkMode == PK_MODE_SLAUGHTER &&
        pa->infamyLevel >= s_pvpCfg.bountyInfamyThreshold &&
        !s_bountyActive && !OW_IsInSafeZone(pa->position)) {
        Vector3 bp = FindSafeCombatPos(pa->position, 11.0f);
        int bid = AI_SpawnHeroBot(bp, TEAM_ALLY);
        if (bid >= 0) {
            EquipRivalLoadout(bid, s_realm.realmId);
            Entity_SetPkMode(bid, PK_MODE_SLAUGHTER);
            Entity_ScaleMaxHealth(bid, 1.35f);
            s_bountyActive = true;
            s_bountyAgentId = bid;
            RegisterSpawned(bid, OW_SPAWN_AI_RIVAL_PK, true);
            OW_SetBanner("TRUY NA GIANG HO! BO KHOAI THIET HUYET DANG TOI BAT BAN!", 4.0f);
        }
    }

    // 4. Sparring 1v1 detection & Non-Lethal 1 HP finish
    if (pa->pkMode == PK_MODE_SPAR) {
        int attId = -1;
        if (Entity_ConsumeSparDefeated(player->agentId, &attId)) {
            FinishSparMatch(player, (attId >= 0) ? attId : s_sparOpponentId, player->agentId);
            return;
        }
        if (s_sparOpponentId >= 0 && Entity_GetAgent(s_sparOpponentId) != NULL) {
            if (Entity_ConsumeSparDefeated(s_sparOpponentId, &attId)) {
                FinishSparMatch(player, player->agentId, s_sparOpponentId);
                return;
            }
        } else {
            // Look for any nearby SPAR Hero to form a Duel Ring
            for (int i = 0; i < MAX_AGENTS; i++) {
                if (i == player->agentId) continue;
                const Agent *cand = Entity_GetAgent(i);
                if (!cand || cand->archetype != ARCH_HERO || cand->pkMode != PK_MODE_SPAR) continue;
                if (DistXZ(pa->position, cand->position) <= s_pvpCfg.sparRingRadius) {
                    s_sparActive = true;
                    s_sparOpponentId = i;
                    s_sparCenter = (Vector3){
                        (pa->position.x + cand->position.x) * 0.5f,
                        pa->position.y,
                        (pa->position.z + cand->position.z) * 0.5f
                    };
                    break;
                }
            }
        }
    }
}

void OWSandbox_Draw3D(const PlayerEntity *player, Camera3D cam) {
    (void)cam;
    if (!s_hasRealm) return;

    // 1. Tế Đàn Vạn Yêu (3D Summoning Altar)
    if (s_realm.altarPos.x != 0.0f) {
        Vector3 ap = s_realm.altarPos;
        ap.y = MapManager_GetGroundHeightAt(ap.x, ap.z);
        DrawCylinder(ap, 1.35f, 1.55f, 0.45f, 8, (Color){ 92, 68, 48, 255 });
        DrawCylinder((Vector3){ ap.x, ap.y + 0.45f, ap.z }, 0.55f, 0.72f, 1.4f, 8, (Color){ 125, 92, 58, 255 });
        float bob = 2.15f + 0.18f * sinf(s_clock * 3.0f);
        DrawSphere((Vector3){ ap.x, ap.y + bob, ap.z }, 0.32f, (Color){ 255, 165, 55, 240 });
        DrawCircle3D((Vector3){ ap.x, ap.y + 0.14f, ap.z }, 3.4f, (Vector3){ 1, 0, 0 }, 90.0f, (Color){ 255, 185, 75, 175 });
    }

    // 2. Cờ Tỉ Thí (3D Duel Banner)
    if (s_realm.duelBannerPos.x != 0.0f) {
        Vector3 dp = s_realm.duelBannerPos;
        dp.y = MapManager_GetGroundHeightAt(dp.x, dp.z);
        DrawCylinder(dp, 0.08f, 0.10f, 3.0f, 8, (Color){ 170, 145, 95, 255 });
        DrawCube((Vector3){ dp.x + 0.45f, dp.y + 2.35f, dp.z }, 0.90f, 0.75f, 0.08f, (Color){ 215, 65, 55, 255 });
        DrawCircle3D((Vector3){ dp.x, dp.y + 0.14f, dp.z }, 3.2f, (Vector3){ 1, 0, 0 }, 90.0f, GOLD);
    }

    // 3. Active 1v1 Sparring Ring (Golden 12m Arena)
    if (s_sparActive) {
        DrawCircle3D((Vector3){ s_sparCenter.x, s_sparCenter.y + 0.16f, s_sparCenter.z },
                     s_pvpCfg.sparRingRadius, (Vector3){ 1, 0, 0 }, 90.0f, GOLD);
    }

    // 4. Player PK Aura Ring (Spar = Gold, Slaughter = Crimson)
    const Agent *pa = player ? Entity_GetAgent(player->agentId) : NULL;
    if (pa && pa->pkMode != PK_MODE_PEACE) {
        Color rc = (pa->pkMode == PK_MODE_SLAUGHTER) ? (Color){ 255, 45, 45, 235 } : (Color){ 255, 215, 75, 225 };
        DrawCircle3D((Vector3){ pa->position.x, pa->position.y + 0.14f, pa->position.z },
                     1.15f, (Vector3){ 1, 0, 0 }, 90.0f, rc);
    }
}

void OWSandbox_DrawHUD(const PlayerEntity *player, Camera3D cam) {
    if (!s_hasRealm) return;
    int sw = GetScreenWidth();
    const Agent *pa = player ? Entity_GetAgent(player->agentId) : NULL;
    bool touch = UI_IsTouchControlsEnabled();

    // 3D world labels for Altar & Duel Banner
    if (s_realm.altarPos.x != 0.0f) {
        Vector3 ap = { s_realm.altarPos.x, MapManager_GetGroundHeightAt(s_realm.altarPos.x, s_realm.altarPos.z) + 2.7f, s_realm.altarPos.z };
        Vector2 sp = GetWorldToScreen(ap, cam);
        if (sp.x > 0 && sp.x < sw && sp.y > 0 && sp.y < GetScreenHeight()) {
            const char *lbl = "TE DAN VAN YEU [B]";
            int tw = MeasureText(lbl, 12);
            DrawRectangle((int)sp.x - tw / 2 - 6, (int)sp.y - 10, tw + 12, 18, (Color){ 22, 16, 12, 210 });
            DrawText(lbl, (int)sp.x - tw / 2, (int)sp.y - 7, 12, ORANGE);
        }
    }
    if (s_realm.duelBannerPos.x != 0.0f) {
        Vector3 dp = { s_realm.duelBannerPos.x, MapManager_GetGroundHeightAt(s_realm.duelBannerPos.x, s_realm.duelBannerPos.z) + 3.2f, s_realm.duelBannerPos.z };
        Vector2 sp = GetWorldToScreen(dp, cam);
        if (sp.x > 0 && sp.x < sw && sp.y > 0 && sp.y < GetScreenHeight()) {
            const char *lbl = "CO TY THI [F]";
            int tw = MeasureText(lbl, 12);
            DrawRectangle((int)sp.x - tw / 2 - 6, (int)sp.y - 10, tw + 12, 18, (Color){ 24, 14, 14, 210 });
            DrawText(lbl, (int)sp.x - tw / 2, (int)sp.y - 7, 12, GOLD);
        }
    }

    // Quick-access button on HUD (right side below Dã Tẩu quest card)
    float btnW = 148.0f, btnH = 30.0f;
    float bx = (float)sw - btnW - 16.0f;
    float by = touch ? 162.0f : 130.0f;
    Rectangle qb = { bx, by, btnW, btnH };
    if (WuxiaUI_DrawButton(qb, s_panelOpen ? "DONG KY NGO [B]" : "KY NGO / BOSS [B]",
                           WUXIA_GOLD, s_panelOpen, 12.0f)) {
        s_panelOpen = !s_panelOpen;
    }

    if (pa) {
        Rectangle pkb = { bx - 128.0f, by, 120.0f, btnH };
        Color pkCol = (pa->pkMode == PK_MODE_SLAUGHTER) ? WUXIA_CRIMSON
                    : (pa->pkMode == PK_MODE_SPAR)      ? WUXIA_GOLD
                                                        : WUXIA_JADE;
        const char *pkLbl = (pa->pkMode == PK_MODE_SLAUGHTER)
            ? TextFormat("DO SAT (%d)", pa->infamyLevel)
            : OWSandbox_GetPkModeLabel(pa->pkMode);
        if (WuxiaUI_DrawButton(pkb, pkLbl, pkCol, pa->pkMode != PK_MODE_PEACE, 11.5f)) {
            OWSandbox_CyclePkMode((PlayerEntity *)player);
        }
    }

    if (!s_panelOpen) return;

    // --- Sandbox Quick Panel ([B]) ---
    float pw = 500.0f, ph = 336.0f;
    Rectangle pb = { (float)sw * 0.5f - pw * 0.5f, 114.0f, pw, ph };
    WuxiaUI_DrawPanel(pb, WUXIA_GOLD, true);
    WuxiaUI_DrawText("KY NGO GIANG HO — TU DO CHIEN DAU & THOI TIET",
                     pb.x + 18.0f, pb.y + 12.0f, 15.0f, WUXIA_GOLD);

    Rectangle closeR = { pb.x + pb.width - 74.0f, pb.y + 8.0f, 60.0f, 26.0f };
    if (WuxiaUI_DrawButton(closeR, "DONG", WUXIA_CRIMSON, false, 12.0f)) {
        s_panelOpen = false;
    }

    // Tabs: [0] Nhả Quái & Boss | [1] Thời Tiết & Tỉ Thí
    Rectangle t0 = { pb.x + 18.0f, pb.y + 44.0f, 224.0f, 30.0f };
    Rectangle t1 = { pb.x + 252.0f, pb.y + 44.0f, 230.0f, 30.0f };
    if (WuxiaUI_DrawButton(t0, "NHA QUAI / BOSS / CAO THU", WUXIA_GOLD, s_panelTab == 0, 12.0f)) s_panelTab = 0;
    if (WuxiaUI_DrawButton(t1, "THOI TIET & CHE DO PK", WUXIA_AZURE, s_panelTab == 1, 12.0f)) s_panelTab = 1;

    if (s_panelTab == 0) {
        int cnt = s_realm.spawnerPresetCount;
        if (cnt > OW_MAX_SPAWNER_PRESETS) cnt = OW_MAX_SPAWNER_PRESETS;
        for (int i = 0; i < cnt; i++) {
            int col = i % 2;
            int row = i / 2;
            Rectangle sr = {
                pb.x + 18.0f + (float)col * 234.0f,
                pb.y + 86.0f + (float)row * 56.0f,
                224.0f, 46.0f
            };
            Color sc = (s_realm.spawnerPresets[i].actionKind == OW_SPAWN_WORLD_BOSS) ? WUXIA_CRIMSON
                     : (s_realm.spawnerPresets[i].actionKind == OW_SPAWN_AI_RIVAL_SPAR) ? WUXIA_GOLD
                     : (s_realm.spawnerPresets[i].actionKind == OW_SPAWN_AI_RIVAL_PK) ? ORANGE
                                                                                      : WUXIA_AZURE;
            if (WuxiaUI_DrawButton(sr, s_realm.spawnerPresets[i].label, sc, false, 12.5f)) {
                OWSandbox_TriggerPreset((PlayerEntity *)player, i);
                s_panelOpen = false;
            }
        }
        Rectangle clrBtn = { pb.x + 18.0f, pb.y + 272.0f, pb.width - 36.0f, 44.0f };
        int activeCustom = OWSandbox_GetActiveCustomSpawnCount();
        if (WuxiaUI_DrawButton(clrBtn, TextFormat("TINH HOA SA MAC — THU HOI QUAI/BOSS (%d)", activeCustom),
                               WUXIA_JADE, false, 13.0f)) {
            OWSandbox_ClearCustomSpawns();
        }
    } else {
        int wCnt = OWWeather_GetProfileCount();
        for (int i = 0; i < wCnt && i < OW_MAX_WEATHER_PROFILES; i++) {
            const OWWeatherProfileDef *wp = OWWeather_GetProfile(i);
            if (!wp) continue;
            int col = i % 2;
            int row = i / 2;
            Rectangle wr = {
                pb.x + 18.0f + (float)col * 234.0f,
                pb.y + 86.0f + (float)row * 56.0f,
                224.0f, 46.0f
            };
            bool activeW = (OWWeather_GetCurrentIndex() == i);
            if (WuxiaUI_DrawButton(wr, wp->name, wp->accentColor, activeW, 12.0f)) {
                OWWeather_SetWeather(i, 0.0f);
            }
        }

        WuxiaUI_DrawText("CHUYEN TRANG THAI PK GIANG HO [V]:", pb.x + 18.0f, pb.y + 216.0f, 12.5f, WUXIA_TEXT_MUTED);
        AgentPkMode curPk = pa ? pa->pkMode : PK_MODE_PEACE;
        struct { AgentPkMode m; const char *lbl; Color col; } pkBtns[3] = {
            { PK_MODE_PEACE,     "HOA BINH",     WUXIA_JADE },
            { PK_MODE_SPAR,      "TY THI (1v1)", WUXIA_GOLD },
            { PK_MODE_SLAUGHTER, "DO SAT (PK)",  WUXIA_CRIMSON }
        };
        for (int k = 0; k < 3; k++) {
            Rectangle pr = { pb.x + 18.0f + (float)k * 156.0f, pb.y + 242.0f, 148.0f, 46.0f };
            if (WuxiaUI_DrawButton(pr, pkBtns[k].lbl, pkBtns[k].col, curPk == pkBtns[k].m, 12.5f)) {
                OWSandbox_SetPkMode((PlayerEntity *)player, pkBtns[k].m);
            }
        }
    }
}
