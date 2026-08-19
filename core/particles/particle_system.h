#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include "core/color_gradient.h"
#include "core/force_field.h"
#include "core/skill_curve.h"
#include "core/sprite_anim.h"
#include "core/vfx_config.h"
#include "raylib.h"
#include <stdbool.h>

// Forward Declaration để cấu trúc có thể tự tham chiếu chính nó cho Sub-Emitter
typedef struct ParticleConfig ParticleConfig;

struct ParticleConfig {
  Vector3 position;
  Vector3 velocity;
  Color colorStart;
  Color colorEnd;
  float radius;
  float lifetime;

  float rotation;
  float angularVelocity;
  float velocityInheritance;

  // ForceField tùy chọn: NULL = không dùng; non-NULL = apply force field mỗi
  // frame
  const ForceField *forceField;
  // Dynamic axis for FORCE_RADIAL_AXIS/FORCE_VORTEX_AXIS layers. The GPU
  // backend packs this with the registered force field; zero is the legacy
  // no-axis default.
  Vector3 forceAxisOrigin;
  Vector3 forceAxisDir;

  // Tùy chọn chuyển màu dải stop và ảnh hoạt cảnh atlas
  const ColorGradient *gradient;
  const SpriteAnim *spriteAnim;
  // PER-PARTICLE PHASE into that animation, in seconds. SpriteAnim derives its
  // frame from the particle's ABSOLUTE age, so a burst of sprites spawned on
  // the same instant all show the SAME frame for their whole lives — with an
  // authored sheet that reads as one image being moved around rather than as
  // many puffs. There is no other way to break the lockstep: giving templates
  // different rates only splits the burst into as many groups as there are
  // templates, and they all still start at frame 0 together.
  // Randomise it over the sheet's duration; 0 keeps the old behaviour.
  float spriteAnimPhase;
  // Per-particle temporal/spatial variation. A zero rate is treated as 1.0,
  // so every existing aggregate initializer retains its old playback. Flip is
  // deliberately applied at draw time: it does not alter an asset or make a
  // directional sheet the default building block for a fire composition.
  float spriteAnimRate;
  bool spriteFlipX;
  bool spriteFlipY;

  // Optional emitter attachment. The particle inherits the target's movement,
  // scaled by followStrength and a release curve (NULL = 1 -> 0 over life).
  // Both pointers must refer to stable storage for the particle's lifetime.
  // Supplying a generation pointer prevents particles from following a newly
  // allocated effect that reuses the same pool slot.
  const Vector3 *followTarget;
  const unsigned int *followTargetGeneration;
  unsigned int followGeneration;
  float followStrength;
  const SkillCurve *followCurve;

  // Optional over-lifetime multiplier curves (t01 = 0 at spawn, 1 at death —
  // same "age fraction" convention as `gradient` above). NULL = today's
  // exact legacy behavior (fixed radius; velocity driven only by
  // forceField/physics). Non-NULL: sampled fresh every frame, multiplying
  // the base value — e.g. radiusCurve = {0,1,1,1,0} makes a particle fade
  // in then shrink away instead of popping at a constant size.
  const SkillCurve *radiusCurve; // multiplies `radius` when drawn
  const SkillCurve *speedCurve;  // multiplies `velocity`'s contribution to
                                  // position each Update frame (does not
                                  // touch the stored velocity itself, so it
                                  // composes cleanly with forceField physics)
  const SkillCurve *alphaCurve;  // multiplies `colorStart.a` when drawn,
                                  // overriding the colorStart/colorEnd (or
                                  // gradient) alpha computation entirely for
                                  // this particle — RGB is unaffected, still
                                  // comes from colorStart/colorEnd/gradient
  const SkillCurve *emissiveCurve; // multiplies RGB brightness over lifetime
                                   // (>1.0 pushes channels toward 255, making
                                   // the particle brighter and more likely to
                                   // exceed PostFX bloomThreshold). NULL = no-op.

  // ============================================================
  // 3.1 SUB-EMITTER SYSTEM — MỞ RỘNG[cite: 4]
  // ============================================================
  const ParticleConfig
      *onDeathEmit; // NULL = không dùng, hạt con nổ ra khi hạt mẹ chết[cite: 4]
  int onDeathEmitCount; // Số lượng hạt con bùng nổ khi chết[cite: 4]

  const ParticleConfig
      *onLiveEmit; // Phát liên tục (tạo vệt đuôi bụi) khi còn sống[cite: 4]
  float onLiveEmitRate; // Số lượng hạt con sinh ra trên mỗi giây
                        // (particles/sec)[cite: 4]

