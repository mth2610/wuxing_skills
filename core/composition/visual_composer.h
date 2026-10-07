#ifndef VISUAL_COMPOSER_H
#define VISUAL_COMPOSER_H

// ============================================================================
// VISUAL COMPOSER — the Đợt E/F survivor set
//
// F0 (the purge) was executed on 28/07/2026 by the owner's instruction: every
// composition predating the Đợt E/F rebuild was DELETED, along with the two
// spirit components built during it. The survivor set is the eleven documented
// in full below, on top of the engine layers they sit on (VFX_Sequence,
// vfx_light, post-FX, particle, trail, decal).
//
// **The block at the bottom (`@gen:vc_declarations`) is NOT part of that set.**
// Those are pre-Đợt-E effects the owner restored on purpose, to have something
// to rewrite the two water skills against. They are a working scaffold with a
// planned end date, not survivors — do not build new work on them, and do not
// let them quietly become permanent. A restored `.inl` also needs its `#include`
// back in visual_composer.c: a file that exists but is not included compiles
// into nothing and fails at LINK time, which is how this batch first surfaced.
//
// The reasoning, from the spec (§F0): there is no point porting, relighting or
// documenting effects that are about to be replaced, and the old set was built
// before the F1 lit-particle foundation existed — every one of them was authored
// against a lighting model that no longer applies.
//
// **Skills are deliberately bare right now.** The old `VFX_ComposeCast` /
// `VFX_ComposeImpact` / `VFX_ComposeProjectileTrail` backbone went with the
// purge, so a skill's visuals are whatever it rebuilds from this set. That is
// E7's job (the retrofit checkpoint), and it is the point of the stop-gate: the
// plan is proven by rebuilding three skills from these pieces, or it is
// re-scoped.
// ============================================================================

#include "raylib.h"
#include "core/particles/particle_system.h"
#include "core/particles/particle_manager.h"
#include "core/composition/common/vc_motion.h"   // Motion Library (orbit/helix/jitter/breathe)
#include "core/presets/vc_material.h"            // Element Material Table (VC_MaterialId)
#include "core/geometry/procedural_mesh_utils.h" // GroundHeightSampleFn (H2 ground wave)
#include "core/trails/trail_recipe.h"            // TrailPresetId + what a preset row contains
#include "core/gas/gas_system.h"                 // Volumetric smoke/fire/energy simulation
#include "core/liquid/liquid_motion.h"
#include "core/composition/common/vc_params.h"   // Universal VFX Parameter System

// ── Per-frame drivers ───────────────────────────────────────────────────────
// The pooled components (character aura) and the E3 sequencer ride these two
// calls, already wired in main.c. A new pooled component needs no main.c edit.
void VFX_Compose_Update(float dt);
void VFX_Compose_Draw3D(Camera3D cam);

// ── Primary: one-shot lightning arc ────────────────────────────────────────
// A bounded, flickering geometric arc between arbitrary world-space endpoints.
// `from` may be the character's cast socket and `to` the hit/click point. The
// result owns itself through its travel plus `postImpactDuration`; SetEndpoints supports a moving source,
// while Kill is only for cancellation. Seed 0 derives a stable seed from the
// endpoints, otherwise callers can make replays deterministic explicitly.
// This visual primitive never spawns a point light: the owning skill chooses
// whether its cast source and/or hit point need gameplay-facing contact lights.
typedef struct {
    VC_MaterialId material;
    float width;             // body half-width in metres; default 0.075
    float lifetime;          // legacy total-life fallback when postImpactDuration is negative
    float travelDuration;    // source-to-target discharge time; default 0.10
    float postImpactDuration; // seconds to keep arcing after impact; default 0.30, 0 = die on impact
    float coreEmission;      // HDR ion-channel multiplier; default 4.5
    float haloEmission;      // low-energy field multiplier; default 0.32
    float jaggedness;        // maximum lateral displacement in metres; default 0.80
    float flickerInterval;  // seconds between geometric re-seeds; default 0.045
    int branchCount;         // 0..2 secondary branches; default 0 (opt-in)
    unsigned int seed;
} VFX_LightningArcConfig;

VFX_LightningArcConfig VFX_LightningArc_DefaultConfig(void);
int  VFX_LightningArc_Spawn(Vector3 from, Vector3 to, const VFX_LightningArcConfig *config);
void VFX_LightningArc_SetEndpoints(int handle, Vector3 from, Vector3 to);
void VFX_LightningArc_Kill(int handle);

// ── Primary: moving lightning trail ─────────────────────────────────────────
// A bounded, history-driven electrical curve. Feed the moving head through
// SetHead; Core maps the retained polyline to one continuous lightning stroke,
// then dissipates it on Stop. This is for a sword tip, projectile, dash, or
// any curved electrical motion; use LightningArc when both endpoints are known
// at spawn time.
typedef struct {
    VC_MaterialId material;
    float width;             // half-width in metres; default 0.055
    float pointLifetime;     // history duration in seconds; default 0.26
    // Retained for source compatibility. Path detail is now bounded history
    // plus the stroke shader, rather than a segment-per-sample renderer.
    float sampleDistance;
    float jaggedness;        // local electrical displacement, metres; default 0.16
    float coreEmission;      // HDR ion-channel multiplier; default 4.2
    float haloEmission;      // soft field multiplier; default 0.36
    float flickerInterval;   // seconds; default 0.045
    unsigned int seed;
} VFX_LightningTrailConfig;

VFX_LightningTrailConfig VFX_LightningTrail_DefaultConfig(void);
int  VFX_LightningTrail_Spawn(Vector3 head, const VFX_LightningTrailConfig *config);
void VFX_LightningTrail_SetHead(int handle, Vector3 head);
void VFX_LightningTrail_Stop(int handle);
void VFX_LightningTrail_Kill(int handle);

// Reference fixture: ground-hopping electric aftershocks. It is deliberately
// a composition built ON LightningTrail, not an alternate core renderer.
void VFX_ComposeLightningGroundRicochet(Vector3 impactPos, VC_MaterialId material,
                                        float scale, unsigned int seed);

// ── P0 primary lifecycle vocabulary ─────────────────────────────────────────
// Event: call once and it self-dissipates. Draw: call each frame, no owned pool.
// Emitter/Trail: Spawn returns a handle; Update/Set may retune it; Stop/Kill
// releases it. FlameVolume is the documented legacy exception until P2 turns it
// into a per-instance FlameEmitter; its sandbox fixture is therefore timed.

// ── F2. Smoke / dust puff ───────────────────────────────────────────────────
// Layered alpha sprites with per-sprite spin that grow while they fade,
// deliberately dark so the lighting pass supplies the brightness. Draw with
// BLEND_ALPHA; a glowing puff is this plus a SECOND additive draw, never this
// one flipped to additive. `density` 0..1 scales the sprite count. Needs
// particle lighting on: tuning.cfg → particle_lighting_strength.
void VFX_ComposeCloudPuff(Vector3 pos, VC_MaterialId matId, float scale, float density);
void VFX_ComposeSmokePuff(Vector3 pos, VC_MaterialId matId, float scale, float density);
int  VFX_SmokeEmitter_Spawn(Vector3 pos, VC_MaterialId matId, float scale, float density);
void VFX_SmokeEmitter_SetTransform(int handle, Vector3 pos, Vector3 wind);
void VFX_SmokeEmitter_SetDensity(int handle, float density01);
void VFX_SmokeEmitter_Stop(int handle);
void VFX_KillSmokeEmitter(int handle);

// ── Primary VFX: Smoke Volume (UE5 Niagara Architecture) ────────────────────
// Five semantic styles: heavy roil, dark puff, light puff, wispy smoke, and
// a self-emissive energy wisp. The energy variant uses its own plasma surface
// contract rather than interpreting packed smoke RGB as colour.
// Implements Morton-Taylor-Turner plume dynamics, radial entrainment, and 6-way lighting.
typedef enum {
    VFX_SMOKE_STYLE_ROIL = 0,        // Heavy thermal convection column (smoke_roil_8x8)
    VFX_SMOKE_STYLE_PUFF_DARK = 1,   // Dense black detonation smoke (smoke_puff_8x8)
    VFX_SMOKE_STYLE_PUFF_LIGHT = 2,  // Light hit / impact dust smoke (smoke_puff_light_8x8)
    VFX_SMOKE_STYLE_WISPY = 3,       // Dispersed drifting smoke wisps (smoke_wispy_8x8)
    VFX_SMOKE_STYLE_ENERGY_WISP = 4, // Additive plasma wisp (plasma_wisps_8x8)
    VFX_SMOKE_STYLE_COUNT,
    VFX_SMOKE_STYLE_DEFAULT = VFX_SMOKE_STYLE_ROIL,
} VFX_SmokeStyle;

void VFX_ComposeSmokeVolume(Vector3 pos, float scale, float density, VFX_SmokeStyle style);
int  VFX_SmokeVolumeEmitter_Spawn(Vector3 pos, float scale, float density, VFX_SmokeStyle style);
void VFX_SmokeVolumeEmitter_SetTransform(int handle, Vector3 pos, Vector3 wind);
void VFX_SmokeVolumeEmitter_SetDensity(int handle, float density01);
void VFX_SmokeVolumeEmitter_Stop(int handle);
void VFX_KillSmokeVolumeEmitter(int handle);
const char *VFX_SmokeStyle_Name(VFX_SmokeStyle style);

