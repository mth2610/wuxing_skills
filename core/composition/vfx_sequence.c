#include "core/composition/vfx_sequence.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/time_fx.h"
#include "core/screen_distort.h"
#include "core/post_fx.h"
#include "core/decals/decal_system.h"
#include "core/presets/vfx_presets.h"
#include <stddef.h>

typedef struct {
    float   t;
    bool    fired;
    VFX_Beat beat;
} VFX_SeqBeatSlot;

struct VFX_Sequence {
    bool    inUse;
    bool    playing;
    bool    unscaled;
    Vector3 origin;
    VC_MaterialId mat;
    float   scale;
    float   clock;
    float   lastBeatT;
    int     beatCount;
    unsigned int serial;      // play order, for "recycle the oldest playing"
    VFX_SeqBeatSlot beats[VFX_SEQ_MAX_BEATS];
};

static struct VFX_Sequence s_seqs[VFX_SEQ_MAX];
static unsigned int        s_seqSerial = 1;

// ─────────────────────────────────────────────────────────────────────────────

static void VFX_SeqReset(struct VFX_Sequence *s, Vector3 origin, VC_MaterialId mat, float scale)
{
    s->inUse     = true;
    s->playing   = false;
    s->unscaled  = false;
    s->origin    = origin;
    s->mat       = mat;
    s->scale     = (scale > 0.0f) ? scale : 1.0f;
    s->clock     = 0.0f;
    s->lastBeatT = 0.0f;
    s->beatCount = 0;
    s->serial    = 0;
}

VFX_Sequence *VFX_SeqBegin(Vector3 origin, VC_MaterialId mat, float scale)
{
    for (int i = 0; i < VFX_SEQ_MAX; i++) {
        if (!s_seqs[i].inUse) {
            VFX_SeqReset(&s_seqs[i], origin, mat, scale);
            return &s_seqs[i];
        }
    }

    // Pool full. Recycle the oldest PLAYING sequence — a fresh effect the player
    // is about to see matters more than the tail of one already half over. A
    // sequence still BUILDING is never stolen: someone holds that pointer and is
    // mid-authoring, and yanking it would corrupt their beats.
    int oldest = -1;
    unsigned int oldestSerial = 0xFFFFFFFFu;
    for (int i = 0; i < VFX_SEQ_MAX; i++) {
        if (s_seqs[i].playing && s_seqs[i].serial < oldestSerial) {
            oldestSerial = s_seqs[i].serial;
            oldest = i;
        }
    }
    if (oldest < 0) {
        // Every slot is held by an unplayed sequence: that is a caller leaking
        // VFX_SeqBegin without VFX_SeqPlay, not a busy frame. Say so — returning
        // NULL silently would present as "my effect randomly doesn't play".
        static bool s_warnedLeak = false;
        if (!s_warnedLeak) {
            s_warnedLeak = true;
            TraceLog(LOG_WARNING,
                     "VFX_SEQ: pool exhausted (%d) and NOTHING is playing — a caller is "
                     "calling VFX_SeqBegin without VFX_SeqPlay. Sequences will be dropped.",
                     VFX_SEQ_MAX);
        }
        return NULL;
    }

    static bool s_warnedFull = false;
    if (!s_warnedFull) {
        s_warnedFull = true;
        TraceLog(LOG_WARNING,
                 "VFX_SEQ: pool full (%d) — recycling the oldest playing sequence. "
                 "Raise VFX_SEQ_MAX if this is normal load, not a leak.", VFX_SEQ_MAX);
    }
    VFX_SeqReset(&s_seqs[oldest], origin, mat, scale);
    return &s_seqs[oldest];
}

void VFX_SeqAt(VFX_Sequence *s, float t, VFX_Beat beat)
{
    if (s == NULL || !s->inUse) return;

    if (s->playing) {
        // Documented in the header: the track is fixed at Play. Warn rather than
        // silently ignore — "that beat never fired" would otherwise have two
        // indistinguishable causes.
        static bool s_warnedLate = false;
        if (!s_warnedLate) {
            s_warnedLate = true;
            TraceLog(LOG_WARNING,
                     "VFX_SEQ: VFX_SeqAt called AFTER VFX_SeqPlay — beat ignored. "
                     "Author the whole track before playing it.");
        }
        return;
    }

    if (s->beatCount >= VFX_SEQ_MAX_BEATS) {
        static bool s_warnedBeats = false;
        if (!s_warnedBeats) {
            s_warnedBeats = true;
            TraceLog(LOG_WARNING,
                     "VFX_SEQ: sequence is full (%d beats) — beat dropped. "
                     "Raise VFX_SEQ_MAX_BEATS.", VFX_SEQ_MAX_BEATS);
        }
        return;
    }

    if (t < 0.0f) t = 0.0f;
    s->beats[s->beatCount].t     = t;
    s->beats[s->beatCount].fired = false;
    s->beats[s->beatCount].beat  = beat;
    s->beatCount++;
    if (t > s->lastBeatT) s->lastBeatT = t;
}

