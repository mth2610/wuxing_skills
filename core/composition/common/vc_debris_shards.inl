// ── P3. VFX_ComposeDebrisShards — chips, not sparks ──────────────────────────
//
// REWRITTEN from `VFX_ComposeShardDebris`, 30/07/2026. That was a pre-Đợt-E
// survivor kept as a working scaffold with a planned end date, not a survivor of
// the F0 purge, and it carried four defects that are all named landmines:
//
//   1. IT WAS DRAWN WITH A LIT `EffectMaterial` (MAT_ROCK / MAT_METAL / ...).
//      Lit geometry in the night arena is black-on-black — ENGINE_LANDMINES §3,
//      and the reason `VFX_ComposeGroundWave` is additive with an AUTHORED
//      shading gradient. The chips were being lit by a scene with almost no
//      light in it.
//   2. `rlDisableBackfaceCulling()` ... draw ... `rlEnableBackfaceCulling()`
//      WITH NO BATCH FLUSH. rlgl queues the geometry and draws it LATER, after
//      culling has been switched back on, so the state the chips were submitted
//      under was never the state they were drawn under (ENGINE_LANDMINES, "The
//      batch-flush rule applies to BACKFACE CULLING too", 30/07).
//   3. THE DUST WAS FRAMERATE-DEPENDENT: `if (GetRandomValue(0, 100) < 25)` per
//      frame is a 25%-per-FRAME chance, so a 120 fps machine got twice the dust
//      of a 60 fps one. Emission is a RATE with a carried fraction, always.
//   4. It ignored the element entirely — every chip drew WHITE and the material
//      only picked a shader preset.
//
// WHAT A CHIP IS, and why it is not a sprite: **a thing that is the same shape
// from every angle is a spark.** Debris reads as debris because it is angular,
// because it is SQUASHED (a chip is flatter than it is wide), and because it
// TUMBLES — and the tumble is only visible if the faces change brightness as
// they turn. So each chip is a jittered, squashed box, and each FACE is shaded
// flat against an authored key direction on the CPU. That is the whole trick:
// the shading is authored rather than lit, which keeps it out of landmine 1
// while still giving the tumble something to catch.
//
// WHY NOT `DrawCoreCube`, which the plan named: it takes ONE `Color` for the
// whole box, so it cannot express per-face shading, and a single-colour box
// tumbling in a dark scene is a silhouette that never changes. The geometry here
// IS a squashed cube with per-instance jitter — it is the shading that
// DrawCoreCube has no parameter for.
//
// ONE-SHOT: `count` chips per CALL, from an impact or a break. Never call this
// from a draw path (core/docs/LANDMINES.md, "A sequence called from Draw
// restarts every frame") — it is a burst, not an emitter.
//
// Managed archetype: private pool + VC_DebrisShards_Update/_Draw3D.

#define DEBRIS_MAX 128         // pool
#define DEBRIS_PER_CALL_MAX 24 // ceiling on one burst
// Dust shed by a chip in flight, motes per second. A RATE with a fraction
// carried between frames, per chip — see defect 3 above.
#define DEBRIS_DUST_RATE 9.0f
#define DEBRIS_DUST_BATCH_MAX 2 // per chip per frame, so a hitch cannot burst
// Motes thrown on a ground hit. A COUNT, correctly: it is an event, not a flow.
#define DEBRIS_HIT_MOTES 2
// A chip is FLATTER than it is wide. 1.0 x 0.62 x 0.34 of the caller's scale,
// jittered per instance — this ratio is what stops a "chip" reading as a pebble.
#define DEBRIS_SQUASH_Y 0.62f
#define DEBRIS_SQUASH_Z 0.34f
// Real gravity, and the scale rule says to judge every force against it
// (core/tuning.h §3b). Chips are heavy; they fall at g.
#define DEBRIS_GRAVITY 9.81f
#define DEBRIS_AIR_DRAG 0.4f
// Ground bounce: how much speed survives, and how much the tumble is damped.
#define DEBRIS_BOUNCE 0.45f
#define DEBRIS_FRICTION 0.6f
#define DEBRIS_SPIN_DAMP 0.7f
#define DEBRIS_REST_SPEED 0.25f // below this on a bounce, it settles

