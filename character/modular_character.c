// character/modular_character.c
#include "character/modular_character.h"
#include "core/resource_manager.h"
#include "core/surface_material.h"
#include "raymath.h"
#include <string.h>
#include <stdio.h>

static ModularItemDef s_itemRegistry[MODULAR_MAX_ITEMS];
static int            s_itemCount = 0;
static bool           s_systemInitialized = false;

void ModularSystem_Init(void) {
    if (s_systemInitialized) return;
    s_itemCount = 0;
    memset(s_itemRegistry, 0, sizeof(s_itemRegistry));
    s_systemInitialized = true;
    TraceLog(LOG_INFO, "MODULAR_SYSTEM: Initialized modular character management system.");
}

void ModularSystem_Shutdown(void) {
    s_itemCount = 0;
    s_systemInitialized = false;
    TraceLog(LOG_INFO, "MODULAR_SYSTEM: Shutdown modular system.");
}

int ModularSystem_RegisterItem(const char *id, ModularSlot slot, bool isSkinned,
                               const char *boneSocket, Vector3 offsetPos, Vector3 offsetRotEuler,
                               const char *modelGlbPath) {
    if (!id || !modelGlbPath || s_itemCount >= MODULAR_MAX_ITEMS) return -1;

    // Check if ID already exists
    int existing = ModularSystem_FindItem(id);
    if (existing >= 0) return existing;

    int idx = s_itemCount++;
    ModularItemDef *item = &s_itemRegistry[idx];
    strncpy(item->id, id, sizeof(item->id) - 1);
    item->slot = slot;
    item->isSkinned = isSkinned;
    if (boneSocket) strncpy(item->boneSocket, boneSocket, sizeof(item->boneSocket) - 1);

    // Compute local socket offset matrix
    Matrix rotX = MatrixRotateX(offsetRotEuler.x * DEG2RAD);
    Matrix rotY = MatrixRotateY(offsetRotEuler.y * DEG2RAD);
    Matrix rotZ = MatrixRotateZ(offsetRotEuler.z * DEG2RAD);
    Matrix rot = MatrixMultiply(MatrixMultiply(rotZ, rotY), rotX);
    Matrix trans = MatrixTranslate(offsetPos.x, offsetPos.y, offsetPos.z);
    item->localOffset = MatrixMultiply(rot, trans);

    // Load 3D model if path is valid
    if (modelGlbPath[0] != '\0' && FileExists(modelGlbPath)) {
        item->model = ResourceManager_LoadModel(modelGlbPath);
        if (item->model.meshCount > 0) {
            SurfaceMaterial_Apply(&item->model);
            item->loaded = true;
            TraceLog(LOG_INFO, "MODULAR_SYSTEM: Registered item [%2d] '%s' (slot %d, %s, %d meshes)",
                     idx, id, (int)slot, isSkinned ? "SKINNED" : "SOCKET", item->model.meshCount);
        } else {
            item->loaded = false;
            TraceLog(LOG_WARNING, "MODULAR_SYSTEM: Failed to load model for item '%s' from '%s'", id, modelGlbPath);
        }
    } else {
        item->model = (Model){0};
        item->loaded = false;
    }

    return idx;
}

int ModularSystem_FindItem(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < s_itemCount; i++) {
        if (strcmp(s_itemRegistry[i].id, id) == 0) return i;
    }
    return -1;
}

void ModularInstance_Init(ModularInstance *inst, ModularTier tier) {
    if (!inst) return;
    memset(inst, 0, sizeof(ModularInstance));
    inst->tier = tier;
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++) {
        inst->loadout.itemIndices[s] = -1;
    }
    CharacterModel_ResetState(&inst->animState);
    SpringBone_SetupWuxiaDefaults(&inst->springBones);
    WeaponTrail_Init(&inst->weaponTrail);
    inst->scale = 1.0f;
    inst->tint = WHITE;
    inst->active = true;
}

bool ModularInstance_Equip(ModularInstance *inst, ModularSlot slot, const char *itemId) {
    if (!inst || slot < 0 || slot >= MODULAR_SLOT_COUNT) return false;
    int idx = ModularSystem_FindItem(itemId);
    return ModularInstance_EquipIndex(inst, slot, idx);
}

bool ModularInstance_EquipIndex(ModularInstance *inst, ModularSlot slot, int itemIndex) {
    if (!inst || slot < 0 || slot >= MODULAR_SLOT_COUNT) return false;
    if (itemIndex >= 0 && itemIndex < s_itemCount) {
        inst->loadout.itemIndices[slot] = itemIndex;
        return true;
    } else if (itemIndex == -1) {
        inst->loadout.itemIndices[slot] = -1;
        return true;
    }
    return false;
}

