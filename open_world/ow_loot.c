// open_world/ow_loot.c
#include "open_world/ow_loot.h"
#include "character/player_profile.h"
#include "game/quest_datau.h"
#include "maps/map_manager.h"
#include "skills/skill_manager.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOOT_ORBS 160

typedef struct {
    bool       active;
    bool       available;
    OWNodeKind kind;
    Vector3    pos;
    int        hp;
    int        maxHp;
    float      respawnTimer;
    float      pulsePhase;
} OWNode;

typedef struct {
    bool       active;
    OWLootKind kind;
    int        amount;
    Vector3    pos;
    Vector3    vel;
    float      age;
} OWLootOrb;

static OWNode    s_nodes[OW_MAX_NODE_SPAWNS];
static int       s_nodeCount = 0;
static OWLootOrb s_orbs[MAX_LOOT_ORBS];
static int       s_realmElement = 3;
static float     s_herbAutoTimer = 0.0f;
static float     s_xpPopupTimer = 0.0f;
static int       s_xpPopupAccum = 0;
static int       s_goldPopupAccum = 0;

static bool      s_orbInstInit = false;
static Mesh      s_orbMesh = { 0 };
static Shader    s_orbShader = { 0 };
static Material  s_orbMat = { 0 };
static int       s_orbLocTint = -1;

static void EnsureOrbInstancingInit(void) {
    if (s_orbInstInit) return;
    static const char *ORB_VS =
        "#version 330\n"
        "in vec3 vertexPosition;\n"
        "in vec3 vertexNormal;\n"
        "in mat4 instanceTransform;\n"
        "uniform mat4 mvp;\n"
        "out vec3 fragNormal;\n"
        "void main() {\n"
        "    fragNormal = normalize(mat3(instanceTransform) * vertexNormal);\n"
        "    gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);\n"
        "}\n";
    static const char *ORB_FS =
        "#version 330\n"
        "in vec3 fragNormal;\n"
        "uniform vec4 u_tint;\n"
        "out vec4 finalColor;\n"
        "void main() {\n"
        "    float d = dot(normalize(fragNormal), normalize(vec3(0.4, 0.8, 0.3))) * 0.4 + 0.6;\n"
        "    finalColor = vec4(u_tint.rgb * d + u_tint.rgb * 0.25, 1.0);\n"
        "}\n";
    s_orbShader = LoadShaderFromMemory(ORB_VS, ORB_FS);
    s_orbShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(s_orbShader, "instanceTransform");
    s_orbLocTint = GetShaderLocation(s_orbShader, "u_tint");
    s_orbMat = LoadMaterialDefault();
    s_orbMat.shader = s_orbShader;
    s_orbMesh = GenMeshCube(0.26f, 0.34f, 0.26f);
    s_orbInstInit = true;
}

