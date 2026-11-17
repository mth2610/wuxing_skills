#include "character/character_model.h"
#include "character/spring_bone.h"
#include "character/wardrobe_system.h"
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
#define CHAR_MAX_TOTAL_ANIMATIONS 64
#define CHAR_MAX_SKIN_BONES 128
#define CHAR_SKIN_CACHE_SLOTS 32

typedef struct
{
    const Mesh *meshesPtr;
    int animIndex;
    int frame;
    unsigned int deflateMask;
    unsigned int clothStamp;
    unsigned int lastUseSeq;
} SkinCacheEntry;

static SkinCacheEntry s_skinCache[CHAR_SKIN_CACHE_SLOTS];
static unsigned int s_skinCacheSeq = 0;

static void SkinCache_InvalidateAll(void)
{
    memset(s_skinCache, 0, sizeof(s_skinCache));
    s_skinCacheSeq = 0;
}

static bool SkinCache_IsHit(const Mesh *meshesPtr, int animIndex, int frame, unsigned int deflateMask, unsigned int clothStamp)
{
    if (!meshesPtr)
        return false;
    for (int i = 0; i < CHAR_SKIN_CACHE_SLOTS; i++)
    {
        if (s_skinCache[i].meshesPtr == meshesPtr)
        {
            if (s_skinCache[i].animIndex == animIndex &&
                s_skinCache[i].frame == frame &&
                s_skinCache[i].deflateMask == deflateMask &&
                s_skinCache[i].clothStamp == clothStamp)
            {
                s_skinCache[i].lastUseSeq = ++s_skinCacheSeq;
                return true;
            }
            return false;
        }
    }
    return false;
}

static void SkinCache_Store(const Mesh *meshesPtr, int animIndex, int frame, unsigned int deflateMask, unsigned int clothStamp)
{
    if (!meshesPtr)
        return;
    int target = -1;
    unsigned int oldestSeq = 0xFFFFFFFFu;
    for (int i = 0; i < CHAR_SKIN_CACHE_SLOTS; i++)
    {
        if (s_skinCache[i].meshesPtr == meshesPtr)
        {
            target = i;
            break;
        }
        if (s_skinCache[i].meshesPtr == NULL)
        {
            target = i;
            break;
        }
        if (s_skinCache[i].lastUseSeq < oldestSeq)
        {
            oldestSeq = s_skinCache[i].lastUseSeq;
            target = i;
        }
    }
    if (target >= 0)
    {
        s_skinCache[target].meshesPtr = meshesPtr;
        s_skinCache[target].animIndex = animIndex;
        s_skinCache[target].frame = frame;
        s_skinCache[target].deflateMask = deflateMask;
        s_skinCache[target].clothStamp = clothStamp;
        s_skinCache[target].lastUseSeq = ++s_skinCacheSeq;
    }
}

static Model s_model;
static ModelAnimation s_animPool[CHAR_MAX_TOTAL_ANIMATIONS];
static ModelAnimation *s_animations = s_animPool;
static int s_animCount = 0;
static bool s_loaded = false;

static MeshAdjacency s_charAdjacency;
static bool s_charAdjacencyBuilt = false;

typedef struct
{
    int indices[CHAR_ANIM_MAX_VARIANTS];
    int count;
} SlotVariants;

static SlotVariants s_slots[CHAR_ANIM_COUNT];

static const char *s_slotNames[CHAR_ANIM_COUNT] = {
    [CHAR_ANIM_IDLE] = "IDLE",
    [CHAR_ANIM_WALK] = "WALK",
    [CHAR_ANIM_RUN] = "RUN",
    [CHAR_ANIM_JUMP] = "JUMP",
    [CHAR_ANIM_JUMP_DOWN] = "JUMP_DOWN",
    [CHAR_ANIM_DODGE] = "DODGE",
    [CHAR_ANIM_PUNCH] = "PUNCH",
    [CHAR_ANIM_KICK] = "KICK",
    [CHAR_ANIM_PALM] = "PALM",
    [CHAR_ANIM_BLOCK] = "BLOCK",
    [CHAR_ANIM_PARRY] = "PARRY",
    [CHAR_ANIM_SWORD_IDLE] = "SWORD_IDLE",
    [CHAR_ANIM_SWORD_SLASH] = "SWORD_SLASH",
    [CHAR_ANIM_SWORD_THRUST] = "SWORD_THRUST",
    [CHAR_ANIM_SWORD_PARRY] = "SWORD_PARRY",
    [CHAR_ANIM_SWORD_BLOCK] = "SWORD_BLOCK",
    [CHAR_ANIM_CAST] = "CAST",
    [CHAR_ANIM_MEDITATE] = "MEDITATE",
    [CHAR_ANIM_HIT] = "HIT",
    [CHAR_ANIM_KNOCKDOWN] = "KNOCKDOWN",
    [CHAR_ANIM_DEATH] = "DEATH",
};

static const char *s_keywords[CHAR_ANIM_COUNT][6] = {
    [CHAR_ANIM_IDLE] = {"idle", "stand", NULL},
    [CHAR_ANIM_WALK] = {"walk", "jog", NULL},
    [CHAR_ANIM_RUN] = {"run", "sprint", NULL},
    [CHAR_ANIM_JUMP] = {"jump", "leap", NULL},
    [CHAR_ANIM_JUMP_DOWN] = {"jumping_down", "jump_down", "jumpdown", "land", NULL},
    [CHAR_ANIM_DODGE] = {"dodge", "evade", "roll", "dash", NULL},
    [CHAR_ANIM_PUNCH] = {"punch", "jab", "cross", "hook", NULL},
    [CHAR_ANIM_KICK] = {"kick", NULL},
    [CHAR_ANIM_PALM] = {"palm", "push", "slap", NULL},
    [CHAR_ANIM_BLOCK] = {"unarmed_block", "block", "guard", NULL},
    [CHAR_ANIM_PARRY] = {"unarmed_parry", "parry", "deflect", "taiji", NULL},
    [CHAR_ANIM_SWORD_IDLE] = {"loco_idle_sword", "sword_idle", "idle_sword", NULL},
    [CHAR_ANIM_SWORD_SLASH] = {"sword_slash", "slash", "cleave", "whirlwind", NULL},
    [CHAR_ANIM_SWORD_THRUST] = {"sword_thrust", "thrust", "lunge", NULL},
    [CHAR_ANIM_SWORD_PARRY] = {"sword_parry", NULL},
    [CHAR_ANIM_SWORD_BLOCK] = {"sword_block", NULL},
    [CHAR_ANIM_CAST] = {"cast", "spell", "flourish", NULL},
    [CHAR_ANIM_MEDITATE] = {"meditate", "meditation", "thiendinh", "charge", NULL},
    [CHAR_ANIM_HIT] = {"hit", "hurt", "damage", NULL},
    [CHAR_ANIM_KNOCKDOWN] = {"down", "fall", "knockdown", NULL},
    [CHAR_ANIM_DEATH] = {"death", "die", "defeat", NULL},
};

