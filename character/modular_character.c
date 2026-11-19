// character/modular_character.c
#include "character/modular_character.h"
#include "core/resource_manager.h"
#include "core/surface_material.h"
#include "environment/wind/wind_system.h"
#include "raymath.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static ModularItemDef s_itemRegistry[MODULAR_MAX_ITEMS];
static int s_itemCount = 0;
static bool s_systemInitialized = false;

#define MODULAR_MAX_UNIQUE_INSTANCES 16
typedef struct {
    const ModularInstance *owner;
    int    slotSourceIdx[MODULAR_SLOT_COUNT];
    Model  slotClones[MODULAR_SLOT_COUNT];
    bool   hasBaseBodyFallback;
    Model  baseBodyFallback;
} ModularInstanceVboCache;

static ModularInstanceVboCache s_vboCache[MODULAR_MAX_UNIQUE_INSTANCES];

static void ModularVboCache_ClearAll(void)
{
    for (int i = 0; i < MODULAR_MAX_UNIQUE_INSTANCES; i++)
    {
        for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
        {
            if (s_vboCache[i].slotClones[s].meshCount > 0)
                CharacterModel_UnloadSkinnedClone(&s_vboCache[i].slotClones[s]);
            s_vboCache[i].slotSourceIdx[s] = -1;
        }
        if (s_vboCache[i].hasBaseBodyFallback)
        {
            CharacterModel_UnloadSkinnedClone(&s_vboCache[i].baseBodyFallback);
            s_vboCache[i].hasBaseBodyFallback = false;
        }
        s_vboCache[i].owner = NULL;
    }
}

static Model ModularVboCache_GetSlotModel(const ModularInstance *inst, int slot, int itemIdx, Model srcModel)
{
    if (!inst || !inst->useUniqueGpuBuffers || srcModel.meshCount <= 0)
        return srcModel;

    int slotIdx = -1;
    for (int i = 0; i < MODULAR_MAX_UNIQUE_INSTANCES; i++)
    {
        if (s_vboCache[i].owner == inst) { slotIdx = i; break; }
        if (s_vboCache[i].owner == NULL && slotIdx < 0) slotIdx = i;
    }
    if (slotIdx < 0)
        return srcModel;

    ModularInstanceVboCache *c = &s_vboCache[slotIdx];
    if (c->owner != inst)
    {
        c->owner = inst;
        for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
            c->slotSourceIdx[s] = -1;
        c->hasBaseBodyFallback = false;
    }

    if (slot < 0)
    {
        if (!c->hasBaseBodyFallback)
        {
            c->baseBodyFallback = CharacterModel_CloneSkinnedModel(srcModel);
            c->hasBaseBodyFallback = (c->baseBodyFallback.meshCount > 0);
        }
        return c->hasBaseBodyFallback ? c->baseBodyFallback : srcModel;
    }

    if (c->slotSourceIdx[slot] != itemIdx || c->slotClones[slot].meshCount <= 0)
    {
        if (c->slotClones[slot].meshCount > 0)
            CharacterModel_UnloadSkinnedClone(&c->slotClones[slot]);
        c->slotClones[slot] = CharacterModel_CloneSkinnedModel(srcModel);
        c->slotSourceIdx[slot] = itemIdx;
    }
    return (c->slotClones[slot].meshCount > 0) ? c->slotClones[slot] : srcModel;
}

void ModularSystem_Init(void)
{
    if (s_systemInitialized)
        return;
    s_itemCount = 0;
    memset(s_itemRegistry, 0, sizeof(s_itemRegistry));
    s_systemInitialized = true;
    TraceLog(LOG_INFO, "MODULAR_SYSTEM: Initialized modular character management system.");
}

void ModularSystem_Shutdown(void)
{
    ModularVboCache_ClearAll();
    s_itemCount = 0;
    memset(s_itemRegistry, 0, sizeof(s_itemRegistry));
    s_systemInitialized = false;
    TraceLog(LOG_INFO, "MODULAR_SYSTEM: Shutdown modular system.");
}