static float DistXZ(Vector3 a, Vector3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

static void CompleteNodeHarvest(OWNode *n) {
    n->available = false;
    n->respawnTimer = (n->kind == OW_NODE_GOLDEN_RELIC_CHEST) ? 90.0f :
                      (n->kind == OW_NODE_BURIED_CHEST) ? 45.0f :
                      (n->kind == OW_NODE_ORE_VEIN) ? 35.0f : 25.0f;

    if (n->kind == OW_NODE_HERB) {
        OWLoot_SpawnOrb(n->pos, OW_LOOT_HERB, 1);
        OWLoot_SpawnOrb(n->pos, OW_LOOT_GOLD, 15);
    } else if (n->kind == OW_NODE_ORE_VEIN) {
        OWLoot_SpawnOrb(n->pos, OW_LOOT_STONE, 1);
        OWLoot_SpawnOrb(n->pos, OW_LOOT_GOLD, 35);
    } else if (n->kind == OW_NODE_BURIED_CHEST) {
        OWLoot_SpawnOrb(n->pos, OW_LOOT_GOLD, 95);
        OWLoot_SpawnOrb(n->pos, OW_LOOT_STONE, 1);
        OWLoot_SpawnOrb(n->pos, OW_LOOT_POTION_HEAL, 1);
    } else if (n->kind == OW_NODE_GOLDEN_RELIC_CHEST) {
        OWLoot_SpawnOrb(n->pos, OW_LOOT_GOLD, 240);
        OWLoot_SpawnOrb(n->pos, OW_LOOT_STONE, 2);
        OWLoot_SpawnOrb(n->pos, OW_LOOT_DATAU_TOKEN, 1);
        OWLoot_SpawnOrb(n->pos, OW_LOOT_POTION_HEAL, 1);
    }
    DaTau_OnForageCollected(1);
    PlayerProfile_SaveGlobal();
}

void OWLoot_Init(const OWRealmDef *realm) {
    memset(s_nodes, 0, sizeof(s_nodes));
    memset(s_orbs, 0, sizeof(s_orbs));
    s_nodeCount = 0;
    s_herbAutoTimer = 0.0f;
    s_realmElement = realm ? realm->realmId : 3;
    if (!realm) return;

    for (int i = 0; i < realm->nodeCount && i < OW_MAX_NODE_SPAWNS; i++) {
        const OWNodeSpawn *sp = &realm->nodes[i];
        Vector3 p = { sp->x, MapManager_GetGroundHeightAt(sp->x, sp->z), sp->z };
        int hp = (sp->kind == OW_NODE_ORE_VEIN) ? 3 : 1;
        int idx = s_nodeCount++;
        s_nodes[idx] = (OWNode){
            .active = true,
            .available = sp->availableAtStart,
            .kind = sp->kind,
            .pos = p,
            .hp = hp,
            .maxHp = hp,
            .respawnTimer = 0.0f,
            .pulsePhase = (float)(idx + 1) * 0.7f
        };
    }
}

void OWLoot_SpawnOrb(Vector3 pos, OWLootKind kind, int amount) {
    for (int i = 0; i < MAX_LOOT_ORBS; i++) {
        if (!s_orbs[i].active) {
            float ang = ((float)(rand() % 360)) * DEG2RAD;
            float spd = 1.8f + ((float)(rand() % 100)) * 0.015f;
            s_orbs[i] = (OWLootOrb){
                .active = true,
                .kind = kind,
                .amount = (amount > 0) ? amount : 1,
                .pos = { pos.x, pos.y + 0.5f, pos.z },
                .vel = { cosf(ang) * spd, 4.2f, sinf(ang) * spd },
                .age = 0.0f
            };
            return;
        }
    }
}

bool OWLoot_TryHarvestNearest(Vector3 playerPos) {
    for (int i = 0; i < s_nodeCount; i++) {
        OWNode *n = &s_nodes[i];
        if (!n->active || !n->available) continue;
        if (DistXZ(playerPos, n->pos) <= 2.8f) {
            n->hp--;
            if (n->hp <= 0) {
                CompleteNodeHarvest(n);
            } else {
                AddFloatingText(n->pos, TextFormat("Khai khoang (%d/%d)", n->maxHp - n->hp, n->maxHp),
                                (Color){ 245, 200, 110, 255 }, 14.0f, 0.9f);
            }
            return true;
        }
    }
    return false;
}

int OWLoot_GetActiveNodeCount(void) {
    int c = 0;
    for (int i = 0; i < s_nodeCount; i++) {
        if (s_nodes[i].active && s_nodes[i].available) c++;
    }
    return c;
}

void OWLoot_TriggerSandstormReveal(void) {
    int revealed = 0;
    for (int i = 0; i < s_nodeCount; i++) {
        if (s_nodes[i].kind == OW_NODE_GOLDEN_RELIC_CHEST && !s_nodes[i].available) {
            s_nodes[i].available = true;
            s_nodes[i].hp = 1;
            s_nodes[i].respawnTimer = 0.0f;
            revealed++;
        }
    }
    if (revealed > 0) {
        OW_SetBanner("BAO CAT CO MO NOI LEN! LO DIEN RUONG HOANG KIM!", 4.5f);
    }
}

bool OWLoot_GetNearestAvailableNodePos(Vector3 fromPos, Vector3 *outPos) {
    float bestD = 1e9f;
    bool found = false;
    for (int i = 0; i < s_nodeCount; i++) {
        if (!s_nodes[i].active || !s_nodes[i].available) continue;
        float d = DistXZ(fromPos, s_nodes[i].pos);
        if (d < bestD) { bestD = d; if (outPos) *outPos = s_nodes[i].pos; found = true; }
    }
    return found;
}

const char *OWLoot_GetNearbyPrompt(Vector3 playerPos) {
    for (int i = 0; i < s_nodeCount; i++) {
        if (s_nodes[i].active && s_nodes[i].available && DistXZ(playerPos, s_nodes[i].pos) <= 2.8f) {
            if (s_nodes[i].kind == OW_NODE_ORE_VEIN) return "[F] KHAI THAC KHOANG THACH";
            if (s_nodes[i].kind == OW_NODE_HERB)     return "[F] HAI LINH THAO";
            return "[F] MO RUONG KHO BAU!";
        }
    }
    return NULL;
}

void OWLoot_Update(const Agent *pa, float dt) {
    for (int i = 0; i < s_nodeCount; i++) {
        OWNode *n = &s_nodes[i];
        if (!n->active) continue;
        n->pulsePhase += dt * 3.0f;
        if (!n->available && n->respawnTimer > 0.0f) {
            n->respawnTimer -= dt;
            if (n->respawnTimer <= 0.0f) {
                n->available = true;
                n->hp = n->maxHp;
            }
        }
    }
    if (!pa) return;

    s_herbAutoTimer += dt;
    if (s_herbAutoTimer >= 0.45f) {
        s_herbAutoTimer = 0.0f;
        for (int i = 0; i < s_nodeCount; i++) {
            OWNode *n = &s_nodes[i];
            if (n->active && n->available && n->kind == OW_NODE_HERB &&
                DistXZ(pa->position, n->pos) <= 1.6f) {
                CompleteNodeHarvest(n);
                break;
            }
        }
    }

    PlayerProfile *prof = PlayerProfile_GetGlobal();
    s_xpPopupTimer += dt;
    if (s_xpPopupTimer >= 0.30f) {
        if (s_xpPopupAccum > 0) {
            AddFloatingText(pa->position, TextFormat("+%d XP", s_xpPopupAccum), (Color){ 130, 235, 255, 255 }, 14.0f, 1.1f);
            s_xpPopupAccum = 0;
        }
        if (s_goldPopupAccum > 0) {
            AddFloatingText((Vector3){ pa->position.x, pa->position.y + 0.5f, pa->position.z },
                            TextFormat("+%d Vang", s_goldPopupAccum), GOLD, 14.0f, 1.1f);
            s_goldPopupAccum = 0;
        }
        s_xpPopupTimer = 0.0f;
    }

    float magnetRange = (OW_GetKillStreak() >= 40) ? 9.5f : 6.5f;
    for (int i = 0; i < MAX_LOOT_ORBS; i++) {
        OWLootOrb *o = &s_orbs[i];
        if (!o->active) continue;
        o->age += dt;
        if (o->age > 35.0f) { o->active = false; continue; }
        float dist = DistXZ(o->pos, pa->position);
        if (dist <= magnetRange && o->age > 0.20f) {
            Vector3 dir = { pa->position.x - o->pos.x, (pa->position.y + 0.8f) - o->pos.y, pa->position.z - o->pos.z };
            float dlen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (dlen < 0.85f) {
                o->active = false;
                if (o->kind == OW_LOOT_GOLD) {
                    prof->gold += o->amount;
                    s_goldPopupAccum += o->amount;
                } else if (o->kind == OW_LOOT_SKILL_XP) {
                    if (pa->lastCastSkillIndex >= 0) {
                        PlayerProfile_AddSkillXp(pa->lastCastSkillIndex, (uint32_t)o->amount);
                    }
                    s_xpPopupAccum += o->amount;
                } else if (o->kind == OW_LOOT_STONE) {
                    prof->elementalStones[s_realmElement % 5] += o->amount;
                    AddFloatingText(pa->position, TextFormat("+%d Khoang Thach", o->amount), ORANGE, 15.0f, 1.4f);
                } else if (o->kind == OW_LOOT_HERB) {
                    prof->herbs += o->amount;
                    AddFloatingText(pa->position, TextFormat("+%d Linh Thao", o->amount), GREEN, 15.0f, 1.4f);
                } else if (o->kind == OW_LOOT_POTION_HEAL) {
                    PlayerProfile_AddPotion(POTION_HEAL_HP, o->amount);
                    AddFloatingText(pa->position, "+1 Hoi Sinh Dan", (Color){ 255, 130, 130, 255 }, 15.0f, 1.5f);
                } else if (o->kind == OW_LOOT_DATAU_TOKEN) {
                    prof->daTauTokens += o->amount;
                    AddFloatingText(pa->position, "+1 Lenh Bai Da Tau", SKYBLUE, 15.0f, 1.5f);
                }
                continue;
            }
            float pull = 16.5f * dt;
            o->pos.x += (dir.x / dlen) * pull;
            o->pos.y += (dir.y / dlen) * pull;
            o->pos.z += (dir.z / dlen) * pull;
        } else {
            o->vel.y -= 14.0f * dt;
            o->pos.x += o->vel.x * dt;
            o->pos.y += o->vel.y * dt;
            o->pos.z += o->vel.z * dt;
            float gy = MapManager_GetGroundHeightAt(o->pos.x, o->pos.z) + 0.35f;
            if (o->pos.y < gy) {
                o->pos.y = gy;
                o->vel.x *= 0.6f;
                o->vel.z *= 0.6f;
                o->vel.y = 0.0f;
            }
        }
    }
}

void OWLoot_Draw3D(Camera3D cam) {
    const float maxDrawDistSq = 95.0f * 95.0f;
    for (int i = 0; i < s_nodeCount; i++) {
        const OWNode *n = &s_nodes[i];
        if (!n->active || !n->available) continue;
        float dx = n->pos.x - cam.position.x;
        float dz = n->pos.z - cam.position.z;
        if (dx * dx + dz * dz > maxDrawDistSq) continue;

        float bob = sinf(n->pulsePhase) * 0.08f;
        if (n->kind == OW_NODE_HERB) {
            // Jade Lotus / Desert Herb cluster
            DrawCylinder(n->pos, 0.50f, 0.56f, 0.03f, 12, (Color){ 80, 215, 130, 72 });
            for (int p = 0; p < 4; p++) {
                float ang = (float)p * (PI * 0.5f) + 0.35f;
                Vector3 leafPos = { n->pos.x + cosf(ang) * 0.22f, n->pos.y + 0.12f, n->pos.z + sinf(ang) * 0.22f };
                DrawCylinderEx(n->pos, leafPos, 0.14f, 0.03f, 6, (Color){ 48, 152, 78, 255 });
            }
            DrawSphere((Vector3){ n->pos.x, n->pos.y + 0.34f + bob, n->pos.z }, 0.20f, (Color){ 95, 240, 150, 240 });
            DrawSphere((Vector3){ n->pos.x, n->pos.y + 0.34f + bob, n->pos.z }, 0.28f, (Color){ 135, 255, 185, 75 });
        } else if (n->kind == OW_NODE_ORE_VEIN) {
            // Faceted crystal ore cluster emerging from dark stone base
            DrawCylinder(n->pos, 0.52f, 0.38f, 0.24f, 7, (Color){ 86, 74, 64, 255 });
            DrawCylinderEx((Vector3){ n->pos.x - 0.10f, n->pos.y + 0.12f, n->pos.z + 0.08f },
                           (Vector3){ n->pos.x - 0.24f, n->pos.y + 0.96f, n->pos.z + 0.18f },
                           0.16f, 0.02f, 6, (Color){ 214, 150, 58, 255 });
            DrawCylinderEx((Vector3){ n->pos.x + 0.06f, n->pos.y + 0.14f, n->pos.z - 0.04f },
                           (Vector3){ n->pos.x + 0.18f, n->pos.y + 1.18f + bob * 0.4f, n->pos.z - 0.12f },
                           0.19f, 0.02f, 6, (Color){ 238, 182, 78, 255 });
            DrawCylinderEx((Vector3){ n->pos.x + 0.18f, n->pos.y + 0.10f, n->pos.z + 0.16f },
                           (Vector3){ n->pos.x + 0.34f, n->pos.y + 0.72f, n->pos.z + 0.28f },
                           0.13f, 0.02f, 6, (Color){ 188, 124, 48, 255 });
        } else {
            // Lacquered wooden/gilded reliquary chest
            bool golden = (n->kind == OW_NODE_GOLDEN_RELIC_CHEST);
            Color bodyCol = golden ? (Color){ 176, 128, 42, 255 } : (Color){ 96, 56, 32, 255 };
            Color trimCol = golden ? GOLD : (Color){ 198, 162, 86, 255 };
            DrawCube((Vector3){ n->pos.x, n->pos.y + 0.24f, n->pos.z }, 0.92f, 0.48f, 0.64f, bodyCol);
            DrawCube((Vector3){ n->pos.x, n->pos.y + 0.54f, n->pos.z }, 0.96f, 0.18f, 0.68f, trimCol);
            DrawCube((Vector3){ n->pos.x - 0.34f, n->pos.y + 0.34f, n->pos.z }, 0.10f, 0.62f, 0.68f, trimCol);
            DrawCube((Vector3){ n->pos.x + 0.34f, n->pos.y + 0.34f, n->pos.z }, 0.10f, 0.62f, 0.68f, trimCol);
            DrawSphere((Vector3){ n->pos.x, n->pos.y + 0.48f, n->pos.z + 0.35f }, 0.09f, golden ? (Color){ 110, 245, 200, 255 } : GOLD);
            if (golden) {
                DrawCylinder((Vector3){ n->pos.x, n->pos.y + 0.2f, n->pos.z }, 0.36f, 0.08f, 7.5f, 12, (Color){ 255, 225, 110, 75 });
            }
        }
    }

    EnsureOrbInstancingInit();
    static Matrix xf[6][MAX_LOOT_ORBS];
    int cnt[6] = { 0 };
    float tNow = (float)GetTime();
    for (int i = 0; i < MAX_LOOT_ORBS; i++) {
        const OWLootOrb *o = &s_orbs[i];
        if (!o->active) continue;
        int k = (int)o->kind;
        if (k < 0 || k >= 6) k = 0;
        if (cnt[k] < MAX_LOOT_ORBS) {
            Matrix R = MatrixRotateY(tNow * 2.5f + (float)i * 0.6f);
            Matrix T = MatrixTranslate(o->pos.x, o->pos.y + 0.08f * sinf(tNow * 4.0f + (float)i), o->pos.z);
            xf[k][cnt[k]++] = MatrixMultiply(R, T);
        }
    }
    static const Vector4 kOrbColors[6] = {
        { 1.0f, 0.84f, 0.20f, 1.0f }, // GOLD
        { 1.0f, 0.55f, 0.15f, 1.0f }, // STONE
        { 0.30f, 0.95f, 0.45f, 1.0f }, // HERB
        { 1.0f, 0.25f, 0.25f, 1.0f }, // POTION
        { 0.45f, 0.85f, 1.0f, 1.0f }, // TOKEN
        { 0.35f, 0.92f, 1.0f, 1.0f }  // SKILL_XP
    };
    for (int k = 0; k < 6; k++) {
        if (cnt[k] <= 0) continue;
        BeginShaderMode(s_orbShader);
        SetShaderValue(s_orbShader, s_orbLocTint, &kOrbColors[k], SHADER_UNIFORM_VEC4);
        DrawMeshInstanced(s_orbMesh, s_orbMat, xf[k], cnt[k]);
        EndShaderMode();
    }
}