  // Velocity stretch (New)
  float stretchStrength;
  float stretchMinSpeed;

  // Collision parameters (New)
  bool collisionEnabled;
  float collisionElasticity;
  float collisionFloorY;
  const struct ParticleConfig *onCollisionEmit;
  int onCollisionEmitCount;

  // Particle Ribbon trails (New)
  int trailLength;
  float trailWidthRatio;
  Color trailColorStart;
  Color trailColorEnd;

  // Unified Config representation (Phase 3)
  VFX_GeneralConfig general;
  VFX_GeometryConfig geometry;
  VFX_PhysicsConfig physics;
  VFX_AnimationConfig animation;
  VFX_RenderConfig render;
};

static inline void ParticleConfig_Unify(ParticleConfig *cfg) {
  // 1. Populate unified from legacy flat fields if legacy is set and unified is empty
  if (cfg->general.life == 0.0f && cfg->lifetime != 0.0f) {
    cfg->general.life = cfg->lifetime;
    cfg->general.priority = 0;
    cfg->general.tag = 0;
  }
  if (cfg->geometry.radius == 0.0f && cfg->radius != 0.0f) {
    cfg->geometry.radius = cfg->radius;
    cfg->geometry.scale = 1.0f;
    cfg->geometry.width = 0.0f;
    cfg->geometry.length = 0.0f;
  }
  if (cfg->physics.position.x == 0.0f && cfg->physics.position.y == 0.0f && cfg->physics.position.z == 0.0f) {
    cfg->physics.position = cfg->position;
    cfg->physics.velocity = cfg->velocity;
    cfg->physics.speed = 0.0f;
    cfg->physics.forceField = cfg->forceField;
  }
  if (cfg->physics.followTarget == NULL && cfg->followTarget != NULL) {
    cfg->physics.followTarget = cfg->followTarget;
    cfg->physics.followTargetGeneration = cfg->followTargetGeneration;
    cfg->physics.followGeneration = cfg->followGeneration;
    cfg->physics.followStrength = cfg->followStrength;
    cfg->physics.followCurve = cfg->followCurve;
  }
  if (cfg->physics.collisionEnabled == false && cfg->collisionEnabled != false) {
    cfg->physics.collisionEnabled = cfg->collisionEnabled;
    cfg->physics.collisionElasticity = cfg->collisionElasticity;
    cfg->physics.collisionFloorY = cfg->collisionFloorY;
    cfg->physics.onCollisionEmit = cfg->onCollisionEmit;
    cfg->physics.onCollisionEmitCount = cfg->onCollisionEmitCount;
  }
  if (cfg->animation.spriteAnim == NULL && cfg->spriteAnim != NULL) {
    cfg->animation.spriteAnim = cfg->spriteAnim;
    cfg->animation.spriteAnimRate = cfg->spriteAnimRate;
    cfg->animation.spriteFlipX = cfg->spriteFlipX;
    cfg->animation.spriteFlipY = cfg->spriteFlipY;
    cfg->animation.radiusCurve = cfg->radiusCurve;
    cfg->animation.speedCurve = cfg->speedCurve;
    cfg->animation.alphaCurve = cfg->alphaCurve;
    cfg->animation.emissiveCurve = cfg->emissiveCurve;
  }
  if (cfg->render.gradient == NULL && cfg->gradient != NULL) {
    cfg->render.gradient = cfg->gradient;
    cfg->render.colorStart = cfg->colorStart;
    cfg->render.colorEnd = cfg->colorEnd;
    cfg->render.tint = WHITE;
  } else if (cfg->render.colorStart.a == 0 && cfg->colorStart.a != 0) {
    cfg->render.colorStart = cfg->colorStart;
    cfg->render.colorEnd = cfg->colorEnd;
    cfg->render.gradient = cfg->gradient;
    cfg->render.tint = WHITE;
  }
  if (cfg->render.stretchStrength == 0.0f && cfg->stretchStrength != 0.0f) {
    cfg->render.stretchStrength = cfg->stretchStrength;
    cfg->render.stretchMinSpeed = cfg->stretchMinSpeed;
  }
  if (cfg->render.trailLength == 0 && cfg->trailLength != 0) {
    cfg->render.trailLength = cfg->trailLength;
    cfg->render.trailWidthRatio = cfg->trailWidthRatio;
    cfg->render.trailColorStart = cfg->trailColorStart;
    cfg->render.trailColorEnd = cfg->trailColorEnd;
  }

  // 2. Populate legacy flat fields from unified if unified is set and legacy is empty
  if (cfg->lifetime == 0.0f && cfg->general.life != 0.0f) {
    cfg->lifetime = cfg->general.life;
  }
  if (cfg->radius == 0.0f && cfg->geometry.radius != 0.0f) {
    cfg->radius = cfg->geometry.radius;
  }
  if (cfg->forceField == NULL && cfg->physics.forceField != NULL) {
    cfg->forceField = cfg->physics.forceField;
  }
  if (cfg->followTarget == NULL && cfg->physics.followTarget != NULL) {
    cfg->followTarget = cfg->physics.followTarget;
    cfg->followTargetGeneration = cfg->physics.followTargetGeneration;
    cfg->followGeneration = cfg->physics.followGeneration;
    cfg->followStrength = cfg->physics.followStrength;
    cfg->followCurve = cfg->physics.followCurve;
  }
  if (cfg->position.x == 0.0f && cfg->position.y == 0.0f && cfg->position.z == 0.0f) {
    cfg->position = cfg->physics.position;
    cfg->velocity = cfg->physics.velocity;
  }
  if (cfg->collisionEnabled == false && cfg->physics.collisionEnabled != false) {
    cfg->collisionEnabled = cfg->physics.collisionEnabled;
    cfg->collisionElasticity = cfg->physics.collisionElasticity;
    cfg->collisionFloorY = cfg->physics.collisionFloorY;
    cfg->onCollisionEmit = cfg->physics.onCollisionEmit;
    cfg->onCollisionEmitCount = cfg->physics.onCollisionEmitCount;
  }
  if (cfg->spriteAnim == NULL && cfg->animation.spriteAnim != NULL) {
    cfg->spriteAnim = cfg->animation.spriteAnim;
    cfg->spriteAnimRate = cfg->animation.spriteAnimRate;
    cfg->spriteFlipX = cfg->animation.spriteFlipX;
    cfg->spriteFlipY = cfg->animation.spriteFlipY;
  }
  if (cfg->radiusCurve == NULL && cfg->animation.radiusCurve != NULL) {
    cfg->radiusCurve = cfg->animation.radiusCurve;
    cfg->speedCurve = cfg->animation.speedCurve;
    cfg->alphaCurve = cfg->animation.alphaCurve;
    cfg->emissiveCurve = cfg->animation.emissiveCurve;
  }
  if (cfg->gradient == NULL && cfg->render.gradient != NULL) {
    cfg->gradient = cfg->render.gradient;
  }
  if (cfg->colorStart.a == 0 && cfg->render.colorStart.a != 0) {
    cfg->colorStart = cfg->render.colorStart;
    cfg->colorEnd = cfg->render.colorEnd;
  }
  if (cfg->stretchStrength == 0.0f && cfg->render.stretchStrength != 0.0f) {
    cfg->stretchStrength = cfg->render.stretchStrength;
    cfg->stretchMinSpeed = cfg->render.stretchMinSpeed;
  }
  if (cfg->trailLength == 0 && cfg->render.trailLength != 0) {
    cfg->trailLength = cfg->render.trailLength;
    cfg->trailWidthRatio = cfg->render.trailWidthRatio;
    cfg->trailColorStart = cfg->render.trailColorStart;
    cfg->trailColorEnd = cfg->render.trailColorEnd;
  }
}

