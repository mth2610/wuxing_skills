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
#include "core/composition/common/vc_motion.h"   // Motion Library (orbit/helix/jitter/breathe)
#include "core/presets/vc_material.h"            // Element Material Table (VC_MaterialId)
#include "core/geometry/procedural_mesh_utils.h" // GroundHeightSampleFn (H2 ground wave)
#include "core/trails/trail_recipe.h"            // TrailPresetId + what a preset row contains
#include "core/gas/gas_system.h"                 // Volumetric smoke/fire/energy simulation

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
void VFX_ComposeSmokePuff(Vector3 pos, VC_MaterialId matId, float scale, float density);
int  VFX_SmokeEmitter_Spawn(Vector3 pos, VC_MaterialId matId, float scale, float density);
void VFX_SmokeEmitter_SetTransform(int handle, Vector3 pos, Vector3 wind);
void VFX_SmokeEmitter_SetDensity(int handle, float density01);
void VFX_SmokeEmitter_Stop(int handle);
void VFX_KillSmokeEmitter(int handle);

// ── F3. Flame volume ────────────────────────────────────────────────────────
// Legacy Emitter (P2 migration target): a fire that is a VOLUME rather than a sprite fan: black-body ramp, a core that
// stays at the base, and a smoke hand-off as the body cools. Continuous — call
// every frame; emission is a RATE derived from a live-count target, so density
// does not move with the frame rate. `intensity` 0..1.
void VFX_ComposeFlameVolume(Vector3 pos, VC_MaterialId matId, float scale, float intensity);
int  VFX_FlameEmitter_Spawn(Vector3 pos, VC_MaterialId matId, float scale, float intensity);
void VFX_FlameEmitter_SetTransform(int handle, Vector3 pos, Vector3 wind);
void VFX_FlameEmitter_SetIntensity(int handle, float intensity01);
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

// ── P4. Ember trail ────────────────────────────────────────────────────────
// Handle-owned moving source: Spawn once, update its transform while the owner
// moves, then Stop (preserve spawned embers) or Kill (stop source immediately).
int  VFX_EmberTrail_Spawn(Vector3 pos, Vector3 velocity, VC_MaterialId mat,
                          float scale, float embersPerSecond);
void VFX_EmberTrail_SetTransform(int handle, Vector3 pos, Vector3 velocity);
void VFX_EmberTrail_Stop(int handle);
void VFX_KillEmberTrail(int handle);

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

// ── F4. Character aura ──────────────────────────────────────────────────────
// Three layers: discrete motes crossing the silhouette (the layer that actually
// reads as an aura), a breathing shell + ground contact, and a real VFXLight
// tracking the agent so the character is lit BY their own aura (E2). Attaches to
// an agent and follows it via SkillManager_GetAgentPos.
//
// Long-lived and per-agent, so unlike a fire-and-forget composition it must be
// released: call VFX_KillCharacterAura on cleanse/death. It also self-releases
// when the agent stops resolving (despawn), so a missed Kill costs one slot
// until then rather than leaking forever. Re-attaching to an agent that already
// has one RETUNES it instead of stacking a second.
// Returns a handle, or the recycled slot's handle when the pool (8) is full.
int  VFX_ComposeCharacterAura(int agentId, VC_MaterialId matId, float intensity);
void VFX_AuraSetIntensity(int handle, float intensity01); // ramped, never popped
void VFX_KillCharacterAura(int handle);

// ── E5.1. Glint sparkle ─────────────────────────────────────────────────────
// Anisotropic star glints over a Fibonacci point cloud (the holy/metal/faith
// signature). Continuous: call once per frame with a running `time`. `scale` is
// the cloud radius in metres. Additive + unlit per the blend law. Needs no
// asset: falls back to a generated 4-point star if glint_star_4pt.png is absent.
void VFX_ComposeGlintSparkle(Vector3 center, VC_MaterialId mat, float scale, float time);