static void ToLowerCopy(char *dst, const char *src, int maxLen)
{
    int i = 0;
    for (; src[i] != '\0' && i < maxLen - 1; i++)
    {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static bool SlotContainsAnim(CharacterAnimSlot slot, int animIdx)
{
    for (int i = 0; i < s_slots[slot].count; i++)
    {
        if (s_slots[slot].indices[i] == animIdx)
            return true;
    }
    return false;
}

static bool AddSlotIndex(CharacterAnimSlot slot, int animIdx)
{
    if (slot < 0 || slot >= CHAR_ANIM_COUNT)
        return false;
    if (animIdx < 0 || animIdx >= s_animCount)
        return false;
    if (SlotContainsAnim(slot, animIdx))
        return true;
    if (s_slots[slot].count < CHAR_ANIM_MAX_VARIANTS)
    {
        s_slots[slot].indices[s_slots[slot].count++] = animIdx;
        return true;
    }
    return false;
}

// Lightweight JSON manifest parser for assets/characters/player_anims.json
static void LoadAnimManifest(const char *manifestPath)
{
    if (!FileExists(manifestPath))
        return;
    char *text = LoadFileText(manifestPath);
    if (!text)
        return;

    int textLen = (int)strlen(text);
    char *lowerText = (char *)malloc((size_t)textLen + 1);
    if (!lowerText)
    {
        UnloadFileText(text);
        return;
    }
    ToLowerCopy(lowerText, text, textLen + 1);

    TraceLog(LOG_INFO, "CHARACTER_MODEL: Loading animation manifest from %s", manifestPath);

    for (int slot = 0; slot < CHAR_ANIM_COUNT; slot++)
    {
        char slotKey[64];
        char quotedKey[72];
        ToLowerCopy(slotKey, s_slotNames[slot], sizeof(slotKey));
        snprintf(quotedKey, sizeof(quotedKey), "\"%s\"", slotKey);

        char *pos = strstr(lowerText, quotedKey);
        if (pos)
        {
            size_t offset = (size_t)(pos - lowerText);
            char *colon = strchr(text + offset, ':');
            if (colon)
            {
                char *arrStart = strchr(colon, '[');
                char *arrEnd = arrStart ? strchr(arrStart, ']') : strchr(colon, '\n');
                if (!arrEnd)
                    arrEnd = colon + strlen(colon);

                int len = (int)(arrEnd - colon);
                char lineBuf[1024];
                if (len >= (int)sizeof(lineBuf))
                    len = sizeof(lineBuf) - 1;
                strncpy(lineBuf, colon, len);
                lineBuf[len] = '\0';

                // Extract quoted clip names
                char *p = lineBuf;
                bool slotCleared = false;
                while ((p = strchr(p, '\"')) != NULL)
                {
                    p++; // skip opening quote
                    char *closeQ = strchr(p, '\"');
                    if (!closeQ)
                        break;
                    *closeQ = '\0';

                    int idx = CharacterModel_FindAnimIndex(p);
                    if (idx >= 0)
                    {
                        if (!slotCleared)
                        {
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

    free(lowerText);
    UnloadFileText(text);
}

bool CharacterModel_LoadEx(const char *meshPath, const char *animBankPath)
{
    s_loaded = false;
    s_charAdjacencyBuilt = false;

    for (int i = 0; i < CHAR_ANIM_COUNT; i++)
    {
        s_slots[i].count = 0;
        for (int v = 0; v < CHAR_ANIM_MAX_VARIANTS; v++)
        {
            s_slots[i].indices[v] = -1;
        }
    }

    s_model = ResourceManager_LoadModel(meshPath);
    if (s_model.meshCount == 0)
    {
        return false;
    }

    SurfaceMaterial_Apply(&s_model);

    s_animCount = 0;

    // 1. Check embedded animations in meshPath
    int embCount = 0;
    ModelAnimation *embAnims = ResourceManager_LoadModelAnimations(meshPath, &embCount);
    if (embAnims && embCount > 0)
    {
        for (int i = 0; i < embCount && s_animCount < CHAR_MAX_TOTAL_ANIMATIONS; i++)
        {
            s_animPool[s_animCount++] = embAnims[i];
        }
    }

    // 2. Check external animation bank path
    if (animBankPath && animBankPath[0] != '\0' && strcmp(meshPath, animBankPath) != 0)
    {
        int bankCount = 0;
        ModelAnimation *bankAnims = ResourceManager_LoadModelAnimations(animBankPath, &bankCount);
        if (bankAnims && bankCount > 0)
        {
            for (int i = 0; i < bankCount && s_animCount < CHAR_MAX_TOTAL_ANIMATIONS; i++)
            {
                bool exists = false;
                for (int j = 0; j < s_animCount; j++)
                {
                    if (strcmp(s_animPool[j].name, bankAnims[i].name) == 0)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                {
                    s_animPool[s_animCount++] = bankAnims[i];
                }
            }
        }
    }

    if (s_animCount == 0)
    {
        TraceLog(LOG_WARNING, "CHARACTER_MODEL: %s has no animations and bank (%s) returned none",
                 meshPath, animBankPath ? animBankPath : "NULL");
        return false;
    }

    s_animations = s_animPool;

    TraceLog(LOG_INFO, "CHARACTER_MODEL: Loaded %s with %d skeletal animation clips (bank: %s):",
             meshPath, s_animCount, animBankPath ? animBankPath : "none");
    for (int a = 0; a < s_animCount; a++)
    {
        TraceLog(LOG_INFO, "  [%2d] \"%s\" (%d frames, %.2fs)",
                 a, s_animations[a].name, s_animations[a].keyframeCount,
                 (float)s_animations[a].keyframeCount / ANIM_FPS);
    }

    // Auto-match slots based on keywords
    for (int slot = 0; slot < CHAR_ANIM_COUNT; slot++)
    {
        for (int a = 0; a < s_animCount; a++)
        {
            char lowerName[64];
            ToLowerCopy(lowerName, s_animations[a].name, sizeof(lowerName));

            for (int k = 0; k < 6 && s_keywords[slot][k] != NULL; k++)
            {
                if (strstr(lowerName, s_keywords[slot][k]) != NULL)
                {
                    AddSlotIndex((CharacterAnimSlot)slot, a);
                    break;
                }
            }
        }
    }

    // Fallbacks for unassigned slots
    if (s_slots[CHAR_ANIM_RUN].count == 0 && s_slots[CHAR_ANIM_WALK].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_RUN, s_slots[CHAR_ANIM_WALK].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_IDLE].count == 0 && s_slots[CHAR_ANIM_IDLE].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_SWORD_IDLE, s_slots[CHAR_ANIM_IDLE].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_SLASH].count == 0 && s_slots[CHAR_ANIM_PUNCH].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_SWORD_SLASH, s_slots[CHAR_ANIM_PUNCH].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_THRUST].count == 0 && s_slots[CHAR_ANIM_SWORD_SLASH].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_SWORD_THRUST, s_slots[CHAR_ANIM_SWORD_SLASH].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_BLOCK].count == 0 && s_slots[CHAR_ANIM_PALM].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_SWORD_BLOCK, s_slots[CHAR_ANIM_PALM].indices[0]);
    }
    if (s_slots[CHAR_ANIM_SWORD_PARRY].count == 0)
    {
        if (s_slots[CHAR_ANIM_SWORD_BLOCK].count > 0)
            AddSlotIndex(CHAR_ANIM_SWORD_PARRY, s_slots[CHAR_ANIM_SWORD_BLOCK].indices[0]);
        else if (s_slots[CHAR_ANIM_PALM].count > 0)
            AddSlotIndex(CHAR_ANIM_SWORD_PARRY, s_slots[CHAR_ANIM_PALM].indices[0]);
    }
    if (s_slots[CHAR_ANIM_JUMP_DOWN].count == 0 && s_slots[CHAR_ANIM_JUMP].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_JUMP_DOWN, s_slots[CHAR_ANIM_JUMP].indices[0]);
    }
    if (s_slots[CHAR_ANIM_DODGE].count == 0)
    {
        if (s_slots[CHAR_ANIM_RUN].count > 0)
            AddSlotIndex(CHAR_ANIM_DODGE, s_slots[CHAR_ANIM_RUN].indices[0]);
        else if (s_slots[CHAR_ANIM_WALK].count > 0)
            AddSlotIndex(CHAR_ANIM_DODGE, s_slots[CHAR_ANIM_WALK].indices[0]);
    }
    if (s_slots[CHAR_ANIM_HIT].count == 0 && s_slots[CHAR_ANIM_IDLE].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_HIT, s_slots[CHAR_ANIM_IDLE].indices[0]);
    }
    if (s_slots[CHAR_ANIM_BLOCK].count == 0 && s_slots[CHAR_ANIM_PALM].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_BLOCK, s_slots[CHAR_ANIM_PALM].indices[0]);
    }
    if (s_slots[CHAR_ANIM_PARRY].count == 0)
    {
        if (s_slots[CHAR_ANIM_BLOCK].count > 0)
            AddSlotIndex(CHAR_ANIM_PARRY, s_slots[CHAR_ANIM_BLOCK].indices[0]);
        else if (s_slots[CHAR_ANIM_PALM].count > 0)
            AddSlotIndex(CHAR_ANIM_PARRY, s_slots[CHAR_ANIM_PALM].indices[0]);
    }
    if (s_slots[CHAR_ANIM_MEDITATE].count == 0 && s_slots[CHAR_ANIM_IDLE].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_MEDITATE, s_slots[CHAR_ANIM_IDLE].indices[0]);
    }
    if (s_slots[CHAR_ANIM_KNOCKDOWN].count == 0 && s_slots[CHAR_ANIM_HIT].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_KNOCKDOWN, s_slots[CHAR_ANIM_HIT].indices[0]);
    }
    if (s_slots[CHAR_ANIM_DEATH].count == 0 && s_slots[CHAR_ANIM_KNOCKDOWN].count > 0)
    {
        AddSlotIndex(CHAR_ANIM_DEATH, s_slots[CHAR_ANIM_KNOCKDOWN].indices[0]);
    }

    // Optional JSON manifest override
    LoadAnimManifest("assets/characters/player_anims.json");

    CharacterModel_PrintInfo();

    SkinCache_InvalidateAll();
    s_loaded = true;
    return true;
}