// ── F3. Flame / Ambient Fire (Niagara NS_Fire & Flame Volume) ───────────────
// Continuous fire with thermal buoyancy, Planck black-body cooling, vortex swirling
// dynamics and smoke transition. Continuous — call every frame; emission is a RATE
// derived from a live-count target, so density does not move with the frame rate.
typedef enum {
    VFX_FLAME_STYLE_NIAGARA_ROIL = 0, // UE5 Niagara Fire Roil 8x8 + Vortex Swirl (Default)
    VFX_FLAME_STYLE_VOLUME = 1,       // Raymarched volume puff (4-channel packed sheet)
    VFX_FLAME_STYLE_COLUMN = 2,       // Single vertical flame tongue column
    VFX_FLAME_STYLE_PUFF = 3,         // Multi-sprite directionless puff
    VFX_FLAME_STYLE_FIREBALL = 4,     // Dense swirling fireball
    VFX_FLAME_STYLE_FIRE_TONGUE_01 = 5, // Taichi rising flame tongue 01
    VFX_FLAME_STYLE_COUNT = 6,
    VFX_FLAME_STYLE_DEFAULT = VFX_FLAME_STYLE_NIAGARA_ROIL
} VFX_FlameStyle;

// Unified / Generalized API
void VFX_ComposeFlame(Vector3 pos, VC_MaterialId matId, float scale, float intensity);
void VFX_ComposeAmbientFire(Vector3 pos, VC_MaterialId matId, float scale, float intensity);
void VFX_ComposeAmbientFireEx(Vector3 pos, VC_MaterialId matId, float scale, float intensity, VFX_FlameStyle style);
const char *VFX_FlameStyle_Name(VFX_FlameStyle style);
void VFX_ComposeFlameVolume(Vector3 pos, VC_MaterialId matId, float scale, float intensity);
// One-shot NE_Explosion fireball layer. It is intentionally separate from the
// rate-based ambient-fire emitter: one call creates the complete 16-24 puff burst.
void VFX_ComposeFireballBurst(Vector3 pos, VC_MaterialId matId, float scale,
                              float severity01);

int  VFX_FlameEmitter_Spawn(Vector3 pos, VC_MaterialId matId, float scale, float intensity);
int  VFX_FlameEmitter_SpawnEx(Vector3 pos, VC_MaterialId matId, float scale, float intensity, VFX_FlameStyle style);
void VFX_FlameEmitter_SetTransform(int handle, Vector3 pos, Vector3 wind);
void VFX_FlameEmitter_SetIntensity(int handle, float intensity01);
void VFX_FlameEmitter_SetStyle(int handle, VFX_FlameStyle style);
void VFX_FlameEmitter_SetVortex(int handle, float strength);
void VFX_FlameEmitter_Stop(int handle);
void VFX_KillFlameEmitter(int handle);

// ── Volumetric gas plume ───────────────────────────────────────────────────
// A stationary simulated volume. Unlike SmokeColumn/FlameVolume, this advects
// density, heat and reaction through the gas grid and is depth-aware against
// scene geometry. Spawn once; Stop ends feeding and preserves the dissipating
// volume, while Kill removes it immediately. Mobile v1 admits one plume.
typedef struct {
    GasKind kind;
    GasPriority priority;
    float radius;           // half-width in metres; default 0.9
    float height;           // volume height in metres; default 3.2
    float emitDuration;     // seconds of active feeding; default 1.8
    float decayDuration;    // seconds retained after feed ends; default 2.2
    float intensity;        // 0..1 density/emission scale; default 1
    float pulsesPerSecond;  // fixed-rate micro-injections; fire default 24
    Vector3 wind;           // world-space drift velocity in m/s
    /* Optional post-simulation optics. Zero inherits the GasKind preset;
     * negative disables a control. See GasVolumeDesc for bounded ranges. */
    float detailStrength;
    float shadowStrength;
    float backgroundAdapt;
    /* False keeps shipping material tinting. True retains the GasKind preset
     * palette for diagnostics or effects whose identity is kind-driven. */
    bool usePresetPalette;
} VFX_GasPlumeConfig;

VFX_GasPlumeConfig VFX_GasPlume_DefaultConfig(GasKind kind);
int  VFX_ComposeGasPlume(Vector3 pos, VC_MaterialId mat,
                         const VFX_GasPlumeConfig *config);
int  VFX_GasPlume_Spawn(Vector3 pos, VC_MaterialId mat,
                        const VFX_GasPlumeConfig *config);
void VFX_GasPlume_SetIntensity(int handle, float intensity01);
void VFX_GasPlume_Stop(int handle);
void VFX_KillGasPlume(int handle);

// ── Volumetric gas vortex ──────────────────────────────────────────────────
// A rotating energy-gas composition. Orbiting sources inject tangential,
// inward, and upward velocity into one depth-aware simulated volume, producing
// a rising corkscrew rather than a stationary plume. Mobile v1 admits one gas
// volume, so spawning this may replace an equal/lower-priority gas effect.
typedef struct {
    GasPriority priority;
    float radius;           // outer vortex radius in metres; default 1.35
    float height;           // simulation volume height; default 3.4
    float emitDuration;     // seconds of active orbit feeding; default 2.6
    float decayDuration;    // seconds retained after feed ends; default 2.0
    float intensity;        // 0..1 density/emission scale; default 1
    float pulsesPerSecond;  // moving sources per second; default 30
    float angularSpeed;     // source orbit speed in radians/second; default 5.2
    float lift;             // world-space upward injection speed; default 1.15
} VFX_GasVortexConfig;

VFX_GasVortexConfig VFX_GasVortex_DefaultConfig(void);
int  VFX_ComposeGasVortex(Vector3 pos, VC_MaterialId mat,
                          const VFX_GasVortexConfig *config);
int  VFX_GasVortex_Spawn(Vector3 pos, VC_MaterialId mat,
                         const VFX_GasVortexConfig *config);
void VFX_GasVortex_SetIntensity(int handle, float intensity01);
void VFX_GasVortex_Stop(int handle);
void VFX_KillGasVortex(int handle);

// ── Volumetric gas shockwave ───────────────────────────────────────────────
// A one-shot expanding ring of simulated energy smoke. Sixteen sources cover
// each ring event and push the gas radially outward; the volume remains alive
// after expansion so its luminous wake rolls up and dissipates naturally.
typedef struct {
    GasPriority priority;
    float radius;           // final wave radius in metres; default 2.8
    float height;           // simulation volume height; default 3.0
    float expandDuration;   // active expansion time; default 0.72 seconds
    float decayDuration;    // retained smoke time after expansion; default 1.7
    float intensity;        // 0..1 density/emission scale; default 1
    float ringsPerSecond;   // complete injection rings per second; default 18
    float outwardSpeed;     // residual radial gas velocity in m/s; default 1.0
    float lift;             // upward gas velocity in m/s; default 0.42
} VFX_GasShockwaveConfig;

VFX_GasShockwaveConfig VFX_GasShockwave_DefaultConfig(void);
void VFX_ComposeGasShockwave(Vector3 pos, VC_MaterialId mat,
                             const VFX_GasShockwaveConfig *config);
int  VFX_GasShockwave_Spawn(Vector3 pos, VC_MaterialId mat,
                            const VFX_GasShockwaveConfig *config);
void VFX_GasShockwave_Stop(int handle);
void VFX_KillGasShockwave(int handle);

// ── PRIMARY. Volumetric flame jet ──────────────────────────────────────────
// A directed cone of simulated fire from start to end. Each fixed-rate pulse
// lays overlapping lobes along the whole segment, so the attack reads
// immediately; reaction supplies the hot core while retained density cools
// into a dark smoky tail. Mobile Gas v1 admits one volume at a time.
typedef struct {
    GasPriority priority;
    float radius;           // terminal half-width in metres; default 0.62
    float emitDuration;     // seconds of active fire feeding; default 0.85
    float decayDuration;    // retained cooling smoke time; default 1.35
    float intensity;        // 0..1 density/emission scale; default 1
    float pulsesPerSecond;  // complete four-lobe pulses/sec; default 20
    float speed;            // forward gas velocity in m/s; default 4.8
    float turbulence;       // lateral edge breakup in m/s; default 1.10
    float lift;             // upward velocity mixed toward the tip; default 0.55
    unsigned int seed;      // deterministic edge variation
} VFX_FlameJetConfig;

VFX_FlameJetConfig VFX_FlameJet_DefaultConfig(void);
void VFX_ComposeFlameJet(Vector3 start, Vector3 end, VC_MaterialId mat,
                         const VFX_FlameJetConfig *config);
int  VFX_FlameJet_Spawn(Vector3 start, Vector3 end, VC_MaterialId mat,
                        const VFX_FlameJetConfig *config);
void VFX_FlameJet_SetIntensity(int handle, float intensity01);
void VFX_FlameJet_Stop(int handle);
void VFX_KillFlameJet(int handle);

// ── P4. Shield shell ───────────────────────────────────────────────────────
// Legacy surface payload retained for source compatibility. ShieldShell now
// intentionally ignores these sheets and renders one shared glass sphere;
// `body`, `flowMap`, and `mask` are no longer sampled by the composition.
typedef struct {
    Texture2D body;
    Texture2D flowMap;
    Texture2D mask;
    /* Preferred mobile input: RG=flow vector, B=energy/soft mask. */
    Texture2D packedMap;
    /* Optional static matcap for the outer glass shell. */
    Texture2D matcapMap;
    float flowSpeed;
    float flowStrength;
    float flowTiling;
    float maskTiling;
} VFX_ShieldSurface;