// ── E5.1b. Radiant starburst ───────────────────────────────────────────────
// A one-shot camera-facing flash: compact hot centre, asymmetric tapered rays,
// and a restrained haze. `t01` drives appear → expand → dissipate; call it
// every frame during that interval. For a projectile head, use the continuous
// function below instead.
void VFX_ComposeRadiantStarburst(Vector3 center, VC_MaterialId mat, float radius,
                                 float t01);
// A continuous projectile-head flare. `time` is a running clock; it changes ray
// lengths and corona energy without rotating the whole silhouette like a wheel.
void VFX_ComposeRadiantStarburstHead(Vector3 center, VC_MaterialId mat,
                                     float radius, float time);

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

// ── PRIMARY. Portal disc ────────────────────────────────────────────────────
// A flat disc lying in a plane the WORLD chose, with all its energy in the rim
// and a dark middle — additive adds nothing through the centre, so the scene
// behind shows through and it reads as a hole rather than a coin. The interior
// swirls by scrolling POLAR UVs, so the material turns while the silhouette
// stays rock steady.
//
// It deliberately does NOT use `TRAIL_TYPE_PORTAL`, which the plan named as the
// unused primitive: that draws one camera-facing billboard, so it is the same
// shape from every angle and has no rim. A portal that does not foreshorten as
// you walk around it is a decal floating in front of the camera. TRAIL_TYPE_PORTAL
// should be deleted rather than adopted.
//
// CONTINUOUS: call every frame, `t01` 0 → 1. It OPENS by growing from nothing,
// holds, and COLLAPSES shut — unlike a beam, which stops being fed and goes out.
// `normal` is the plane it lies in ((0,1,0) = flat on the ground, the summoning-
// seal pose). `radius` is the disc's radius in metres at full open.
void VFX_ComposePortalDisc(Vector3 center, Vector3 normal, VC_MaterialId mat,
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

// ── PRIMARY. Converge motes ─────────────────────────────────────────────────
// A FEW broad luminous ribbons that appear at random points in the space around
// a centre and are swept into it along a curve. Each one is a swept trail
// (TRAIL_PRESET_BACKDROP, lifted per instance with VFX_TrailSetHdrGain) whose
// head this composition integrates: launched sideways at a fraction of the
// orbital speed of a point attractor, so it cannot hold station and falls in
// along an arc rather than down a spoke.
//
// IN WAVES, NOT AS A STREAM. The whole population appears at once, on directions
// spread evenly over the sphere, is drawn in together, and the next wave follows
// after a breath that shortens as the charge fills. A rate-driven emitter puts
// four ribbons at four unrelated stages of four unrelated flights, which reads as
// drifting rather than as a cast.
//
// SPARSE BY CONSTRUCTION. `charge_streams` (4) is the whole population, and a
// streamer is a simulated trail out of the engine's pool of EIGHT — so this is a
// look decision and a budget at the same time. `moteCount` scales the BEAT (more
// of it = a shorter breath between waves), not a spawn rate.
//
// Every timing inside is a fraction of the orbital PERIOD, never a number of
// seconds, so the tell reads the same at 0.3 m and at 3 m — see
// vc_converge_motes.inl for the arithmetic and core/docs/LANDMINES.md for what
// went wrong when it was seconds.
//
// A converge is not a charge: a summon draws motes into a rune, a drain pulls
// them off a victim, an absorb takes them into a weapon, and none of those wants
// a hot core in the middle. Add `VFX_ComposeCoreGlow` when you do — that is
// exactly what `VFX_ComposeChargeConverge` now is.
//
// Continuous — call every frame while it should exist. `radius` is the scale of
// the tell in metres (ribbons appear between 1.5 and 4 of it out), `t01` 0→1
// drives the beat/pull/brightness, `moteCount` scales how urgently waves follow
// one another.
void VFX_ComposeConvergeMotes(Vector3 center, VC_MaterialId mat, float radius,
                              float t01, int moteCount);
// The `charge_size` dial, readable by the composite below. Internal to the
// composition module, and it is a prototype here rather than a `static` forward
// declaration in the composite because every .inl is pasted into ONE translation
// unit — where a repeated file-scope `static` NAME is exactly what
// core/tests/composition_tu_test.c exists to catch, and that guard cannot tell a
// second declaration of one symbol from two different symbols.
float VC_ConvergeMotesSizeMul(void);
// The streamers' SINK radius as a fraction of the converge radius: where a
// ribbon stops feeding because it has arrived. A score that draws something at
// the middle must size it with THIS, or the ribbons and the thing they pour into
// disagree about where the destination's surface is.
float VC_ConvergeMotesSinkFrac(void);

// ── E5.3. Charge converge ───────────────────────────────────────────────────
// COMPOSITE, and a pure score over two primaries with no visual idea of its own:
// converge motes, plus a core glow at the destination (which brings its own
// point light). `charge_core = 0` drops the destination and leaves the motes.
// The anticipation beat: motes spiralling INTO a point while it brightens.
// Continuous, `t01` 0→1 over the wind-up.
void VFX_ComposeChargeConverge(Vector3 center, VC_MaterialId mat, float radius, float t01, int moteCount);

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

// ── E6.6. Energy burst ──────────────────────────────────────────────────────
// An expanding SHEET of energy: sprites thrown centrifugally from a RING (not a
// disc), so nothing fills the centre and the burst reads as a shell opening. The
// outward push is spent by drag, then curl noise takes over and the smoke
// churns. One-shot. `intensity` 0..1 scales count, speed and brightness.
void VFX_ComposeEnergyBurst(Vector3 pos, VC_MaterialId matId, float scale,
                            float intensity);

// ── E6.6b. Impact package ───────────────────────────────────────────────────
// The impact as ONE sequence: light flash, the energy burst above, a distortion
// and a decal, timed as a track. `severity01` is the single dial — it scales the
// pieces together and gates the beats that must not fire on a light hit. It must
// live in exactly ONE place; ramping it per-beat as well multiplies (a 1.69x
// scale is 4.7x the fill). `normal` is the surface that was hit. No shake.
//
// **THE GATES ARE PART OF THE CONTRACT.** severity >= 0.45 fires HITSTOP, which
// slows time; severity >= 0.35 fires the screen distortion. Replacing a purely
// visual burst? Stay UNDER 0.45, or every hit in the game develops a stutter —
// this is exactly what happened when the F0 purge mapped old impact calls onto
// this one at 0.55-0.7 (owner: "như là thời gian bị chậm lại").
void VFX_ComposeImpactPackage(Vector3 pos, Vector3 normal, VC_MaterialId matId,
                              float scale, float severity01);
// ...and its PRIMARIES, callable on their own. A skill that wants only the
// flash, or only the mark, calls one of these instead of firing a package and
// switching the rest off. Each carries its own tier gate and its own `impact_*`
// budget switch, so reaching for a piece cannot bypass the budget.
// `severity01` scales TIMES only (lifetime, strength); the SIZE ramp lives in
// the package alone, or it multiplies. One-shot: call once from a state
// transition, never from a draw path.
void VFX_ComposeImpactFlash(Vector3 pos, VC_MaterialId matId, float scale, float severity01);
void VFX_ComposeImpactDistort(Vector3 pos, float scale, float severity01);
void VFX_ComposeImpactDecal(Vector3 pos, VC_MaterialId matId, float scale, float severity01);

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
    VOL_ENERGY = 0, // strands, tight surface, fast flow — a bolt's wake
    VOL_SMOKE  = 1, // reserved: P2 SmokeEmitter, not a shipping tube
    VOL_FIRE   = 2, // reserved: P2 FlameEmitter, not a shipping tube
    // Not a kind — the count. Range-check against THIS. `VFX_ComposeVolumeTrail`
    // validated against the last style by name and silently clamped every HAZE
    // request to BLADE for a day (core/docs/LANDMINES.md, 30/07).
    VFX_VOLUME_KIND_COUNT
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
    // Not a kind — the count. Range-check against THIS, never the last kind by
    // name: a check written against a named member starts clamping silently the
    // day someone appends one (core/docs/LANDMINES.md).
    VFX_COLUMN_KIND_COUNT
} VFX_ColumnKind;