bool CharacterModel_Load(const char *filePath)
{
    const char *defaultAnimBank = "assets/characters/animations/humanoid_anims.glb";
    return CharacterModel_LoadEx(filePath, defaultAnimBank);
}

bool CharacterModel_IsLoaded(void)
{
    return s_loaded;
}

void CharacterModel_ResetState(CharacterAnimState *state)
{
    if (!state)
        return;
    state->frame = 0;
    state->frameTimer = 0.0f;
    state->currentSlot = CHAR_ANIM_IDLE;
    state->activeVariant = 0;
    state->activeAnimIndex = (s_slots[CHAR_ANIM_IDLE].count > 0) ? s_slots[CHAR_ANIM_IDLE].indices[0] : -1;
    state->oneShotActive = false;
    state->oneShotSpeed = 1.0f;
}

void CharacterModel_Update(CharacterAnimState *state, float dt, bool isMoving)
{
    if (!state || !s_loaded)
        return;

    if (!state->oneShotActive)
    {
        CharacterAnimSlot desired = isMoving ? CHAR_ANIM_WALK : CHAR_ANIM_IDLE;
        if (desired != state->currentSlot)
        {
            state->currentSlot = desired;
            state->activeVariant = 0;
            state->activeAnimIndex = (s_slots[desired].count > 0) ? s_slots[desired].indices[0] : -1;
            state->frame = 0;
            state->frameTimer = 0.0f;
        }
    }

    int animIdx = state->activeAnimIndex;
    if (animIdx < 0 || animIdx >= s_animCount)
        return;

    int frameCount = s_animations[animIdx].keyframeCount;
    float speed = state->oneShotActive ? state->oneShotSpeed : 1.0f;

    state->frameTimer += dt * ANIM_FPS * speed;
    while (state->frameTimer >= 1.0f)
    {
        state->frameTimer -= 1.0f;
        state->frame++;
        if (state->frame >= frameCount)
        {
            if (state->oneShotActive)
            {
                state->oneShotActive = false;
                state->currentSlot = isMoving ? CHAR_ANIM_WALK : CHAR_ANIM_IDLE;
                state->activeVariant = 0;
                state->activeAnimIndex = (s_slots[state->currentSlot].count > 0) ? s_slots[state->currentSlot].indices[0] : -1;
                state->frame = 0;
            }
            else
            {
                state->frame = 0; // loop
            }
        }
    }
}

void CharacterModel_Trigger(CharacterAnimState *state, CharacterAnimSlot slot)
{
    CharacterModel_TriggerVariantTimed(state, slot, 0, 0.0f);
}

void CharacterModel_TriggerTimed(CharacterAnimState *state, CharacterAnimSlot slot, float seconds)
{
    CharacterModel_TriggerVariantTimed(state, slot, 0, seconds);
}

void CharacterModel_TriggerVariant(CharacterAnimState *state, CharacterAnimSlot slot, int variantIndex)
{
    CharacterModel_TriggerVariantTimed(state, slot, variantIndex, 0.0f);
}

void CharacterModel_TriggerVariantTimed(CharacterAnimState *state, CharacterAnimSlot slot, int variantIndex, float seconds)
{
    if (!state || !s_loaded)
        return;
    if (slot < 0 || slot >= CHAR_ANIM_COUNT)
        return;
    if (s_slots[slot].count <= 0)
        return;

    int vIdx = variantIndex;
    if (vIdx < 0)
        vIdx = 0;
    if (vIdx >= s_slots[slot].count)
        vIdx = s_slots[slot].count - 1;

    int animIdx = s_slots[slot].indices[vIdx];
    if (animIdx < 0 || animIdx >= s_animCount)
        return;

    float speed = 1.0f;
    if (seconds > 0.0f)
    {
        float naturalSeconds = (float)s_animations[animIdx].keyframeCount / ANIM_FPS;
        speed = naturalSeconds / seconds;
        if (speed < 0.25f)
            speed = 0.25f;
        if (speed > 20.0f)
            speed = 20.0f;
    }

    state->currentSlot = slot;
    state->activeVariant = vIdx;
    state->activeAnimIndex = animIdx;
    state->frame = 0;
    state->frameTimer = 0.0f;
    state->oneShotActive = true;
    state->oneShotSpeed = speed;
}

void CharacterModel_TriggerAttack(CharacterAnimState *state, CharacterAnimSlot slot)
{
    CharacterModel_TriggerTimed(state, slot, 0.0f);
}

void CharacterModel_TriggerAttackTimed(CharacterAnimState *state, CharacterAnimSlot slot, float seconds)
{
    CharacterModel_TriggerTimed(state, slot, seconds);
}

bool CharacterModel_TriggerByIndex(CharacterAnimState *state, int animIdx, float seconds)
{
    if (!state || !s_loaded || animIdx < 0 || animIdx >= s_animCount)
        return false;

    float speed = 1.0f;
    if (seconds > 0.0f)
    {
        float naturalSeconds = (float)s_animations[animIdx].keyframeCount / ANIM_FPS;
        speed = naturalSeconds / seconds;
        if (speed < 0.25f)
            speed = 0.25f;
        if (speed > 20.0f)
            speed = 20.0f;
    }

    state->activeAnimIndex = animIdx;
    state->frame = 0;
    state->frameTimer = 0.0f;
    state->oneShotActive = true;
    state->oneShotSpeed = speed;
    return true;
}

