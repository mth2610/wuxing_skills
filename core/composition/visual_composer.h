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
#include "core/particle_system.h"
#include "core/composition/common/vc_motion.h"   // Motion Library (orbit/helix/jitter/breathe)
#include "core/presets/vc_material.h"            // Element Material Table (VC_MaterialId)
#include "core/geometry/procedural_mesh_utils.h" // GroundHeightSampleFn (H2 ground wave)

// ── Per-frame drivers ───────────────────────────────────────────────────────
// The pooled components (character aura) and the E3 sequencer ride these two
// calls, already wired in main.c. A new pooled component needs no main.c edit.
void VFX_Compose_Update(float dt);
void VFX_Compose_Draw3D(Camera3D cam);

// ── F2. Smoke / dust puff ───────────────────────────────────────────────────
// Layered alpha sprites with per-sprite spin that grow while they fade,
// deliberately dark so the lighting pass supplies the brightness. Draw with
// BLEND_ALPHA; a glowing puff is this plus a SECOND additive draw, never this
// one flipped to additive. `density` 0..1 scales the sprite count. Needs
// particle lighting on: tuning.cfg → particle_lighting_strength.
void VFX_ComposeSmokePuff(Vector3 pos, VC_MaterialId matId, float scale, float density);

// ── F3. Flame volume ────────────────────────────────────────────────────────
// A fire that is a VOLUME rather than a sprite fan: black-body ramp, a core that
// stays at the base, and a smoke hand-off as the body cools. Continuous — call
// every frame; emission is a RATE derived from a live-count target, so density
// does not move with the frame rate. `intensity` 0..1.
void VFX_ComposeFlameVolume(Vector3 pos, VC_MaterialId matId, float scale, float intensity);

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

// ── E5.2. Rune circle ───────────────────────────────────────────────────────
// A summoning seal: concentric ribbon rings, alternating written/plain, each on
// its own spin and breathe. `normal` = the plane's normal ((0,1,0) = flat on the
// ground). `t01` 0→1 drives open/hold/close. Continuous.
void VFX_ComposeRuneCircle(Vector3 center, Vector3 normal, VC_MaterialId mat, float radius, float t01, int ringCount);

// ── PRIMARY. Core glow ──────────────────────────────────────────────────────
// One hot point of light: a near-white core, a mid glow kept just over the bloom
// threshold, and a wide falloff around them. Every composite that needs a
// destination or a source wants exactly this — a charge's centre, an orb's
// heart, a muzzle, a rune's hub.
//
// THREE sprites and not one, and the reason is not taste: the bright pass clamps
// each pixel's contribution, so bloom SIZE comes from how many pixels clear the
// threshold, not how far one clears it. The mid layer is the one that buys the
// bloom; the core supplies the white; the halo stops it ending at a sprite edge.
//
// Immediate mode — call every frame while the glow should exist. Emits by rate.
// `intensity01` drives brightness, size and the point light together.
void VFX_ComposeCoreGlow(Vector3 center, VC_MaterialId mat, float radius, float intensity01);

// ── PRIMARY. Energy orb ─────────────────────────────────────────────────────
// A sphere that reads as a VOLUME: dark interior, bright limb, filaments
// crawling over the surface, white-hot point at the centre. The head of a
// projectile, an orb skill, a held charge.
//
// The rim is a fresnel term (`aura_shell.fs`), not stacked additive shells —
// additive stacking is brightest through the CENTRE, which is exactly backwards
// for an orb. Immediate mode; call every frame it should exist.
void VFX_ComposeEnergyOrb(Vector3 center, VC_MaterialId mat, float radius, float intensity01);