int ModularSystem_RegisterItem(const char *id, ModularSlot slot, bool isSkinned,
                               const char *boneSocket, Vector3 offsetPos, Vector3 offsetRotEuler,
                               const char *modelGlbPath)
{
    if (!id || !id[0] || !modelGlbPath || s_itemCount >= MODULAR_MAX_ITEMS)
        return -1;

    int existing = ModularSystem_FindItem(id);
    if (existing >= 0)
        return existing;

    int idx = s_itemCount++;
    ModularItemDef *item = &s_itemRegistry[idx];
    memset(item, 0, sizeof(ModularItemDef));
    strncpy(item->id, id, sizeof(item->id) - 1);
    item->slot = slot;
    item->isSkinned = isSkinned;
    if (boneSocket && boneSocket[0] != '\0')
        strncpy(item->boneSocket, boneSocket, sizeof(item->boneSocket) - 1);

    // Compute local socket offset matrix (row-vector convention: rotate then translate)
    Matrix rotX = MatrixRotateX(offsetRotEuler.x * DEG2RAD);
    Matrix rotY = MatrixRotateY(offsetRotEuler.y * DEG2RAD);
    Matrix rotZ = MatrixRotateZ(offsetRotEuler.z * DEG2RAD);
    Matrix rot = MatrixMultiply(MatrixMultiply(rotZ, rotY), rotX);
    Matrix trans = MatrixTranslate(offsetPos.x, offsetPos.y, offsetPos.z);
    item->localOffset = MatrixMultiply(rot, trans);

    // Load 3D model if path is valid (do not gate with FileExists: fails on Android APK assets)
    if (modelGlbPath[0] != '\0')
    {
        item->model = ResourceManager_LoadModel(modelGlbPath);
        if (item->model.meshCount > 0)
        {
            SurfaceMaterial_Apply(&item->model);
            item->loaded = true;
            TraceLog(LOG_INFO, "MODULAR_SYSTEM: Registered item [%2d] '%s' (slot %d, %s, %d meshes)",
                     idx, id, (int)slot, isSkinned ? "SKINNED" : "SOCKET", item->model.meshCount);
        }
        else
        {
            item->loaded = false;
            TraceLog(LOG_WARNING, "MODULAR_SYSTEM: Failed to load model for item '%s' from '%s'", id, modelGlbPath);
        }
    }
    else
    {
        item->model = (Model){0};
        item->loaded = false;
    }

    return idx;
}

int ModularSystem_FindItem(const char *id)
{
    if (!id || !id[0])
        return -1;
    char c0 = id[0];
    for (int i = 0; i < s_itemCount; i++)
    {
        if (s_itemRegistry[i].id[0] == c0 && strcmp(s_itemRegistry[i].id, id) == 0)
            return i;
    }
    return -1;
}

const ModularItemDef *ModularSystem_GetItem(int index)
{
    if (index < 0 || index >= s_itemCount)
        return NULL;
    return &s_itemRegistry[index];
}

void ModularInstance_Init(ModularInstance *inst, ModularTier tier)
{
    if (!inst)
        return;
    memset(inst, 0, sizeof(ModularInstance));
    inst->tier = tier;
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
    {
        inst->loadout.itemIndices[s] = -1;
    }
    CharacterModel_ResetState(&inst->animState);
    SpringBone_Init(&inst->springBones);
    WeaponTrail_Init(&inst->weaponTrail);
    inst->scale = 1.0f;
    inst->tint = WHITE;
    inst->lastClothStepTime = -1.0;
    inst->active = true;
}

static float s_windClock = 0.0f;

static Vector3 ModularInstance_SampleWind(Vector3 worldPos, void *userData)
{
    (void)userData;
    return Wind_EvaluateVelocity(worldPos, s_windClock);
}

static int RegisterSlotClothIfActive(ModularInstance *inst, ModularSlot slot, int *garmentCount)
{
    int idx = inst->loadout.itemIndices[slot];
    if (idx < 0 || idx >= s_itemCount)
        return 0;
    ModularItemDef *item = &s_itemRegistry[idx];
    if (!item->loaded || !item->isSkinned || item->model.meshCount <= 0)
        return 0;

    int reg = SpringBone_RegisterModelCloth(&inst->springBones, &item->model);
    if (reg > 0 && garmentCount)
        (*garmentCount)++;
    return reg;
}