void InitParticleSystem(void);
/* Internal backend entry point. New gameplay/VFX code uses particle_manager.h. */
void ParticleSystem_SpawnLegacy(ParticleConfig config);
void ParticleSystem_SpawnFromEmitter(ParticleConfig config, int emitterId, int renderMode);
typedef struct ParticleSurfaceSample { Vector3 position; float radius; } ParticleSurfaceSample;
int ParticleSystem_GetSurfaceSamples(int emitterId, ParticleSurfaceSample *outSamples, int maxSamples);
void SpawnParticle(ParticleConfig config);
void ParticleSystem_GetStats(int *active, int *max); // Item 32
void UpdateParticles(float dt);
void DrawParticles(Camera3D camera, Texture2D texture);
// Render-layer entry points. BODY contains occluding/coloured alpha particles;
// EMISSION contains additive HDR particles. The caller composites the two
// buffers after scene rendering, so a bright destination cannot wash out body
// colour before tone mapping.
void DrawParticlesBody(Camera3D camera, Texture2D texture);
void DrawParticlesEmission(Camera3D camera, Texture2D texture);
void UnloadParticleSystem(void);

// ─── Đợt E / F1 — lit particles (core/docs/ELDEN_VFX_SPEC.md §0.1b, F1) ──────
//
// Flat-shaded smoke can only ever look like a decal OF smoke. Volume reads from
// lighting: a bright rim toward the light, a dark occluded core, and a glow when
// backlit. `strength01` blends between the legacy unlit look (0, the DEFAULT —
// nothing already shipped changes) and fully lit (1).
//
// `scatter01` fakes forward scattering: smoke lights up when the sun is BEHIND
// it. This is the most convincing volumetric cue the eye has, and the acceptance
// test for F1 — a puff between the camera and a bright light must visibly glow
// through. ~0.5-1.0 is a natural range; 0 = off.
//
// Applies to the WHOLE batch, not per particle: the immediate-mode vertex format
// has no spare channel. That matches F1b's blend law rather than fighting it —
//
//   THE BLEND LAW: if the thing would BLOCK light in reality, draw it
//   BLEND_ALPHA and light it. If it EMITS light, draw it BLEND_ADDITIVE and
//   leave it unlit. Smoke that glows is TWO draws — an alpha body plus an
//   additive glow — never one additive draw. Additive output can never be
//   darker than its background, which is why additive smoke always reads as
//   glowing gas rather than smoke.
//
// Gated to GFX_MED and above: per-fragment lighting on every particle is real
// fill-rate, and Mali is the constraint.
/* THE GLOW SPRITE — a second default, for the halo half of a glowing particle.
 *
 * The particle system's fallback sprite is a SPARK: a compact Gaussian core in a
 * faint halo, which is the right shape for the thing that blooms. It is the
 * wrong shape for a glow. Scaled up to serve as a halo it reads as a DISC WITH
 * AN EDGE, because most of its falloff happens near the rim — measured as a
 * visible circular boundary on mid-grey scenery (§7.6c).
 *
 * This one is (1 - r^2)^3: energy near the centre, a long tail, and — the
 * property that matters for a large faint sprite — it reaches EXACTLY zero at
 * r = 1 with ZERO SLOPE, so the quad's own boundary can never show. A Gaussian
 * or a Lorentzian never reaches zero, has to be cut, and the cut is what the eye
 * finds.
 *
 * A glowing particle is TWO particles, and they do not share a sprite:
 *     core  small, saturated, ADDITIVE, unlit, high emissiveBoost, default sprite
 *     halo  ~4x radius, same hue at low alpha, ADDITIVE, unlit, THIS sprite
 * Created on first use; owned by the particle system, valid until shutdown. */