// ── E5.3. Charge converge ───────────────────────────────────────────────────
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
// until VFX_KillSweptTrail (typically a static Matrix on the owning skill).
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
typedef enum {
    VFX_TRAIL_BLADE = 0,   // thin, hard outer edge, lies in the plane of the
                           // swing (holds a real silhouette), SweepSlash mask
    VFX_TRAIL_RIBBON = 1,  // soft, wide, cloth-like, camera-facing
    VFX_TRAIL_FILAMENT = 2, // several thin strands — the "many threads" look;
                            // sheds strands below GFX_MED
    // WIDE, FAINT, SOFT-EDGED — the BACKDROP behind a sharper trail, not a trail
    // in its own right. A projectile is at least two trails: a hazy field that
    // gives the wake mass, and a defined ribbon on top of it that gives it a
    // shape. Drawing only the sharp one reads as a wire; drawing only the hazy
    // one reads as smoke.
    //
    // It is a STYLE and not its own function on purpose. It follows the same
    // trajectory, keeps the same history, and lags with the same cloth — it
    // differs in width profile, opacity, sheet and how loosely it is anchored,
    // and "differs only in numbers" is the definition of a parameter
    // (VFX_PLAN §Part 4, the rule that keeps the library from reaching 120
    // functions without gaining a capability).
    VFX_TRAIL_HAZE = 3
} VFX_TrailStyle;

int  VFX_ComposeSweptTrail(const Matrix *followTransform, VC_MaterialId mat,
                           float width, float lifetime, VFX_TrailStyle style);
void VFX_TrailSetWidth(int handle, float width01);   // ramped, for wind-down
void VFX_KillSweptTrail(int handle);

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

// ── PRIMARY: spark trail ────────────────────────────────────────────────────
// ONE small moving thing with a CURVED tail, self-terminating. The piece Charge
// Converge and the old Spirit Swarm were missing: motes that are dots read as
// dots, and a stretched sprite cannot help because a stretch is a straight
// segment while a mote spiralling inward is doing nothing but turning.
// `vel` m/s sets the motion and the direction the tail lays in; `length` is the
// tail in metres, `life` in seconds. Returns a trail id, or -1 when the pool is
// full — an emitter running at a rate can ignore it.
// One-shot per spark: the CALLER spawns these at a rate, never one per frame
// per mote.
int VFX_ComposeSparkTrail(Vector3 pos, Vector3 vel, VC_MaterialId matId,
                          float length, float life);

// ── COMPOSITE. Projectile ───────────────────────────────────────────────────
// The reference bolt, and a SCORE over primaries with no visual idea of its own:
// an energy orb, a wide faint HAZE field whose base meets the orb and whose apex
// trails behind, one defined RIBBON tail, and two FILAMENT wisps spiralling
// around the flight axis with one end anchored at the ball.
//
// ONE-SHOT + POOLED: call once when the projectile spawns, keep the handle,
// release it on impact. `followTransform` is caller-owned and must outlive the
// handle — typically a static Matrix on the owning skill.
int  VFX_ComposeProjectile(const Matrix *followTransform, VC_MaterialId mat, float radius);
void VFX_KillProjectile(int handle);

// Batch helpers for the restored water stream: bind the tube shader once and
// draw N streams inside, instead of a Begin/End per projectile. Not generated —
// the scan only picks up VFX_Compose* entry points.
void VFX_BeginWaterStreams(float time);
void VFX_EndWaterStreams(void);

// @gen:vc_declarations begin
void VFX_ComposeBlackHole(VC_MaterialId matId, Vector3 pos, float radius, float time);
void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width, float progress, float time);
void VFX_ComposeIceCrystal(Vector3 basePos, int seed);
void VFX_ComposeParticleUpgradesTest(Vector3 pos);
void VFX_ComposeShardDebris(Vector3 pos, int count, float speed, VC_MaterialId matId);
void VFX_ComposeStonePillar(Vector3 basePos, float progress);
void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time);
void VFX_ComposeWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time);
void VFX_DrawIceCrystalBurst(Vector3 center, int crystalCount, int seed, float growProgress);
// @gen:vc_declarations end
#endif // VISUAL_COMPOSER_H
