#ifndef FORCE_FIELD_H
#define FORCE_FIELD_H

#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* ============================================================
   * NOISE PRIMITIVES - usable standalone or through ForceField
   * ============================================================ */

  /* Standard 3D Perlin noise, output roughly in [-1, 1]. */
  float Noise_Perlin3D(float x, float y, float z);

  /* 3D value noise (cheaper than Perlin), output in [0, 1]. */
  float Noise_Value3D(float x, float y, float z);

  /* Approximate 3D curl noise: central finite differences of three offset
   * Perlin potentials (12 Perlin samples). The output is a (near)
   * divergence-free vector field.
   * scale: spatial frequency. It only changes the feature size of the field,
   * the output amplitude does not depend on it.
   * Amplitude: NOT limited to [-1, 1]. Measured over 500k random samples the
   * components reach about +-4.6, so size `strength` accordingly. */
  Vector3 Noise_Curl3D(float x, float y, float z, float scale);

  /* ============================================================
   * FORCE PRIMITIVES - basic force kinds
   * ============================================================ */

  typedef enum
  {
    FORCE_GRAVITY_DIR = 0,     /* constant acceleration along a direction
                                * (e.g. -Y = gravity) */
    FORCE_GRAVITY_POINT = 1,   /* attract to a point (repel if strength < 0) */
    FORCE_VORTEX = 2,          /* swirl around an axis (origin + direction = axis) */
    FORCE_WIND = 3,            /* directional wind plus Perlin gusts */
    FORCE_NOISE_PERLIN = 4,    /* Perlin noise field: pushes particles along a
                                * pseudo-random vector */
    FORCE_NOISE_CURL = 5,      /* curl noise field: divergence-free, nice swirls */
    FORCE_DRAG = 6,            /* drag proportional to velocity (vel must be
                                * passed to evaluate) */
    FORCE_VISCOSITY = 7,       /* Legacy exponential damping rate in s^-1;
                                * not dynamic viscosity in Pa*s. */
    FORCE_RADIAL_AXIS = 8,     /* Centripetal / centrifugal force about a DYNAMIC
                                * axis (axisOrigin, axisDir supplied at evaluate
                                * time). */
    FORCE_VORTEX_AXIS = 9,     /* Swirl around the same DYNAMIC axis. */
    FORCE_VECTOR_TEXTURE = 10, /* Sample velocity from a dynamic texture (baked
                                * flow / fluid sim) instead of a procedural
                                * formula - see the "VECTOR TEXTURE FIELD" block
                                * below. Only effective on the COMPUTE path
                                * (GpuParticleSystem); CPU evaluate is a no-op,
                                * like FORCE_VISCOSITY on the particle pool. */
    /* Particle-only post-integration receiver. origin = point, direction =
     * outward normal, strength = restitution [0,1], falloff = tangential
     * retention [0,1]. radius/noise parameters unused. Infinite plane; zero
     * normal disables it. Evaluate returns no acceleration for this layer. */
    FORCE_RECEIVER_PLANE = 11
    /* NOTE: this enum order MUST match the #define FT_* values in
     * particles.comp (GLSL cannot include a C header, so keep them in sync by
     * hand). New types MUST be appended at the end - never insert in the
     * middle or renumber, because (float)type is written straight into
     * params0.w in ForceFieldGPU. */
  } ForceType;

  /* Description of a single force layer. */
  typedef struct
  {
    ForceType type;

    /* Position / direction */
    Vector3 origin;    /* origin: center of attractor / vortex / ... (unused for
                        * GRAVITY_DIR, DRAG, RADIAL_AXIS) */
    Vector3 direction; /* force direction / vortex axis (should be normalized) */

    /* Strength and attenuation
     * For FORCE_RADIAL_AXIS: positive strength = pull toward the axis,
     * negative = push away. radius = affected region (measured as the
     * perpendicular distance to the axis). falloff = attenuation by the
     * perpendicular distance to the axis. */
    /* Legacy acceleration magnitude in m/s^2 for force primitives; DRAG and
     * VISCOSITY are rates in s^-1, RECEIVER_PLANE is restitution, and texture
     * fields retain their packed multiplier contract. Motion's compatibility
     * adapter explicitly treats its old guide/target force layers as Newtons.
     * New physical authoring uses ForceLaw with distinct typed quantities. */
    float strength;
    float radius;  /* 0.0 = unlimited range; > 0 = only inside this sphere /
                    * cylinder radius */
    float falloff; /* <= 0 = constant; (0, 1] = linear; > 1 = squared (only
                    * when radius > 0). Values above 1 all behave like 2.0. */

    /* Noise parameters (used by WIND, NOISE_PERLIN, NOISE_CURL) */
    float noiseScale; /* spatial frequency (larger = finer detail) */
    float noiseSpeed; /* scroll speed of the noise over time */
  } ForceLayer;

  /* ============================================================
   * VECTOR TEXTURE FIELD (FORCE_VECTOR_TEXTURE)
   *
   * No new field is added to ForceLayer (layout / SSBO stay unchanged) - the
   * existing fields are reused with different semantics for this type:
   *   origin.xz    = world-space center of the rectangular sample region
   *                  (origin.y ignored)
   *   direction.xz = half-extent of the sample region along X/Z
   *                  (direction.y ignored)
   *   strength     = multiplier applied to the vector read from the texture
   *   noiseScale   = (float)texture slot index - cast to int, see
   *                  GPU_VECTOR_FIELD_SLOTS in
   *                  core/particles/gpu/particle_gpu_legacy.h
   *   radius, falloff, noiseSpeed = NOT used, MUST be 0 (radius > 0 would add
   *                  a spherical attenuation around origin on top of the
   *                  box cutoff)
   *
   * Expected texture format: RG channels = XZ flow direction remapped
   * [-1,1] -> [0,1] (same as flow_map.h). Particles outside the half-extent
   * region receive zero acceleration (hard box cutoff), no edge clamp - author
   * the texture with soft borders if a seamless blend is needed.
   * ============================================================ */

  /* ============================================================
   * FORCE FIELD = a combination of several ForceLayers
   * Evaluate at a point -> total acceleration (Vector3)
   * ============================================================ */