Texture2D ParticleSystem_GlowSprite(void);

/* SPAWN A GLOWING PARTICLE — ONE draw, premultiplied.
 *
 * This used to spawn two: an additive core plus a large faint halo, because
 * post-process bloom cannot spread from a source a few pixels across (measured:
 * switching bloom off changes the frame by 0.09/255, so the glow around a spark
 * is DRAWN, not post-processed).
 *
 * Measured again with the blend the rest of this project already had, a single
 * premultiplied particle beats that pair outright:
 *
 *     structure                    draws   |d| white   |d| dark
 *     one premultiplied particle     1       0.801      ~1.65
 *     additive core + halo           2       0.132      ~1.70
 *
 * §5.2's law does both jobs at once. Where coverage is high the equation reduces
 * to `src`, so the particle COVERS a bright background with its own colour and
 * keeps a silhouette; where coverage falls to zero it is `src + dst`, so the
 * skirt still adds light. Additive can only ever add, which is why it dissolves
 * into anything already near 1.0.
 *
 * The caller's blendMode is overridden on purpose: this function is the answer to
 * "make it glow", and premultiplied is that answer. Pass a SATURATED colour —
 * white at high boost is still white, while a saturated one splits into a white
 * core and a coloured skirt, which is what reads as a spark.
 *
 * Still spawn a second, wider particle by hand when an effect wants a deliberately
 * broad haze; that is a look, not the baseline. */
void ParticleSystem_SpawnGlow(ParticleConfig core);

void ParticleSystem_SetLighting(float strength01, float scatter01);
void ParticleSystem_GetLighting(float *outStrength, float *outScatter);
bool IsParticleSystemActive(void);
bool ParticleSystem_HasAdditiveParticles(void);
// True while any drawable particle carries a packed 4-channel volume sheet.
// Those particles can ONLY be decoded by particle_lit.fs, so this is what forces
// that shader to bind even when lighting is off or the quality tier is LOW —
// the default shader would read the sheet's density channels as a colour.
bool ParticleSystem_HasVolumeParticles(void);