int  VFX_ShieldShell_Spawn(Vector3 pos, VC_MaterialId mat, float radius, float intensity);
int  VFX_ShieldShell_SpawnEx(Vector3 pos, VC_MaterialId mat, float radius,
                             float intensity, const VFX_ShieldSurface *surface);
void VFX_ShieldShell_SetTransform(int handle, Vector3 pos);
void VFX_ShieldShell_SetIntensity(int handle, float intensity01);
void VFX_ShieldShell_SetSurface(int handle, const VFX_ShieldSurface *surface);
void VFX_ShieldShell_SetImpact(int handle, Vector3 impactWorld, float timeSinceImpact);
// Whether this shell STANDS ON terrain (default true). A ground bubble's bright
// contact band is most of what sells it; a shell that floats has nothing to meet,
// and the same band cuts a hard ellipse across the middle of it. Per shell, not a
// global — a floating charge ball and a ground shield can be alive at once.
void VFX_ShieldShell_SetGroundContact(int handle, bool onGround);
void VFX_ShieldShell_Stop(int handle);
void VFX_KillShieldShell(int handle);

// Dedicated post-3D shell pass retained for render-order compatibility. It
// does not sample the framebuffer; it draws the packed-texture/Fresnel shell.
// Safe to call even when none are alive.
void VFX_ShieldShell_DrawRefraction(Camera3D camera);
void VFX_FlowShield_DrawRefraction(Camera3D camera);

// ── E5.1. Glint sparkle ─────────────────────────────────────────────────────
// Anisotropic star glints over a Fibonacci point cloud (the holy/metal/faith
// ── E5.2. Rune circle ───────────────────────────────────────────────────────
// A summoning seal: concentric ribbon rings, alternating written/plain, each on
// its own spin and breathe. `normal` = the plane's normal ((0,1,0) = flat on the
// ground). `t01` 0→1 drives open/hold/close. Continuous.
void VFX_ComposeRuneCircle(Vector3 center, Vector3 normal, VC_MaterialId mat, float radius, float t01, int ringCount);

// ── PRIMARY. Core glow ──────────────────────────────────────────────────────
// One hot point of light: a near-white core, a mid glow kept just over the bloom
// threshold, and a camera-facing circular starburst (soft falloff, ring, four
// long cardinal rays and four shorter diagonals). Every composite that needs a
// destination or a source wants exactly this — a charge's centre, an orb's
// heart, a muzzle, a rune's hub.
//
// THREE sprites and not one, and the reason is not taste: the bright pass clamps
// each pixel's contribution, so bloom SIZE comes from how many pixels clear the
// threshold, not how far one clears it. The mid layer is the one that buys the
// bloom; the core supplies the white; the outer procedural sheet supplies the
// ring/rays and reaches zero smoothly before the billboard edge.
//
// Immediate mode — call every frame while the glow should exist. Emits by rate.
// `intensity01` drives brightness, size and the point light together.
void VFX_ComposeCoreGlow(Vector3 center, VC_MaterialId mat, float radius, float intensity01);


// ── PRIMARY. Shock ring ─────────────────────────────────────────────────────
// The expanding ring, OFF the ground: an impact in the air, a parry, a barrier
// breaking. `VFX_ComposeGroundWave` raycasts the terrain and stands a lip UP out
// of it; this shares none of that — but the height function is the smaller half
// of the difference.
//
// The larger half: a ground ring is never seen edge-on (you look down at the
// floor), while a mid-air ring is seen from every angle including exactly along
// its own plane, where a flat annulus is a LINE. So this ring's cross-section is
// a LENS with real thickness out of its plane, drawn on both faces. It also
// takes an ORIENTATION, which a ground wave cannot: `normal` is the plane it
// expands in — (0,1,0) gives the horizontal pose.
//
// Additive and unlit with AUTHORED shading (a lit material in the night arena is
// black-on-black, ENGINE_LANDMINES §3). CONTINUOUS: call every frame with `t01`
// running 0 → 1. `radius` is where the front arrives at t01 = 1, in metres.
void VFX_ComposeShockRing(Vector3 center, Vector3 normal, VC_MaterialId mat,
                          float radius, float t01);


// ── PRIMARY. Debris shards ──────────────────────────────────────────────────
// Angular chips thrown off an impact or a break. NOT sprites, and that is the
// definition rather than a preference: a thing that is the same shape from every
// angle is a SPARK. A chip is a squashed, per-instance-jittered box that TUMBLES,
// and its faces are flat-shaded on the CPU against an authored key direction —
// so the tumble is visible as faces changing brightness and occasionally
// flashing. (Authored, not lit: a lit material on small geometry in the night
// arena is black-on-black, ENGINE_LANDMINES §3.)
//
// Chips OCCLUDE, so they draw BLEND_ALPHA and depth-write; the dust they shed
// EMITS, so it is additive and unlit. That is one effect, two draws, per the
// blend law — never one draw compromising between them.
//
// ONE-SHOT: `count` chips per CALL, from a state transition. Calling it from a
// draw path spawns a burst every frame and exhausts the pool in about two.
// `vel` is the burst's base velocity in m/s and the chips spread in a cone
// around it; pass a zero vector for the classic upward scatter off a surface.
// `scale` is a chip's longest axis in metres. `count` is clamped DOWN by the
// quality tier and by a per-call ceiling of 24.
void VFX_ComposeDebrisShards(Vector3 pos, Vector3 vel, VC_MaterialId mat,
                             float scale, int count);

// ── E5.4. Dissolve exit ─────────────────────────────────────────────────────
// The shared erosion-out: an alpha mask eaten away by noise with a bright
// leading edge, shedding embers. Attach to ANY effect's death instead of
// inventing another fade. Continuous, `t01` 0→1 while dying.
void VFX_ComposeDissolveExit(Vector3 pos, VC_MaterialId mat, float scale, float t01);

// ── E6.5. Sweep slash ───────────────────────────────────────────────────────
// A weapon-art arc: a ribbon band whose HEAD outruns its TAIL along one arc,
// masked by a generated blade-streak sheet (hot against the outer edge, smeared
// inward, striated along the sweep), with screen refraction and sparks off the
// leading edge. Continuous, `t01` 0→1 over the swing. `dir` = where the arc's
// MIDPOINT points, `length` = arc radius in metres, `arcRad` = swept angle.
// The swing plane is tilted off horizontal by the `slash_tilt` tunable.
void VFX_ComposeSweepSlash(Vector3 origin, Vector3 dir, VC_MaterialId mat,
                           float length, float arcRad, float t01);

// ── E6.5b. Centripetal Catmull-Rom Martial Arts Slash ─────────────────────────
// A dynamic crescent cleave evaluated via Centripetal Catmull-Rom Spline (alpha=0.5).
// Features needle-sharp tapered tips, dual camera-facing aura and cutting core,
// planar slicing disk presence, tip lighting, wind radial blast, and stretched sparks.
// `origin` = character/weapon anchor, `yaw` = facing angle in radians,
// `progress` = 0..1 swing progress over `duration` (seconds).
void VFX_ComposeCentripetalSlash(Vector3 origin, float yaw, VC_MaterialId mat,
                                float progress, float duration, Camera3D camera);

void VFX_ComposeCentripetalSlashEx(Vector3 origin, float yaw, Color coreColor, Color rimColor,
                                  float progress, float duration, Camera3D camera);

// ── E6.5c. Skinned Mesh Surface Aura Emitter ─────────────────────────────────
// Uniform O(1) barycentric surface sampling across the animated character mesh.
// Emits elemental aura particles flowing outward along surface normals.

// ── E6.5d. Optical Starburst & Anamorphic Cine Streak (VFX 1) ────────────────
void VFX_DrawOpticalStarburstStreak(Vector3 pos, Color coreCol, Color streakCol,
                                    float starRadius, float streakLength,
                                    float streakThickness, float intensity, Camera3D camera);
void VFX_ComposeOpticalFlare(Vector3 pos, float starRadius, float streakLength, float intensity, Camera3D camera);

// ── Generic mesh VFX: independent particle emitter and surface aura ─────────
typedef enum {
    VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_STATIC = 0,
    VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_RISE,
    VFX_MESH_PARTICLE_VARIANT_SMOKE_LIGHT_RISE,
    VFX_MESH_PARTICLE_VARIANT_SMOKE_DARK_RISE,
    VFX_MESH_PARTICLE_VARIANT_FIRE_ROIL,
    VFX_MESH_PARTICLE_VARIANT_EMBER_SPARK_LIFT,
    VFX_MESH_PARTICLE_VARIANT_COUNT
} VFX_MeshParticleVariant;

typedef struct {
    const Mesh *mesh;
    const Model *model;
    Matrix transform;
    VFX_MeshParticleVariant variant;
    VC_MaterialId material;
    float intensity;
    unsigned int seed;
} VFX_MeshParticleEmitterDesc;

