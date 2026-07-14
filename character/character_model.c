#include "character/character_model.h"
#include "core/resource_manager.h"
#include "core/surface_material.h"
#include <string.h>
#include <ctype.h>

// raylib's glTF loader bakes animation keyframes at a fixed ~60fps timestep
// (load log: "punching | Frames: 63 | Duration: 1.033333s" ≈ 61 fps) — NOT
// the source clip's authored fps. Playing back at 30 here made every clip
// run at half speed (walk cycle lagging the actual move speed, punch taking
// ~2s), so this must match raylib's bake rate, not Mixamo's 30fps.
#define ANIM_FPS 60.0f

static Model s_model;
static ModelAnimation *s_animations = NULL;
static int s_animCount = 0;
static bool s_loaded = false;
static int s_slotAnimIndex[CHAR_ANIM_COUNT];

static void ToLowerCopy(char *dst, const char *src, int maxLen) {
    int i = 0;
    for (; src[i] != '\0' && i < maxLen - 1; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

bool CharacterModel_Load(const char *filePath) {
    s_loaded = false;
    for (int i = 0; i < CHAR_ANIM_COUNT; i++) s_slotAnimIndex[i] = -1;

    s_model = ResourceManager_LoadModel(filePath);
    if (s_model.meshCount == 0) {
        return false;
    }

    // G2: replace raylib's UNLIT default shader with the stylized-realism
    // surface shader (half-Lambert + rim + fog). Safe no-op if the surface
    // material system isn't initialized yet.
    SurfaceMaterial_Apply(&s_model);

    s_animations = ResourceManager_LoadModelAnimations(filePath, &s_animCount);
    if (s_animations == NULL || s_animCount == 0) {
        TraceLog(LOG_WARNING, "CHARACTER_MODEL: %s loaded but has no animations", filePath);
        return false;
    }

    // Keyword candidates per slot, checked in clip-scan order — first match
    // wins. Real clip names depend entirely on how the user names/exports
    // them from Blender — adjust these if CharacterModel_Load's log shows
    // an unresolved slot with a real file present.
    static const char *keywords[CHAR_ANIM_COUNT][3] = {
        [CHAR_ANIM_IDLE]  = { "idle", NULL, NULL },
        [CHAR_ANIM_WALK]  = { "walk", NULL, NULL },
        [CHAR_ANIM_PUNCH] = { "punch", "jab", NULL },
        [CHAR_ANIM_KICK]  = { "kick", NULL, NULL },
        [CHAR_ANIM_PALM]  = { "palm", "push", "slap" },
        [CHAR_ANIM_CAST]  = { "cast", NULL, NULL },
    };

    for (int slot = 0; slot < CHAR_ANIM_COUNT; slot++) {
        for (int a = 0; a < s_animCount && s_slotAnimIndex[slot] == -1; a++) {
            char lowerName[32];
            ToLowerCopy(lowerName, s_animations[a].name, sizeof(lowerName));
            for (int k = 0; k < 3 && keywords[slot][k] != NULL; k++) {
                if (strstr(lowerName, keywords[slot][k]) != NULL) {
                    s_slotAnimIndex[slot] = a;
                    break;
                }
            }
        }
        if (s_slotAnimIndex[slot] != -1) {
            TraceLog(LOG_INFO, "CHARACTER_MODEL: slot %d matched animation \"%s\" (clip %d)",
                      slot, s_animations[s_slotAnimIndex[slot]].name, s_slotAnimIndex[slot]);
        } else {
            TraceLog(LOG_WARNING, "CHARACTER_MODEL: slot %d has no matching animation clip", slot);
        }
    }

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
    state->oneShotActive = false;
    state->oneShotSpeed = 1.0f;
}

void CharacterModel_Update(CharacterAnimState *state, float dt, bool isMoving) {
    if (!state || !s_loaded) return;

    if (!state->oneShotActive) {
        CharacterAnimSlot desired = isMoving ? CHAR_ANIM_WALK : CHAR_ANIM_IDLE;
        if (desired != state->currentSlot) {
            state->currentSlot = desired;
            state->frame = 0;
            state->frameTimer = 0.0f;
        }
    }

    int animIndex = s_slotAnimIndex[state->currentSlot];
    if (animIndex == -1) return; // no clip matched this slot — nothing to advance

    int frameCount = s_animations[animIndex].keyframeCount;
    float speed = state->oneShotActive ? state->oneShotSpeed : 1.0f; // idle/walk always natural
    state->frameTimer += dt * ANIM_FPS * speed;
    while (state->frameTimer >= 1.0f) {
        state->frameTimer -= 1.0f;
        state->frame++;
        if (state->frame >= frameCount) {
            if (state->oneShotActive) {
                // One-shot finished — flow straight into walk when the
                // player is still moving (snapping to idle mid-run read as
                // a visible hitch), idle otherwise.
                state->oneShotActive = false;
                state->currentSlot = isMoving ? CHAR_ANIM_WALK : CHAR_ANIM_IDLE;
                state->frame = 0;
            } else {
                state->frame = 0; // loop (idle/walk)
            }
        }
    }
}

void CharacterModel_TriggerAttack(CharacterAnimState *state, CharacterAnimSlot slot) {
    CharacterModel_TriggerAttackTimed(state, slot, 0.0f);
}

void CharacterModel_TriggerAttackTimed(CharacterAnimState *state, CharacterAnimSlot slot, float seconds) {
    if (!state || !s_loaded) return;
    if (s_slotAnimIndex[slot] == -1) return; // no clip matched for this slot
    // A running one-shot is deliberately interruptible (see .h) — a fresh
    // attack press restarts the swing rather than being eaten by a longer
    // clip (casting is 4.2s) that hasn't finished.

    float speed = 1.0f;
    if (seconds > 0.0f) {
        float naturalSeconds = (float)s_animations[s_slotAnimIndex[slot]].keyframeCount / ANIM_FPS;
        speed = naturalSeconds / seconds;
        if (speed < 0.25f) speed = 0.25f; // don't slow-motion a clip into absurdity
        if (speed > 8.0f)  speed = 8.0f;  // nor strobe it
    }

    state->currentSlot = slot;
    state->frame = 0;
    state->frameTimer = 0.0f;
    state->oneShotActive = true;
    state->oneShotSpeed = speed;
}

void CharacterModel_Draw(const CharacterAnimState *state, Vector3 position, float yawRadians, float scale, Color tint) {
    if (!state || !s_loaded) return;

    int animIndex = s_slotAnimIndex[state->currentSlot];
    if (animIndex != -1 && IsModelAnimationValid(s_model, s_animations[animIndex])) {
        UpdateModelAnimation(s_model, s_animations[animIndex], state->frame);
    }

    float yawDegrees = yawRadians * RAD2DEG;
    DrawModelEx(s_model, position, (Vector3){ 0.0f, 1.0f, 0.0f }, yawDegrees,
                (Vector3){ scale, scale, scale }, tint);
}