// Rebuilds the secondary-motion rig from active worn garments only (ignoring
// base body and slots suppressed by a full outfit).
void ModularInstance_RebuildCloth(ModularInstance *inst)
{
    if (!inst)
        return;

    SpringBone_Init(&inst->springBones);

    int registered = 0;
    int garments = 0;
    int fullOutfitIdx = inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT];
    bool hasFullOutfit = (fullOutfitIdx >= 0 && fullOutfitIdx < s_itemCount && s_itemRegistry[fullOutfitIdx].loaded);

    // 1. Register Full Outfit first so its primary cloth chain dimensions take priority
    if (hasFullOutfit)
    {
        registered += RegisterSlotClothIfActive(inst, MODULAR_SLOT_FULL_OUTFIT, &garments);
    }

    // 2. Register remaining visible skinned slots
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
    {
        if (s == MODULAR_SLOT_BASE_BODY || s == MODULAR_SLOT_FULL_OUTFIT)
            continue;
        if (hasFullOutfit && (inst->occupiedSlotsMask & (1 << s)))
            continue;
        registered += RegisterSlotClothIfActive(inst, (ModularSlot)s, &garments);
    }

    if (registered == 0)
    {
        SpringBone_SetupWuxiaDefaults(&inst->springBones);
    }
    else
    {
        TraceLog(LOG_INFO, "MODULAR: Cloth rig rebuilt with %d simulated segments from %d garment(s).",
                 registered, garments);
    }
}

bool ModularInstance_Equip(ModularInstance *inst, ModularSlot slot, const char *itemId)
{
    if (!inst || slot < 0 || slot >= MODULAR_SLOT_COUNT)
        return false;
    int idx = ModularSystem_FindItem(itemId);
    return ModularInstance_EquipIndex(inst, slot, idx);
}

bool ModularInstance_EquipIndex(ModularInstance *inst, ModularSlot slot, int itemIndex)
{
    if (!inst || slot < 0 || slot >= MODULAR_SLOT_COUNT)
        return false;
    if (itemIndex >= 0 && itemIndex < s_itemCount)
    {
        if (inst->loadout.itemIndices[slot] == itemIndex)
            return true;
        inst->loadout.itemIndices[slot] = itemIndex;
        ModularInstance_RebuildCloth(inst);
        return true;
    }
    else if (itemIndex == -1)
    {
        if (inst->loadout.itemIndices[slot] == -1)
            return true;
        inst->loadout.itemIndices[slot] = -1;
        ModularInstance_RebuildCloth(inst);
        return true;
    }
    return false;
}

void ModularInstance_Update(ModularInstance *inst, float dt, bool isMoving)
{
    if (!inst || !inst->active)
        return;
    CharacterModel_Update(&inst->animState, dt, isMoving);
    ModularInstance_UpdateCloth(inst, dt);
}

void ModularInstance_UpdateCloth(ModularInstance *inst, float dt)
{
    if (!inst || !inst->active)
        return;
    if (dt <= 1.0e-5f)
        return;

    // Prevent double-stepping in the same frame when both Update() and Draw() run
    double now = GetTime();
    if (inst->lastClothStepTime >= 0.0 && (now - inst->lastClothStepTime) < 0.0001)
        return;
    inst->lastClothStepTime = now;

    // Re-seat cloth chains on teleport (> 2.0m jump; squared threshold = 4.0)
    if (inst->springBones.initialized &&
        Vector3DistanceSqr(inst->position, inst->springBones.prevCharPos) > 4.0f)
    {
        SpringBone_Reset(&inst->springBones);
    }

    float safeScale = (inst->scale > 0.0001f) ? inst->scale : 1.0f;
    s_windClock = (float)now;

    SpringBone_UpdateWithWind(&inst->springBones, &inst->animState, inst->position, inst->yaw,
                              safeScale, dt, ModularInstance_SampleWind, NULL);
}

static inline Matrix ComposeModelToWorldMatrix(Vector3 charPos, float charYaw, float charScale)
{
    float c = cosf(charYaw) * charScale;
    float s = sinf(charYaw) * charScale;
    return (Matrix){
        .m0 = c, .m4 = 0.0f, .m8 = s, .m12 = charPos.x, .m1 = 0.0f, .m5 = charScale, .m9 = 0.0f, .m13 = charPos.y, .m2 = -s, .m6 = 0.0f, .m10 = c, .m14 = charPos.z, .m3 = 0.0f, .m7 = 0.0f, .m11 = 0.0f, .m15 = 1.0f};
}