typedef struct
{
    bool active;
    Vector3 position;
    Vector3 velocity;
    Vector3 spinAxis;
    float spinAngle;  // degrees
    float spinSpeed;  // degrees/sec
    Vector3 halfSize; // metres, per axis — the squash
    float elapsed;
    float lifetime;
    float dustAcc; // carried fraction of the dust rate
    unsigned int seed;
    VC_MaterialId matId;
} VC_DebrisShard;

static VC_DebrisShard s_debris[DEBRIS_MAX];
static bool s_debrisInit = false;

// THE KEY DIRECTION. Authored, not sampled: there is no light to sample in the
// night arena, and that is the whole point of landmine 1. Roughly overhead and
// off to one side, so a tumbling chip sweeps through it once per revolution.
static const Vector3 k_debrisKey = {-0.35f, 0.86f, 0.37f};
// Ambient floor, diffuse gain, and a tight specular lobe. The AMBIENT is what
// stops a face going pure black (a black facet in a dark scene is a hole, not a
// shadow); the SPECULAR is what makes the tumble read as catching light rather
// than merely changing value.
#define DEBRIS_AMBIENT 0.22f
#define DEBRIS_DIFFUSE 0.78f
#define DEBRIS_SPEC_POW 26.0f
#define DEBRIS_SPEC_GAIN 0.85f
// The last quarter of a chip's life is its fade. Chips OCCLUDE, so they are
// BLEND_ALPHA and the fade is a real dissolve of opacity.
#define DEBRIS_FADE_FROM 0.75f

static float s_debrisScaleMul = 1.0f;
static float s_debrisSpinMul = 1.0f;
static float s_debrisDustMul = 1.0f;
static float s_debrisLifeMul = 1.0f;

static void DebrisShards_InitShared(void)
{
    if (s_debrisInit)
        return;
    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (docs/LANDMINES.md).
    Tuning_RegisterFloat("debris_scale", &s_debrisScaleMul, 1.0f);
    Tuning_RegisterFloat("debris_spin", &s_debrisSpinMul, 1.0f);
    Tuning_RegisterFloat("debris_dust", &s_debrisDustMul, 1.0f);
    Tuning_RegisterFloat("debris_life", &s_debrisLifeMul, 1.0f);
    s_debrisInit = true;
}

// ── The tier gate ───────────────────────────────────────────────────────────
//
// It may only ever clamp DOWN, and it scales the COUNT rather than switching the
// effect off: a low tier gets fewer chips of the same effect, not a different
// one. The floor is 1 — a burst that asks for debris and gets none is a gate
// that turned the feature off, which is not a tier.
static int DebrisShards_TierCount(int requested)
{
    if (requested < 1)
        return 0;
    int n;
    switch (GfxQuality_Get())
    {
    case GFX_HIGH: n = requested; break;
    case GFX_MED:  n = (requested * 3) / 4; break;
    case GFX_LOW:  n = requested / 2; break;
    default:       n = requested / 3; break;
    }
    if (n < 1)
        n = 1;
    if (n > DEBRIS_PER_CALL_MAX)
        n = DEBRIS_PER_CALL_MAX;
    return n;
}

// A per-chip LCG, so a chip's silhouette is stable across frames without storing
// eight offsets per chip. Same generator the scaffold used.
static float DebrisShards_Rand(unsigned int *state)
{
    *state = *state * 1103515245u + 12345u;
    return (float)((*state / 65536u) % 32768u) / 32767.0f;
}

// ── Public API ──────────────────────────────────────────────────────────────