int VFX_MeshParticleEmitter_Spawn(const VFX_MeshParticleEmitterDesc *desc);
int VFX_ComposeMeshParticleEmitter(const VFX_MeshParticleEmitterDesc *desc);
void VFX_MeshParticleEmitter_SetTransform(int handle, Matrix transform);
void VFX_MeshParticleEmitter_SetVariant(int handle, VFX_MeshParticleVariant variant);
void VFX_MeshParticleEmitter_SetIntensity(int handle, float intensity01);
void VFX_MeshParticleEmitter_Kill(int handle);
void VFX_KillMeshParticleEmitter(int handle);
const char *VFX_MeshParticleVariant_Name(VFX_MeshParticleVariant variant);

typedef struct {
    Color materialColor;
    float rimWidth;
    float rimIntensity;
    float opacity;
} VFX_MeshSurfaceAuraParams;

typedef enum {
    VFX_MESH_SURFACE_AURA_CYAN = 0,
    VFX_MESH_SURFACE_AURA_VIOLET,
    VFX_MESH_SURFACE_AURA_AMBER,
    VFX_MESH_SURFACE_AURA_EMBER,
    VFX_MESH_SURFACE_AURA_VARIANT_COUNT
} VFX_MeshSurfaceAuraVariant;

const char *VFX_MeshSurfaceAuraVariant_Name(VFX_MeshSurfaceAuraVariant variant);
VFX_MeshSurfaceAuraParams VFX_MeshSurfaceAuraParams_MakeVariant(VFX_MeshSurfaceAuraVariant variant);

void VFX_DrawMeshSurfaceAura(Mesh mesh, Matrix transform,
                             const VFX_MeshSurfaceAuraParams *params);
void VFX_DrawModelSurfaceAura(Model model, Matrix transform,
                              const VFX_MeshSurfaceAuraParams *params);

// ── E6.5f. 3D Vacuum Suction Vortex Converge (VFX 3) ─────────────────────────
void VFX_ComposeVacuumConverge(Vector3 focalPoint, float sphereRadius,
                              float progress, Camera3D camera);
void VFX_ComposeVacuumArc(Vector3 pos, float yaw, float progress, float duration, Camera3D camera);


// ── E6.5g. Thin Expanding Vacuum Ground Ring (VFX 4) ─────────────────────────
void VFX_DrawExpandingVacuumRing(Vector3 center, float radius, float bandWidth,
                                 Color ringColor, float alpha01);
void VFX_ComposeVacuumRing(Vector3 center, float radius, float progress);

// ── E6.5h. Composite: Iaido Quick-Draw / Counter Stance (VFX 5) ──────────────
void VFX_ComposeIaidoStance(Vector3 playerPos, float yaw, float progress,
                            float duration, Camera3D camera,
                            const void *animStatePtr);

// ── E6.5i. Ghost of Tsushima Guiding Wind Ribbon (VFX 6) ─────────────────────
void VFX_ComposeGuidingWind(Vector3 startPos, Vector3 targetPos, float progress, Camera3D camera);

// ── E6.7. Light shaft ───────────────────────────────────────────────────────
// Godrays. Camera-facing tapered ribbons that CONVERGE at `from` and widen
// toward `to`, each breathing on its own clock, with a hot narrow core over a
// wide soft one so the luma clears the bloom threshold and E1's streak bloom
// does the rest. Continuous. `width` = the cone's FULL width at `to`, metres.
// NOTE: no soft-particle depth fade (a second sampler unbinds texture0 under
// rlvk), so shafts fade by distance along their own length, not by what they hit.
void VFX_ComposeLightShaft(Vector3 from, Vector3 to, VC_MaterialId mat,
                           float width, float intensity);

// ── H1. Swept trail ─────────────────────────────────────────────────────────
// The swept weapon/body trail: a strip that records where something HAS BEEN,
// instead of sprites re-emitted along its path. Đợt H's first task, because
// `core/trail_system.h` was 18 shipping entry points that no composition used.
//
// `followTransform` is sampled at its ORIGIN every frame and must stay valid
// until VFX_KillTrail (typically a static Matrix on the owning skill).
// `width` is the FULL width in metres at its widest — a CEILING, not a value:
// the drawn width is also capped against the length the tip actually travelled
// (1:20 blade, 1:10 ribbon, 1:40 filament), so a slow or hard-turning weapon
// gets a thin trail rather than a fat stub. `lifetime` is the tail's memory in
// seconds (how long a laid-down point stays in the strip), clamped to 1.0 s by
// TRAIL_HISTORY_COUNT.
//
// ONE-SHOT + POOLED: call once from a state transition, keep the handle, and
// release it. Calling it every frame stacks trails until the pool (8) recycles.
// Kill does not cut the strip out of existence — it stops the feed, and the
// strip drains its own history and fades, which is the wind-down.
//
// Per-instance sheet inputs. A recipe owns geometry, layer ratios and blend;
// the caller owns its visual identity. Pass NULL to the `Ex` functions to use
// the recipe defaults. A non-NULL value has no hidden fallback for flow/mask:
// a zero texture id disables that pass deliberately.
//
// Example:
//   VFX_TrailSurface s = {.texture = body, .flowMap = flow,
//                         .flowSpeed = 0.7f, .flowStrength = 0.18f,
//                         .flowTiling = 1.5f};
//   VFX_ComposeTrailEx(&xf, VC_MAT_WATER, 0.35f, 0.7f,
//                      TRAIL_PRESET_MAIN, &s);
typedef struct {
    Texture2D texture;   // body sheet; id == 0 keeps the recipe default sheet
    Texture2D flowMap;   // RG direction map; id == 0 disables flow distortion
    Texture2D noiseMask; // R erosion mask; id == 0 disables dissolve erosion
    float flowSpeed;
    float flowStrength;
    float flowTiling;
    float dissolve;
    float maskTiling;
} VFX_TrailSurface;

// ── PRIMARY. THE trail ──────────────────────────────────────────────────────
// One composition for every ribbon-shaped trail. What used to be two entry
// points over two private style tables (VFX_ComposeRibbonTrail with
// VFX_RIBBON_*, VFX_ComposeStrandTrail with VFX_STRAND_*) backed by two
// hand-written fragment modes is now one call selecting a row of
// `k_trailPresets[]` — see core/trails/trail_recipe.h for what a row contains
// and core/composition/common/vc_trail.inl for the rows themselves.
//
// `preset` is a TrailPresetId: BLADE / MAIN / WISP / BACKDROP are the swept
// cloth-driven ribbons; ENERGY / SMOKE are the wave-driven strand trails.
//
//   int h = VFX_ComposeTrail(&xf, VC_MAT_FIRE, 0.1f, 2.0f, TRAIL_PRESET_MAIN);
//   VFX_TrailSetWidth(h, 0.0f);   // ramped wind-down
//   VFX_KillTrail(h);             // or let it drain when it stops being fed
//
// The Ex form supplies a per-instance surface (its own sheet/flow map/mask)
// without touching the shared preset row.
int  VFX_ComposeTrail(const Matrix *followTransform, VC_MaterialId mat,
                      float width, float lifetime, TrailPresetId preset);
int  VFX_ComposeTrailEx(const Matrix *followTransform, VC_MaterialId mat,
                        float width, float lifetime, TrailPresetId preset,
                        const VFX_TrailSurface *surface);
void VFX_TrailSetWidth(int handle, float width01); // ramped, for wind-down
// Per-instance EMISSION lift (the recipe's `hdrGain`, which is what bloom
// catches). Each trail owns a copy of its preset row, so this brightens ONE
// ribbon without editing the shared preset — use it when a dim preset's SHAPE is
// what you want and its brightness is not. 1.0 = the preset's own value.
void VFX_TrailSetHdrGain(int handle, float gain);
void VFX_KillTrail(int handle);
// Immediate cut: the strip is gone this frame instead of draining. For a ribbon
// that is ABSORBED at a destination rather than left behind — pair it with a
// width ramp to nothing over the last stretch, or the cut pops.
void VFX_Trail_Extinguish(int handle);

// ── PRIMARY. Volume trail ───────────────────────────────────────────────────
// A swept VOLUME, and nothing else. The tube that `VFX_TRAIL_HAZE` proved out on
// 30/07 existed only as a STYLE of the swept weapon trail, so reaching it meant
// taking the weapon trail's cloth, its per-style aspect table and its spark
// layer along with it. Smoke, fire, a dragon's breath and (once P4 lands) a
// beam want the volume and none of those three.
//
// It reuses TRAIL_SHAPE_TUBE wholesale. There is exactly ONE tube in this tree
// and this is not a second one.
//
// `kind` selects THREE things: the sheet, how hard the surface is deformed by
// noise, and how fast that sheet flows over it. Everything structural — the
// teardrop profile, the caps, the layer stack, the tier ladder, the aspect law —
// is shared, and that is the point: three kinds are a PARAMETER, not three
// implementations (VFX_PLAN §4.1).
//
// Shipping currently accepts only VOL_ENERGY. VOL_SMOKE and VOL_FIRE remain
// reserved compatibility values until owner visual approval; P2 SmokeEmitter
// and FlameEmitter own those primitives instead.
//
// `radius` is the tube's radius in METRES at its widest, and it is a CEILING:
// it is also capped against the length the emitter has actually travelled (a
// volume runs about 1:2.5, full width against its own length), so an emitter
// that has barely moved gets a wisp instead of a ball. `lifetime` is the tail's
// memory in seconds, clamped to 1.0 s by TRAIL_HISTORY_COUNT.
//
// ONE-SHOT + POOLED, exactly like the swept trail: call once from a state
// transition, keep the handle, release it. Calling it every frame stacks volumes
// until the pool (8) recycles. `followTransform` is caller-owned and must
// outlive the handle. Kill stops the FEED rather than cutting the volume out of
// existence, so it drains its own history and fades.
typedef enum {
    VFX_VOLUME_SMOKE  = 0, // absorbing gaseous volume wake (smoke, dark miasma, dust)
    VFX_VOLUME_FIRE   = 1, // blazing flame volume wake
    VFX_VOLUME_STEAM  = 2, // wispy water vapor / steam wake
    VFX_VOLUME_ENERGY = 3, // glowing magical energy / plasma wake with white-hot core
    VFX_VOLUME_KIND_COUNT,
    // Compatibility aliases
    VOL_ENERGY = VFX_VOLUME_ENERGY,
    VOL_SMOKE  = VFX_VOLUME_SMOKE,
    VOL_FIRE   = VFX_VOLUME_FIRE,
} VFX_VolumeKind;