int  VFX_ComposeSmokeColumn(Vector3 pos, VC_MaterialId mat, float radius,
                            float height, VFX_ColumnKind kind, bool funnel);
void VFX_SmokeColumn_Stop(int handle);

int  VFX_ComposeVolumeTrail(const Matrix *followTransform, VC_MaterialId mat,
                            float radius, float lifetime, VFX_VolumeKind kind);
int  VFX_ComposeVolumeTrailEx(const Matrix *followTransform, VC_MaterialId mat,
                               float radius, float lifetime, VFX_VolumeKind kind,
                               const VFX_TrailSurface *surface);
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

// ── PRIMARY: ASTRAL SPEAR — directional crystal projectile ────────────────
// Independent of Rift Bolt and FlowShield: a faceted dart with a coverage body,
// narrow hot seams, two broken counter-rotating halos, and one tapered turbulent
// wake. `followTransform` is sampled at its origin every frame and must remain
// valid until Stop finishes or Kill is called. Heading is derived from motion.
// `radius` is the body radius in metres and is clamped to 0.07-0.15.
//
// ONE-SHOT + POOLED (6). Stop eases the whole composition out; Kill is the
// immediate cancellation cut.
int  VFX_ComposeAstralSpear(const Matrix *followTransform, VC_MaterialId mat,
                            float radius);