// `vel` is the burst's BASE velocity in m/s — chips spread around it. Pass a
// zero vector for the classic upward scatter off a surface. `scale` is the
// chip's longest axis in metres. `count` chips per CALL, one-shot.
void VFX_ComposeDebrisShards(Vector3 pos, Vector3 vel, VC_MaterialId mat,
                             float scale, int count)
{
    DebrisShards_InitShared();
    if (scale <= 0.0f)
        scale = 0.05f;
    int want = DebrisShards_TierCount(count);
    if (want <= 0)
        return;

    float baseSpeed = Vector3Length(vel);
    // With no base velocity the burst is the classic scatter off a surface:
    // upward-biased, so the chips arc and fall rather than skidding away flat.
    bool scatter = (baseSpeed < 0.01f);

    int spawned = 0;
    for (int i = 0; i < DEBRIS_MAX && spawned < want; i++)
    {
        VC_DebrisShard *d = &s_debris[i];
        if (d->active)
            continue;

        d->active = true;
        d->position = pos;

        Vector3 dir;
        float speed;
        if (scatter)
        {
            float yaw = Random01() * 2.0f * PI;
            float pitch = (Random01() * 70.0f + 20.0f) * DEG2RAD;
            dir = Vector3Normalize((Vector3){cosf(yaw) * cosf(pitch),
                                             sinf(pitch),
                                             sinf(yaw) * cosf(pitch)});
            speed = Math_Mix(1.4f, 3.2f, Random01());
        }
        else
        {
            // A CONE around the burst direction, not a sphere: debris off an
            // impact carries the impact's momentum, and a symmetric spray reads
            // as a firework instead.
            dir = VC_DirCone(Vector3Scale(vel, 1.0f / baseSpeed), 0.85f,
                             Random01(), Random01());
            speed = baseSpeed * Math_Mix(0.55f, 1.35f, Random01());
        }
        d->velocity = Vector3Scale(dir, speed);

        d->spinAxis = Vector3Normalize((Vector3){Random01() * 2.0f - 1.0f,
                                                 Random01() * 2.0f - 1.0f,
                                                 Random01() * 2.0f - 1.0f});
        d->spinAngle = Random01() * 360.0f;
        // Fast, and VARIED: chips tumbling at one rate read as a rigid cloud.
        d->spinSpeed = Math_Mix(180.0f, 720.0f, Random01()) * s_debrisSpinMul;

        // The squash, with per-instance variation on top of the base ratio.
        float s = scale * Math_Mix(0.55f, 1.0f, Random01()) * s_debrisScaleMul;
        d->halfSize = (Vector3){
            s * 0.5f,
            s * 0.5f * DEBRIS_SQUASH_Y * Math_Mix(0.8f, 1.25f, Random01()),
            s * 0.5f * DEBRIS_SQUASH_Z * Math_Mix(0.8f, 1.25f, Random01()),
        };

        d->elapsed = 0.0f;
        d->lifetime = Math_Mix(0.55f, 1.10f, Random01()) * s_debrisLifeMul;
        d->dustAcc = 0.0f;
        d->seed = (unsigned int)(Random01() * 100000.0f) + 1u;
        d->matId = mat;
        spawned++;
    }

    if (spawned < want)
    {
        // Announced. A burst that silently drops half its chips and one that was
        // authored with half as many look identical (core/CLAUDE.md §4).
        TraceLog(LOG_WARNING,
                 "VFX_DEBRIS: pool full — %d of %d chips spawned (pool %d)",
                 spawned, want, DEBRIS_MAX);
    }
}

// ── Per-frame ───────────────────────────────────────────────────────────────