// ── H. Smoke / fire COLUMN — a volume that rises from a FIXED source ────────
//
// Not VFX_ComposeVolumeTrail with different numbers. A volume trail is what a
// MOVING emitter leaves behind — its shape is the path, and it deliberately has
// no force field. A column's emitter does not move at all: the whole shape is
// what happens to the material after it is emitted, so the force field IS the
// effect. Two archetypes over one primitive (TRAIL_SHAPE_TUBE).
//
// `pos` is the source, in world metres. `radius` is the tube radius at the
// source; `funnel` decides whether it stays that width (cylinder) or widens
// with height (TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN). `height` is advisory — the
// column's real reach is rise speed x history length, and the value is logged
// so the two can be compared.
//
// ONE-SHOT + POOLED. Call once, keep the handle, release it with
// VFX_SmokeColumn_Stop — which stops the FEED and lets the laid material drain
// and fade. Calling it every frame stacks columns until the pool (6) recycles.
typedef enum {
    VFX_COLUMN_SMOKE = 0,
    VFX_COLUMN_FIRE,
    VFX_COLUMN_STEAM,
    VFX_COLUMN_ENERGY,
    // Not a kind — the count. Range-check against THIS, never the last kind by
    // name: a check written against a named member starts clamping silently the
    // day someone appends one (core/docs/LANDMINES.md).
    VFX_COLUMN_KIND_COUNT
} VFX_ColumnKind;

int  VFX_ComposeSmokeColumn(Vector3 pos, VC_MaterialId mat, float radius,
                            float height, VFX_ColumnKind kind, bool funnel);
void VFX_SmokeColumn_Stop(int handle);

int  VFX_ComposeVolumeTrail(const Matrix *followTransform, VC_MaterialId mat,
                            float radius, float lifetime, VFX_VolumeKind kind, bool funnel);
int  VFX_ComposeVolumeTrailEx(const Matrix *followTransform, VC_MaterialId mat,
                               float radius, float lifetime, VFX_VolumeKind kind,
                               const VFX_TrailSurface *surface);
void VFX_VolumeTrail_Stop(int handle);
void VFX_KillVolumeTrail(int handle);
// ── H2. Ground wave ─────────────────────────────────────────────────────────
// An expanding ring of ground-CONFORMING geometry: it rises, it has a lip whose
// crest leads, and its inner face is brighter than its outer one — the thing a
// flat additive decal cannot do. `radius` is where the front arrives at t01 = 1,
// in metres. `heightFn` (procedural_mesh_utils.h) is sampled per vertex so the
// wave follows a slope instead of clipping through it; pass NULL for flat at
// `center.y`. Additive and unlit with an AUTHORED shading gradient — a lit
// material on ground geometry is black-on-black in the night arena
// (ENGINE_LANDMINES §3).
//
// CONTINUOUS: call every frame from a draw path with t01 running 0 -> 1. Called
// once it draws a single frame and looks like nothing happened.
void VFX_ComposeGroundWave(Vector3 center, VC_MaterialId mat, float radius,
                           float t01, GroundHeightSampleFn heightFn, void *ud);
// The terrain sampler almost every caller wants: the ACTIVE map's ground height.
// Pass it as `heightFn` (with ud = NULL). Passing NULL instead gives a flat ring
// at center.y, which looks correct on level ground and wrong on any slope.
float VFX_GroundHeightFromMap(float worldX, float worldZ, void *unused);
bool VFX_GroundSurfaceFromMap(float worldX, float worldZ, Vector3 *outPosition,
                              Vector3 *outNormal, void *unused);

// PURGE (27/08/2026): `VFX_ComposeSparkTrail` and `vc_spark_trail.inl` are DELETED
// at the owner's call — "nó rất xấu". It was a primary whose entire subject was ONE
// additive dash with a curved tail, and at any size that actually read on screen it
// read as a dash: RIFT BOLT's first capture had a single shed spark crossing the husk
// as a bright bar with a post-FX streak star on it. Same rule as the F0 purge in
// `core/skill_helper.h` — the NAME is gone rather than pointed at something else.
//
// Its two consumers went with it: RIFT BOLT's debris layer was cut, and CONTACT SPARK
// — which only ever used the shared curves and the 1:28 aspect, never the entry point —
// now owns them itself as `CONTACT_SPARK_ASPECT` / `s_contactSparkWidth`. A future
// debris primitive is a fresh authoring job, not a revival of this one.

// PURGE (17/08/2026): `VFX_ComposeProjectile` / `VFX_KillProjectile` and
// `vc_projectile.inl` are DELETED, with deliberately no successor. It was measured, not
// judged: of the three largest in-band effects it scored worst on every axis on a bright
// background — it lost 79% of its body area, attenuated only 28.7% of its own footprint
// (i.e. it was riding on added light, §4/§5.7), and its internal structure collapsed 10x.
// Numbers and method: `third_party/vulkan/docs/BRIGHT_BACKGROUND_VFX_SPEC.md` §11b,
// reproducible via `scripts/render_vfx_matrix.sh`.
//
// It had no gameplay consumer — the only caller was the sandbox NEWFX fixture. Following
// the F0 purge rule in `core/skill_helper.h`: the names are gone rather than pointed at
// something else, because an alias that quietly changes meaning is how a purge turns into
// a mystery. Rebuilding a bolt is a fresh authoring job, and §5.2–5.6 is the recipe.
//
// NOTE `VFX_ComposeVolumeTrail` SURVIVES this. It was the projectile's field layer, but
// it is also a fixture of its own and its shader `core/trails/shaders/trail_volume.fs` is
// shared with the trail system's volume tubes and with SMOKE COLUMN — deleting the
// projectile does not remove the structure-collapse defect measured in that shader.

// ── PRIMARY: RIFT BOLT — the projectile head the purge above asked for ──────
// A flying HOLLOW SHELL with a wake of one straight spine and three loose
// spiralling threads. It composes rather than renders: the head is a FlowShield
// instance, the wake is four swept trails, and this composition owns the
// lifecycle, the heading, the speed and the geometry that ties them together.
//
// IT DRAWS NOTHING ITSELF (owner, 27/08/2026: "bản rỗng thì cứ dùng flow shield
// thôi sao phải phức tạp?"). An earlier version carried its own two-wall shell
// shader — a second copy of the hollow membrane, the far-then-near draw order
// and the refraction that vc_flow_shield.inl already owned. `rift_bolt.fs` is
// deleted rather than kept behind a flag.
//
// KNOWN COST OF THAT REUSE, measured: FlowShield's vein field is authored for a
// ~1.5 m dome (`flow_shield_vein_scale`, a GLOBAL tunable it shares with the
// real shield). On a 0.08 m bolt the same filament count lives in about a
// twentieth of the screen area and averages out, so the head has very little
// internal contrast: `absvar` on white 6.7 against 33.2 for the purpose-built
// shell it replaced, `darken%` 73.7 against 84.2. Giving FlowShield a
// per-instance vein scale would fix it and is an API change to an
// owner-approved effect, so it is not made here.
//
// `followTransform` is sampled at its ORIGIN every frame and must stay valid
// until the handle is released (a static Matrix on the owning skill, exactly
// like the trails). The bolt DERIVES its heading and speed from that motion —
// there is no direction argument — so a skill that steers its projectile only
// moves the matrix it was already moving.
//
// SCALE. `radius` is the shell radius in metres; the authoring band is
// 0.06-0.12 and the default is 0.08 (0.16 m across, a fist). That is BELOW root
// CLAUDE.md's 0.10-0.20 mesh band, on the owner's call: a thrown bolt is the
// smallest thing the band covers and reads as a projectile rather than an orb
// only well under fire_ball's 0.25 m combat collider. Every wake width and
// twist radius is a multiple of this, so one number rescales the whole effect.
//
// NOTE FlowShield takes a GROUND point and lifts the sphere half a radius above
// it (SHIELD_BURIED_LIFT); this composition cancels that, so `pos` here really
// is the centre. See RiftBolt_ShieldAnchor.
//
// ONE-SHOT + POOLED (6). Call once from a state transition, keep the handle,
// release it. Stop ends the FEED — shell and ribbons each fade over their own
// ramp; Kill is the cancellation cut.
//
//   static Matrix xf;   // updated by the skill each frame
//   int bolt = VFX_ComposeRiftBolt(&xf, VC_MAT_FIRE, 0.08f);
//   ...
//   VFX_RiftBolt_Stop(bolt);
//
// THE WAKE is one MAIN spine plus TWO threads, and they do different jobs: the
// spine rides the flight axis dead straight and is the longest thing in the
// effect (0.95 s of memory); the threads are thinner, shorter than it and than
// each other (0.78 / 0.48 s), on opposite sides of their own helices about that
// axis at 0.45 and 1.00 of the shell's RADIUS.
//
// TWO AND NOT THREE, because three constraints compete for the room between the
// axis and the shell's surface — 20 px at the authored size. A thread must stay
// inside the shell (orbit <= 1 radius) or it visibly detaches; it must not
// narrow to sub-pixel or it breaks into dashes (>= ~5 px); and for N threads to
// read as N rather than as one rope, their orbit SPACING must exceed their
// ribbon WIDTH. Three demand 15-21 px of the 20 available and came out as a
// single braided rope. Two, at 9 and 20 px with 10 and 8 px ribbons, have 11 px
// of clear air. Three would need a shell around 0.14 m.
//
// Four things keep the helices from reading as machined springs: non-harmonic
// rates, a wandering angular rate, an inward-only breathing orbit radius, and a
// pitch clamp against the bolt's actual speed. The threads use MAIN at thread
// width rather than WISP: WISP's `strand.gain` is the only one above 1 in the
// swept table, which pushes the sheet's hairs APART — correct for an inner core
// inside another ribbon, and a row of dots when it is the whole ribbon.
//
// How many ribbons a bolt gets is decided ONCE at spawn from the live bolt
// count: a lone bolt takes all three, two bolts take spine + longer thread,
// three or more take the spine alone, and the composition never commits more
// than 6 of the shared 8-trail pool.
//
// It also spawns a moving VFXLight; that needs no wiring.
int  VFX_ComposeRiftBolt(const Matrix *followTransform, VC_MaterialId mat, float radius);
void VFX_RiftBolt_SetIntensity(int handle, float intensity01);
void VFX_RiftBolt_Stop(int handle);   // wind-down: the husk dims out
void VFX_KillRiftBolt(int handle);    // immediate cut, for cancellation

