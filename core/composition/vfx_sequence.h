#ifndef VFX_SEQUENCE_H
#define VFX_SEQUENCE_H

// ── Đợt E / E3 — VFX_Sequence: the choreography layer ────────────────────────
//
// WHY THIS EXISTS. Elden Ring effects read clearly at a glance because
// particles, light, hitstop, shake, distortion and sound all land on ONE shared
// beat track with a strict `anticipation → burst → sustain → dissipate`
// envelope. In this project every skill hand-codes its own phase timing, so the
// envelope is accidental: two skills that should feel like siblings drift apart
// the moment one is edited, and nothing enforces the shape that makes an effect
// legible. A sequence is that missing beat track.
//
// It is also the reason E3 comes BEFORE E5/E6 in the plan: compositions written
// against a sequencer are choreographed from the start, while compositions
// retrofitted into one afterwards keep their hand-rolled timers forever.
//
// USAGE
//   VFX_Sequence *s = VFX_SeqBegin(pos, VC_MAT_FIRE, 1.0f);
//   VFX_SeqAt(s, 0.00f, (VFX_Beat){ .kind = VFX_BEAT_LIGHT, .a = 1.5f, .b = 0.2f });
//   VFX_SeqAt(s, 0.18f, (VFX_Beat){ .kind = VFX_BEAT_SHAKE, .a = 0.35f });
//   VFX_SeqAt(s, 0.18f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = MyBoom });
//   int h = VFX_SeqPlay(s);            // h only needed if you may stop it early
//
// Or take the standard envelope and override pieces:
//   VFX_Sequence *s = VFX_SeqPreset(pos, VC_MAT_FIRE, 1.0f, 0.15f, 0.10f, 0.40f, 0.5f);
//
// THE CLOCK — a deliberate choice, not an accident. Sequences advance on the
// SCALED dt (post `TimeFX_Apply`), the same dt `VFX_Compose_Update` already
// receives. So a sequence that fires its own hitstop also stretches its own
// remaining beats — that stretch IS the Elden Ring feel, and it keeps a
// sequence in sync with the particles it spawned, which are on the same clock.
// Set `unscaled = true` (VFX_SeqSetUnscaled) for the exceptions — UI flourishes
// and anything that must keep wall-clock time through a hitstop.

#include "raylib.h"
#include "core/presets/vc_material.h"
#include <stdbool.h>

#define VFX_SEQ_MAX        16
#define VFX_SEQ_MAX_BEATS  24

typedef enum {
    VFX_BEAT_COMPOSE = 0, // a VFX_Compose* wrapped in the (pos, scale, ud) shape
    VFX_BEAT_LIGHT,       // VFXLight_Spawn
    VFX_BEAT_SHAKE,       // CameraFX_Shake
    VFX_BEAT_HITSTOP,     // TimeFX_Hitstop
    VFX_BEAT_DISTORT,     // ScreenDistort_Add
    VFX_BEAT_RADIAL,      // PostFX_RadialBurst (E1a)
    VFX_BEAT_DECAL,       // DecalSystem_Add
    VFX_BEAT_CALLBACK     // arbitrary user fn — gameplay/audio, NOT a visual
} VFX_BeatKind;

// What `a`, `b`, `c` mean is per-kind. A bare `float a` tells the reader
// nothing, so the table is the contract:
//
//   KIND      a                b               c              other
//   ───────────────────────────────────────────────────────────────────────────
//   COMPOSE   scale multiplier  —               —              cb REQUIRED
//   LIGHT     radius (m)        lifetime (s)    —              color
//   SHAKE     trauma 0..1       —               —
//   HITSTOP   duration (s)      timeScale       —
//   DISTORT   radius (m)        strength        lifetime (s)
//   RADIAL    strength (~0.15)  duration (s)    —
//   DECAL     scale             lifetime (s)    rotation (deg) ud = Texture2D*
//   CALLBACK  scale multiplier  —               —              cb REQUIRED
//
// A zero in a required slot is replaced by a sane default rather than producing
// an invisible beat — an effect that silently does nothing is the failure mode
// this whole spec keeps warning about.
typedef struct {
    VFX_BeatKind kind;
    Vector3      offset;   // relative to the sequence origin, in metres, scaled by the sequence's scale
    float        a, b, c;  // see the table above
    Color        color;    // {0,0,0,0} = take it from VFX_Material(mat) (glow — the hot identity colour)
    void       (*cb)(Vector3 pos, float scale, void *ud);
    void        *ud;
} VFX_Beat;

typedef struct VFX_Sequence VFX_Sequence;   // opaque; static pool, no malloc

// Reserve a sequence and start authoring it. Returns NULL only if the pool is
// exhausted by sequences that are all still BUILDING (never played) — a leak in
// the caller, and it is logged. A full pool of *playing* sequences recycles the
// oldest instead, so a burst of effects degrades rather than vanishing.
VFX_Sequence *VFX_SeqBegin(Vector3 origin, VC_MaterialId mat, float scale);

// Add a beat at `t` seconds after play. Beats may be added in any order; they
// are sorted on Play. Adding after Play is IGNORED (and warned once) — a
// sequence's track is fixed when it starts, otherwise "why did that beat not
// fire" has two possible causes and no way to tell them apart.
void VFX_SeqAt(VFX_Sequence *s, float t, VFX_Beat beat);

// Opt this sequence out of time scaling (see THE CLOCK above). Before Play.
void VFX_SeqSetUnscaled(VFX_Sequence *s, bool unscaled);

// Start playing. Returns a handle for VFX_SeqStop, or -1 if `s` is NULL.
//
// NOTE — deviation from ELDEN_VFX_SPEC.md §E3, which lists `void VFX_SeqPlay`
// alongside `VFX_SeqStop(int handle)`: that pair leaves no way to ever OBTAIN a
// handle, so Stop would be uncallable. Returning it is the minimal fix.
int VFX_SeqPlay(VFX_Sequence *s);

// Stop a playing sequence; its unfired beats never fire. Safe on a stale handle.
void VFX_SeqStop(int handle);

// Standard 4-phase envelope, pre-filled: a charge-up light, the burst (light +
// shake + radial + distortion), a sustain light, and a dissipating fade. The
// caller then adds its own COMPOSE beats — the preset cannot know what your
// effect actually spawns, only when it should land.
//
// Phase arguments are DURATIONS in seconds, not timestamps:
//   anticipation — wind-up before the hit lands
//   burst        — the hit itself
//   sustain      — how long it holds
//   dissipate    — the tail
VFX_Sequence *VFX_SeqPreset(Vector3 origin, VC_MaterialId mat, float scale,
                            float anticipation, float burst,
                            float sustain, float dissipate);

// Driven from VFX_Compose_Update / VFX_Compose_Draw3D — no main.c wiring.
void VFX_Sequence_Update(float scaledDt);
void VFX_Sequence_GetStats(int *playing, int *max);

#endif // VFX_SEQUENCE_H
