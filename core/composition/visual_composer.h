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

// ── E6.7. Light shaft ───────────────────────────────────────────────────────
// Godrays. Camera-facing tapered ribbons that CONVERGE at `from` and widen
// toward `to`, each breathing on its own clock, with a hot narrow core over a
// wide soft one so the luma clears the bloom threshold and E1's streak bloom
// does the rest. Continuous. `width` = the cone's FULL width at `to`, metres.
// NOTE: no soft-particle depth fade (a second sampler unbinds texture0 under
// rlvk), so shafts fade by distance along their own length, not by what they hit.
void VFX_ComposeLightShaft(Vector3 from, Vector3 to, VC_MaterialId mat,
                           float width, float intensity);

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