void ModularInstance_Update(ModularInstance *inst, float dt, bool isMoving) {
    if (!inst || !inst->active) return;
    CharacterModel_Update(&inst->animState, dt, isMoving);

    // Update dynamic cloth spring bones (skirts, sleeves, ribbons)
    Vector3 windVel = (Vector3){ 0.0f, 0.0f, 0.0f };
    SpringBone_Update(&inst->springBones, &inst->animState, inst->position, inst->yaw, inst->scale, dt, windVel);
}

bool ModularSystem_GetSocketMatrixEx(Vector3 charPos, float charYaw, float charScale,
                                     ModularSlot slot, const char *boneSocket,
                                     const CharacterAnimState *animState,
                                     Matrix localOffset, Matrix *outMatrix) {
    if (!outMatrix) return false;

    Matrix socketModelMat = MatrixIdentity();
    bool trackedBone = false;

    if (boneSocket && boneSocket[0] != '\0') {
        trackedBone = CharacterModel_GetBoneTransform(boneSocket, animState, &socketModelMat);
    }

    if (!trackedBone) {
        // Fallback to calibrated socket anchor relative to character root (1 unit = 1 meter)
        Vector3 anchorPos = {0.0f, 0.0f, 0.0f};
        switch (slot) {
            case MODULAR_SLOT_HEADGEAR:
                anchorPos = (Vector3){ 0.0f, 1.74f, 0.0f };
                break;
            case MODULAR_SLOT_MASK:
                anchorPos = (Vector3){ 0.0f, 1.68f, 0.08f };
                break;
            case MODULAR_SLOT_HAIR:
                anchorPos = (Vector3){ 0.0f, 1.72f, 0.0f };
                break;
            case MODULAR_SLOT_WAIST:
                anchorPos = (Vector3){ 0.12f, 0.95f, 0.08f };
                break;
            case MODULAR_SLOT_WEAPON_R:
                anchorPos = (Vector3){ 0.24f, 0.85f, 0.10f };
                break;
            case MODULAR_SLOT_WEAPON_L:
                anchorPos = (Vector3){ -0.24f, 0.85f, 0.10f };
                break;
            case MODULAR_SLOT_BACK:
                anchorPos = (Vector3){ 0.0f, 1.25f, -0.16f };
                break;
            default:
                anchorPos = (Vector3){ 0.0f, 0.0f, 0.0f };
                break;
        }
        socketModelMat = MatrixTranslate(anchorPos.x, anchorPos.y, anchorPos.z);
    }

    // Proper transform hierarchy:
    // v_world = localOffset * socketModelMat * scale * rotY + charPos
    Matrix mat = MatrixMultiply(localOffset, socketModelMat);
    mat = MatrixMultiply(mat, MatrixScale(charScale, charScale, charScale));
    mat = MatrixMultiply(mat, MatrixRotateY(charYaw));
    mat = MatrixMultiply(mat, MatrixTranslate(charPos.x, charPos.y, charPos.z));

    *outMatrix = mat;
    return true;
}

bool ModularSystem_GetSocketMatrix(Vector3 charPos, float charYaw, float charScale,
                                   ModularSlot slot, Matrix localOffset, Matrix *outMatrix) {
    return ModularSystem_GetSocketMatrixEx(charPos, charYaw, charScale, slot, NULL, NULL, localOffset, outMatrix);
}

