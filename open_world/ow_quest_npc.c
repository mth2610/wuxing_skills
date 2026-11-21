// open_world/ow_quest_npc.c
#include "open_world/ow_quest_npc.h"
#include "open_world/ow_loot.h"
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "entities/entities.h"
#include "game/quest_datau.h"
#include "maps/map_manager.h"
#include "skills/skill_manager.h"
#include "ui/ui_hub.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_QUEST_AGENTS 6
#define MAX_TOTEMS       3

typedef struct {
    bool           active;
    DaTauQuestType type;
    Vector3        center;
    bool           sealStarted;
    int            sealWave;
    int            agentIds[MAX_QUEST_AGENTS];
    int            agentCount;
    float          thiefAngle;
    Vector3        totemPos[MAX_TOTEMS];
    bool           totemCleared[MAX_TOTEMS];
} OWQuestEncounter;

static OWQuestEncounter s_encounter = { 0 };
static Vector3          s_daTauPos = { 0 };
static Vector3          s_alchemistPos = { 0 };
static Vector3          s_stelePos = { 0 };
static Vector3          s_canyonCenter = { 0 };
static Vector3          s_dunesCenter = { 0 };
static int              s_realmId = 3;

static int   s_steleAgentId   = -1;
static float s_steleHealDelay = 0.0f;
static float s_stelePrevHp    = 10000.0f;
static bool  s_npcDialogOpen  = false;