bool CharacterModel_TriggerByName(CharacterAnimState *state, const char *clipName, float seconds)
{
    if (!state || !s_loaded || !clipName)
        return false;
    int animIdx = CharacterModel_FindAnimIndex(clipName);
    if (animIdx < 0)
        return false;
    TraceLog(LOG_INFO, "CHARACTER_MODEL: TriggerByName \"%s\" -> idx=%d (%.2fs)", clipName, animIdx, seconds);
    return CharacterModel_TriggerByIndex(state, animIdx, seconds);
}

int CharacterModel_GetVariantCount(CharacterAnimSlot slot)
{
    if (slot < 0 || slot >= CHAR_ANIM_COUNT)
        return 0;
    return s_slots[slot].count;
}

int CharacterModel_GetSlotAnimIndex(CharacterAnimSlot slot, int variantIndex)
{
    if (slot < 0 || slot >= CHAR_ANIM_COUNT)
        return -1;
    if (variantIndex < 0 || variantIndex >= s_slots[slot].count)
        return -1;
    return s_slots[slot].indices[variantIndex];
}

bool CharacterModel_SetSlotVariant(CharacterAnimSlot slot, int variantIndex, int animIndex)
{
    if (slot < 0 || slot >= CHAR_ANIM_COUNT)
        return false;
    if (animIndex < 0 || animIndex >= s_animCount)
        return false;
    if (variantIndex < 0 || variantIndex >= CHAR_ANIM_MAX_VARIANTS)
        return false;

    s_slots[slot].indices[variantIndex] = animIndex;
    if (variantIndex >= s_slots[slot].count)
    {
        s_slots[slot].count = variantIndex + 1;
    }
    return true;
}

bool CharacterModel_AddSlotVariant(CharacterAnimSlot slot, int animIndex)
{
    return AddSlotIndex(slot, animIndex);
}

void CharacterModel_ClearSlotVariants(CharacterAnimSlot slot)
{
    if (slot < 0 || slot >= CHAR_ANIM_COUNT)
        return;
    s_slots[slot].count = 0;
    for (int i = 0; i < CHAR_ANIM_MAX_VARIANTS; i++)
    {
        s_slots[slot].indices[i] = -1;
    }
}

int CharacterModel_GetTotalAnimCount(void)
{
    return s_animCount;
}

int CharacterModel_FindAnimIndex(const char *name)
{
    if (!name || s_animCount == 0)
        return -1;
    char lowerTarget[64];
    ToLowerCopy(lowerTarget, name, sizeof(lowerTarget));

    // 1. Exact match
    for (int i = 0; i < s_animCount; i++)
    {
        char lowerClip[64];
        ToLowerCopy(lowerClip, s_animations[i].name, sizeof(lowerClip));
        if (strcmp(lowerClip, lowerTarget) == 0)
            return i;
    }
    // 2. Substring match
    for (int i = 0; i < s_animCount; i++)
    {
        char lowerClip[64];
        ToLowerCopy(lowerClip, s_animations[i].name, sizeof(lowerClip));
        if (strstr(lowerClip, lowerTarget) != NULL)
            return i;
    }
    return -1;
}

const char *CharacterModel_GetAnimName(int animIndex)
{
    if (animIndex < 0 || animIndex >= s_animCount)
        return NULL;
    return s_animations[animIndex].name;
}

float CharacterModel_GetAnimDuration(int animIndex)
{
    if (animIndex < 0 || animIndex >= s_animCount)
        return 0.0f;
    return (float)s_animations[animIndex].keyframeCount / ANIM_FPS;
}

