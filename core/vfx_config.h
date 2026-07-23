#ifndef CORE_VFX_CONFIG_H
#define CORE_VFX_CONFIG_H

typedef enum {
    VFX_BLEND_ALPHA = 0,   // DEFAULT — smoke, dust, ash: anything that occludes
    VFX_BLEND_ADDITIVE,    // embers, sparks, glow, the incandescent core of fire
} VFX_BlendMode;

#include "raylib.h"
#include <stddef.h>
#include "core/force_field.h"
#include "core/color_gradient.h"
#include "core/sprite_anim.h"
#include "core/skill_curve.h"

// 1. General Config
typedef struct {
    float life;      // lifetime / duration
    int priority;    // pool eviction priority (VFXPriority)
    int tag;         // ownerTag
} VFX_GeneralConfig;

// 2. Geometry Config
typedef struct {
    float scale;     // overall size scale
    float radius;    // spherical radius (for particles/boulders)
    float width;     // thickness / beam width
    float length;    // trail/ribbon length
} VFX_GeometryConfig;

// 3. Physics Config
struct ParticleConfig;
typedef struct {
    Vector3 position;
    Vector3 velocity;
    float speed;
    const ForceField *forceField;
    
    // Impact/Collision upgrades
    bool collisionEnabled;
    float collisionElasticity; // 0.0 (stick) to 1.0 (perfect bounce)
    float collisionFloorY;     // local floor level
    const struct ParticleConfig *onCollisionEmit; // sub-emitter spawned on bounce
    int onCollisionEmitCount;
} VFX_PhysicsConfig;

// 4. Animation Config
typedef struct {
    const SpriteAnim *spriteAnim;
    const SkillCurve *radiusCurve;
    const SkillCurve *speedCurve;
    const SkillCurve *alphaCurve;
    const SkillCurve *emissiveCurve;
    const SkillCurve *widthCurve;
} VFX_AnimationConfig;

// 5. Render Config
typedef struct {
    Color tint;
    Color colorStart;
    Color colorEnd;
    const ColorGradient *gradient;
    Shader shader;
    Texture2D texture;

    // Blend mode — Đợt E / F1b, THE BLEND LAW:
    //   if the thing would BLOCK light in reality, it is ALPHA and it gets lit;
    //   if it EMITS light, it is ADDITIVE and stays unlit.
    // Smoke that glows is TWO populations — an alpha body plus an additive core
    // — never one additive draw. Additive output can never be darker than its
    // background, which is why additive smoke always reads as glowing gas.
    // 0 = VFX_BLEND_ALPHA (default), so nothing already written changes.
    int blendMode;         // VFX_BlendMode

    // EMISSIVE opt-out. Lighting is a MULTIPLY, so anything that emits its own
    // light must not go through it — a flame body multiplied by a dim night sky
    // turns brown, which is exactly what happened when F1's lighting was applied
    // globally. Smoke/dust/ash occlude and want lighting; fire, sparks, glow and
    // magic do not. 0 = lit (default, so existing effects are unaffected).
    int unlit;

    // Velocity stretch
    float stretchStrength; // 0.0 = disabled (default)
    float stretchMinSpeed; // speed threshold to apply stretch

    // Particle Ribbon trails
    int trailLength;       // 0 = disabled (default), Max = 8
    float trailWidthRatio; // trail width factor relative to particle radius
    Color trailColorStart;
    Color trailColorEnd;

    // Noise/Flow Distortion for Trails & Ribbons
    float distortionStrength; // 0.0 = disabled (default)
    float distortionSpeed;    // time multiplier for distortion noise
} VFX_RenderConfig;

#endif // CORE_VFX_CONFIG_H
