// open_world/ow_quest_npc.c
#include "open_world/ow_quest_npc.h"
#include "open_world/ow_loot.h"
#include "entities/entities.h"
#include "game/quest_datau.h"
#include "maps/map_manager.h"
#include "skills/skill_manager.h"
#include "ui/ui_hub.h"
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
        DrawCube((Vector3){ sp.x, sp.y + 1.0f, sp.z }, 0.85f, 2.0f, 0.55f, (Color){ 120, 112, 102, 255 });
        DrawCubeWires((Vector3){ sp.x, sp.y + 1.0f, sp.z }, 0.88f, 2.04f, 0.58f, ORANGE);
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

    Vector3 dp = OW_GetDaTauNpcPos();
    Vector2 sp = GetWorldToScreen((Vector3){ dp.x, dp.y + 2.3f, dp.z }, cam);
    const char *tag = (q && q->phase == DATAU_PHASE_READY_RETURN) ? "[?] DA TAU (TRA NV)" :
                      (q && q->phase == DATAU_PHASE_WAIT_ACCEPT)  ? "[!] DA TAU (NHAN NV)" : "DA TAU";
    Color tc = (q && q->phase != DATAU_PHASE_IN_PROGRESS) ? GOLD : LIGHTGRAY;
    int tw = MeasureText(tag, 13);
    DrawRectangle((int)sp.x - tw / 2 - 6, (int)sp.y - 10, tw + 12, 20, (Color){ 16, 20, 28, 215 });
    DrawText(tag, (int)sp.x - tw / 2, (int)sp.y - 7, 13, tc);

    Vector3 ap = s_alchemistPos;
    ap.y = MapManager_GetGroundHeightAt(ap.x, ap.z) + 2.3f;
    Vector2 asp = GetWorldToScreen(ap, cam);
    const char *atag = "LUYEN DAN SU [F]";
    int atw = MeasureText(atag, 12);
    DrawRectangle((int)asp.x - atw / 2 - 6, (int)asp.y - 10, atw + 12, 18, (Color){ 16, 26, 22, 210 });
    DrawText(atag, (int)asp.x - atw / 2, (int)asp.y - 7, 12, (Color){ 140, 240, 175, 255 });

    if (s_npcDialogOpen && q) {
        Rectangle db = { (float)sw / 2 - 220.0f, 145.0f, 440.0f, 180.0f };
        DrawRectangleRec(db, (Color){ 16, 20, 28, 245 });
        DrawRectangleLinesEx(db, 2.0f, GOLD);
        DrawText("SU GIA DA TAU — NHAN/TRA NHIEM VU", (int)db.x + 18, (int)db.y + 14, 15, GOLD);
        DrawText(q->title, (int)db.x + 18, (int)db.y + 46, 15, WHITE);
        DrawText(q->desc, (int)db.x + 18, (int)db.y + 70, 12, LIGHTGRAY);
        DrawText(TextFormat("Phan thuong: %d Vang  |  %d Diem  |  +1 Khoang  |  +1 Thao",
                            q->rewardGold, q->rewardPoints),
                 (int)db.x + 18, (int)db.y + 96, 12, (Color){ 145, 240, 175, 255 });

        const char *btnTxt = (q->phase == DATAU_PHASE_WAIT_ACCEPT)  ? "[F] NHAN NHIEM VU NAY!" :
                             (q->phase == DATAU_PHASE_READY_RETURN) ? "[F] TRA NHIEM VU & NHAN THUONG!" :
                                                                      "DANG THUC HIEN (THEO GIO) [F]";
        Rectangle btn = { db.x + 18, db.y + 128, db.width - 36, 36 };
        DrawRectangleRec(btn, (Color){ 42, 38, 24, 255 });
        DrawRectangleLinesEx(btn, 1.5f, GOLD);
        int bw = MeasureText(btnTxt, 14);
        DrawText(btnTxt, (int)(btn.x + btn.width * 0.5f - bw * 0.5f), (int)btn.y + 11, 14, GOLD);
    }
}
