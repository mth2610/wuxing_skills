// boss/hac_dien_ton_gia_def.c — DATA half: Boss Hắc Diện Tôn Giả (Phase 0
// boss, thiết kế §V.2). New bosses = new files like this one; only _def.c
// files may include VFX/draw headers (boss_system.c stays pure logic).
#include "boss/boss_system.h"
#include "core/skill_manager.h" // ELEMENT_COLOR_* — the phase visual cue
#include "raylib.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static Color ElementColor(int elem) {
    switch (elem) {
        case 0:  return ELEMENT_COLOR_WATER;
        case 1:  return ELEMENT_COLOR_WOOD;
        case 2:  return ELEMENT_COLOR_FIRE;
        case 3:  return ELEMENT_COLOR_EARTH;
        case 4:  return ELEMENT_COLOR_METAL;
        default: return ELEMENT_COLOR_TAIJI;
    }
}

// Hắc Diện Tôn Giả: a black faceless orb-spirit. Dark levitating core (the
// "hắc diện"), breathing scale, plus a rune ring whose color tracks the
// CURRENT phase element — the No-Tutorial visual cue that the boss changed
// element (rãnh hoa văn đổi màu).
static void DrawHacDien(const Agent *self, float phaseT) {
    Color elemColor = ElementColor(self->currentElement);

    // Levitation bob + breathing pulse.
    float bob    = 0.9f + sinf(phaseT * 1.3f) * 0.12f;
    float breath = 1.0f + sinf(phaseT * 2.1f) * 0.05f;
    Vector3 core = { self->position.x, self->position.y + bob, self->position.z };

    // Body: near-black sphere, slightly desaturated toward the element so
    // it reads as "possessed" by the phase element, alpha 255 (scene rule).
    Color body = (Color){ (unsigned char)(10 + elemColor.r / 8),
                          (unsigned char)(10 + elemColor.g / 8),
                          (unsigned char)(10 + elemColor.b / 8), 255 };
    DrawSphereEx(core, 0.55f * breath, 12, 12, body);

    // Rune ring: 3 rotating element-colored circles around the core.
    for (int i = 0; i < 3; i++) {
        float t = phaseT * (0.6f + 0.25f * i);
        Vector3 axis = { sinf(t), 1.0f, cosf(t * 0.7f) };
        DrawCircle3D(core, 0.85f + 0.1f * i, axis, 90.0f + 25.0f * i + t * 40.0f, elemColor);
    }

    // Ground sigil under the boss — same element color, phase cue readable
    // from the isometric camera even when the core is occluded.
    Vector3 ground = { self->position.x, 0.03f, self->position.z };
    DrawCircle3D(ground, 1.1f + sinf(phaseT * 2.1f) * 0.06f, (Vector3){ 1, 0, 0 }, 90.0f, elemColor);
}

const BossDef BOSS_HAC_DIEN_TON_GIA = {
    .name = "HAC_DIEN_TON_GIA",
    .maxHealth = 400.0f,
    // Phase 0 full HP → 75% → 50% → 25%.
    .phaseHpThresholds = { 1.0f, 0.75f, 0.50f, 0.25f },
    // Biến hệ order: Thủy → Hỏa → Thổ → Mộc.
    .phaseElements = { ELEM_WATER, ELEM_FIRE, ELEM_EARTH, ELEM_WOOD },
    .phaseSkillNames = { "GLACIAL_CANNON", "FIRE", "STONE_PRISON", "LEAF_WHIRLWIND" },
    .castIntervalSeconds = 3.0f,
    .drawVisual = DrawHacDien,
};