#define FORCE_FIELD_MAX_LAYERS 8

#ifdef CORE_FORCE_FIELD_TYPE_DECLARED
  struct ForceField
  {
    ForceLayer layers[FORCE_FIELD_MAX_LAYERS];
    int layerCount;
  };
#else
typedef struct ForceField
{
  ForceLayer layers[FORCE_FIELD_MAX_LAYERS];
  int layerCount;
} ForceField;
#define CORE_FORCE_FIELD_TYPE_DECLARED 1
#endif

  /* Remove all layers (ff becomes an empty field). */
  void ForceField_Clear(ForceField *ff);

  /* Append a ForceLayer. Returns false if the field is full
   * (>= FORCE_FIELD_MAX_LAYERS) or ff is NULL. */
  bool ForceField_AddLayer(ForceField *ff, ForceLayer layer);

  /* Total acceleration at position pos with velocity vel (needed by
   * FORCE_DRAG), at time `time`.
   *
   * NOTE (BREAKING CHANGE): every call must pass axisOrigin and axisDir.
   * axisOrigin / axisDir define the DYNAMIC AXIS used by every
   * FORCE_RADIAL_AXIS / FORCE_VORTEX_AXIS layer. axisDir is normalized
   * internally; a (near) zero-length axisDir disables axis-type layers. If the
   * field has no axis-type layer, pass (Vector3){0} for both.
   *
   * ff == NULL returns zero. layerCount is clamped to
   * [0, FORCE_FIELD_MAX_LAYERS]. */
  Vector3 ForceField_Evaluate(const ForceField *ff, Vector3 pos, Vector3 vel,
                              float time, Vector3 axisOrigin, Vector3 axisDir);

  /* Damping factor to multiply into velocity, combined from every
   * FORCE_VISCOSITY layer of the field. Returns 1.0 when there is none. */
  float ForceField_GetViscosityDamping(const ForceField *ff, float dt);

  /* Particle backends call after integration; other ForceField consumers are
   * unaffected. Preserves outgoing velocity and resolves penetration by
   * radius. */
  void ForceField_ResolveParticleContacts(const ForceField *ff, float radius,
                                          Vector3 *position, Vector3 *velocity);

  /* ============================================================
   * GPU LAYOUT
   *
   * NOTE FORCE_RADIAL_AXIS / FORCE_VORTEX_AXIS: axisOrigin/axisDir is the
   * dynamic axis (same as the parameters of ForceField_Evaluate) and is BAKED
   * into layers[i].origin/direction AT PACK TIME (see ForceField_PackGPU),
   * because the GPU has no way to pass an extra per-layer parameter at
   * evaluate time. So if a ForceField has several AXIS-type layers that use
   * different axes, call ForceField_PackGPU once per group (or split the
   * field) - one pack applies ONE axisOrigin/axisDir pair to all AXIS-type
   * layers of that field.
   * ============================================================ */

  typedef struct
  {
    Vector4 origin;    /* xyz used, w padding */
    Vector4 direction; /* xyz used, w padding */
    Vector4 params0;   /* x: strength, y: radius, z: falloff, w: (float)type */
    Vector4 params1;   /* x: noiseScale, y: noiseSpeed, z/w: reserved */
  } ForceLayerGPU;

  typedef struct
  {
    ForceLayerGPU layers[FORCE_FIELD_MAX_LAYERS];
    int layerCount;
    int _pad[3]; /* pad to a multiple of 16 bytes, matches the ivec4 meta in
                  * GLSL */
  } ForceFieldGPU;

  /* Compile-time layout guards (C99-compatible static assert): the GPU struct
   * is memcpy'd straight into an SSBO, so any padding drift must fail the
   * build instead of silently corrupting force fields on mobile drivers. */
  typedef char ForceField_Assert_LayerGPU_Size[(sizeof(ForceLayerGPU) == 64) ? 1 : -1];
  typedef char ForceField_Assert_FieldGPU_Size[(sizeof(ForceFieldGPU) ==
                                                (FORCE_FIELD_MAX_LAYERS * 64 + 16))
                                                   ? 1
                                                   : -1];

  /* Convert a ForceField (CPU) to the flat layout that can be memcpy'd to an
   * SSBO. axisOrigin/axisDir: same meaning as in ForceField_Evaluate - the
   * dynamic axis for FORCE_RADIAL_AXIS / FORCE_VORTEX_AXIS layers. axisDir is
   * normalized here before it is baked. If axisDir has (near) zero length the
   * axis-type layers are left out of the pack, which matches the CPU path
   * where they evaluate to zero. Pass (Vector3){0} for both if the field has
   * no axis-type layer.
   *
   * BREAKING CHANGE (vs the older 2-parameter version): every call site must
   * pass these two extra parameters. When this changed, the function had no
   * callers in the repo (unused), so no call site needed updating.
   *
   * ff or out == NULL is a no-op. */
  void ForceField_PackGPU(const ForceField *ff, Vector3 axisOrigin,
                          Vector3 axisDir, ForceFieldGPU *out);

  /* ============================================================
   * WIND ZONE GLOBAL
   * One global ForceField automatically applied to EVERY particle in
   * UpdateParticles() - no need to assign it per ParticleConfig.
   * Use it for ambient wind, storms or other always-on background forces.
   *
   * Thread safety: the WindZone_* functions share one global field and are
   * NOT thread safe. Call the setters from the main thread only.
   * ============================================================ */

  /* Set up the global wind zone.
   *   direction  : main wind direction (normalized internally; a zero-length
   *                direction gives no directional wind)
   *   strength   : base wind acceleration (same unit as ForceLayer.strength)
   *   noiseAmp   : amplitude of the superimposed curl noise (0 = straight wind)
   *   noiseFreq  : spatial frequency of the noise (0.005 - 0.05 is usually
   *                good) */
  void WindZone_Set(Vector3 direction, float strength, float noiseAmp, float noiseFreq);

  /* Disable the wind zone, particles stop receiving it. */
  void WindZone_Clear(void);

  /* Returns true while the wind zone is active. */
  bool WindZone_IsActive(void);
  /* Borrowed immutable snapshot source for compute packing; NULL when inactive. */
  const ForceField *WindZone_GetField(void);

  /* Wind acceleration at position pos. Called internally by particle_system.
   * Skill code normally does NOT call this directly. */
  Vector3 WindZone_Evaluate(Vector3 pos, Vector3 vel, float time);

#ifdef __cplusplus
}
#endif

#endif /* FORCE_FIELD_H */