static Matrix ComputeSocketModelMatrix(ModularSlot slot, const char *boneSocket,
                                       const CharacterAnimState *animState, Matrix localOffset)
{
    Matrix socketModelMat = MatrixIdentity();
    bool trackedBone = false;

    if (boneSocket && boneSocket[0] != '\0')
    {
        trackedBone = CharacterModel_GetBoneTransform(boneSocket, animState, &socketModelMat);
    }

    if (!trackedBone)
    {
        Vector3 anchorPos = {0.0f, 0.0f, 0.0f};
        switch (slot)
        {
        case MODULAR_SLOT_HEADGEAR:
            anchorPos = (Vector3){0.0f, 1.74f, 0.0f};
            break;
        case MODULAR_SLOT_MASK:
            anchorPos = (Vector3){0.0f, 1.68f, 0.08f};
            break;
        case MODULAR_SLOT_HAIR:
            anchorPos = (Vector3){0.0f, 1.72f, 0.0f};
            break;
        case MODULAR_SLOT_WAIST:
            anchorPos = (Vector3){0.12f, 0.95f, 0.08f};
            break;
        case MODULAR_SLOT_WEAPON_R:
            anchorPos = (Vector3){0.24f, 0.85f, 0.10f};
            break;
        case MODULAR_SLOT_WEAPON_L:
            anchorPos = (Vector3){-0.24f, 0.85f, 0.10f};
            break;
        case MODULAR_SLOT_BACK:
            anchorPos = (Vector3){0.0f, 1.25f, -0.16f};
            break;
        default:
            anchorPos = (Vector3){0.0f, 0.0f, 0.0f};
            break;
        }
        socketModelMat = MatrixTranslate(anchorPos.x, anchorPos.y, anchorPos.z);
    }

    return MatrixMultiply(localOffset, socketModelMat);
}

bool ModularSystem_GetSocketMatrixEx(Vector3 charPos, float charYaw, float charScale,
                                     ModularSlot slot, const char *boneSocket,
                                     const CharacterAnimState *animState,
                                     Matrix localOffset, Matrix *outMatrix)
{
    if (!outMatrix)
        return false;

    Matrix localToModel = ComputeSocketModelMatrix(slot, boneSocket, animState, localOffset);
    Matrix modelToWorld = ComposeModelToWorldMatrix(charPos, charYaw, charScale);
    *outMatrix = MatrixMultiply(localToModel, modelToWorld);
    return true;
}

bool ModularSystem_GetSocketMatrix(Vector3 charPos, float charYaw, float charScale,
                                   ModularSlot slot, Matrix localOffset, Matrix *outMatrix)
{
    return ModularSystem_GetSocketMatrixEx(charPos, charYaw, charScale, slot, NULL, NULL, localOffset, outMatrix);
}

#include "rlgl.h"

extern bool EnvShadow_IsCapturing(void);
extern Shader EnvShadow_GetDepthShader(void);