static void VC_DebrisShards_Update(float dt)
{
    if (dt <= 0.0f)
        return;

    for (int i = 0; i < DEBRIS_MAX; i++)
    {
        VC_DebrisShard *d = &s_debris[i];
        if (!d->active)
            continue;

        d->elapsed += dt;
        if (d->elapsed >= d->lifetime)
        {
            d->active = false;
            continue;
        }

        d->velocity.y -= DEBRIS_GRAVITY * dt;
        d->velocity = Vector3Scale(d->velocity, 1.0f - DEBRIS_AIR_DRAG * dt);
        d->position = Vector3Add(d->position, Vector3Scale(d->velocity, dt));
        d->spinAngle += d->spinSpeed * dt;

        // DUST IN FLIGHT — a RATE with the fraction carried between frames. The
        // version this replaced rolled a 25% chance per FRAME, which made the
        // dust twice as dense at 120 fps as at 60.
        const VFX_ElementMaterial *m = VFX_Material(d->matId);
        d->dustAcc += dt * DEBRIS_DUST_RATE * s_debrisDustMul;
        int motes = (int)d->dustAcc;
        if (motes > DEBRIS_DUST_BATCH_MAX)
            motes = DEBRIS_DUST_BATCH_MAX;
        d->dustAcc -= (float)motes;
        for (int p = 0; p < motes; p++)
        {
            SpawnParticle((ParticleConfig){
                .position = d->position,
                .velocity = (Vector3){(Random01() - 0.5f) * 0.2f,
                                      (Random01() - 0.5f) * 0.2f,
                                      (Random01() - 0.5f) * 0.2f},
                .radius = d->halfSize.x * 0.30f + Random01() * 0.008f,
                .lifetime = Math_Mix(0.12f, 0.30f, Random01()),
                // The dust EMITS — it is the heat/glint coming off the chip, not
                // the chip. Additive and unlit, per the blend law; the chip
                // itself is the alpha half of the pair.
                .colorStart = VC_WithAlpha(VC_Whiten(m->glow, 0.55f), 210),
                .colorEnd = VC_WithAlpha(m->glow, 0),
                .render.blendMode = VFX_BLEND_ADDITIVE,
                .render.unlit = 1,
                .render.emissiveBoost = 1.7f,
            });
        }

        // GROUND. A bounce is an EVENT, so its motes are a count, not a rate.
        if (d->position.y < 0.0f)
        {
            d->position.y = 0.0f;
            d->velocity.y = -d->velocity.y * DEBRIS_BOUNCE;
            d->velocity.x *= DEBRIS_FRICTION;
            d->velocity.z *= DEBRIS_FRICTION;
            d->spinSpeed *= DEBRIS_SPIN_DAMP;

            for (int p = 0; p < DEBRIS_HIT_MOTES; p++)
            {
                SpawnParticle((ParticleConfig){
                    .position = d->position,
                    .velocity = (Vector3){(Random01() - 0.5f) * 1.2f,
                                          Random01() * 0.8f,
                                          (Random01() - 0.5f) * 1.2f},
                    .radius = d->halfSize.x * 0.45f + Random01() * 0.012f,
                    .lifetime = Math_Mix(0.15f, 0.30f, Random01()),
                    .colorStart = VC_WithAlpha(VC_Whiten(m->glow, 0.45f), 200),
                    .colorEnd = VC_WithAlpha(m->glow, 0),
                    .render.blendMode = VFX_BLEND_ADDITIVE,
                    .render.unlit = 1,
                    .render.emissiveBoost = 1.5f,
                });
            }

            if (fabsf(d->velocity.y) < DEBRIS_REST_SPEED)
            {
                d->velocity = (Vector3){0.0f, 0.0f, 0.0f};
                d->spinSpeed = 0.0f;
            }
        }
    }
}

// ── Draw ────────────────────────────────────────────────────────────────────