void CharacterModel_PrintInfo(void)
{
    TraceLog(LOG_INFO, "==================== CHARACTER ANIMATION MATRIX ====================");
    for (int slot = 0; slot < CHAR_ANIM_COUNT; slot++)
    {
        int count = s_slots[slot].count;
        if (count == 0)
        {
            TraceLog(LOG_WARNING, "  [%-18s] (0 variants) <UNASSIGNED>", s_slotNames[slot]);
        }
        else
        {
            for (int v = 0; v < count; v++)
            {
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

#define DEFLATE_FLAG_TORSO (1u << 0)
#define DEFLATE_FLAG_LEG (1u << 1)
#define DEFLATE_FLAG_HIPS (1u << 2)
#define DEFLATE_FLAG_ARM (1u << 3)

// Rotation matrix taking unit vector `from` onto unit vector `to` (row-vector convention).
static Matrix RotationBetweenDirs(Vector3 from, Vector3 to)
{
    Vector3 f = Vector3Normalize(from);
    Vector3 t = Vector3Normalize(to);
    float d = Vector3DotProduct(f, t);
    if (d > 0.99999f)
        return MatrixIdentity();
    if (d < -0.99999f)
    {
        Vector3 axis = Vector3CrossProduct(f, (Vector3){1.0f, 0.0f, 0.0f});
        if (Vector3LengthSqr(axis) < 1.0e-6f)
            axis = Vector3CrossProduct(f, (Vector3){0.0f, 1.0f, 0.0f});
        return MatrixRotate(Vector3Normalize(axis), PI);
    }
    Vector3 axis = Vector3Normalize(Vector3CrossProduct(f, t));
    return MatrixRotate(axis, acosf(Clamp(d, -1.0f, 1.0f)));
}

static Matrix BindPoseMatrixOf(const Model *model, int boneIndex)
{
    Transform *bp = &model->skeleton.bindPose[boneIndex];
    return MatrixMultiply(MatrixMultiply(MatrixScale(bp->scale.x, bp->scale.y, bp->scale.z),
                                         QuaternionToMatrix(bp->rotation)),
                          MatrixTranslate(bp->translation.x, bp->translation.y, bp->translation.z));
}

// Lookup bone index by name via master skeleton (never blind index fallback for cloth bones)
static int FindAnimBoneIndexByName(ModelAnimation anim, const char *boneName, int hintIdx)
{
    if (!boneName || anim.boneCount <= 0)
        return -1;

    if (s_model.skeleton.bones != NULL)
    {
        if (hintIdx >= 0 && hintIdx < s_model.skeleton.boneCount && hintIdx < anim.boneCount &&
            strcmp(boneName, s_model.skeleton.bones[hintIdx].name) == 0)
        {
            return hintIdx;
        }
        for (int a = 0; a < s_model.skeleton.boneCount && a < anim.boneCount; a++)
        {
            if (strcmp(boneName, s_model.skeleton.bones[a].name) == 0)
            {
                return a;
            }
        }
        return -1;
    }

    return (hintIdx >= 0 && hintIdx < anim.boneCount) ? hintIdx : -1;
}

// Trích xuất Quaternion xoay thuần túy từ ma trận biến đổi 4x4 (loại bỏ scale & translation)
static Quaternion MatrixToRotationQuatClean(Matrix m)
{
    Vector3 col0 = {m.m0, m.m1, m.m2};
    Vector3 col1 = {m.m4, m.m5, m.m6};
    Vector3 col2 = {m.m8, m.m9, m.m10};
    float sx = Vector3Length(col0);
    float sy = Vector3Length(col1);
    float sz = Vector3Length(col2);
    if (sx > 1e-6f)
    {
        m.m0 /= sx;
        m.m1 /= sx;
        m.m2 /= sx;
    }
    if (sy > 1e-6f)
    {
        m.m4 /= sy;
        m.m5 /= sy;
        m.m6 /= sy;
    }
    if (sz > 1e-6f)
    {
        m.m8 /= sz;
        m.m9 /= sz;
        m.m10 /= sz;
    }
    m.m12 = m.m13 = m.m14 = 0.0f;
    m.m3 = m.m7 = m.m11 = 0.0f;
    m.m15 = 1.0f;
    return QuaternionFromMatrix(m);
}

static bool ApplyClothBonePose(Model *model, int boneIndex, const char *boneName,
                               Matrix bindPoseMatrix, const SpringBoneSystem *spring)
{
    if (!spring)
        return false;

    Vector3 headModel, dirModel, bindHead, bindDir;
    if (!SpringBone_GetBoneModelPose(spring, boneName, &headModel, &dirModel))
        return false;
    if (!SpringBone_GetBindPose(spring, boneName, &bindHead, &bindDir))
        return false;
    if (Vector3LengthSqr(dirModel) < 1.0e-6f || Vector3LengthSqr(bindDir) < 1.0e-6f)
        return false;
    (void)bindHead;

    Transform bp = model->skeleton.bindPose[boneIndex];
    Quaternion qBind = QuaternionNormalize(bp.rotation);

    // 1. Tính Quaternion xoay delta của xương cha trong không gian Model:
    //    qPoseParent = qBindParent * qDelta  =>  qDelta = inv(qBindParent) * qPoseParent
    Quaternion qParentDelta = QuaternionIdentity();
    int parentIdx = model->skeleton.bones[boneIndex].parent;
    if (parentIdx >= 0 && parentIdx < model->skeleton.boneCount)
    {
        Quaternion qBindParent = QuaternionNormalize(model->skeleton.bindPose[parentIdx].rotation);
        Matrix poseParentMat = MatrixMultiply(BindPoseMatrixOf(model, parentIdx), model->boneMatrices[parentIdx]);
        Quaternion qPoseParent = QuaternionNormalize(MatrixToRotationQuatClean(poseParentMat));
        // Trong quy ước row-vector của Raylib, xoay tuần tự q1 rồi q2 là QuaternionMultiply(q2, q1)
        qParentDelta = QuaternionMultiply(qPoseParent, QuaternionInvert(qBindParent));
    }

    // 2. Xoay bindDir theo xương cha trước -> refDir
    Vector3 refDir = Vector3Normalize(Vector3RotateByQuaternion(bindDir, qParentDelta));

    // 3. Tính Quaternion xoay dư (swing) từ refDir sang dirModel của SpringBone
    Quaternion qSwing = QuaternionFromVector3ToVector3(refDir, Vector3Normalize(dirModel));

    // 4. Tổng hợp xoay: qBind -> qParentDelta -> qSwing
    Quaternion qFinal = QuaternionMultiply(qParentDelta, qBind);
    qFinal = QuaternionMultiply(qSwing, qFinal);
    qFinal = QuaternionNormalize(qFinal);

    // 5. Dựng ma trận Global Pose tại vị trí headModel mới
    Matrix pose = MatrixMultiply(
        MatrixMultiply(MatrixScale(bp.scale.x, bp.scale.y, bp.scale.z), QuaternionToMatrix(qFinal)),
        MatrixTranslate(headModel.x, headModel.y, headModel.z));

    model->boneMatrices[boneIndex] = MatrixMultiply(MatrixInvert(bindPoseMatrix), pose);
    return true;
}

// Evaluates bone `b` after ensuring its parent has already been evaluated.
// Fixes glTF post-order joint arrays where child bones precede parent bones.
static void EvaluateBoneHierarchyRecursive(Model *model, int b, ModelAnimation anim,
                                           int currentFrame, int nextFrame, float blend,
                                           const SpringBoneSystem *spring, bool *evaluated)
{
    if (b < 0 || b >= model->skeleton.boneCount || evaluated[b])
        return;

    int parentIdx = model->skeleton.bones[b].parent;
    if (parentIdx >= 0 && parentIdx < model->skeleton.boneCount && !evaluated[parentIdx])
    {
        EvaluateBoneHierarchyRecursive(model, parentIdx, anim, currentFrame, nextFrame, blend, spring, evaluated);
    }

    evaluated[b] = true;
    const char *boneName = model->skeleton.bones[b].name;
    int animBoneIdx = FindAnimBoneIndexByName(anim, boneName, b);
    Matrix bindPoseMatrix = BindPoseMatrixOf(model, b);

    if (animBoneIdx >= 0 && anim.keyframeCount > 0)
    {
        model->currentPose[b].translation = Vector3Lerp(
            anim.keyframePoses[currentFrame][animBoneIdx].translation,
            anim.keyframePoses[nextFrame][animBoneIdx].translation, blend);
        model->currentPose[b].rotation = QuaternionSlerp(
            anim.keyframePoses[currentFrame][animBoneIdx].rotation,
            anim.keyframePoses[nextFrame][animBoneIdx].rotation, blend);
        model->currentPose[b].scale = Vector3Lerp(
            anim.keyframePoses[currentFrame][animBoneIdx].scale,
            anim.keyframePoses[nextFrame][animBoneIdx].scale, blend);

        Transform *cp = &model->currentPose[b];
        Matrix currentPoseMatrix = MatrixMultiply(
            MatrixMultiply(MatrixScale(cp->scale.x, cp->scale.y, cp->scale.z),
                           QuaternionToMatrix(cp->rotation)),
            MatrixTranslate(cp->translation.x, cp->translation.y, cp->translation.z));

        model->boneMatrices[b] = MatrixMultiply(MatrixInvert(bindPoseMatrix), currentPoseMatrix);
    }
    else if (!ApplyClothBonePose(model, b, boneName, bindPoseMatrix, spring))
    {
        if (parentIdx >= 0 && parentIdx < model->skeleton.boneCount)
        {
            Matrix bindParent = BindPoseMatrixOf(model, parentIdx);
            Matrix invBindParent = MatrixInvert(bindParent);
            Matrix skin = MatrixMultiply(MatrixMultiply(MatrixInvert(bindPoseMatrix), bindParent),
                                         model->boneMatrices[parentIdx]);
            skin = MatrixMultiply(skin, invBindParent);
            model->boneMatrices[b] = MatrixMultiply(skin, bindPoseMatrix);
        }
        else
        {
            model->boneMatrices[b] = MatrixIdentity();
        }
    }
}

static void CharacterModel_UpdateSkinnedMeshWithCloth(Model model, ModelAnimation anim, float frame,
                                                      bool deflateTorso, bool deflateLegs,
                                                      const SpringBoneSystem *spring)
{
    if (model.boneMatrices == NULL || model.skeleton.bones == NULL || anim.keyframePoses == NULL)
        return;

    static int s_profileOn = -1;
    static double s_clothMs = 0.0, s_rigidMs = 0.0;
    static int s_clothCalls = 0, s_rigidCalls = 0;
    static long s_clothVerts = 0, s_rigidVerts = 0;
    static int s_clothVertsMax = 0;
    static double s_clothLoopMs = 0.0, s_clothUploadMs = 0.0;
    static int s_uploadCalls = 0, s_clothUploadCalls = 0;
    if (s_profileOn < 0)
        s_profileOn = (getenv("WUXING_SKIN_PROFILE") != NULL) ? 1 : 0;
    const double skinStart = s_profileOn ? GetTime() : 0.0;

    int currentFrame = (int)frame;
    int nextFrame = currentFrame + 1;
    float blend = frame - (float)currentFrame;
    if (blend < 0.0f)
        blend = 0.0f;
    if (blend > 1.0f)
        blend = 1.0f;
    if (anim.keyframeCount > 0)
    {
        currentFrame %= anim.keyframeCount;
        nextFrame %= anim.keyframeCount;
    }

    const int boneCount = model.skeleton.boneCount;
    const bool needDeflate = deflateTorso || deflateLegs;
    Matrix boneNormMatrices[CHAR_MAX_SKIN_BONES];
    unsigned char boneDeflateFlags[CHAR_MAX_SKIN_BONES];
    bool evaluatedStack[CHAR_MAX_SKIN_BONES];
    bool *evaluated = evaluatedStack;
    if (boneCount <= CHAR_MAX_SKIN_BONES)
    {
        memset(evaluatedStack, 0, (size_t)boneCount * sizeof(bool));
    }
    else
    {
        evaluated = (bool *)calloc((size_t)boneCount, sizeof(bool));
        if (!evaluated)
            return;
    }

    for (int b = 0; b < boneCount; b++)
    {
        EvaluateBoneHierarchyRecursive(&model, b, anim, currentFrame, nextFrame, blend, spring, evaluated);

        if (b < CHAR_MAX_SKIN_BONES)
        {
            const char *boneName = model.skeleton.bones[b].name;
            boneNormMatrices[b] = MatrixTranspose(MatrixInvert(model.boneMatrices[b]));
            unsigned char flags = 0;
            if (needDeflate && boneName != NULL)
            {
                if (strstr(boneName, "Spine") || strstr(boneName, "Shoulder"))
                {
                    flags |= DEFLATE_FLAG_TORSO;
                }
                else if (strstr(boneName, "UpLeg") || strstr(boneName, "Leg"))
                {
                    flags |= DEFLATE_FLAG_LEG;
                }
                else if (strstr(boneName, "Hips"))
                {
                    flags |= DEFLATE_FLAG_HIPS;
                }
                else if (strstr(boneName, "Arm") && !strstr(boneName, "ForeArm"))
                {
                    flags |= DEFLATE_FLAG_ARM;
                }
            }
            boneDeflateFlags[b] = flags;
        }
    }

    if (evaluated != evaluatedStack)
    {
        free(evaluated);
    }

    // CPU skinning vertex deformation pass
    double s_loopMs = 0.0, s_uploadMs = 0.0;
    s_uploadCalls = 0;
    for (int m = 0; m < model.meshCount; m++)
    {
        const double meshStart = s_profileOn ? GetTime() : 0.0;
        Mesh mesh = model.meshes[m];
        if (mesh.boneWeights == NULL || mesh.boneIndices == NULL ||
            mesh.animVertices == NULL)
            continue;

        const int vertexValuesCount = mesh.vertexCount * 3;
        const bool hasNormals = (mesh.normals != NULL && mesh.animNormals != NULL);
        int boneCounter = 0;
        bool bufferUpdateRequired = false;

        for (int vCounter = 0; vCounter < vertexValuesCount; vCounter += 3)
        {
            float vx = mesh.vertices[vCounter];
            float vy = mesh.vertices[vCounter + 1];
            float vz = mesh.vertices[vCounter + 2];

            // Automatic body deflation to eliminate clipping through equipped outerwear
            if (needDeflate)
            {
                float torsoWeight = 0.0f;
                float legWeight = 0.0f;
                float armWeight = 0.0f;
                int tempCounter = boneCounter;
                for (int j = 0; j < 4; j++, tempCounter++)
                {
                    float bw = mesh.boneWeights[tempCounter];
                    int bi = mesh.boneIndices[tempCounter];
                    if (bw > 0.001f && bi >= 0 && bi < boneCount && bi < CHAR_MAX_SKIN_BONES)
                    {
                        unsigned char flags = boneDeflateFlags[bi];
                        if (flags & DEFLATE_FLAG_TORSO)
                        {
                            torsoWeight += bw;
                        }
                        else if ((flags & DEFLATE_FLAG_LEG) || ((flags & DEFLATE_FLAG_HIPS) && vy <= 1.05f))
                        {
                            legWeight += bw;
                        }
                        else if (flags & DEFLATE_FLAG_ARM)
                        {
                            armWeight += bw;
                        }
                    }
                }

                // Trong CharacterModel_UpdateSkinnedMeshWithCloth (character_model.c):
                if (deflateTorso && torsoWeight > 0.12f)
                {
                    float shrink = 1.0f - (0.16f * torsoWeight);
                    if (shrink < 0.82f)
                        shrink = 0.82f;
                    vx *= shrink;
                    vz *= shrink;
                }

                if (deflateLegs && legWeight > 0.12f)
                {
                    float shrink = 1.0f - (0.22f * legWeight);
                    if (shrink < 0.75f)
                        shrink = 0.75f;
                    float legCenterX = (vx > 0.0f) ? 0.10f : -0.10f;
                    if (fabsf(vx) < 0.035f)
                        legCenterX = 0.0f; // Pelvis center
                    vx = legCenterX + (vx - legCenterX) * shrink;
                    vz *= shrink;
                }
            }

            float outVx = 0.0f, outVy = 0.0f, outVz = 0.0f;
            float outNx = 0.0f, outNy = 0.0f, outNz = 0.0f;
            float nx = 0.0f, ny = 0.0f, nz = 0.0f;
            if (hasNormals)
            {
                nx = mesh.normals[vCounter];
                ny = mesh.normals[vCounter + 1];
                nz = mesh.normals[vCounter + 2];
            }

            for (int j = 0; j < 4; j++, boneCounter++)
            {
                float boneWeight = mesh.boneWeights[boneCounter];
                int boneIdx = mesh.boneIndices[boneCounter];

                if (boneWeight <= 0.0001f || boneIdx < 0 || boneIdx >= boneCount)
                    continue;

                const Matrix *bm = &model.boneMatrices[boneIdx];
                outVx += (bm->m0 * vx + bm->m4 * vy + bm->m8 * vz + bm->m12) * boneWeight;
                outVy += (bm->m1 * vx + bm->m5 * vy + bm->m9 * vz + bm->m13) * boneWeight;
                outVz += (bm->m2 * vx + bm->m6 * vy + bm->m10 * vz + bm->m14) * boneWeight;
                bufferUpdateRequired = true;

                if (hasNormals)
                {
                    Matrix nmStorage;
                    const Matrix *nm = (boneIdx < CHAR_MAX_SKIN_BONES)
                                           ? &boneNormMatrices[boneIdx]
                                           : (nmStorage = MatrixTranspose(MatrixInvert(*bm)), &nmStorage);
                    outNx += (nm->m0 * nx + nm->m4 * ny + nm->m8 * nz) * boneWeight;
                    outNy += (nm->m1 * nx + nm->m5 * ny + nm->m9 * nz) * boneWeight;
                    outNz += (nm->m2 * nx + nm->m6 * ny + nm->m10 * nz) * boneWeight;
                }
            }

            mesh.animVertices[vCounter] = outVx;
            mesh.animVertices[vCounter + 1] = outVy;
            mesh.animVertices[vCounter + 2] = outVz;
            if (hasNormals)
            {
                mesh.animNormals[vCounter] = outNx;
                mesh.animNormals[vCounter + 1] = outNy;
                mesh.animNormals[vCounter + 2] = outNz;
            }
        }

        if (s_profileOn)
        {
            double nowMs = GetTime();
            s_loopMs += nowMs - meshStart;
        }
        if (bufferUpdateRequired)
        {
            const double upStart = s_profileOn ? GetTime() : 0.0;
            UpdateMeshBuffer(mesh, SHADER_LOC_VERTEX_POSITION, mesh.animVertices, mesh.vertexCount * 3 * sizeof(float), 0);
            if (hasNormals)
            {
                UpdateMeshBuffer(mesh, SHADER_LOC_VERTEX_NORMAL, mesh.animNormals, mesh.vertexCount * 3 * sizeof(float), 0);
            }
            if (s_profileOn)
            {
                s_uploadCalls++;
                s_uploadMs += GetTime() - upStart;
            }
        }
    }

    if (s_profileOn)
    {
        int modelVerts = 0;
        for (int m = 0; m < model.meshCount; m++)
        {
            if (model.meshes[m].boneWeights != NULL)
                modelVerts += model.meshes[m].vertexCount;
        }
        double ms = (GetTime() - skinStart) * 1000.0;
        if (spring)
        {
            s_clothMs += ms;
            s_clothVerts += modelVerts;
            s_clothCalls++;
            if (modelVerts > s_clothVertsMax)
                s_clothVertsMax = modelVerts;
            s_clothLoopMs += s_loopMs * 1000.0;
            s_clothUploadMs += s_uploadMs * 1000.0;
            s_clothUploadCalls += s_uploadCalls;
        }
        else
        {
            s_rigidMs += ms;
            s_rigidVerts += modelVerts;
            s_rigidCalls++;
        }
        if (s_clothCalls + s_rigidCalls >= 120)
        {
            if (s_clothCalls > 0)
            {
                TraceLog(LOG_INFO, "[SKIN PROFILE] cloth %.2f ms/frame (loop %.2f, upload %.2f in %.1f calls) over %d frames (%d verts/frame, max %d)",
                         s_clothMs / (double)s_clothCalls, s_clothLoopMs / (double)s_clothCalls,
                         s_clothUploadMs / (double)s_clothCalls,
                         (double)s_clothUploadCalls / (double)s_clothCalls, s_clothCalls,
                         s_clothVerts / s_clothCalls, s_clothVertsMax);
            }
            if (s_rigidCalls > 0)
            {
                TraceLog(LOG_INFO, "[SKIN PROFILE] rigid %.2f ms/frame over %d frames (%d verts/frame)",
                         s_rigidMs / (double)s_rigidCalls, s_rigidCalls, s_rigidVerts / s_rigidCalls);
            }
            s_clothMs = s_rigidMs = 0.0;
            s_clothCalls = s_rigidCalls = 0;
            s_clothVerts = s_rigidVerts = 0;
            s_clothVertsMax = 0;
            s_clothLoopMs = s_clothUploadMs = 0.0;
            s_clothUploadCalls = 0;
        }
    }
}

static void BoneDebugDump(Model m, ModelAnimation anim, const char *tag, int frame)
{
    static int s_dumps = 0;
    if (!getenv("WUXING_BONE_DEBUG") || s_dumps >= 3)
        return;
    s_dumps++;
    TraceLog(LOG_INFO, "[BONEDUMP %s] bones=%d animBones=%d frames=%d", tag, m.skeleton.boneCount, anim.boneCount, anim.keyframeCount);
    for (int b = 0; b < m.skeleton.boneCount; b++)
    {
        const char *n = m.skeleton.bones[b].name;
        if (strcmp(n, "mixamorig:Hips") != 0 && strcmp(n, "mixamorig:Spine") != 0 && strcmp(n, "mixamorig:LeftForeArm") != 0)
            continue;
        int ai = FindAnimBoneIndexByName(anim, n, b);
        Transform bp = m.skeleton.bindPose[b];
        TraceLog(LOG_INFO, "[BONEDUMP %s] %s b=%d animIdx=%d bindT=(%.3f,%.3f,%.3f) bindR=(%.3f,%.3f,%.3f,%.3f) poseT=(%.3f,%.3f,%.3f) poseR=(%.3f,%.3f,%.3f,%.3f)",
                 tag, n, b, ai,
                 bp.translation.x, bp.translation.y, bp.translation.z,
                 bp.rotation.x, bp.rotation.y, bp.rotation.z, bp.rotation.w,
                 (ai >= 0) ? anim.keyframePoses[frame][ai].translation.x : -999.0f,
                 (ai >= 0) ? anim.keyframePoses[frame][ai].translation.y : -999.0f,
                 (ai >= 0) ? anim.keyframePoses[frame][ai].translation.z : -999.0f,
                 (ai >= 0) ? anim.keyframePoses[frame][ai].rotation.x : -999.0f,
                 (ai >= 0) ? anim.keyframePoses[frame][ai].rotation.y : -999.0f,
                 (ai >= 0) ? anim.keyframePoses[frame][ai].rotation.z : -999.0f,
                 (ai >= 0) ? anim.keyframePoses[frame][ai].rotation.w : -999.0f);
    }
}

static void CharacterModel_DrawCustomInternal(Model customModel, const CharacterAnimState *state, Vector3 position,
                                              float yawRadians, float scale, Color tint,
                                              bool deflateTorso, bool deflateLegs,
                                              const SpringBoneSystem *spring)
{
    if (!state || !s_loaded || customModel.meshCount == 0)
        return;

    int animIndex = state->activeAnimIndex;
    if (animIndex >= 0 && animIndex < s_animCount && customModel.skeleton.boneCount > 0)
    {
        BoneDebugDump(customModel, s_animations[animIndex], "model", state->frame);
        unsigned int deflateMask = (deflateTorso ? 1u : 0u) | (deflateLegs ? 2u : 0u);
        unsigned int clothStamp = (spring != NULL) ? (spring->stamp | 0x80000000u) : 0u;
        if (!SkinCache_IsHit(customModel.meshes, animIndex, state->frame, deflateMask, clothStamp))
        {
            if (spring == NULL && deflateMask == 0u &&
                customModel.skeleton.boneCount == s_animations[animIndex].boneCount)
            {
                UpdateModelAnimation(customModel, s_animations[animIndex], state->frame);
            }
            else
            {
                float frameSample = (spring != NULL)
                                        ? ((float)state->frame + Clamp(state->frameTimer, 0.0f, 1.0f))
                                        : (float)state->frame;
                CharacterModel_UpdateSkinnedMeshWithCloth(customModel, s_animations[animIndex], frameSample,
                                                          deflateTorso, deflateLegs, spring);
            }
            SkinCache_Store(customModel.meshes, animIndex, state->frame, deflateMask, clothStamp);
        }
    }

    float yawDegrees = yawRadians * RAD2DEG;
    DrawModelEx(customModel, position, (Vector3){0.0f, 1.0f, 0.0f}, yawDegrees,
                (Vector3){scale, scale, scale}, tint);
}

void CharacterModel_DrawCustomEx(Model customModel, const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint, bool deflateTorso, bool deflateLegs)
{
    CharacterModel_DrawCustomInternal(customModel, state, position, yawRadians, scale, tint,
                                      deflateTorso, deflateLegs, NULL);
}

void CharacterModel_DrawCustomCloth(Model customModel, const CharacterAnimState *state, Vector3 position,
                                    float yawRadians, float scale, Color tint,
                                    const SpringBoneSystem *spring)
{
    CharacterModel_DrawCustomInternal(customModel, state, position, yawRadians, scale, tint,
                                      false, false, spring);
}

void CharacterModel_DrawCustom(Model customModel, const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint)
{
    CharacterModel_DrawCustomEx(customModel, state, position, yawRadians, scale, tint, false, false);
}

void CharacterModel_Draw(const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint)
{
    ModularInstance *globalInst = Wardrobe_GetGlobalModularInstance();
    if (globalInst && globalInst->active)
    {
        globalInst->position = position;
        globalInst->yaw = yawRadians;
        globalInst->scale = scale;
        globalInst->tint = tint;
        if (state)
            globalInst->animState = *state;
        ModularInstance_UpdateCloth(globalInst, GetFrameTime());
        ModularInstance_Draw(globalInst);
        return;
    }
    CharacterModel_DrawCustom(s_model, state, position, yawRadians, scale, tint);
}

Model CharacterModel_GetModel(void)
{
    return s_model;
}

MeshAdjacency *CharacterModel_GetAdjacency(void)
{
    if (!s_charAdjacencyBuilt && s_loaded)
    {
        if (s_model.meshCount > 0)
        {
            MeshAdjacency_Build(&s_charAdjacency, s_model.meshes[0]);
            s_charAdjacencyBuilt = true;
        }
    }
    return s_charAdjacencyBuilt ? &s_charAdjacency : NULL;
}

bool CharacterModel_SampleSurfacePoint(Vector3 position, float yawRadians, float scale,
                                       Vector3 *outWorldPos, Vector3 *outWorldNormal)
{
    if (!s_loaded || s_model.meshCount == 0)
        return false;

    int meshIdx = (s_model.meshCount == 1) ? 0 : GetRandomValue(0, s_model.meshCount - 1);
    Mesh mesh = s_model.meshes[meshIdx];
    if (mesh.vertexCount <= 0)
        return false;

    const float *vBuf = (mesh.animVertices != NULL) ? mesh.animVertices : mesh.vertices;
    const float *nBuf = (mesh.animNormals != NULL) ? mesh.animNormals : mesh.normals;
    if (!vBuf)
        return false;

    Vector3 localPos = {0};
    Vector3 localNorm = (Vector3){0.0f, 1.0f, 0.0f};

    if (mesh.indices != NULL && mesh.triangleCount > 0)
    {
        int tri = GetRandomValue(0, mesh.triangleCount - 1);
        int i0 = mesh.indices[tri * 3 + 0];
        int i1 = mesh.indices[tri * 3 + 1];
        int i2 = mesh.indices[tri * 3 + 2];

        float r1 = (float)GetRandomValue(0, 10000) / 10000.0f;
        float r2 = (float)GetRandomValue(0, 10000) / 10000.0f;
        if (r1 + r2 > 1.0f)
        {
            r1 = 1.0f - r1;
            r2 = 1.0f - r2;
        }
        float r3 = 1.0f - r1 - r2;

        localPos.x = r1 * vBuf[i0 * 3 + 0] + r2 * vBuf[i1 * 3 + 0] + r3 * vBuf[i2 * 3 + 0];
        localPos.y = r1 * vBuf[i0 * 3 + 1] + r2 * vBuf[i1 * 3 + 1] + r3 * vBuf[i2 * 3 + 1];
        localPos.z = r1 * vBuf[i0 * 3 + 2] + r2 * vBuf[i1 * 3 + 2] + r3 * vBuf[i2 * 3 + 2];

        if (nBuf)
        {
            localNorm.x = r1 * nBuf[i0 * 3 + 0] + r2 * nBuf[i1 * 3 + 0] + r3 * nBuf[i2 * 3 + 0];
            localNorm.y = r1 * nBuf[i0 * 3 + 1] + r2 * nBuf[i1 * 3 + 1] + r3 * nBuf[i2 * 3 + 1];
            localNorm.z = r1 * nBuf[i0 * 3 + 2] + r2 * nBuf[i1 * 3 + 2] + r3 * nBuf[i2 * 3 + 2];
            float nLenSq = localNorm.x * localNorm.x + localNorm.y * localNorm.y + localNorm.z * localNorm.z;
            if (nLenSq > 1e-6f)
            {
                float invN = 1.0f / sqrtf(nLenSq);
                localNorm.x *= invN;
                localNorm.y *= invN;
                localNorm.z *= invN;
            }
        }
    }
    else
    {
        int vIdx = GetRandomValue(0, mesh.vertexCount - 1);
        localPos.x = vBuf[vIdx * 3 + 0];
        localPos.y = vBuf[vIdx * 3 + 1];
        localPos.z = vBuf[vIdx * 3 + 2];

        if (nBuf)
        {
            localNorm.x = nBuf[vIdx * 3 + 0];
            localNorm.y = nBuf[vIdx * 3 + 1];
            localNorm.z = nBuf[vIdx * 3 + 2];
        }
    }

    float c = cosf(yawRadians);
    float s = sinf(yawRadians);

    Vector3 scaledPos = (Vector3){localPos.x * scale, localPos.y * scale, localPos.z * scale};
    Vector3 rotatedPos = (Vector3){
        scaledPos.x * c + scaledPos.z * s,
        scaledPos.y,
        -scaledPos.x * s + scaledPos.z * c};

    if (outWorldPos)
    {
        *outWorldPos = Vector3Add(rotatedPos, position);
    }

    if (outWorldNormal)
    {
        Vector3 rotatedNorm = (Vector3){
            localNorm.x * c + localNorm.z * s,
            localNorm.y,
            -localNorm.x * s + localNorm.z * c};
        float rnLenSq = rotatedNorm.x * rotatedNorm.x + rotatedNorm.y * rotatedNorm.y + rotatedNorm.z * rotatedNorm.z;
        if (rnLenSq > 1e-6f)
        {
            float invLen = 1.0f / sqrtf(rnLenSq);
            rotatedNorm.x *= invLen;
            rotatedNorm.y *= invLen;
            rotatedNorm.z *= invLen;
        }
        else
        {
            rotatedNorm = (Vector3){0.0f, 1.0f, 0.0f};
        }
        *outWorldNormal = rotatedNorm;
    }

    return true;
}

bool CharacterModel_GetBoneTransform(const char *boneName, const CharacterAnimState *state, Matrix *outBoneMatrix)
{
    if (!s_loaded || !boneName || !outBoneMatrix || s_model.skeleton.boneCount <= 0)
        return false;

    int boneIdx = -1;
    for (int i = 0; i < s_model.skeleton.boneCount; i++)
    {
        if (strcmp(s_model.skeleton.bones[i].name, boneName) == 0)
        {
            boneIdx = i;
            break;
        }
    }
    if (boneIdx < 0)
        return false;

    int animIdx = (state) ? state->activeAnimIndex : -1;
    int frame = (state) ? state->frame : 0;
    bool hasAnim = (animIdx >= 0 && animIdx < s_animCount && s_animations[animIdx].keyframePoses != NULL);

    Transform t;
    int animBoneIdx = hasAnim ? FindAnimBoneIndexByName(s_animations[animIdx], boneName, boneIdx) : -1;
    if (hasAnim && animBoneIdx >= 0 && frame >= 0 && frame < s_animations[animIdx].keyframeCount)
    {
        int nextFrame = (frame + 1) % s_animations[animIdx].keyframeCount;
        float blend = (state) ? state->frameTimer : 0.0f;
        if (blend < 0.0f)
            blend = 0.0f;
        if (blend > 1.0f)
            blend = 1.0f;

        Transform t0 = s_animations[animIdx].keyframePoses[frame][animBoneIdx];
        Transform t1 = s_animations[animIdx].keyframePoses[nextFrame][animBoneIdx];
        t.translation = Vector3Lerp(t0.translation, t1.translation, blend);
        t.rotation = QuaternionSlerp(t0.rotation, t1.rotation, blend);
        t.scale = Vector3Lerp(t0.scale, t1.scale, blend);
    }
    else if (s_model.currentPose != NULL)
    {
        t = s_model.currentPose[boneIdx];
    }
    else if (s_model.skeleton.bindPose != NULL)
    {
        t = s_model.skeleton.bindPose[boneIdx];
    }
    else
    {
        return false;
    }

    Matrix localTrans = MatrixTranslate(t.translation.x, t.translation.y, t.translation.z);
    Matrix localRot = QuaternionToMatrix(t.rotation);
    Matrix localScale = MatrixScale(t.scale.x, t.scale.y, t.scale.z);
    Matrix mat = MatrixMultiply(MatrixMultiply(localScale, localRot), localTrans);

    *outBoneMatrix = mat;
    return true;
}