// Batch helpers for the restored water stream: bind the tube shader once and
// draw N streams inside, instead of a Begin/End per projectile. Not generated —
// the scan only picks up VFX_Compose* entry points.
void VFX_BeginWaterStreams(float time);
void VFX_EndWaterStreams(void);

// Ends trail emission while preserving the laid ribbon so it drifts and
// dissolves on its own. VFX_KillTrail(handle) remains available for an
// immediate cut.
void VFX_Trail_Stop(int trailId);

typedef enum {
    CONTACT_SPARK_STATIC = 0,
    CONTACT_SPARK_CENTRIFUGAL
} ContactSparkMode;

typedef enum {
    VFX_DECAL_VARIANT_IMPACT = 0,
    VFX_DECAL_VARIANT_SCORCH,
    VFX_DECAL_VARIANT_FROST,
    VFX_DECAL_VARIANT_COUNT
} VFX_DecalVariant;

const char *VFX_DecalVariant_Name(VFX_DecalVariant variant);

typedef enum {
    VFX_SURFACE_PARTICLE_RING_VARIANT_DUST = 0,
    VFX_SURFACE_PARTICLE_RING_VARIANT_SMOKE_PUFF_DARK,
    VFX_SURFACE_PARTICLE_RING_VARIANT_SMOKE_WISP,
    VFX_SURFACE_PARTICLE_RING_VARIANT_ENERGY_WISP,
    VFX_SURFACE_PARTICLE_RING_VARIANT_SMOKE_PUFF_DENSE,
    VFX_SURFACE_PARTICLE_RING_VARIANT_PLASMA_VORTEX,
    VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT
} VFX_SurfaceParticleRingVariant;

const char *VFX_SurfaceParticleRingVariant_Name(VFX_SurfaceParticleRingVariant variant);

typedef enum {
    VFX_IMPACT_DUST_VARIANT_DUST_PUFF = 0,
    VFX_IMPACT_DUST_VARIANT_DARK_SMOKE_PUFF,
    VFX_IMPACT_DUST_VARIANT_SMOKE_WISP,
    VFX_IMPACT_DUST_VARIANT_ENERGY_WISP,
    VFX_IMPACT_DUST_VARIANT_COUNT
} VFX_ImpactDustVariant;

const char *VFX_ImpactDustVariant_Name(VFX_ImpactDustVariant variant);

/* Receiver vocabulary is deliberately supplied by gameplay/collision. The map
 * sampler owns geometry and normals, not a semantic material classification. */
typedef enum {
    VFX_IMPACT_SURFACE_EARTH = 0,
    VFX_IMPACT_SURFACE_FIRE,
    VFX_IMPACT_SURFACE_WOOD,
    VFX_IMPACT_SURFACE_METAL,
    VFX_IMPACT_SURFACE_WATER,
    VFX_IMPACT_SURFACE_COUNT
} VFX_ImpactSurface;

typedef struct {
    Vector3 position;
    Vector3 normal;
    VC_MaterialId material;
    VFX_ImpactSurface surface;
    float scale;
    float severity01;
} VFX_SurfaceImpactEvent;

/* One collision event, composed from existing dust, decal, spark and fluid
 * primitives. It owns no pool and is safe to call from projectiles or skills. */
void VFX_SurfaceImpact_Emit(const VFX_SurfaceImpactEvent *event);

// ── Primary VFX: Wood Growth Vine & Tendril (RMF + Growth Dynamics) ─────────
typedef enum {
    WOOD_VINE_VARIANT_SERPENTINE = 0, // Organic free-space searching tendril
    WOOD_VINE_VARIANT_ENTANGLE,       // Helical wrap & constrict around target model
    WOOD_VINE_VARIANT_SPIKE_SPEAR,    // Explosive straight piercing wooden javelin/spear
    WOOD_VINE_VARIANT_ANCIENT_ROOT,   // Heavy gnarled ground crawler hugging terrain
    WOOD_VINE_VARIANT_SEED_SPROUT,    // Ground seed impact eruption with uncurling shoots
    WOOD_VINE_VARIANT_COUNT
} VFX_WoodVineVariant;

typedef enum {
    WOOD_VINE_STYLE_JADE_EMERALD = 0, // Dark bog-oak + brilliant cyan-jade veins (High contrast on grass)
    WOOD_VINE_STYLE_BLOOD_BRAMBLE,    // Mahogany bark + blazing ruby/cinnabar veins (Red vs green grass)
    WOOD_VINE_STYLE_GOLDEN_AMBER,     // Ancient ironwood + molten golden-amber veins
    WOOD_VINE_STYLE_WITHER_GHOST,     // Silver weathered timber + eerie violet soul veins
    WOOD_VINE_STYLE_COUNT
} VFX_WoodVineStyle;

// ── Wuxing Elemental Reaction States (Universal for Botanical Systems) ───────
typedef enum {
    WOOD_REACTION_NORMAL = 0,    // Normal organic growth (Dormant/Standard)
    WOOD_REACTION_WATER,         // Thủy sinh Mộc: Hydration bloom (leaves & flowers emerge)
    WOOD_REACTION_METAL,         // Kim khắc Mộc: Severed cut (cut tip drops, stump withers)
    WOOD_REACTION_FIRE,          // Hỏa thiêu Mộc: Combustion (charcoal embers & ash dissolve)
    WOOD_REACTION_COUNT
} VFX_WoodReactionState;

const char* VFX_WoodReactionState_Name(VFX_WoodReactionState state);

// ── Modular Child Combination Presets (Composite VFX assembly) ──────────────
typedef enum {
    WOOD_VINE_COMBO_AUTO = 0,         // Auto: dynamic reaction & style driven
    WOOD_VINE_COMBO_BARE_STEM,        // Bare stem: bark only, no thorns/foliage
    WOOD_VINE_COMBO_THORNY_BRAMBLE,   // Bramble: bark + thorns
    WOOD_VINE_COMBO_LEAFY_TENDRIL,    // Leafy vine: bark + thorns + foliage leaves
    WOOD_VINE_COMBO_LOTUS_BLOOM,      // Sacred Lotus: foliage + Lotus flowers
    WOOD_VINE_COMBO_ORCHID_BLOOM,     // Celestial Orchid: foliage + Orchid flowers
    WOOD_VINE_COMBO_PLUM_BLOSSOM,     // Ironwood Plum: foliage + Plum Blossom flowers
    WOOD_VINE_COMBO_FULL_FLOURISH,    // Full flourish: twin + thorns + leaves + flowers
    WOOD_VINE_COMBO_WITHERED_AUTUMN,  // Withered autumn: withered browning foliage
    WOOD_VINE_COMBO_COUNT
} VFX_WoodVineCombo;

const char* VFX_WoodVineCombo_Name(VFX_WoodVineCombo combo);

// ── Leaf Shapes & Flower Types (Atomic Botanical Components) ────────────────
typedef enum {
    WOOD_LEAF_SHAPE_OVAL = 0,    // Broadleaf / oval blade (verdant, jade)
    WOOD_LEAF_SHAPE_WILLOW,      // Slender elongated tendril blade
    WOOD_LEAF_SHAPE_MAPLE,       // Lobed / serrated bramble blade (blood bramble)
    WOOD_LEAF_SHAPE_COUNT
} VFX_WoodLeafShape;