void VFX_SeqSetUnscaled(VFX_Sequence *s, bool unscaled)
{
    if (s == NULL || !s->inUse || s->playing) return;
    s->unscaled = unscaled;
}

int VFX_SeqPlay(VFX_Sequence *s)
{
    if (s == NULL || !s->inUse) return -1;

    // Sort by time. Beats may be authored in any order, and Update relies on
    // ascending `t` to fire them in the right order when several land in one
    // frame. Insertion sort: 24 elements, once, at play time.
    for (int i = 1; i < s->beatCount; i++) {
        VFX_SeqBeatSlot key = s->beats[i];
        int j = i - 1;
        while (j >= 0 && s->beats[j].t > key.t) {
            s->beats[j + 1] = s->beats[j];
            j--;
        }
        s->beats[j + 1] = key;
    }

    s->clock   = 0.0f;
    s->playing = true;
    s->serial  = s_seqSerial++;
    return (int)(s - s_seqs);
}

void VFX_SeqStop(int handle)
{
    if (handle < 0 || handle >= VFX_SEQ_MAX) return;
    s_seqs[handle].playing = false;
    s_seqs[handle].inUse   = false;
}

// ─────────────────────────────────────────────────────────────────────────────

static void VFX_SeqFireBeat(const struct VFX_Sequence *s, const VFX_Beat *b)
{
    Vector3 pos = {
        s->origin.x + b->offset.x * s->scale,
        s->origin.y + b->offset.y * s->scale,
        s->origin.z + b->offset.z * s->scale,
    };

    // {0,0,0,0} means "the element decides". `glow` rather than `body`: a beat
    // is a moment of emission — a flash, a spark, a light — and glow is the hot
    // identity colour of a material.
    Color col = b->color;
    if (col.r == 0 && col.g == 0 && col.b == 0 && col.a == 0)
        col = VFX_Material(s->mat)->glow;

    float scale = (b->a > 0.0f) ? b->a : 1.0f;

    switch (b->kind)
    {
    case VFX_BEAT_COMPOSE:
    case VFX_BEAT_CALLBACK:
        // Same mechanism, different intent, and the split is load-bearing: a
        // future quality-tier filter may drop COMPOSE beats on a weak device,
        // but must never drop CALLBACK — that is where gameplay and audio hang.
        if (b->cb) b->cb(pos, s->scale * scale, b->ud);
        break;

    case VFX_BEAT_LIGHT:
        VFXLight_Spawn(pos, col,
                       ((b->a > 0.0f) ? b->a : 1.5f) * s->scale,
                       (b->b > 0.0f) ? b->b : 0.25f,
                       // Only two priorities exist (vfx_light.h). LOW: a sequence is a
                       // normal skill beat and must never evict an ultimate's light.
                       VFX_PRIORITY_LOW);
        break;

    case VFX_BEAT_SHAKE:
        CameraFX_Shake((b->a > 0.0f) ? b->a : 0.3f);
        break;

    case VFX_BEAT_HITSTOP:
        // timeScale 0 would freeze time outright and never resume on some
        // paths; 0.05 is "almost stopped" and is what callers actually want.
        TimeFX_Hitstop((b->a > 0.0f) ? b->a : 0.06f,
                       (b->b > 0.0f) ? b->b : 0.05f);
        break;

    case VFX_BEAT_DISTORT:
        ScreenDistort_Add(pos,
                          ((b->a > 0.0f) ? b->a : 0.45f) * s->scale,
                          (b->b > 0.0f) ? b->b : 0.35f,
                          (b->c > 0.0f) ? b->c : 0.35f,
                          1.0f);
        break;

    case VFX_BEAT_RADIAL:
        PostFX_RadialBurst(pos,
                           (b->a > 0.0f) ? b->a : 0.15f,
                           (b->b > 0.0f) ? b->b : 0.5f);
        break;

    case VFX_BEAT_DECAL:
        // `ud` carries the texture — the beat struct deliberately has no
        // Texture2D field, since only this one kind needs it and every other
        // beat would pay for the size.
        if (b->ud != NULL) {
            DecalSystem_Add(pos, b->c,
                            ((b->a > 0.0f) ? b->a : 1.0f) * s->scale,
                            *(Texture2D *)b->ud,
                            (b->b > 0.0f) ? b->b : 3.0f, col);
        }
        break;

    default:
        break;
    }
}