void ModularInstance_Draw(ModularInstance *inst) {
    if (!inst || !inst->active) return;

    int bodyIdx = inst->loadout.itemIndices[MODULAR_SLOT_BASE_BODY];
    Model masterModel;
    if (bodyIdx >= 0 && s_itemRegistry[bodyIdx].loaded) {
        masterModel = s_itemRegistry[bodyIdx].model;
    } else {
        masterModel = CharacterModel_GetModel();
    }
    if (masterModel.meshCount <= 0 || masterModel.meshes == NULL) return;

    // 1. Draw base body with CPU skinning (deflating covered parts if clothing is worn)
    if (!inst->hideBaseBody) {
        bool hasTorso = (inst->loadout.itemIndices[MODULAR_SLOT_TORSO] >= 0) ||
                        (inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT] >= 0);
        bool hasLegs = (inst->loadout.itemIndices[MODULAR_SLOT_LEGS] >= 0) ||
                       (inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT] >= 0);
        CharacterModel_DrawCustomEx(masterModel, &inst->animState, inst->position, inst->yaw, inst->scale, inst->tint, hasTorso, hasLegs);
    }

    // 2. Full Outfit pass (if equipped)
    int fullOutfitIdx = inst->loadout.itemIndices[MODULAR_SLOT_FULL_OUTFIT];
    if (fullOutfitIdx >= 0 && s_itemRegistry[fullOutfitIdx].loaded) {
        ModularItemDef *foItem = &s_itemRegistry[fullOutfitIdx];
        if (foItem->model.meshCount > 0 && foItem->model.meshes != NULL) {
            CharacterModel_DrawCustom(foItem->model, &inst->animState,
                                      inst->position, inst->yaw, inst->scale, inst->tint);
        }
    }

    // 3. Draw remaining equipped items (skip base body, full outfit, and any slots suppressed by full outfit)
    bool weaponSampled = false;
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++) {
        if (s == MODULAR_SLOT_BASE_BODY || s == MODULAR_SLOT_FULL_OUTFIT) continue;
        if (fullOutfitIdx >= 0 && (inst->occupiedSlotsMask & (1 << s))) continue; // Suppressed by Full Outfit!

        int itemIdx = inst->loadout.itemIndices[s];
        if (itemIdx >= 0 && s_itemRegistry[itemIdx].loaded) {
            ModularItemDef *item = &s_itemRegistry[itemIdx];
            if (item->model.meshCount <= 0 || item->model.meshes == NULL) continue;

            if (item->isSkinned) {
                CharacterModel_DrawCustom(item->model, &inst->animState,
                                          inst->position, inst->yaw, inst->scale, inst->tint);
            } else {
                Matrix finalMat;
                ModularSystem_GetSocketMatrixEx(inst->position, inst->yaw, inst->scale,
                                                (ModularSlot)s, item->boneSocket, &inst->animState,
                                                item->localOffset, &finalMat);

                for (int m = 0; m < item->model.meshCount; m++) {
                    DrawMesh(item->model.meshes[m], item->model.materials[m], finalMat);
                }

                // If right-hand weapon is a sword/blade, sample blade ribbon trail
                if (s == MODULAR_SLOT_WEAPON_R) {
                    bool isAttackAnim = (inst->animState.currentSlot == CHAR_ANIM_SWORD_SLASH ||
                                         inst->animState.currentSlot == CHAR_ANIM_SWORD_THRUST ||
                                         inst->animState.currentSlot == CHAR_ANIM_SWORD_PARRY ||
                                         inst->animState.currentSlot == CHAR_ANIM_SWORD_BLOCK);
                    Vector3 bladeBase = (Vector3){ 0.0f, 0.10f, 0.0f };
                    Vector3 bladeTip  = (Vector3){ 0.0f, 0.85f, 0.0f };
                    float dt = GetFrameTime();
                    if (dt > 0.05f) dt = 0.05f;
                    WeaponTrail_Sample(&inst->weaponTrail, finalMat, bladeBase, bladeTip, dt, isAttackAnim);
                    weaponSampled = true;
                }
            }
        }
    }

    // 4. Fade and draw weapon trail in emission pass
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;
    if (!weaponSampled) {
        WeaponTrail_Fade(&inst->weaponTrail, dt);
    }
    WeaponTrail_Draw(&inst->weaponTrail);
}

void ModularSystem_DrawMinionBatch(int baseBodyItemIndex, int weaponItemIndex,
                                  const CharacterAnimState *sharedAnimState,
                                  const ModularMinionBatchItem *items, int itemCount) {
    if (baseBodyItemIndex < 0 || baseBodyItemIndex >= s_itemCount || !items || itemCount <= 0) return;
    ModularItemDef *bodyDef = &s_itemRegistry[baseBodyItemIndex];
    if (!bodyDef->loaded || bodyDef->model.meshCount == 0) return;

    // 1. EVALUATE SKELETAL CPU SKINNING EXACTLY ONCE FOR THE ENTIRE SQUAD!
    if (sharedAnimState && CharacterModel_IsLoaded()) {
        int animIdx = sharedAnimState->activeAnimIndex;
        if (animIdx >= 0 && animIdx < CharacterModel_GetTotalAnimCount()) {
            // Evaluates shared vertex buffer once in place
            CharacterModel_DrawCustom(bodyDef->model, sharedAnimState,
                                      items[0].position, items[0].yaw, items[0].scale, items[0].tint);
        }
    }

    // 2. Render all remaining minions in the batch using the already-deformed vertex buffer
    for (int i = 1; i < itemCount; i++) {
        float yawDeg = items[i].yaw * RAD2DEG;
        DrawModelEx(bodyDef->model, items[i].position, (Vector3){0.0f, 1.0f, 0.0f}, yawDeg,
                    (Vector3){items[i].scale, items[i].scale, items[i].scale}, items[i].tint);
    }

    // 3. Render socketed weapons for all minions in batch
    if (weaponItemIndex >= 0 && weaponItemIndex < s_itemCount) {
        ModularItemDef *wepDef = &s_itemRegistry[weaponItemIndex];
        if (wepDef->loaded && wepDef->model.meshCount > 0) {
            for (int i = 0; i < itemCount; i++) {
                Matrix wepWorld;
                ModularSystem_GetSocketMatrix(items[i].position, items[i].yaw, items[i].scale,
                                              MODULAR_SLOT_WEAPON_R, wepDef->localOffset, &wepWorld);

                for (int m = 0; m < wepDef->model.meshCount; m++) {
                    DrawMesh(wepDef->model.meshes[m], wepDef->model.materials[m], wepWorld);
                }
            }
        }
    }
}