const char* VFX_WoodLeafShape_Name(VFX_WoodLeafShape shape);

typedef enum {
    WOOD_FLOWER_TYPE_LOTUS = 0,     // Sacred Jade Lotus (multi-tier chalice petals)
    WOOD_FLOWER_TYPE_ORCHID,        // Wild Celestial Orchid (asymmetric fan petals)
    WOOD_FLOWER_TYPE_PLUM_BLOSSOM,  // Five-petal Ironwood Plum Blossom (apricot/ruby blossom)
    WOOD_FLOWER_TYPE_COUNT
} VFX_WoodFlowerType;

const char* VFX_WoodFlowerType_Name(VFX_WoodFlowerType type);

// ── Universal VFX Socket Contract (Anchor points for child attachment) ──────
// Generalized attachment anchor across all elemental composite effects.
typedef struct VFX_Socket {
    Vector3 pos;        // Surface attachment origin in world space
    Vector3 normal;     // Outward normal vector (perpendicular to parent surface)
    Vector3 tangent;    // Direction along the parent spine / flow axis
    union {
        float param;    // Normalized distance along parent curve/spine [0..1]
        float arc;      // Botanical alias
    };
    union {
        float scale;    // Local parent thickness / radius scale (metres)
        float stemRadius; // Botanical alias
    };
} VFX_Socket;

// Botanical alias: 100% binary and source field compatible
typedef struct VFX_Socket VFX_BotanicalSocket;

// ── Atomic Wood Leaf Configuration ──────────────────────────────────────────
typedef struct {
    bool  attached;               // true: anchored to host sockets; false: free physical airborne simulation
    Vector3 origin;               // center of spawn/distribution (metres)
    float radius;                 // dispersion radius (metres)
    int   count;                  // number of leaves N (e.g. 16 to hundreds)
    float mass;                   // Mass override in kg; <=0 derives from sheet material
    Vector3 initialVelocity;      // ejection velocity in m/s
    float velocitySpread;         // velocity scatter in m/s
    const VFX_BotanicalSocket *sockets; // Surface attachment sockets (when attached == true)
    int   socketCount;
    float growth;                 // Growth emergence progress [0..1]
    float wither;                 // Decay/wither progress [0..1]
    float swayAmp;                // Flutter sway under wind (metres)
    float size;                   // Blade scale in metres; <=0 uses species dimensions (oval blade 8 cm)
    VFX_WoodLeafShape shape;
    VFX_WoodVineStyle style;
    unsigned int seed;
    BodyLaminaPreset bodyMaterial; /* Zero selects dry leaf; defaults select fresh. Positive mass overrides derived mass. */
} VFX_WoodLeavesConfig;

VFX_WoodLeavesConfig VFX_WoodLeaves_DefaultConfig(void);
void                 VFX_ComposeWoodLeaves(const VFX_WoodLeavesConfig *config);

// ── Atomic Wood Flower Configuration ────────────────────────────────────────
typedef struct {
    bool  attached;               // true: anchored to host sockets; false: free physical airborne simulation
    Vector3 origin;               // center of spawn/distribution (metres)
    float radius;                 // dispersion radius (metres)
    int   count;                  // number of flowers/petals M (e.g. 12 to hundreds)
    float mass;                   // Mass in kg for compound flower-head proxy
    Vector3 initialVelocity;      // ejection velocity in m/s
    float velocitySpread;         // velocity scatter in m/s
    const VFX_BotanicalSocket *sockets; // Surface attachment sockets (when attached == true)
    int   socketCount;
    float growth;                 // Bloom opening progress [0..1]
    float wither;                 // Decay/wilting progress [0..1]
    float swayAmp;                // Breeze flutter (metres)
    float size;                   // Blossom radius scale (metres, default ~0.14m)
    VFX_WoodFlowerType type;
    VFX_WoodVineStyle  style;
    unsigned int seed;
} VFX_WoodFlowerConfig;

VFX_WoodFlowerConfig VFX_WoodFlower_DefaultConfig(void);
void                 VFX_ComposeWoodFlower(const VFX_WoodFlowerConfig *config);

// ── Atomic Wood Petals Configuration (Standalone / Always Free Falling) ─────
typedef struct {
    Vector3 origin;               // center of spawn/distribution (metres)
    float   radius;               // dispersion radius (metres)
    int     count;                // number of drifting petals (e.g. 32 to hundreds)
    float   mass;                 // Mass override in kg; <=0 derives from sheet material
    Vector3 initialVelocity;      // ejection velocity in m/s
    float   velocitySpread;       // velocity scatter in m/s
    float   size;                 // Blade scale in metres; <=0 uses species dimensions (default plum length 14 mm)
    VFX_WoodFlowerType type;      // petal morphology
    VFX_WoodVineStyle  style;     // elemental color scheme
    unsigned int seed;
    BodyLaminaPreset bodyMaterial; /* Zero selects dry leaf explicitly; DefaultConfig selects fresh petal. */
} VFX_WoodPetalConfig;

VFX_WoodPetalConfig VFX_WoodPetal_DefaultConfig(void);
void                VFX_ComposeWoodPetals(const VFX_WoodPetalConfig *config);

// ── Composite Wood Vine Configuration ───────────────────────────────────────
typedef struct {
    Vector3 startPos;       // Root/emergence origin in world space
    Vector3 targetPos;      // Target or tip destination
    float targetRadius;     // If > 0, wraps around target cylinder/capsule (e.g. 0.38m for character)
    float targetHeight;     // Height of target model to wrap (e.g. 1.8m)
    float length;           // Extended length along path (metres)
    float baseRadius;       // Base thickness at root (metres, default 0.08f)
    float growth;           // Current growth progress [0..1]
    float wither;           // Wither/decay progress [0..1]
    float sapPhase;         // Sap pulse animation phase
    float swayAmp;          // Sway amplitude under wind/motion (metres)
    float coilRadius;       // 0 for direct crawl/whip, >0 for helical spiral
    float coilTurns;        // Number of spiral turns (e.g. 2.2f)
    bool  enableThorns;     // Sprout hooked phyllotaxis thorns along vine
    bool  enableTwin;       // Sprout braided secondary tendril
    bool  enableLeaves;     // Sprout foliage leaves along vine
    VFX_WoodLeavesConfig leaves; // Child A: all leaf variables [a1, a2, a3...]
    bool  enableFlowers;    // Sprout blossoming flowers along vine
    VFX_WoodFlowerConfig flower; // Child B: all flower variables [b1, b2, b3...]
    VFX_WoodVineCombo  combo;      // Modular child combination preset
    float waterFactor;      // 0 = normal dry, 1 = fully hydrated verdant bloom
    float severArc;         // 1.0 = intact, < 1.0 = severed by metal slash at arc
    float fireFactor;       // 0 = normal, 1 = burning charcoal & ash
    bool  castShadow;       // Cast dynamic ground shadows
    VFX_WoodReactionState reaction; // Wuxing elemental reaction state
    VFX_WoodVineVariant variant; // Morphological growth archetype
    VFX_WoodVineStyle   style;   // Elemental tonal palette & contrast style
    unsigned int seed;      // Deterministic PRNG seed
} VFX_WoodVineConfig;

VFX_WoodVineConfig   VFX_WoodVine_DefaultConfig(void);
const char*          VFX_WoodVineVariant_Name(VFX_WoodVineVariant variant);
const char*          VFX_WoodVineStyle_Name(VFX_WoodVineStyle style);
void                 VFX_ComposeWoodVineSeedSprout(Vector3 impactPos, float progress, unsigned int seed, VFX_WoodVineStyle style);
void                 VFX_ComposeWoodVine(const VFX_WoodVineConfig *config);

/* Field-first composition. source==target creates a stationary sphere; otherwise
 * its centre follows a smooth path at speed m/s and holds at the endpoint until
 * duration expires. Forces are Newtons; swirl/turbulence are airflow m/s.
 * count=0 with no continuous emission creates a field only. No capture, arrival
 * action or implicit target blast. Use independent fields for those effects.
 * particleTemplate and emissionSource are copied at spawn; borrowed resources
 * retain their normal lifetime. fieldOverride replaces the ENTIRE field in
 * world space, including its trajectory and lifetime. Templates own body/render
 * settings; mass/density/drag/particleRadius apply only without a template.
 * Migration: legacy formation/arrival/guide/target/body overrides were removed;
 * use FieldDesc for spatial motion and ParticleConfig for custom bodies. */
typedef struct VFX_GuidedParticleConfig {
    Vector3 source, target;
    float speed, duration, guideRadius, maxForceNewtons;
    float swirlSpeed, turbulenceSpeed;
    int count;
    float emitDuration, emissionRate;
    float formationRadius; /* Source emission radius, independent of field. */
    float particleRadius;  /* Visual radius, independent of physical density. */
    float massKg, densityKgM3;
    float drag; /* Linear airflow response s^-1; zero disables default damping. */
    VC_MaterialId material;
    ParticleRenderMode renderMode;
    ParticleRenderStream *surfaceStreamOut;
    const ParticleConfig *particleTemplate;
    const ParticleEmissionSource *emissionSource;
    const FieldDesc *fieldOverride;
} VFX_GuidedParticleConfig;
VFX_GuidedParticleConfig VFX_GuidedParticle_DefaultConfig(void);
int VFX_GuidedParticle_GetParams(VFX_GuidedParticleConfig *cfg,VFX_ParamDef *outParams,int maxParams);
/* Returns field handle, or zero on invalid settings / field or emitter exhaustion.
 * Stop the field with MotionFields_Stop; emission has its own lifetime. */