void VFX_Sequence_Update(float scaledDt)
{
    // Unscaled sequences need the RAW frame time. `scaledDt` has already been
    // through TimeFX_Apply by the time VFX_Compose_Update receives it, so it
    // cannot be un-scaled back without knowing the factor — take the raw value
    // from the source instead.
    float rawDt = GetFrameTime();

    for (int i = 0; i < VFX_SEQ_MAX; i++) {
        struct VFX_Sequence *s = &s_seqs[i];
        if (!s->inUse || !s->playing) continue;

        s->clock += s->unscaled ? rawDt : scaledDt;

        // Fire EVERY beat whose time has passed, in ascending order (the array
        // was sorted at Play). Firing at most one per frame — or skipping beats
        // that a long frame jumped over — would let a frame spike swallow the
        // hitstop, which is exactly the beat you least want dropped.
        bool allFired = true;
        for (int b = 0; b < s->beatCount; b++) {
            if (s->beats[b].fired) continue;
            if (s->beats[b].t <= s->clock) {
                s->beats[b].fired = true;
                VFX_SeqFireBeat(s, &s->beats[b].beat);
            } else {
                allFired = false;
                break;   // sorted: everything after this is later still
            }
        }

        // Retire once the track is done. The small tail past the last beat costs
        // nothing and keeps a slot from being reused in the same frame it ends,
        // which would make handles ambiguous for a caller stopping it late.
        if (allFired && s->clock >= s->lastBeatT) {
            s->playing = false;
            s->inUse   = false;
        }
    }
}

void VFX_Sequence_GetStats(int *playing, int *max)
{
    int n = 0;
    for (int i = 0; i < VFX_SEQ_MAX; i++)
        if (s_seqs[i].inUse && s_seqs[i].playing) n++;
    if (playing) *playing = n;
    if (max) *max = VFX_SEQ_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────

VFX_Sequence *VFX_SeqPreset(Vector3 origin, VC_MaterialId mat, float scale,
                            float anticipation, float burst,
                            float sustain, float dissipate)
{
    VFX_Sequence *s = VFX_SeqBegin(origin, mat, scale);
    if (s == NULL) return NULL;

    if (anticipation < 0.0f) anticipation = 0.0f;
    if (burst       <= 0.0f) burst        = 0.08f;
    if (sustain     <  0.0f) sustain      = 0.0f;
    if (dissipate   <  0.0f) dissipate    = 0.0f;

    // The four phase boundaries as absolute times on the track.
    float tBurst     = anticipation;
    float tSustain   = tBurst + burst;
    float tDissipate = tSustain + sustain;
    float tEnd       = tDissipate + dissipate;

    // ANTICIPATION — a small, growing light and nothing else. The wind-up must
    // read as "something is coming" without competing with the hit; anything
    // loud here steals the burst's impact.
    if (anticipation > 0.0f) {
        VFX_SeqAt(s, 0.0f, (VFX_Beat){
            .kind = VFX_BEAT_LIGHT, .a = 0.8f, .b = anticipation * 1.1f });
    }

    // BURST — everything lands on ONE frame. That simultaneity is the whole
    // point of a beat track: light, shake, screen warp and the radial smear
    // arriving together is what the eye reads as a single violent event, and it
    // is exactly what hand-coded per-skill timers keep getting slightly wrong.
    VFX_SeqAt(s, tBurst, (VFX_Beat){ .kind = VFX_BEAT_LIGHT,   .a = 3.0f, .b = burst + sustain });
    VFX_SeqAt(s, tBurst, (VFX_Beat){ .kind = VFX_BEAT_SHAKE,   .a = 0.35f });
    VFX_SeqAt(s, tBurst, (VFX_Beat){ .kind = VFX_BEAT_DISTORT, .a = 0.6f, .b = 0.4f, .c = 0.35f });
    VFX_SeqAt(s, tBurst, (VFX_Beat){ .kind = VFX_BEAT_RADIAL,  .a = 0.15f, .b = 0.45f });

    // SUSTAIN — a dimmer, longer light holding the moment open.
    if (sustain > 0.0f) {
        VFX_SeqAt(s, tSustain, (VFX_Beat){
            .kind = VFX_BEAT_LIGHT, .a = 1.6f, .b = sustain + dissipate });
    }

    // DISSIPATE — a last faint light so the effect fades instead of cutting.
    // A hard cut at the end is what makes an effect read as "it stopped"
    // rather than "it finished".
    if (dissipate > 0.0f) {
        VFX_SeqAt(s, tDissipate, (VFX_Beat){
            .kind = VFX_BEAT_LIGHT, .a = 0.7f, .b = dissipate });
    }

    // Anchor the track's end even when the caller adds no later beats, so the
    // sequence lives the full envelope it was asked for.
    if (tEnd > s->lastBeatT) s->lastBeatT = tEnd;

    // NOT filled in: hitstop and COMPOSE beats. Hitstop is a GAMEPLAY decision
    // (it stops the world, so a preset must not impose it), and the preset
    // cannot know what your effect spawns — only when it should land.
    return s;
}
