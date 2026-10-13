#include "character/character_model.h"
#include "core/resource_manager.h"
#include "core/surface_material.h"
#include "core/mesh_adjacency.h"
#include "raymath.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define ANIM_FPS 60.0f

static Model s_model;
static ModelAnimation *s_animations = NULL;
static int s_animCount = 0;
static bool s_loaded = false;

typedef struct {
    int indices[CHAR_ANIM_MAX_VARIANTS];
    int count;
} SlotVariants;

static SlotVariants s_slots[CHAR_ANIM_COUNT];

static const char *s_slotNames[CHAR_ANIM_COUNT] = {
    [CHAR_ANIM_IDLE]        = "IDLE",
    [CHAR_ANIM_WALK]        = "WALK",
    [CHAR_ANIM_RUN]         = "RUN",
    [CHAR_ANIM_JUMP]        = "JUMP",
    [CHAR_ANIM_JUMP_DOWN]   = "JUMP_DOWN",
    [CHAR_ANIM_DODGE]       = "DODGE",
    [CHAR_ANIM_PUNCH]       = "PUNCH",
    [CHAR_ANIM_KICK]        = "KICK",
    [CHAR_ANIM_PALM]        = "PALM",
    [CHAR_ANIM_SWORD_IDLE]  = "SWORD_IDLE",
    [CHAR_ANIM_SWORD_SLASH] = "SWORD_SLASH",
    [CHAR_ANIM_SWORD_THRUST]= "SWORD_THRUST",
    [CHAR_ANIM_SWORD_PARRY] = "SWORD_PARRY",
    [CHAR_ANIM_SWORD_BLOCK] = "SWORD_BLOCK",
    [CHAR_ANIM_CAST]        = "CAST",
    [CHAR_ANIM_HIT]         = "HIT",
    [CHAR_ANIM_KNOCKDOWN]   = "KNOCKDOWN",
};

static const char *s_keywords[CHAR_ANIM_COUNT][6] = {
    [CHAR_ANIM_IDLE]        = { "idle", "stand", NULL },
    [CHAR_ANIM_WALK]        = { "walk", "jog", NULL },
    [CHAR_ANIM_RUN]         = { "run", "sprint", NULL },
    [CHAR_ANIM_JUMP]        = { "jump", "leap", NULL },
    [CHAR_ANIM_JUMP_DOWN]   = { "jumping_down", "jump_down", "jumpdown", "land", NULL },
    [CHAR_ANIM_DODGE]       = { "dodge", "evade", "roll", "dash", NULL },
    [CHAR_ANIM_PUNCH]       = { "punch", "jab", "cross", "hook", NULL },
    [CHAR_ANIM_KICK]        = { "kick", NULL },
    [CHAR_ANIM_PALM]        = { "palm", "push", "slap", NULL },
    [CHAR_ANIM_SWORD_IDLE]  = { "loco_idle_sword", "sword_idle", "idle_sword", NULL },
    [CHAR_ANIM_SWORD_SLASH] = { "sword_slash", "slash", "cleave", "whirlwind", NULL },
    [CHAR_ANIM_SWORD_THRUST]= { "sword_thrust", "thrust", "lunge", NULL },
    [CHAR_ANIM_SWORD_PARRY] = { "sword_parry", "parry", "deflect", NULL },
    [CHAR_ANIM_SWORD_BLOCK] = { "sword_block", "block", "guard", NULL },
    [CHAR_ANIM_CAST]        = { "cast", "spell", "flourish", NULL },
    [CHAR_ANIM_HIT]         = { "hit", "hurt", "damage", NULL },
    [CHAR_ANIM_KNOCKDOWN]   = { "down", "fall", "knockdown", NULL },
};