static float DistXZ(Vector3 a, Vector3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

static void ClearEncounterAgents(void) {
    for (int i = 0; i < s_encounter.agentCount; i++) {
        if (s_encounter.agentIds[i] >= 0 && Entity_GetAgent(s_encounter.agentIds[i])) {
            Entity_ApplyDamage(s_encounter.agentIds[i], 1e9f, (Vector3){ 0 });
        }
    }
    memset(&s_encounter, 0, sizeof(s_encounter));
}

static void SpawnSealWave(Vector3 center, int wave) {
    s_encounter.agentCount = 0;
    int cnt = 2 + wave;
    for (int i = 0; i < cnt && s_encounter.agentCount < MAX_QUEST_AGENTS; i++) {
        float ang = (float)i * (2.0f * PI / (float)cnt);
        Vector3 sp = { center.x + cosf(ang) * 5.5f, 0.0f, center.z + sinf(ang) * 5.5f };
        sp.y = MapManager_GetGroundHeightAt(sp.x, sp.z);
        int id = Entity_SpawnAgent(sp, 65.0f + wave * 15.0f, s_realmId, TEAM_ENEMY, ARCH_MINION);
        if (id >= 0) s_encounter.agentIds[s_encounter.agentCount++] = id;
    }
    OW_SetBanner(TextFormat("CO TRAN PHAT SANG! DOT QUAI %d/3!", wave), 3.0f);
}

static void DrawNpcFigure(Vector3 pos, Color robeCol, Color hatCol, Color ringCol) {
    pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
    DrawCircle3D((Vector3){ pos.x, pos.y + 0.08f, pos.z }, 1.0f, (Vector3){ 1, 0, 0 }, 90.0f, ringCol);
    DrawCylinder(pos, 0.38f, 0.25f, 1.25f, 10, robeCol);
    DrawSphere((Vector3){ pos.x, pos.y + 1.45f, pos.z }, 0.24f, (Color){ 240, 205, 175, 255 });
    DrawCylinder((Vector3){ pos.x, pos.y + 1.62f, pos.z }, 0.52f, 0.05f, 0.24f, 12, hatCol);
}

void OWQuestNpc_Init(const OWRealmDef *realm, bool spawnStele) {
    memset(&s_encounter, 0, sizeof(s_encounter));
    s_npcDialogOpen = false;
    s_steleAgentId = -1;
    s_steleHealDelay = 0.0f;
    s_stelePrevHp = 10000.0f;
    if (!realm) return;

    s_realmId      = realm->realmId;
    s_daTauPos     = realm->daTauNpcPos;
    s_alchemistPos = realm->alchemistPos;
    s_stelePos     = realm->stelePos;
    s_canyonCenter = realm->canyonCenter;
    s_dunesCenter  = realm->dunesCenter;

    if (spawnStele) {
        Vector3 spStele = { s_stelePos.x, MapManager_GetGroundHeightAt(s_stelePos.x, s_stelePos.z), s_stelePos.z };
        s_steleAgentId = Entity_SpawnAgent(spStele, 10000.0f, s_realmId, TEAM_ENEMY, ARCH_MINION);
        Entity_SetStatic(s_steleAgentId, true);
    }
}

void OWQuestNpc_SyncEncounter(void) {
    const DaTauQuest *q = DaTau_GetCurrent();
    if (!q || q->phase != DATAU_PHASE_IN_PROGRESS) {
        ClearEncounterAgents();
        return;
    }
    if (s_encounter.active && s_encounter.type == q->type) return;

    ClearEncounterAgents();
    s_encounter.active = true;
    s_encounter.type = q->type;

    if (q->type == DATAU_ANCIENT_SEAL) {
        s_encounter.center = (Vector3){ s_canyonCenter.x, MapManager_GetGroundHeightAt(s_canyonCenter.x, s_canyonCenter.z), s_canyonCenter.z };
        s_encounter.sealStarted = false;
        s_encounter.sealWave = 0;
    } else if (q->type == DATAU_SAND_THIEF) {
        s_encounter.center = (Vector3){ s_dunesCenter.x - 7.0f, MapManager_GetGroundHeightAt(s_dunesCenter.x - 7.0f, s_dunesCenter.z - 7.0f), s_dunesCenter.z - 7.0f };
        int id = Entity_SpawnAgent(s_encounter.center, 110.0f, s_realmId, TEAM_ENEMY, ARCH_MINION);
        if (id >= 0) s_encounter.agentIds[s_encounter.agentCount++] = id;
    } else if (q->type == DATAU_CARAVAN_RESCUE) {
        s_encounter.center = (Vector3){ s_dunesCenter.x + 3.0f, MapManager_GetGroundHeightAt(s_dunesCenter.x + 3.0f, s_dunesCenter.z + 3.0f), s_dunesCenter.z + 3.0f };
        for (int i = 0; i < 4; i++) {
            float ang = (float)i * (PI * 0.5f);
            Vector3 sp = { s_encounter.center.x + cosf(ang) * 3.8f, 0.0f, s_encounter.center.z + sinf(ang) * 3.8f };
            sp.y = MapManager_GetGroundHeightAt(sp.x, sp.z);
            int id = Entity_SpawnAgent(sp, 55.0f, s_realmId, TEAM_ENEMY, ARCH_MINION);
            if (id >= 0) s_encounter.agentIds[s_encounter.agentCount++] = id;
        }
    } else if (q->type == DATAU_PURIFY_TOTEMS) {
        s_encounter.center = (Vector3){ s_canyonCenter.x, MapManager_GetGroundHeightAt(s_canyonCenter.x, s_canyonCenter.z), s_canyonCenter.z };
        Vector3 offsets[3] = { { -6.0f, 0.0f, -5.0f }, { 8.0f, 0.0f, 6.0f }, { 1.0f, 0.0f, -16.0f } };
        for (int i = 0; i < 3; i++) {
            Vector3 tp = { s_canyonCenter.x + offsets[i].x, 0.0f, s_canyonCenter.z + offsets[i].z };
            tp.y = MapManager_GetGroundHeightAt(tp.x, tp.z);
            s_encounter.totemPos[i] = tp;
            s_encounter.totemCleared[i] = false;
        }
    }
}

bool OWQuestNpc_TryInteract(Vector3 playerPos) {
    const DaTauQuest *q = DaTau_GetCurrent();

    if (DistXZ(playerPos, s_daTauPos) <= 3.4f) {
        if (!s_npcDialogOpen) {
            s_npcDialogOpen = true;
            return true;
        }
        if (q && q->phase == DATAU_PHASE_WAIT_ACCEPT) {
            if (DaTau_AcceptFromNPC()) {
                OWQuestNpc_SyncEncounter();
                OW_TriggerGuidingWind();
                OW_SetBanner(TextFormat("NHAN NHIEM VU: %s", DaTau_GetCurrent()->title), 3.5f);
                s_npcDialogOpen = false;
                return true;
            }
        } else if (q && q->phase == DATAU_PHASE_READY_RETURN) {
            if (DaTau_TurnInToNPC()) {
                ClearEncounterAgents();
                OW_TriggerGuidingWind();
                OW_SetBanner(DaTau_GetLastRewardText(), 3.8f);
                s_npcDialogOpen = false;
                return true;
            }
        }
        s_npcDialogOpen = false;
        return true;
    }

    if (DistXZ(playerPos, s_alchemistPos) <= 3.2f) {
        UI_Hub_OpenModal(1);
        return true;
    }

    if (q && q->phase == DATAU_PHASE_IN_PROGRESS && s_encounter.active) {
        if (q->type == DATAU_ANCIENT_SEAL && !s_encounter.sealStarted &&
            DistXZ(playerPos, s_encounter.center) <= 4.0f) {
            s_encounter.sealStarted = true;
            s_encounter.sealWave = 1;
            SpawnSealWave(s_encounter.center, 1);
            return true;
        }
        if (q->type == DATAU_PURIFY_TOTEMS) {
            for (int i = 0; i < MAX_TOTEMS; i++) {
                if (!s_encounter.totemCleared[i] && DistXZ(playerPos, s_encounter.totemPos[i]) <= 3.2f) {
                    s_encounter.totemCleared[i] = true;
                    OWLoot_SpawnOrb(s_encounter.totemPos[i], OW_LOOT_STONE, 1);
                    OWLoot_SpawnOrb(s_encounter.totemPos[i], OW_LOOT_GOLD, 45);
                    if (DaTau_AdvanceObjective(1)) OW_TriggerGuidingWind();
                    return true;
                }
            }
        }
    }
    return false;
}

static bool CheckNpcTap(Vector3 npcPos, Vector3 playerPos, Vector2 tapPos, Camera3D cam) {
    Vector3 mid = { npcPos.x, MapManager_GetGroundHeightAt(npcPos.x, npcPos.z) + 1.0f, npcPos.z };
    Vector2 sp = GetWorldToScreen(mid, cam);
    float dx = sp.x - tapPos.x, dy = sp.y - tapPos.y;
    if (dx * dx + dy * dy <= 62.0f * 62.0f) {
        if (DistXZ(playerPos, npcPos) <= 5.5f) {
            OWQuestNpc_TryInteract(playerPos);
        } else {
            OW_SetBanner("LAI GAN NPC HON DE NOI CHUYEN!", 2.2f);
        }
        return true;
    }
    return false;
}

bool OWQuestNpc_HandleTap(Vector3 playerPos, Vector2 tapScreenPos, Camera3D cam) {
    if (CheckNpcTap(s_daTauPos, playerPos, tapScreenPos, cam)) return true;
    if (CheckNpcTap(s_alchemistPos, playerPos, tapScreenPos, cam)) return true;
    return false;
}

bool OWQuestNpc_GetEncounterGuideTarget(Vector3 playerPos, Vector3 *outPos) {
    const DaTauQuest *q = DaTau_GetCurrent();
    if (!q || q->phase != DATAU_PHASE_IN_PROGRESS || !s_encounter.active) return false;
    if (q->type == DATAU_PURIFY_TOTEMS) {
        float bestD = 1e9f;
        bool found = false;
        for (int i = 0; i < MAX_TOTEMS; i++) {
            if (s_encounter.totemCleared[i]) continue;
            float d = DistXZ(playerPos, s_encounter.totemPos[i]);
            if (d < bestD) { bestD = d; if (outPos) *outPos = s_encounter.totemPos[i]; found = true; }
        }
        return found;
    }
    if (q->type == DATAU_SAND_THIEF && s_encounter.agentCount > 0) {
        const Agent *th = Entity_GetAgent(s_encounter.agentIds[0]);
        if (th && outPos) { *outPos = th->position; return true; }
    }
    if (outPos) *outPos = s_encounter.center;
    return true;
}

const char *OWQuestNpc_GetNearbyPrompt(Vector3 playerPos) {
    const DaTauQuest *q = DaTau_GetCurrent();
    if (DistXZ(playerPos, s_daTauPos) <= 3.4f) {
        if (q && q->phase == DATAU_PHASE_WAIT_ACCEPT)  return "[F] NOI CHUYEN & NHAN NHIEM VU DA TAU";
        if (q && q->phase == DATAU_PHASE_READY_RETURN) return "[F] TRA NHIEM VU & NHAN THUONG!";
        return "[F] NOI CHUYEN VOI DA TAU";
    }
    if (DistXZ(playerPos, s_alchemistPos) <= 3.2f) {
        return "[F] MO TIEM DOI VO HOC / DAN DUOC / TU DO";
    }
    if (q && q->phase == DATAU_PHASE_IN_PROGRESS && s_encounter.active) {
        if (q->type == DATAU_ANCIENT_SEAL && !s_encounter.sealStarted && DistXZ(playerPos, s_encounter.center) <= 4.0f)
            return "[F] KHAI AN CO TRAN!";
        if (q->type == DATAU_PURIFY_TOTEMS) {
            for (int i = 0; i < MAX_TOTEMS; i++) {
                if (!s_encounter.totemCleared[i] && DistXZ(playerPos, s_encounter.totemPos[i]) <= 3.2f)
                    return "[F] THANH TAY TRU TA KHI!";
            }
        }
    }
    return NULL;
}

int OWQuestNpc_GetSteleAgentId(void) { return s_steleAgentId; }

void OWQuestNpc_Update(const Agent *pa, float dt) {
    if (s_steleAgentId >= 0) {
        const Agent *st = Entity_GetAgent(s_steleAgentId);
        if (st) {
            if (st->health < s_stelePrevHp - 0.5f) {
                float hit = s_stelePrevHp - st->health;
                AddFloatingText(st->position, TextFormat("Bia Da: -%.0f DMG", hit), ORANGE, 15.0f, 1.0f);
                s_stelePrevHp = st->health;
                s_steleHealDelay = 3.0f;
            }
            if (s_steleHealDelay > 0.0f) {
                s_steleHealDelay -= dt;
                if (s_steleHealDelay <= 0.0f) {
                    Entity_ApplyDamage(s_steleAgentId, -10000.0f, (Vector3){ 0 });
                    s_stelePrevHp = 10000.0f;
                }
            }
        }
    }

    if (pa && s_npcDialogOpen && DistXZ(pa->position, s_daTauPos) > 4.2f) {
        s_npcDialogOpen = false;
    }

    const DaTauQuest *q = DaTau_GetCurrent();
    if (!q || q->phase != DATAU_PHASE_IN_PROGRESS || !s_encounter.active) return;

    if (q->type == DATAU_SAND_THIEF && s_encounter.agentCount > 0) {
        int tid = s_encounter.agentIds[0];
        const Agent *th = Entity_GetAgent(tid);
        if (!th) {
            OWLoot_SpawnOrb(s_encounter.center, OW_LOOT_GOLD, 160);
            OWLoot_SpawnOrb(s_encounter.center, OW_LOOT_DATAU_TOKEN, 1);
            if (DaTau_AdvanceObjective(1)) OW_TriggerGuidingWind();
        } else if (!Entity_IsCrowdControlled(tid)) {
            s_encounter.thiefAngle += dt * 1.15f;
            float tx = s_encounter.center.x + cosf(s_encounter.thiefAngle) * 9.5f;
            float tz = s_encounter.center.z + sinf(s_encounter.thiefAngle * 1.3f) * 8.0f;
            Entity_SetPosition(tid, (Vector3){ tx, MapManager_GetGroundHeightAt(tx, tz), tz });
        }
    } else if (q->type == DATAU_ANCIENT_SEAL && s_encounter.sealStarted) {
        int alive = 0;
        for (int i = 0; i < s_encounter.agentCount; i++) {
            if (Entity_GetAgent(s_encounter.agentIds[i])) alive++;
        }
        if (alive == 0) {
            if (DaTau_AdvanceObjective(1)) {
                OWLoot_SpawnOrb(s_encounter.center, OW_LOOT_GOLD, 120);
                OWLoot_SpawnOrb(s_encounter.center, OW_LOOT_STONE, 2);
                OW_TriggerGuidingWind();
            } else if (s_encounter.sealWave < 3) {
                s_encounter.sealWave++;
                SpawnSealWave(s_encounter.center, s_encounter.sealWave);
            }
        }
    } else if (q->type == DATAU_CARAVAN_RESCUE) {
        int alive = 0;
        for (int i = 0; i < s_encounter.agentCount; i++) {
            if (Entity_GetAgent(s_encounter.agentIds[i])) alive++;
        }
        int cleared = s_encounter.agentCount - alive;
        if (cleared > q->currentCount) {
            if (DaTau_AdvanceObjective(cleared - q->currentCount)) {
                OWLoot_SpawnOrb(s_encounter.center, OW_LOOT_HERB, 2);
                OWLoot_SpawnOrb(s_encounter.center, OW_LOOT_POTION_HEAL, 1);
                OW_TriggerGuidingWind();
            }
        }
    }
}

void OWQuestNpc_Draw3D(void) {
    DrawNpcFigure(s_daTauPos, (Color){ 55, 110, 145, 255 }, (Color){ 210, 185, 120, 255 }, GOLD);
    DrawNpcFigure(s_alchemistPos, (Color){ 55, 135, 85, 255 }, (Color){ 185, 225, 185, 255 }, GREEN);

    if (s_steleAgentId >= 0) {
        Vector3 sp = { s_stelePos.x, MapManager_GetGroundHeightAt(s_stelePos.x, s_stelePos.z), s_stelePos.z };
        DrawCylinder(sp, 0.68f, 0.56f, 0.28f, 8, (Color){ 96, 86, 74, 255 });
        DrawCube((Vector3){ sp.x, sp.y + 1.15f, sp.z }, 0.82f, 1.78f, 0.42f, (Color){ 132, 118, 98, 255 });
        DrawCube((Vector3){ sp.x, sp.y + 2.08f, sp.z }, 0.94f, 0.16f, 0.52f, (Color){ 162, 142, 110, 255 });
        DrawSphere((Vector3){ sp.x, sp.y + 1.55f, sp.z + 0.23f }, 0.13f, (Color){ 245, 175, 75, 235 });
        DrawCylinder((Vector3){ sp.x, sp.y + 0.04f, sp.z }, 0.95f, 1.05f, 0.03f, 16, (Color){ 235, 165, 65, 85 });
    }

    const DaTauQuest *q = DaTau_GetCurrent();
    if (q && q->phase == DATAU_PHASE_IN_PROGRESS && s_encounter.active) {
        if (q->type == DATAU_ANCIENT_SEAL) {
            DrawCircle3D((Vector3){ s_encounter.center.x, s_encounter.center.y + 0.14f, s_encounter.center.z },
                         5.5f, (Vector3){ 1, 0, 0 }, 90.0f, s_encounter.sealStarted ? RED : GOLD);
            DrawCylinder(s_encounter.center, 0.5f, 0.2f, 2.4f, 8, s_encounter.sealStarted ? ORANGE : GOLD);
        } else if (q->type == DATAU_CARAVAN_RESCUE) {
            DrawNpcFigure(s_encounter.center, (Color){ 165, 120, 65, 255 }, (Color){ 235, 210, 140, 255 }, SKYBLUE);
        } else if (q->type == DATAU_SAND_THIEF && s_encounter.agentCount > 0) {
            const Agent *th = Entity_GetAgent(s_encounter.agentIds[0]);
            if (th) DrawCircle3D((Vector3){ th->position.x, th->position.y + 0.15f, th->position.z },
                                 1.1f, (Vector3){ 1, 0, 0 }, 90.0f, GOLD);
        } else if (q->type == DATAU_PURIFY_TOTEMS) {
            for (int i = 0; i < MAX_TOTEMS; i++) {
                if (s_encounter.totemCleared[i]) continue;
                Vector3 tp = s_encounter.totemPos[i];
                DrawCylinder(tp, 0.45f, 0.35f, 2.5f, 8, (Color){ 165, 65, 195, 255 });
                DrawSphere((Vector3){ tp.x, tp.y + 2.8f, tp.z }, 0.35f, PURPLE);
            }
        }
    }
}

void OWQuestNpc_DrawHUD(const Agent *pa, Camera3D cam) {
    (void)pa;
    int sw = GetScreenWidth();
    const DaTauQuest *q = DaTau_GetCurrent();

    Vector3 camFwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    const float maxTagDistSq = 65.0f * 65.0f;

    Vector3 dp = OW_GetDaTauNpcPos();
    Vector3 dpLabel = { dp.x, dp.y + 2.3f, dp.z };
    Vector3 toDp = Vector3Subtract(dpLabel, cam.position);
    float dpDistSq = Vector3LengthSqr(toDp);
    if (dpDistSq < maxTagDistSq && Vector3DotProduct(camFwd, toDp) > 0.0f) {
        float dist = sqrtf(dpDistSq);
        float alphaScale = (dist <= 45.0f) ? 1.0f : (1.0f - (dist - 45.0f) / 20.0f);
        if (alphaScale > 0.05f) {
            Vector2 sp = GetWorldToScreen(dpLabel, cam);
            const char *tag = (q && q->phase == DATAU_PHASE_READY_RETURN) ? "[?] Dã Tẩu (Trả NV)" :
                              (q && q->phase == DATAU_PHASE_WAIT_ACCEPT)  ? "[!] Dã Tẩu (Nhận NV)" : "Sứ Giả Dã Tẩu";
            Color tc = (q && q->phase != DATAU_PHASE_IN_PROGRESS) ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_PRIMARY;
            tc.a = (unsigned char)(255.0f * alphaScale);
            float tw = WuxiaUI_MeasureText(tag, 13.0f);
            Rectangle tr = { sp.x - tw * 0.5f - 8.0f, sp.y - 11.0f, tw + 16.0f, 22.0f };
            DrawRectangleRec(tr, (Color){ 14, 18, 26, (unsigned char)(210.0f * alphaScale) });
            WuxiaUI_DrawTextCentered(tag, tr, 13.0f, tc);
        }
    }

    Vector3 ap = s_alchemistPos;
    ap.y = MapManager_GetGroundHeightAt(ap.x, ap.z) + 2.3f;
    Vector3 toAp = Vector3Subtract(ap, cam.position);
    float apDistSq = Vector3LengthSqr(toAp);
    if (apDistSq < maxTagDistSq && Vector3DotProduct(camFwd, toAp) > 0.0f) {
        float dist = sqrtf(apDistSq);
        float alphaScale = (dist <= 45.0f) ? 1.0f : (1.0f - (dist - 45.0f) / 20.0f);
        if (alphaScale > 0.05f) {
            Vector2 asp = GetWorldToScreen(ap, cam);
            const char *atag = UI_IsTouchControlsEnabled() ? "Luyện Đan Sư" : "Luyện Đan Sư [F]";
            float atw = WuxiaUI_MeasureText(atag, 12.5f);
            Rectangle ar = { asp.x - atw * 0.5f - 8.0f, asp.y - 10.0f, atw + 16.0f, 20.0f };
            DrawRectangleRec(ar, (Color){ 14, 24, 20, (unsigned char)(205.0f * alphaScale) });
            WuxiaUI_DrawTextCentered(atag, ar, 12.5f, (Color){ 140, 240, 175, (unsigned char)(255.0f * alphaScale) });
        }
    }

    if (s_npcDialogOpen && q) {
        Rectangle db = { (float)sw / 2 - 260.0f, 126.0f, 520.0f, 218.0f };
        WuxiaUI_DrawPanel(db, WUXIA_GOLD, true);
        WuxiaUI_DrawDiamond((Vector2){ db.x + 22.0f, db.y + 24.0f }, 6.0f, WUXIA_GOLD, WUXIA_GOLD_BRIGHT);
        WuxiaUI_DrawTextTitle("SỨ GIẢ DÃ TẨU — GIANG HỒ KỲ NGỘ", db.x + 36.0f, db.y + 13.0f, 18.0f, WUXIA_GOLD_BRIGHT);
        WuxiaUI_DrawSpecularRule(db.x + 18.0f, db.y + 44.0f, db.width - 36.0f, WUXIA_GOLD, true);

        Rectangle closeBtn = { db.x + db.width - 84.0f, db.y + 10.0f, 70.0f, 28.0f };
        if (WuxiaUI_DrawButton(closeBtn, "ĐÓNG", WUXIA_CRIMSON, false, 13.0f)) {
            s_npcDialogOpen = false;
        }

        WuxiaUI_DrawText(q->title, db.x + 20.0f, db.y + 56.0f, 18.0f, WUXIA_TEXT_PRIMARY);
        WuxiaUI_DrawText(q->desc, db.x + 20.0f, db.y + 82.0f, 14.0f, WUXIA_TEXT_MUTED);
        WuxiaUI_DrawText(TextFormat("Phần thưởng: +%d Vàng  ·  +%d Điểm  ·  +1 Khoáng  ·  +1 Thảo",
                                    q->rewardGold, q->rewardPoints),
                         db.x + 20.0f, db.y + 112.0f, 13.5f, WUXIA_JADE);

        const char *btnTxt = (q->phase == DATAU_PHASE_WAIT_ACCEPT)  ? "NHẬN NHIỆM VỤ NÀY" :
                             (q->phase == DATAU_PHASE_READY_RETURN) ? "TRẢ NHIỆM VỤ & NHẬN THƯỞNG" :
                                                                      "ĐANG THỰC HIỆN (THEO GIÓ DẪN LỐI)";
        Rectangle btn = { db.x + 20.0f, db.y + 148.0f, db.width - 40.0f, 48.0f };
        if (WuxiaUI_DrawButton(btn, btnTxt, WUXIA_GOLD, q->phase != DATAU_PHASE_IN_PROGRESS, 15.5f) && pa) {
            OWQuestNpc_TryInteract(pa->position);
        }
    }
}