void VFX_AstralSpear_SetIntensity(int handle, float intensity01);
void VFX_AstralSpear_Stop(int handle);
void VFX_KillAstralSpear(int handle);

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

// @gen:vc_declarations begin
void VFX_Beam_SetEndpoints(int handle, Vector3 from, Vector3 to);
void VFX_Beam_Stop(int handle);
int VFX_ComposeBeam(Vector3 from, Vector3 to, VC_MaterialId mat, float width);
void VFX_ComposeBlackHole(VC_MaterialId matId, Vector3 pos, float radius, float time);
void VFX_ComposeContactSpark(Vector3 pos, VC_MaterialId matId, float scale, float severity01);
void VFX_ComposeDecal(Vector3 pos, VC_MaterialId matId, float scale, float severity01, float lifetimeScale);
int VFX_ComposeEmberTrail(Vector3 pos, Vector3 velocity, VC_MaterialId mat, float scale, float embersPerSecond);
void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width, float progress, float time);
void VFX_ComposeFlare(Vector3 center, VC_MaterialId mat, float radius, float intensity01);
int VFX_ComposeFlowShield(Vector3 pos, VC_MaterialId mat, float radius, float intensity);
void VFX_ComposeFluidImpact(Vector3 pos);
int VFX_ComposeGasMaterialLab(Vector3 pos, VC_MaterialId mat);
void VFX_ComposeGuidedParticle(Vector3 source, Vector3 target);
void VFX_ComposeIceCrystal(Vector3 basePos, int seed);
void VFX_ComposeImpactDust(Vector3 pos, VC_MaterialId matId, float scale, float severity01);
int VFX_ComposeLightningArc(Vector3 from, Vector3 to, VC_MaterialId material, float width);
void VFX_ComposeLiquidBench(Vector3 center, float spacing, float t01);
void VFX_ComposeParticleUpgradesTest(Vector3 pos);
int VFX_ComposeRefBands(Vector3 pos, float scale);
int VFX_ComposeRefParticles(Vector3 pos, float scale);
int VFX_ComposeShieldShell(Vector3 pos, VC_MaterialId mat, float radius, float intensity);
int VFX_ComposeSmokeTrail(const Matrix *followTransform, VC_MaterialId mat, float radius, float lifetime, VFX_ColumnKind kind, bool funnel);
void VFX_ComposeStonePillar(Vector3 basePos, float progress);
void VFX_ComposeWaterOrb(Vector3 start, Vector3 target);
void VFX_ComposeWaterRing(Vector3 center, float radius, float t01);
void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time);
void VFX_ComposeWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time);
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
void VFX_SmokeTrail_Stop(int handle);
void VFX_WaterRing_Stop(void);
// @gen:vc_declarations end

// Screen-space producers that submit SSF streams before FluidSurface_HasPending().
void VFX_Compose_SubmitScreenSpaceVFX(void);
#endif // VISUAL_COMPOSER_H