static void ToLowerCopy(char *dst, const char *src, int maxLen) {
    int i = 0;
    for (; src[i] != '\0' && i < maxLen - 1; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static bool SlotContainsAnim(CharacterAnimSlot slot, int animIdx) {
    for (int i = 0; i < s_slots[slot].count; i++) {
        if (s_slots[slot].indices[i] == animIdx) return true;
    }
    return false;
}

static bool AddSlotIndex(CharacterAnimSlot slot, int animIdx) {
    if (slot < 0 || slot >= CHAR_ANIM_COUNT) return false;
    if (animIdx < 0 || animIdx >= s_animCount) return false;
    if (SlotContainsAnim(slot, animIdx)) return true;
    if (s_slots[slot].count < CHAR_ANIM_MAX_VARIANTS) {
        s_slots[slot].indices[s_slots[slot].count++] = animIdx;
        return true;
    }
    return false;
}

// Lightweight JSON manifest parser for assets/characters/player_anims.json
static void LoadAnimManifest(const char *manifestPath) {
    if (!FileExists(manifestPath)) return;
    char *text = LoadFileText(manifestPath);
    if (!text) return;

    TraceLog(LOG_INFO, "CHARACTER_MODEL: Loading animation manifest from %s", manifestPath);

    for (int slot = 0; slot < CHAR_ANIM_COUNT; slot++) {
        char slotKey[64];
        ToLowerCopy(slotKey, s_slotNames[slot], sizeof(slotKey));

        char *pos = strstr(text, slotKey);
        if (pos) {
            char *colon = strchr(pos, ':');
            if (colon) {
                char *lineEnd = strchr(colon, '\n');
                int len = lineEnd ? (int)(lineEnd - colon) : (int)strlen(colon);
                char lineBuf[256];
                if (len >= (int)sizeof(lineBuf)) len = sizeof(lineBuf) - 1;
                strncpy(lineBuf, colon, len);
                lineBuf[len] = '\0';

                // Extract quoted clip names
                char *p = lineBuf;
                bool slotCleared = false;
                while ((p = strchr(p, '\"')) != NULL) {
                    p++; // skip opening quote
                    char *closeQ = strchr(p, '\"');
                    if (!closeQ) break;
                    *closeQ = '\0';

                    int idx = CharacterModel_FindAnimIndex(p);
                    if (idx >= 0) {
                        if (!slotCleared) {
                            CharacterModel_ClearSlotVariants((CharacterAnimSlot)slot);
                            slotCleared = true;
                        }
                        CharacterModel_AddSlotVariant((CharacterAnimSlot)slot, idx);
                        TraceLog(LOG_INFO, "CHARACTER_MODEL: Manifest mapped slot %s variant %d -> \"%s\" (clip %d)",
                                 s_slotNames[slot], s_slots[slot].count - 1, s_animations[idx].name, idx);
                    }
                    p = closeQ + 1;
                }
            }
        }
    }
    UnloadFileText(text);
}

bool CharacterModel_Load(const char *filePath) {
    s_loaded = false;
    for (int i = 0; i < CHAR_ANIM_COUNT; i++) {
        s_slots[i].count = 0;
        for (int v = 0; v < CHAR_ANIM_MAX_VARIANTS; v++) {
            s_slots[i].indices[v] = -1;
        }
    }

    s_model = ResourceManager_LoadModel(filePath);
    if (s_model.meshCount == 0) {
        return false;
    }

    SurfaceMaterial_Apply(&s_model);

    s_animations = ResourceManager_LoadModelAnimations(filePath, &s_animCount);
    if (s_animations == NULL || s_animCount == 0) {
        TraceLog(LOG_WARNING, "CHARACTER_MODEL: %s loaded but has no animations", filePath);
        return false;
    }

    TraceLog(LOG_INFO, "CHARACTER_MODEL: Loaded %s with %d skeletal animation clips:", filePath, s_animCount);
    for (int a = 0; a < s_animCount; a++) {
        TraceLog(LOG_INFO, "  [%2d] \"%s\" (%d frames, %.2fs)",
                 a, s_animations[a].name, s_animations[a].keyframeCount,
                 (float)s_animations[a].keyframeCount / ANIM_FPS);
    }

    // Auto-match slots based on keywords
    for (int slot = 0; slot < CHAR_ANIM_COUNT; slot++) {
        for (int a = 0; a < s_animCount; a++) {
            char lowerName[64];
            ToLowerCopy(lowerName, s_animations[a].name, sizeof(lowerName));

            for (int k = 0; k < 6 && s_keywords[slot][k] != NULL; k++) {
                if (strstr(lowerName, s_keywords[slot][k]) != NULL) {
                    AddSlotIndex((CharacterAnimSlot)slot, a);
                    break;
                }
            }
        }
    }

    // Fallbacks for unassigned slots
    if (s_slots[CHAR_ANIM_RUN].count == 0 && s_slots[CHAR_ANIM_WALK].count > 0) {
        AddSlotIndex(CHAR_ANIM_RUN, s_slots[CHAR_ANIM_WALK].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_IDLE].count == 0 && s_slots[CHAR_ANIM_IDLE].count > 0) {
        AddSlotIndex(CHAR_ANIM_SWORD_IDLE, s_slots[CHAR_ANIM_IDLE].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_SLASH].count == 0 && s_slots[CHAR_ANIM_PUNCH].count > 0) {
        AddSlotIndex(CHAR_ANIM_SWORD_SLASH, s_slots[CHAR_ANIM_PUNCH].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_THRUST].count == 0 && s_slots[CHAR_ANIM_SWORD_SLASH].count > 0) {
        AddSlotIndex(CHAR_ANIM_SWORD_THRUST, s_slots[CHAR_ANIM_SWORD_SLASH].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_BLOCK].count == 0 && s_slots[CHAR_ANIM_PALM].count > 0) {
        AddSlotIndex(CHAR_ANIM_SWORD_BLOCK, s_slots[CHAR_ANIM_PALM].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_PARRY].count == 0) {
        if (s_slots[CHAR_ANIM_SWORD_BLOCK].count > 0)
            AddSlotIndex(CHAR_ANIM_SWORD_PARRY, s_slots[CHAR_ANIM_SWORD_BLOCK].indices[0]);
        else if (s_slots[CHAR_ANIM_PALM].count > 0)
            AddSlotIndex(CHAR_ANIM_SWORD_PARRY, s_slots[CHAR_ANIM_PALM].indices[0]);
    }
    if (s_slots[CHAR_ANIM_JUMP_DOWN].count == 0 && s_slots[CHAR_ANIM_JUMP].count > 0) {
        AddSlotIndex(CHAR_ANIM_JUMP_DOWN, s_slots[CHAR_ANIM_JUMP].indices[0]);
    }
    if (s_slots[CHAR_ANIM_DODGE].count == 0) {
        if (s_slots[CHAR_ANIM_RUN].count > 0)
            AddSlotIndex(CHAR_ANIM_DODGE, s_slots[CHAR_ANIM_RUN].indices[0]);
        else if (s_slots[CHAR_ANIM_WALK].count > 0)
            AddSlotIndex(CHAR_ANIM_DODGE, s_slots[CHAR_ANIM_WALK].indices[0]);
    }
    if (s_slots[CHAR_ANIM_HIT].count == 0 && s_slots[CHAR_ANIM_IDLE].count > 0) {
        AddSlotIndex(CHAR_ANIM_HIT, s_slots[CHAR_ANIM_IDLE].indices[0]);
    }
    if (s_slots[CHAR_ANIM_KNOCKDOWN].count == 0 && s_slots[CHAR_ANIM_HIT].count > 0) {
        AddSlotIndex(CHAR_ANIM_KNOCKDOWN, s_slots[CHAR_ANIM_HIT].indices[0]);
    }

    // Optional JSON manifest override
    LoadAnimManifest("assets/characters/player_anims.json");

    CharacterModel_PrintInfo();

    s_loaded = true;
    return true;
}

bool CharacterModel_IsLoaded(void) {
    return s_loaded;
}

void CharacterModel_ResetState(CharacterAnimState *state) {
    if (!state) return;
    state->frame = 0;
    state->frameTimer = 0.0f;
    state->currentSlot = CHAR_ANIM_IDLE;
    state->activeVariant = 0;
    state->activeAnimIndex = (s_slots[CHAR_ANIM_IDLE].count > 0) ? s_slots[CHAR_ANIM_IDLE].indices[0] : -1;
    state->oneShotActive = false;
    state->oneShotSpeed = 1.0f;
}

void CharacterModel_Update(CharacterAnimState *state, float dt, bool isMoving) {
    if (!state || !s_loaded) return;

    if (!state->oneShotActive) {
        CharacterAnimSlot desired = isMoving ? CHAR_ANIM_WALK : CHAR_ANIM_IDLE;
        if (desired != state->currentSlot) {
            state->currentSlot = desired;
            state->activeVariant = 0;
            state->activeAnimIndex = (s_slots[desired].count > 0) ? s_slots[desired].indices[0] : -1;
            state->frame = 0;
            state->frameTimer = 0.0f;
        }
    }

    int animIdx = state->activeAnimIndex;
    if (animIdx < 0 || animIdx >= s_animCount) return;

    int frameCount = s_animations[animIdx].keyframeCount;
    float speed = state->oneShotActive ? state->oneShotSpeed : 1.0f;

    state->frameTimer += dt * ANIM_FPS * speed;
    while (state->frameTimer >= 1.0f) {
        state->frameTimer -= 1.0f;
        state->frame++;
        if (state->frame >= frameCount) {
            if (state->oneShotActive) {
                state->oneShotActive = false;
                state->currentSlot = isMoving ? CHAR_ANIM_WALK : CHAR_ANIM_IDLE;
                state->activeVariant = 0;
                state->activeAnimIndex = (s_slots[state->currentSlot].count > 0) ? s_slots[state->currentSlot].indices[0] : -1;
                state->frame = 0;
            } else {
                state->frame = 0; // loop
            }
        }
    }
}

void CharacterModel_Trigger(CharacterAnimState *state, CharacterAnimSlot slot) {
    CharacterModel_TriggerVariantTimed(state, slot, 0, 0.0f);
}

void CharacterModel_TriggerTimed(CharacterAnimState *state, CharacterAnimSlot slot, float seconds) {
    CharacterModel_TriggerVariantTimed(state, slot, 0, seconds);
}

void CharacterModel_TriggerVariant(CharacterAnimState *state, CharacterAnimSlot slot, int variantIndex) {
    CharacterModel_TriggerVariantTimed(state, slot, variantIndex, 0.0f);
}

void CharacterModel_TriggerVariantTimed(CharacterAnimState *state, CharacterAnimSlot slot, int variantIndex, float seconds) {
    if (!state || !s_loaded) return;
    if (slot < 0 || slot >= CHAR_ANIM_COUNT) return;
    if (s_slots[slot].count <= 0) return;

    int vIdx = variantIndex;
    if (vIdx < 0) vIdx = 0;
    if (vIdx >= s_slots[slot].count) vIdx = s_slots[slot].count - 1;

    int animIdx = s_slots[slot].indices[vIdx];
    if (animIdx < 0 || animIdx >= s_animCount) return;

    float speed = 1.0f;
    if (seconds > 0.0f) {
        float naturalSeconds = (float)s_animations[animIdx].keyframeCount / ANIM_FPS;
        speed = naturalSeconds / seconds;
        if (speed < 0.25f) speed = 0.25f;
        if (speed > 8.0f)  speed = 8.0f;
    }

    state->currentSlot = slot;
    state->activeVariant = vIdx;
    state->activeAnimIndex = animIdx;
    state->frame = 0;
    state->frameTimer = 0.0f;
    state->oneShotActive = true;
    state->oneShotSpeed = speed;
}

void CharacterModel_TriggerAttack(CharacterAnimState *state, CharacterAnimSlot slot) {
    CharacterModel_TriggerTimed(state, slot, 0.0f);
}

void CharacterModel_TriggerAttackTimed(CharacterAnimState *state, CharacterAnimSlot slot, float seconds) {
    CharacterModel_TriggerTimed(state, slot, seconds);
}

bool CharacterModel_TriggerByName(CharacterAnimState *state, const char *clipName, float seconds) {
    if (!state || !s_loaded || !clipName) return false;

    int animIdx = CharacterModel_FindAnimIndex(clipName);
    if (animIdx < 0) return false;

    float speed = 1.0f;
    if (seconds > 0.0f) {
        float naturalSeconds = (float)s_animations[animIdx].keyframeCount / ANIM_FPS;
        speed = naturalSeconds / seconds;
        if (speed < 0.25f) speed = 0.25f;
        if (speed > 8.0f)  speed = 8.0f;
    }

    state->activeAnimIndex = animIdx;
    state->frame = 0;
    state->frameTimer = 0.0f;
    state->oneShotActive = true;
    state->oneShotSpeed = speed;
    return true;
}

int CharacterModel_GetVariantCount(CharacterAnimSlot slot) {
    if (slot < 0 || slot >= CHAR_ANIM_COUNT) return 0;
    return s_slots[slot].count;
}

int CharacterModel_GetSlotAnimIndex(CharacterAnimSlot slot, int variantIndex) {
    if (slot < 0 || slot >= CHAR_ANIM_COUNT) return -1;
    if (variantIndex < 0 || variantIndex >= s_slots[slot].count) return -1;
    return s_slots[slot].indices[variantIndex];
}

bool CharacterModel_SetSlotVariant(CharacterAnimSlot slot, int variantIndex, int animIndex) {
    if (slot < 0 || slot >= CHAR_ANIM_COUNT) return false;
    if (animIndex < 0 || animIndex >= s_animCount) return false;
    if (variantIndex < 0 || variantIndex >= CHAR_ANIM_MAX_VARIANTS) return false;

    s_slots[slot].indices[variantIndex] = animIndex;
    if (variantIndex >= s_slots[slot].count) {
        s_slots[slot].count = variantIndex + 1;
    }
    return true;
}

bool CharacterModel_AddSlotVariant(CharacterAnimSlot slot, int animIndex) {
    return AddSlotIndex(slot, animIndex);
}

void CharacterModel_ClearSlotVariants(CharacterAnimSlot slot) {
    if (slot < 0 || slot >= CHAR_ANIM_COUNT) return;
    s_slots[slot].count = 0;
    for (int i = 0; i < CHAR_ANIM_MAX_VARIANTS; i++) {
        s_slots[slot].indices[i] = -1;
    }
}

int CharacterModel_GetTotalAnimCount(void) {
    return s_animCount;
}

int CharacterModel_FindAnimIndex(const char *name) {
    if (!name || s_animCount == 0) return -1;
    char lowerTarget[64];
    ToLowerCopy(lowerTarget, name, sizeof(lowerTarget));

    // 1. Exact match
    for (int i = 0; i < s_animCount; i++) {
        char lowerClip[64];
        ToLowerCopy(lowerClip, s_animations[i].name, sizeof(lowerClip));
        if (strcmp(lowerClip, lowerTarget) == 0) return i;
    }
    // 2. Substring match
    for (int i = 0; i < s_animCount; i++) {
        char lowerClip[64];
        ToLowerCopy(lowerClip, s_animations[i].name, sizeof(lowerClip));
        if (strstr(lowerClip, lowerTarget) != NULL) return i;
    }
    return -1;
}

const char* CharacterModel_GetAnimName(int animIndex) {
    if (animIndex < 0 || animIndex >= s_animCount) return NULL;
    return s_animations[animIndex].name;
}

float CharacterModel_GetAnimDuration(int animIndex) {
    if (animIndex < 0 || animIndex >= s_animCount) return 0.0f;
    return (float)s_animations[animIndex].keyframeCount / ANIM_FPS;
}

void CharacterModel_PrintInfo(void) {
    TraceLog(LOG_INFO, "==================== CHARACTER ANIMATION MATRIX ====================");
    for (int slot = 0; slot < CHAR_ANIM_COUNT; slot++) {
        int count = s_slots[slot].count;
        if (count == 0) {
            TraceLog(LOG_WARNING, "  [%-18s] (0 variants) <UNASSIGNED>", s_slotNames[slot]);
        } else {
            for (int v = 0; v < count; v++) {
                int a = s_slots[slot].indices[v];
                TraceLog(LOG_INFO, "  [%-18s] v%d -> clip #%2d \"%s\" (%.2fs)",
                         s_slotNames[slot], v, a,
                         (a >= 0 && a < s_animCount) ? s_animations[a].name : "invalid",
                         (a >= 0 && a < s_animCount) ? (float)s_animations[a].keyframeCount / ANIM_FPS : 0.0f);
            }
        }
    }
    TraceLog(LOG_INFO, "====================================================================");
}

void CharacterModel_Draw(const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint) {
    if (!state || !s_loaded) return;

    int animIndex = state->activeAnimIndex;
    if (animIndex >= 0 && animIndex < s_animCount && IsModelAnimationValid(s_model, s_animations[animIndex])) {
        UpdateModelAnimation(s_model, s_animations[animIndex], state->frame);
    }

    float yawDegrees = yawRadians * RAD2DEG;
    DrawModelEx(s_model, position, (Vector3){ 0.0f, 1.0f, 0.0f }, yawDegrees,
                (Vector3){ scale, scale, scale }, tint);
}

static MeshAdjacency s_charAdjacency;
static bool s_charAdjacencyBuilt = false;

Model CharacterModel_GetModel(void) {
    return s_model;
}

MeshAdjacency* CharacterModel_GetAdjacency(void) {
    if (!s_charAdjacencyBuilt && s_loaded) {
        if (s_model.meshCount > 0) {
            MeshAdjacency_Build(&s_charAdjacency, s_model.meshes[0]);
            s_charAdjacencyBuilt = true;
        }
    }
    return s_charAdjacencyBuilt ? &s_charAdjacency : NULL;
}

bool CharacterModel_SampleSurfacePoint(Vector3 position, float yawRadians, float scale,
                                      Vector3 *outWorldPos, Vector3 *outWorldNormal) {
    if (!s_loaded || s_model.meshCount == 0) return false;

    int meshIdx = (s_model.meshCount == 1) ? 0 : GetRandomValue(0, s_model.meshCount - 1);
    Mesh mesh = s_model.meshes[meshIdx];
    if (mesh.vertexCount <= 0) return false;

    const float *vBuf = (mesh.animVertices != NULL) ? mesh.animVertices : mesh.vertices;
    const float *nBuf = (mesh.animNormals != NULL) ? mesh.animNormals : mesh.normals;
    if (!vBuf) return false;

    Vector3 localPos = { 0 };
    Vector3 localNorm = (Vector3){ 0.0f, 1.0f, 0.0f };

    if (mesh.indices != NULL && mesh.triangleCount > 0) {
        int tri = GetRandomValue(0, mesh.triangleCount - 1);
        int i0 = mesh.indices[tri * 3 + 0];
        int i1 = mesh.indices[tri * 3 + 1];
        int i2 = mesh.indices[tri * 3 + 2];

        float r1 = (float)GetRandomValue(0, 10000) / 10000.0f;
        float r2 = (float)GetRandomValue(0, 10000) / 10000.0f;
        if (r1 + r2 > 1.0f) {
            r1 = 1.0f - r1;
            r2 = 1.0f - r2;
        }
        float r3 = 1.0f - r1 - r2;

        localPos.x = r1 * vBuf[i0 * 3 + 0] + r2 * vBuf[i1 * 3 + 0] + r3 * vBuf[i2 * 3 + 0];
        localPos.y = r1 * vBuf[i0 * 3 + 1] + r2 * vBuf[i1 * 3 + 1] + r3 * vBuf[i2 * 3 + 1];
        localPos.z = r1 * vBuf[i0 * 3 + 2] + r2 * vBuf[i1 * 3 + 2] + r3 * vBuf[i2 * 3 + 2];

        if (nBuf) {
            localNorm.x = r1 * nBuf[i0 * 3 + 0] + r2 * nBuf[i1 * 3 + 0] + r3 * nBuf[i2 * 3 + 0];
            localNorm.y = r1 * nBuf[i0 * 3 + 1] + r2 * nBuf[i1 * 3 + 1] + r3 * nBuf[i2 * 3 + 1];
            localNorm.z = r1 * nBuf[i0 * 3 + 2] + r2 * nBuf[i1 * 3 + 2] + r3 * nBuf[i2 * 3 + 2];
            float nLenSq = localNorm.x * localNorm.x + localNorm.y * localNorm.y + localNorm.z * localNorm.z;
            if (nLenSq > 1e-6f) {
                float invN = 1.0f / sqrtf(nLenSq);
                localNorm.x *= invN;
                localNorm.y *= invN;
                localNorm.z *= invN;
            }
        }
    } else {
        int vIdx = GetRandomValue(0, mesh.vertexCount - 1);
        localPos.x = vBuf[vIdx * 3 + 0];
        localPos.y = vBuf[vIdx * 3 + 1];
        localPos.z = vBuf[vIdx * 3 + 2];

        if (nBuf) {
            localNorm.x = nBuf[vIdx * 3 + 0];
            localNorm.y = nBuf[vIdx * 3 + 1];
            localNorm.z = nBuf[vIdx * 3 + 2];
        }
    }

    float c = cosf(yawRadians);
    float s = sinf(yawRadians);

    Vector3 scaledPos = (Vector3){ localPos.x * scale, localPos.y * scale, localPos.z * scale };
    Vector3 rotatedPos = (Vector3){
        scaledPos.x * c + scaledPos.z * s,
        scaledPos.y,
        -scaledPos.x * s + scaledPos.z * c
    };

    if (outWorldPos) {
        *outWorldPos = Vector3Add(rotatedPos, position);
    }

    if (outWorldNormal) {
        Vector3 rotatedNorm = (Vector3){
            localNorm.x * c + localNorm.z * s,
            localNorm.y,
            -localNorm.x * s + localNorm.z * c
        };
        float rnLenSq = rotatedNorm.x * rotatedNorm.x + rotatedNorm.y * rotatedNorm.y + rotatedNorm.z * rotatedNorm.z;
        if (rnLenSq > 1e-6f) {
            float invLen = 1.0f / sqrtf(rnLenSq);
            rotatedNorm.x *= invLen;
            rotatedNorm.y *= invLen;
            rotatedNorm.z *= invLen;
        } else {
            rotatedNorm = (Vector3){ 0.0f, 1.0f, 0.0f };
        }
        *outWorldNormal = rotatedNorm;
    }

    return true;
}