// ============================================================
// PARTICLE RADIAL BURST SYSTEM (Hợp nhất từ Phase 5)
// ============================================================
typedef struct ParticleRadialBurstConfig {
    int   countMin, countMax;
    float speedMin, speedMax;
    float radiusMin, radiusMax;
    float lifetimeMin, lifetimeMax;
    float pitchRange;
    float upwardBias;

    Color colorStart, colorEnd;
    const ColorGradient *gradient;
    const ForceField *forceField;

    // Unified Config representation (Phase 3)
    VFX_GeneralConfig general;
    VFX_GeometryConfig geometry;
    VFX_PhysicsConfig physics;
    VFX_AnimationConfig animation;
    VFX_RenderConfig render;
} ParticleRadialBurstConfig;

static inline void ParticleRadialBurstConfig_Unify(ParticleRadialBurstConfig *cfg) {
  // 1. Populate unified from legacy flat fields if legacy is set and unified is empty
  if (cfg->general.life == 0.0f && cfg->lifetimeMax != 0.0f) {
    cfg->general.life = (cfg->lifetimeMin + cfg->lifetimeMax) * 0.5f;
    cfg->general.priority = 0;
    cfg->general.tag = 0;
  }
  if (cfg->geometry.radius == 0.0f && cfg->radiusMax != 0.0f) {
    cfg->geometry.radius = (cfg->radiusMin + cfg->radiusMax) * 0.5f;
    cfg->geometry.scale = 1.0f;
    cfg->geometry.width = 0.0f;
    cfg->geometry.length = 0.0f;
  }
  if (cfg->physics.speed == 0.0f && cfg->speedMax != 0.0f) {
    cfg->physics.speed = (cfg->speedMin + cfg->speedMax) * 0.5f;
    cfg->physics.velocity = (Vector3){0.0f, cfg->upwardBias, 0.0f};
    cfg->physics.forceField = cfg->forceField;
  }
  if (cfg->render.gradient == NULL && cfg->gradient != NULL) {
    cfg->render.gradient = cfg->gradient;
    cfg->render.colorStart = cfg->colorStart;
    cfg->render.colorEnd = cfg->colorEnd;
    cfg->render.tint = WHITE;
  } else if (cfg->render.colorStart.a == 0 && cfg->colorStart.a != 0) {
    cfg->render.colorStart = cfg->colorStart;
    cfg->render.colorEnd = cfg->colorEnd;
    cfg->render.gradient = cfg->gradient;
    cfg->render.tint = WHITE;
  }

  // 2. Populate legacy flat fields from unified if unified is set and legacy is empty
  if (cfg->lifetimeMax == 0.0f && cfg->general.life != 0.0f) {
    cfg->lifetimeMin = cfg->general.life * 0.8f;
    cfg->lifetimeMax = cfg->general.life * 1.2f;
  }
  if (cfg->radiusMax == 0.0f && cfg->geometry.radius != 0.0f) {
    cfg->radiusMin = cfg->geometry.radius * 0.7f;
    cfg->radiusMax = cfg->geometry.radius * 1.3f;
  }
  if (cfg->forceField == NULL && cfg->physics.forceField != NULL) {
    cfg->forceField = cfg->physics.forceField;
  }
  if (cfg->speedMax == 0.0f && cfg->physics.speed != 0.0f) {
    cfg->speedMin = cfg->physics.speed * 0.7f;
    cfg->speedMax = cfg->physics.speed * 1.3f;
  }
  if (cfg->gradient == NULL && cfg->render.gradient != NULL) {
    cfg->gradient = cfg->render.gradient;
  }
  if (cfg->colorStart.a == 0 && cfg->render.colorStart.a != 0) {
    cfg->colorStart = cfg->render.colorStart;
    cfg->colorEnd = cfg->render.colorEnd;
  }
}

void ParticleSystem_SpawnRadialBurst(Vector3 origin, float sizeScale, const ParticleRadialBurstConfig *cfg);

// Mesh-based Particle Emission
struct MeshAdjacency;
void SpawnParticleOnMesh(const struct MeshAdjacency *adj, Matrix transform, ParticleConfig config);

// ============================================================
// CHỈ CÓ Ý NGHĨA Ở GPU COMPUTE MODE
// ============================================================
#define MAX_GPU_FORCE_FIELDS 8
void ParticleSystem_ResetForceFieldRegistry(void);

#endif // PARTICLE_SYSTEM_H
