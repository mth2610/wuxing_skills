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
}

bool ModularSystem_GetSocketMatrix(Vector3 charPos, float charYaw, float charScale,
                                   ModularSlot slot, Matrix localOffset, Matrix *outMatrix) {
    if (!outMatrix) return false;

    // Standard default socket anchor offsets relative to humanoid rest center
    Vector3 anchorPos = {0.0f, 0.0f, 0.0f};
    switch (slot) {
        case MODULAR_SLOT_HEADGEAR:
            anchorPos = (Vector3){ 0.0f, 1.72f * charScale, -0.02f * charScale };
            break;
        case MODULAR_SLOT_MASK:
            anchorPos = (Vector3){ 0.0f, 1.68f * charScale, 0.08f * charScale };
            break;
        case MODULAR_SLOT_HAIR:
            anchorPos = (Vector3){ 0.0f, 1.72f * charScale, 0.0f };
            break;
        case MODULAR_SLOT_WAIST:
            anchorPos = (Vector3){ 0.14f * charScale, 0.98f * charScale, 0.08f * charScale };
            break;
        case MODULAR_SLOT_WEAPON_R:
            anchorPos = (Vector3){ 0.35f * charScale, 0.95f * charScale, 0.15f * charScale };
            break;
        case MODULAR_SLOT_WEAPON_L:
            anchorPos = (Vector3){ -0.35f * charScale, 0.95f * charScale, 0.15f * charScale };
            break;
        case MODULAR_SLOT_BACK:
            anchorPos = (Vector3){ 0.0f, 1.35f * charScale, -0.15f * charScale };
            break;
        default:
            anchorPos = (Vector3){ 0.0f, 0.0f, 0.0f };
            break;
    }

    Matrix rotY = MatrixRotateY(charYaw);
    Vector3 rotatedAnchor = Vector3Transform(anchorPos, rotY);
    Vector3 finalPos = Vector3Add(charPos, rotatedAnchor);

    Matrix mat = MatrixMultiply(MatrixScale(charScale, charScale, charScale), localOffset);
    mat = MatrixMultiply(mat, rotY);
    mat = MatrixMultiply(mat, MatrixTranslate(finalPos.x, finalPos.y, finalPos.z));

    *outMatrix = mat;
    return true;
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

    // 1. Draw base body with CPU skinning (unless masked to eliminate clipping)
    if (!inst->hideBaseBody) {
        CharacterModel_DrawCustom(masterModel, &inst->animState, inst->position, inst->yaw, inst->scale, inst->tint);
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
                ModularSystem_GetSocketMatrix(inst->position, inst->yaw, inst->scale,
                                              (ModularSlot)s, item->localOffset, &finalMat);

                for (int m = 0; m < item->model.meshCount; m++) {
                    DrawMesh(item->model.meshes[m], item->model.materials[m], finalMat);
                }
            }
        }
    }
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
