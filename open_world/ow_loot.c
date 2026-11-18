// open_world/ow_loot.c
#include "open_world/ow_loot.h"
#include "character/player_profile.h"
#include "game/quest_datau.h"
#include "maps/map_manager.h"
#include "skills/skill_manager.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOOT_ORBS 48

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
    for (int i = 0; i < MAX_LOOT_ORBS; i++) {
        OWLootOrb *o = &s_orbs[i];
        if (!o->active) continue;
        o->age += dt;
        float dist = DistXZ(o->pos, pa->position);
        if (dist <= 3.4f && o->age > 0.25f) {
            Vector3 dir = { pa->position.x - o->pos.x, (pa->position.y + 0.8f) - o->pos.y, pa->position.z - o->pos.z };
            float dlen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (dlen < 0.75f) {
                o->active = false;
                if (o->kind == OW_LOOT_GOLD) {
                    prof->gold += o->amount;
                    AddFloatingText(pa->position, TextFormat("+%d Vang", o->amount), GOLD, 15.0f, 1.2f);
                } else if (o->kind == OW_LOOT_STONE) {
                    prof->elementalStones[s_realmElement % 5] += o->amount;
                    AddFloatingText(pa->position, TextFormat("+%d Hoang Tho Khoang", o->amount), ORANGE, 15.0f, 1.4f);
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
                PlayerProfile_SaveGlobal();
                continue;
            }
            float pull = 12.0f * dt;
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

void OWLoot_Draw3D(void) {
    for (int i = 0; i < s_nodeCount; i++) {
        const OWNode *n = &s_nodes[i];
        if (!n->active || !n->available) continue;
        float bob = sinf(n->pulsePhase) * 0.08f;
        if (n->kind == OW_NODE_HERB) {
            DrawSphere((Vector3){ n->pos.x, n->pos.y + 0.35f + bob, n->pos.z }, 0.30f, (Color){ 70, 230, 120, 235 });
            DrawCylinder(n->pos, 0.48f, 0.52f, 0.04f, 12, (Color){ 90, 240, 140, 90 });
        } else if (n->kind == OW_NODE_ORE_VEIN) {
            DrawCube((Vector3){ n->pos.x, n->pos.y + 0.45f, n->pos.z }, 0.65f, 0.90f, 0.65f, (Color){ 210, 155, 65, 255 });
            DrawCubeWires((Vector3){ n->pos.x, n->pos.y + 0.45f, n->pos.z }, 0.72f, 0.96f, 0.72f, GOLD);
        } else {
            bool golden = (n->kind == OW_NODE_GOLDEN_RELIC_CHEST);
            Color c = golden ? GOLD : (Color){ 185, 125, 55, 255 };
            DrawCube((Vector3){ n->pos.x, n->pos.y + 0.38f, n->pos.z }, 0.95f, 0.72f, 0.68f, c);
            DrawCubeWires((Vector3){ n->pos.x, n->pos.y + 0.38f, n->pos.z }, 0.98f, 0.75f, 0.70f, WHITE);
            if (golden) {
                DrawCylinder((Vector3){ n->pos.x, n->pos.y, n->pos.z }, 0.35f, 0.10f, 8.0f, 12, (Color){ 255, 225, 110, 95 });
            }
        }
    }

    for (int i = 0; i < MAX_LOOT_ORBS; i++) {
        const OWLootOrb *o = &s_orbs[i];
        if (!o->active) continue;
        Color c = GOLD;
        if (o->kind == OW_LOOT_STONE) c = ORANGE;
        else if (o->kind == OW_LOOT_HERB) c = GREEN;
        else if (o->kind == OW_LOOT_POTION_HEAL) c = RED;
        else if (o->kind == OW_LOOT_DATAU_TOKEN) c = SKYBLUE;
        DrawSphere(o->pos, 0.22f, c);
    }
}