void ModularInstance_Draw(ModularInstance *inst)
{
    if (!inst || !inst->active)
        return;

    const bool isShadow = EnvShadow_IsCapturing();
    const Shader depthShader = isShadow ? EnvShadow_GetDepthShader() : (Shader){0};

    static int s_clothEnabled = -1;
    if (s_clothEnabled < 0)
    {
        const char *env = getenv("WUXING_CLOTH");
        s_clothEnabled = (env && env[0] == '0') ? 0 : 1;
    }
    const SpringBoneSystem *cloth = s_clothEnabled ? &inst->springBones : NULL;

    // 1. Draw base body with CPU skinning (only validate masterModel when body is visible)
    if (!inst->hideBaseBody)
    {
        int bodyIdx = inst->loadout.itemIndices[MODULAR_SLOT_BASE_BODY];
        Model masterModel;
        if (bodyIdx >= 0 && bodyIdx < s_itemCount && s_itemRegistry[bodyIdx].loaded)
        {
            masterModel = ModularVboCache_GetSlotModel(inst, MODULAR_SLOT_BASE_BODY, bodyIdx, s_itemRegistry[bodyIdx].model);
        }
        else
        {
            masterModel = ModularVboCache_GetSlotModel(inst, -1, -1, CharacterModel_GetModel());
        }

        if (masterModel.meshCount > 0 && masterModel.meshes != NULL)
        {
            bool hasTorso = (inst->loadout.itemIndices[MODULAR_SLOT_TORSO] >= 0) ||
                            (inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT] >= 0);
            bool hasLegs = (inst->loadout.itemIndices[MODULAR_SLOT_LEGS] >= 0) ||
                           (inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT] >= 0) ||
                           hasTorso;
            CharacterModel_DrawCustomEx(masterModel, &inst->animState, inst->position, inst->yaw, inst->scale, inst->tint, hasTorso, hasLegs);
        }
    }

    // 2. Full Outfit pass (if equipped)
    int fullOutfitIdx = inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT];
    bool hasFullOutfit = (fullOutfitIdx >= 0 && fullOutfitIdx < s_itemCount && s_itemRegistry[fullOutfitIdx].loaded);
    if (hasFullOutfit)
    {
        ModularItemDef *foItem = &s_itemRegistry[fullOutfitIdx];
        if (foItem->model.meshCount > 0 && foItem->model.meshes != NULL)
        {
            Model foModel = ModularVboCache_GetSlotModel(inst, MODULAR_SLOT_FULL_OUTFIT, fullOutfitIdx, foItem->model);
            CharacterModel_DrawCustomCloth(foModel, &inst->animState,
                                           inst->position, inst->yaw, inst->scale, inst->tint,
                                           cloth);
        }
    }

    float dt = GetFrameTime();
    if (dt > 0.05f)
        dt = 0.05f;

    Matrix modelToWorld = ComposeModelToWorldMatrix(inst->position, inst->yaw, inst->scale);

    // 3. Draw remaining equipped items (skipping slots suppressed by full outfit)
    bool weaponSampled = false;
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
    {
        if (s == MODULAR_SLOT_BASE_BODY || s == MODULAR_SLOT_FULL_OUTFIT)
            continue;
        if (hasFullOutfit && (inst->occupiedSlotsMask & (1 << s)))
            continue;

        int itemIdx = inst->loadout.itemIndices[s];
        if (itemIdx >= 0 && itemIdx < s_itemCount && s_itemRegistry[itemIdx].loaded)
        {
            ModularItemDef *item = &s_itemRegistry[itemIdx];
            if (item->model.meshCount <= 0 || item->model.meshes == NULL)
                continue;

            if (item->isSkinned)
            {
                bool deflateUnderRobe = (s == MODULAR_SLOT_LEGS) &&
                                        ((inst->loadout.itemIndices[MODULAR_SLOT_TORSO] >= 0) || hasFullOutfit);
                Model drawModel = ModularVboCache_GetSlotModel(inst, s, itemIdx, item->model);
                CharacterModel_DrawCustomClothEx(drawModel, &inst->animState,
                                                 inst->position, inst->yaw, inst->scale, inst->tint,
                                                 false, deflateUnderRobe, cloth);
            }
            else
            {
                Matrix localToModel = ComputeSocketModelMatrix((ModularSlot)s, item->boneSocket,
                                                               &inst->animState, item->localOffset);
                Matrix finalMat = MatrixMultiply(localToModel, modelToWorld);

                if (isShadow)
                {
                    rlDisableBackfaceCulling();
                    for (int m = 0; m < item->model.meshCount; m++)
                    {
                        int matIdx = (item->model.meshMaterial && m < item->model.meshCount) ? item->model.meshMaterial[m] : 0;
                        if (matIdx < 0 || matIdx >= item->model.materialCount)
                            matIdx = 0;
                        Material shadowMat = item->model.materials[matIdx];
                        shadowMat.shader = depthShader;
                        DrawMesh(item->model.meshes[m], shadowMat, finalMat);
                    }
                    rlEnableBackfaceCulling();
                }
                else
                {
                    for (int m = 0; m < item->model.meshCount; m++)
                    {
                        DrawMesh(item->model.meshes[m], item->model.materials[m], finalMat);
                    }
                }

                if (!isShadow && s == MODULAR_SLOT_WEAPON_R && dt > 1.0e-5f)
                {
                    bool isAttackAnim = (inst->animState.currentSlot == CHAR_ANIM_SWORD_SLASH ||
                                         inst->animState.currentSlot == CHAR_ANIM_SWORD_THRUST ||
                                         inst->animState.currentSlot == CHAR_ANIM_SWORD_PARRY ||
                                         inst->animState.currentSlot == CHAR_ANIM_SWORD_BLOCK);
                    Vector3 bladeBase = (Vector3){0.0f, 0.10f, 0.0f};
                    Vector3 bladeTip = (Vector3){0.0f, 0.85f, 0.0f};
                    WeaponTrail_Sample(&inst->weaponTrail, finalMat, bladeBase, bladeTip, dt, isAttackAnim);
                    weaponSampled = true;
                }
            }
        }
    }

    // 4. Fade and draw weapon trail in emission pass (never during shadow capture)
    if (!isShadow)
    {
        if (!weaponSampled && dt > 1.0e-5f)
        {
            WeaponTrail_Fade(&inst->weaponTrail, dt);
        }
        WeaponTrail_Draw(&inst->weaponTrail);
    }
}