// One squashed, jittered box with FLAT-SHADED faces.
//
// The jitter is a fraction of each axis's OWN half-extent rather than an
// absolute distance. That is not tidiness: a chip is thin in z, and an absolute
// jitter large enough to be visible on the long axis would push a vertex through
// the opposite face on the short one — the box turns inside out and, with
// backface culling on, parts of it vanish.
static void DebrisShards_DrawChip(const VC_DebrisShard *d, Color base, float alpha01)
{
    unsigned int st = d->seed;
    Vector3 v[8];
    const float sx[8] = {-1, 1, 1, -1, -1, 1, 1, -1};
    const float sy[8] = {-1, -1, 1, 1, -1, -1, 1, 1};
    const float sz[8] = {-1, -1, -1, -1, 1, 1, 1, 1};
    for (int i = 0; i < 8; i++)
    {
        v[i].x = sx[i] * d->halfSize.x * (1.0f + (DebrisShards_Rand(&st) - 0.5f) * 0.55f);
        v[i].y = sy[i] * d->halfSize.y * (1.0f + (DebrisShards_Rand(&st) - 0.5f) * 0.55f);
        v[i].z = sz[i] * d->halfSize.z * (1.0f + (DebrisShards_Rand(&st) - 0.5f) * 0.55f);
    }

    // The chip's rotation, needed on the CPU: the faces are shaded against a
    // WORLD key direction, so their normals have to be taken into world space
    // here. Doing it in the vertex stream instead would shade the chip as if it
    // never turned, which is exactly the thing the tumble exists to show.
    Matrix rot = MatrixRotate(d->spinAxis, d->spinAngle * DEG2RAD);

    static const int faces[6][4] = {
        {0, 3, 2, 1}, {1, 2, 6, 5}, {5, 6, 7, 4},
        {4, 7, 3, 0}, {3, 7, 6, 2}, {4, 0, 1, 5},
    };

    rlBegin(RL_TRIANGLES);
    for (int f = 0; f < 6; f++)
    {
        Vector3 a = v[faces[f][0]], b = v[faces[f][1]];
        Vector3 c = v[faces[f][2]], e = v[faces[f][3]];
        Vector3 n = Vector3CrossProduct(Vector3Subtract(e, a), Vector3Subtract(b, a));
        if (Vector3LengthSqr(n) < 1e-12f)
            continue; // a degenerate face contributes nothing and normalises to garbage
        n = Vector3Normalize(Vector3Transform(Vector3Normalize(n), rot));

        float ndl = Vector3DotProduct(n, k_debrisKey);
        if (ndl < 0.0f)
            ndl = 0.0f;
        float shade = DEBRIS_AMBIENT + DEBRIS_DIFFUSE * ndl;
        float glint = powf(ndl, DEBRIS_SPEC_POW) * DEBRIS_SPEC_GAIN;

        float r = (float)base.r * shade + 255.0f * glint;
        float g = (float)base.g * shade + 255.0f * glint;
        float bl = (float)base.b * shade + 255.0f * glint;
        rlColor4ub((unsigned char)Clamp(r, 0.0f, 255.0f),
                   (unsigned char)Clamp(g, 0.0f, 255.0f),
                   (unsigned char)Clamp(bl, 0.0f, 255.0f),
                   (unsigned char)Clamp(255.0f * alpha01, 0.0f, 255.0f));

        rlVertex3f(a.x, a.y, a.z);
        rlVertex3f(b.x, b.y, b.z);
        rlVertex3f(c.x, c.y, c.z);
        rlVertex3f(a.x, a.y, a.z);
        rlVertex3f(c.x, c.y, c.z);
        rlVertex3f(e.x, e.y, e.z);
    }
    rlEnd();
}

static void VC_DebrisShards_Draw3D(Camera3D cam)
{
    (void)cam;
    int live = 0;
    for (int i = 0; i < DEBRIS_MAX; i++)
        if (s_debris[i].active) { live = 1; break; }
    if (!live)
        return;

    // FLUSHED ON BOTH SIDES OF EVERY STATE CHANGE THE GEOMETRY DEPENDS ON —
    // depth mask, depth test, blend mode AND culling (ENGINE_LANDMINES rule 1
    // and its 30/07 postscript). rlgl batches immediate-mode geometry and draws
    // it LATER, so the state at DRAW time is what applies, not the state when
    // the triangles were queued. The version this replaced flipped culling
    // around the draw with no flush at all.
    //
    // The three states, and why each is what it is:
    //   depth mask ON  — chips are SOLID and must occlude each other.
    //   culling    ON  — they are closed boxes, so the far wall is pure waste.
    //   BLEND_ALPHA    — a chip BLOCKS light. The blend law. Its dust is the
    //                    additive half of the pair and is drawn by the particle
    //                    system, not here.
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    rlDrawRenderBatchActive();

    for (int i = 0; i < DEBRIS_MAX; i++)
    {
        const VC_DebrisShard *d = &s_debris[i];
        if (!d->active)
            continue;

        float t01 = d->elapsed / d->lifetime;
        float alpha = 1.0f;
        if (t01 > DEBRIS_FADE_FROM)
            alpha = 1.0f - (t01 - DEBRIS_FADE_FROM) / (1.0f - DEBRIS_FADE_FROM);

        const VFX_ElementMaterial *m = VFX_Material(d->matId);
        rlPushMatrix();
        rlTranslatef(d->position.x, d->position.y, d->position.z);
        // The vertices already carry the chip's size, and the shading normals are
        // rotated on the CPU — so the matrix only places it. A scale here would
        // silently change the jitter's meaning too.
        rlRotatef(d->spinAngle, d->spinAxis.x, d->spinAxis.y, d->spinAxis.z);
        DebrisShards_DrawChip(d, m->body, alpha);
        rlPopMatrix();
    }

    rlDrawRenderBatchActive();
    EndBlendMode();
    rlDrawRenderBatchActive();
}
