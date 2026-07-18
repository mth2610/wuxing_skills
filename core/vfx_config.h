#ifndef CORE_VFX_CONFIG_H
#define CORE_VFX_CONFIG_H

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
} VFX_AnimationConfig;

// 5. Render Config
typedef struct {
    Color tint;
    Color colorStart;
    Color colorEnd;
    const ColorGradient *gradient;
    Shader shader;
    Texture2D texture;

    // Velocity stretch
    float stretchStrength; // 0.0 = disabled (default)
    float stretchMinSpeed; // speed threshold to apply stretch

    // Particle Ribbon trails
    int trailLength;       // 0 = disabled (default), Max = 8
    float trailWidthRatio; // trail width factor relative to particle radius
    Color trailColorStart;
    Color trailColorEnd;
} VFX_RenderConfig;

#endif // CORE_VFX_CONFIG_H