void ModularInstance_DrawWithState(ModularInstance *inst, const CharacterAnimState *state,
                                   Vector3 position, float yawRadians, float scale, Color tint)
{
    if (!inst || !inst->active)
        return;
    inst->position = position;
    inst->yaw = yawRadians;
    inst->scale = scale;
    inst->tint = tint;
    if (state)
        inst->animState = *state;
    ModularInstance_UpdateCloth(inst, GetFrameTime());
    ModularInstance_Draw(inst);
}

void ModularSystem_DrawMinionBatch(int baseBodyItemIndex, int weaponItemIndex,
                                   const CharacterAnimState *sharedAnimState,
                                   const ModularMinionBatchItem *items, int itemCount)
{
    if (baseBodyItemIndex < 0 || baseBodyItemIndex >= s_itemCount || !items || itemCount <= 0)
        return;
    ModularItemDef *bodyDef = &s_itemRegistry[baseBodyItemIndex];
    if (!bodyDef->loaded || bodyDef->model.meshCount == 0)
        return;

    // 1. Evaluate skeletal CPU skinning once for the entire squad
    if (sharedAnimState && CharacterModel_IsLoaded())
    {
        int animIdx = sharedAnimState->activeAnimIndex;
        if (animIdx >= 0 && animIdx < CharacterModel_GetTotalAnimCount())
        {
            CharacterModel_DrawCustom(bodyDef->model, sharedAnimState,
                                      items[0].position, items[0].yaw, items[0].scale, items[0].tint);
        }
    }

    // 2. Render all remaining minions using the already-deformed vertex buffer
    for (int i = 1; i < itemCount; i++)
    {
        float yawDeg = items[i].yaw * RAD2DEG;
        DrawModelEx(bodyDef->model, items[i].position, (Vector3){0.0f, 1.0f, 0.0f}, yawDeg,
                    (Vector3){items[i].scale, items[i].scale, items[i].scale}, items[i].tint);
    }

    // 3. Render socketed weapons tracked to the shared hand bone pose
    if (weaponItemIndex >= 0 && weaponItemIndex < s_itemCount)
    {
        ModularItemDef *wepDef = &s_itemRegistry[weaponItemIndex];
        if (wepDef->loaded && wepDef->model.meshCount > 0)
        {
            Matrix localToModel = ComputeSocketModelMatrix(MODULAR_SLOT_WEAPON_R, wepDef->boneSocket,
                                                           sharedAnimState, wepDef->localOffset);
            for (int i = 0; i < itemCount; i++)
            {
                Matrix modelToWorld = ComposeModelToWorldMatrix(items[i].position, items[i].yaw, items[i].scale);
                Matrix wepWorld = MatrixMultiply(localToModel, modelToWorld);

                for (int m = 0; m < wepDef->model.meshCount; m++)
                {
                    DrawMesh(wepDef->model.meshes[m], wepDef->model.materials[m], wepWorld);
                }
            }
        }
    }
}