MotionFieldHandle VFX_ComposeGuidedParticleEx(const VFX_GuidedParticleConfig *config);

// ── Generic Parameter Introspection API (CapsLock + / dynamic editing) ──────
int VFX_WoodLeaves_GetParams(VFX_WoodLeavesConfig *cfg, VFX_ParamDef *outParams, int maxParams);
int VFX_WoodFlower_GetParams(VFX_WoodFlowerConfig *cfg, VFX_ParamDef *outParams, int maxParams);
int VFX_WoodPetals_GetParams(VFX_WoodPetalConfig *cfg, VFX_ParamDef *outParams, int maxParams);
int VFX_WoodVine_GetParams(VFX_WoodVineConfig *cfg, VFX_ParamDef *outParams, int maxParams);

// ── Universal Botanical Foliage & Petal Simulation System ────────────────────
typedef enum {
    BOTANICAL_KIND_LEAF = 0,
    BOTANICAL_KIND_PETAL,
    BOTANICAL_KIND_FLOWER_HEAD
} VFX_BotanicalKind;

typedef enum {
    BOTANICAL_STATE_ATTACHED = 0, // Anchored to a surface / vine / tree socket
    BOTANICAL_STATE_FREE,         // Airborne under gravity, aerofoil drag, wind & force fields
    BOTANICAL_STATE_SETTLED       // Resting on terrain / ground
} VFX_BotanicalState;

typedef struct {
    VFX_BotanicalKind   kind;
    VFX_WoodLeafShape   leafShape;
    VFX_WoodFlowerType  flowerType;
    VFX_WoodVineStyle   style;
    Vector3 origin;                 // Center of spawn cluster
    float   radius;                 // Distribution radius in metres
    int     count;                  // Number of items to spawn (1 to hundreds)
    bool    attached;               // If true, attempts to anchor to sockets
    const VFX_BotanicalSocket *sockets; // Optional explicit sockets
    int     socketCount;
    Vector3 initialVelocity;        // Base ejection velocity in m/s
    float   velocitySpread;         // Random velocity scatter in m/s
    float   mass;                   // Mass override in kg; <=0 derives from sheet material
    float   size;                   // Blade scale in metres; <=0 resolves leaf/petal species dimensions
    float   growth;                 // Initial growth [0..1]
    float   lifetime;               // Lifetime in seconds
    unsigned int seed;
    float densityKgM3;             // Density override; <=0 uses sheet material (heads: 600 kg/m^3)
    BodyLaminaPreset bodyMaterial; /* Zero is dry leaf; Default selects fresh leaf. No kind-based remapping. */
} VFX_FoliageSpawnParams;

VFX_FoliageSpawnParams VFX_FoliageSpawnParams_Default(void);
void VFX_FoliageSystem_Init(void);
void VFX_FoliageSystem_Reset(void);
int  VFX_FoliageSystem_GetActiveCount(void);
int  VFX_FoliageSystem_SpawnCluster(const VFX_FoliageSpawnParams *params);
int  VFX_FoliageSystem_DetachInRadius(Vector3 center, float radius, Vector3 impulse);
struct ForceField;
void VFX_FoliageSystem_SetForceField(const struct ForceField *ff);
void VFX_FoliageSystem_ClearForceField(void);
void VFX_FoliageSystem_SetHomingTarget(Vector3 targetPos, float suctionStrength, float swirlStrength);
void VFX_FoliageSystem_ClearHomingTarget(void);
bool VFX_FoliageSystem_IsHomingActive(void);
void VFX_FoliageSystem_Update(float dt, const struct ForceField *externalForceField);
void VFX_FoliageSystem_Draw(void);
void VFX_FoliageSystem_DrawShadowPass(void);

// High-level convenient Botanical API (for skills, maps, and visual events)
int  VFX_Foliage_SpawnFreeLeaves(Vector3 center, float radius, int count, float mass, VFX_WoodVineStyle style);
int  VFX_Foliage_SpawnFreePetals(Vector3 center, float radius, int count, float mass, VFX_WoodFlowerType flowerType, VFX_WoodVineStyle style);
int  VFX_Foliage_SpawnAttachedLeaves(const VFX_BotanicalSocket *sockets, int socketCount, VFX_WoodLeafShape shape, VFX_WoodVineStyle style);
int  VFX_Foliage_SpawnAttachedFlowers(const VFX_BotanicalSocket *sockets, int socketCount, VFX_WoodFlowerType type, VFX_WoodVineStyle style);
int  VFX_Foliage_DetachInRadius(Vector3 center, float radius, Vector3 impulse);
void VC_WoodFoliage_Update(float dt);
void VC_WoodFoliage_Draw3D(Camera3D cam);

// @gen:vc_declarations begin
void VFX_ComposeBlackHole(VC_MaterialId matId, Vector3 pos, float radius, float time);
void VFX_ComposeContactSpark(Vector3 pos, VC_MaterialId matId, float scale, float severity01);
void VFX_ComposeContactSparkMode(Vector3 pos, VC_MaterialId matId, float scale, float severity01, ContactSparkMode mode);
void VFX_ComposeDecal(Vector3 pos, VC_MaterialId matId, float scale, float severity01, float lifetimeScale);
void VFX_ComposeDecalVariant(Vector3 pos, VC_MaterialId matId, float scale, float severity01, float lifetimeScale, VFX_DecalVariant variant);
void VFX_ComposeEmberBurst(Vector3 pos, Vector3 normal, VC_MaterialId matId, float scale, float severity01);
void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width, float progress, float time);
int VFX_ComposeFlowShield(Vector3 pos, VC_MaterialId mat, float radius, float intensity);
int VFX_ComposeGasMaterialLab(Vector3 pos, VC_MaterialId mat);
void VFX_ComposeGroundDustRing(Vector3 pos, VC_MaterialId matId, float scale, float severity01);
void VFX_ComposeGuidedParticle(Vector3 source, Vector3 target);
void VFX_ComposeIceCrystal(Vector3 basePos, int seed);
void VFX_ComposeImpactDust(Vector3 pos, VC_MaterialId matId, float scale, float severity01);
void VFX_ComposeImpactDustVariant(Vector3 pos, VC_MaterialId matId, float scale, float severity01, VFX_ImpactDustVariant variant);
int VFX_ComposeLightningArc(Vector3 from, Vector3 to, VC_MaterialId material, float width);
void VFX_ComposeLiquidBench(Vector3 center,float spacing,float t01);
void VFX_ComposeLiquidImpact(Vector3 pos);
void VFX_ComposeMistVeil(Vector3 pos, float radius, float duration);
void VFX_ComposeMistVeilEx(Vector3 pos, VC_MaterialId matId, float radius, float duration);
void VFX_ComposeParticleUpgradesTest(Vector3 pos);
int VFX_ComposeRefBands(Vector3 pos, float scale);
int VFX_ComposeRefParticles(Vector3 pos, float scale);
int VFX_ComposeShieldShell(Vector3 pos, VC_MaterialId mat, float radius, float intensity);
int VFX_ComposeSmokeTrail(const Matrix *followTransform, VC_MaterialId mat, float radius, float lifetime, VFX_ColumnKind kind, bool funnel);
void VFX_ComposeStonePillar(Vector3 basePos, float progress);
void VFX_ComposeSurfaceImpact(Vector3 pos, VFX_ImpactSurface surface);
void VFX_ComposeSurfaceParticleRing(Vector3 pos, VC_MaterialId matId, float scale, float severity01, VFX_SurfaceParticleRingVariant variant);
void VFX_ComposeWaterOrb(Vector3 start, Vector3 target);
void VFX_ComposeWaterRing(Vector3 center, float radius, float t01);
void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time);
void VFX_ComposeWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time);
void VFX_ComposeWoodVineCluster(Vector3 center, float radius, float height, float growth, float wither, float sapPhase, int vineCount, unsigned int seed);
void VFX_DrawIceCrystalBurst(Vector3 center, int crystalCount, int seed, float growProgress);
void VFX_DrawWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time, float phaseOffset);
void VFX_FlowShield_SetIntensity(int handle, float intensity01);
void VFX_FlowShield_SetTransform(int handle, Vector3 pos);
int VFX_FlowShield_Spawn(Vector3 pos, VC_MaterialId mat, float radius, float intensity);
void VFX_FlowShield_Stop(int handle);
void VFX_KillFlowShield(int handle);
void VFX_KillGasMaterialLab(int handle);
void VFX_KillRefBands(int id);
void VFX_KillRefParticles(int id);
void VFX_LiquidOrb_Spawn(Vector3 start, Vector3 target, LiquidMotionProfile profile);
void VFX_SmokeTrail_Stop(int handle);
void VFX_WaterRing_Stop(void);
// @gen:vc_declarations end

/* Legacy composition names remain source-compatible. */
#include "core/fluid/fluid_motion.h"
#define VFX_ComposeFluidImpact VFX_ComposeLiquidImpact
#define VFX_FluidOrb_Spawn VFX_LiquidOrb_Spawn

// Screen-space producers that submit SSF streams before LiquidSurface_HasPending().
void VFX_Compose_SubmitScreenSpaceVFX(void);

#endif // VISUAL_COMPOSER_H
