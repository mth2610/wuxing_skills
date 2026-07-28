#include "glacial_cannon_skill.h"
#include "core/skill_boilerplate.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/path_spline.h"
#include "core/procedural_mesh_utils.h"
#include "core/utils_math.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/resource_manager.h"
#include "combat/combat.h"
#include "raymath.h"

#define MAX_INSTANCES 4

// ── The ice ridge ───────────────────────────────────────────────────────────
// SPACING IS IN METRES, not in path indices. The path is always 16 points
// whatever the range, so stepping by index put the eruptions 0.3 m apart on a
// short cast and 2 m apart on a long one — the ridge came out as separated
// lumps whose gaps changed with the target, which is what the owner saw. Density
// has to be a property of the WORLD, not of the array that happens to describe
// the path.
#define ICE_WAVE_SPACING   0.42f   // metres between eruptions
#define ICE_WAVE_MAX       28      // hard cap, so a long-range cast cannot flood
// Crystals per eruption. Each burst scatters within a fixed radius, so a few
// eruptions this close together OVERLAP into one continuous ridge instead of a
// row of separate clumps.
#define ICE_WAVE_CRYSTALS  4
// How long an eruption takes to finish growing, as a fraction of the wave's
// total travel. Short: the ice has to look like it is CHASING the front.
#define ICE_WAVE_GROW      0.18f
// Sideways scatter of each eruption off the centre line, in metres. A perfectly
// straight row of ice reads as a fence (WUXING_ART_DIRECTION: perpendicular
// jitter). Deterministic per eruption index — never per frame.
#define ICE_WAVE_JITTER    0.30f

// ── Cold mist ───────────────────────────────────────────────────────────────
// The blue energy mist from the water stream (`tube_skill.c`), not the generic
// dark smoke puff: same gradient, same force profile, so the two water skills
// read as the same element. Sizes are larger and lifetimes longer, because this
// one hangs over a frozen ridge instead of trailing a fast projectile.
//
// Emitted as a RATE from Update (which has dt), never from Draw: a fixed number
// per frame makes the density depend on the frame rate. (The tube spawns its
// mist on a per-frame `GetRandomValue(0,100) < 60` roll, which has exactly that
// bug — worth fixing there separately, not worth copying here.)
#define ICE_MIST_RATE      26.0f   // particles/sec along the travelling front
#define ICE_MIST_BURST     14      // particles thrown out once, on impact
#define ICE_MIST_GRAVITY   1.20f   // m/s^2 down — enough to settle, not to bury
#define ICE_MIST_NOISE     0.18f
#define ICE_MIST_DRAG      3.20f   // high: it spreads out then stops, like fog
#define ICE_MIST_RADIUS_MIN 0.05f
#define ICE_MIST_RADIUS_MAX 0.13f
#define ICE_MIST_LIFE_MIN  0.55f
#define ICE_MIST_LIFE_MAX  1.15f

static ColorGradient s_iceMistGrad;
static ForceField    s_iceMistField;
static bool          s_iceMistReady = false;

static void IceMist_InitShared(void)
{
    if (s_iceMistReady) return;
    // The tube's exact fade: element water, holding most of its alpha then
    // dropping. Using the shared helper rather than hand-stopping the gradient
    // is what keeps the two skills matching when the element colour is retuned.
    ColorGradient_StandardFade(&s_iceMistGrad, ELEMENT_COLOR_WATER, 0.40f, 0.2f);

    ForceField_Clear(&s_iceMistField);
    ForceField_AddLayer(&s_iceMistField, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = (Vector3){0.0f, -1.0f, 0.0f},
        .strength = ICE_MIST_GRAVITY});
    ForceField_AddLayer(&s_iceMistField, (ForceLayer){
        .type = FORCE_NOISE_PERLIN,
        .strength = ICE_MIST_NOISE,
        .noiseScale = 0.8f,
        .noiseSpeed = 0.6f});
    ForceField_AddLayer(&s_iceMistField, (ForceLayer){
        .type = FORCE_DRAG, .strength = ICE_MIST_DRAG});
    s_iceMistReady = true;
}

// One mist particle at `at`. `spread` scales the sideways throw — the front
// trails it gently, the impact throws it.
static void IceMist_Spawn(Vector3 at, float sizeScale, float spread)
{
    ParticleConfig cfg = {0};
    // GROUND-HUGGING. The centre sits just above the floor and the throw is
    // almost entirely sideways: cold vapour spreads, it does not billow. The
    // previous version launched it upward and it read as steam off a kettle.
    cfg.position = at;
    cfg.velocity = (Vector3){(Random01() - 0.5f) * 0.70f * spread,
                             Random01() * 0.06f * spread,   // barely any lift
                             (Random01() - 0.5f) * 0.70f * spread};
    cfg.radius   = Math_Mix(ICE_MIST_RADIUS_MIN, ICE_MIST_RADIUS_MAX, Random01()) * sizeScale;
    cfg.lifetime = Math_Mix(ICE_MIST_LIFE_MIN, ICE_MIST_LIFE_MAX, Random01());
    cfg.colorStart = ColorAlpha(ELEMENT_COLOR_WATER, 0.45f);
    cfg.colorEnd   = ColorAlpha(ELEMENT_COLOR_WATER, 0.0f);
    cfg.gradient   = &s_iceMistGrad;
    cfg.forceField = &s_iceMistField;
    cfg.rotation   = Random01() * 2.0f * PI;
    // THE BLEND LAW (core/particle_system.h): a thing that EMITS light draws
    // BLEND_ADDITIVE and stays unlit. This mist was alpha-blended and lit, so
    // the night arena's lighting multiplied it down to near-black — the "khói
    // đen" the owner saw. Additive output can never be darker than what is
    // behind it, which is the whole reason the law is worded that way.
    cfg.render.blendMode = VFX_BLEND_ADDITIVE;
    cfg.render.unlit = 1;
    cfg.render.emissiveBoost = 1.15f;
    SpawnParticle(cfg);
}

typedef enum
{
    STATE_CASTING,
    STATE_CHANNELING,
    STATE_IMPACT_BURST,
    STATE_DONE
} SkillState;

typedef struct
{
    bool active;
    SkillState state;
    int ownerAgentId;
    Vector3 startPos;
    Vector3 targetPos;
    float timer;
    float sizeScale;
    float damageAccumulator;

    Vector3 pathPoints[32];
    int pathPointCount;
    int lastSpawnedIdx;
    int castSeed; // THÊM BIẾN NÀY

    int burstSeed;
    float burstProgress;
    float mistAcc;   // fractional carry for the rate-based mist emission
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

// TỐI ƯU: Cache lại Skill Index để tránh gọi tra cứu chuỗi tốn kém mỗi lần Cast
static int s_glacialCannonSkillIndex = -1;

#include "glacial_cannon_skill_params.inl"

// Eruption `j` of `count`, as both Draw and the mist emitter need it. Kept in
// one place so the ice and the mist can never disagree about where the ridge is.
static Vector3 GlacialErupt_Point(const SkillInstance *s, int j, int count)
{
    float t = ((float)j + 0.5f) / (float)count;
    Vector3 p = Vector3Lerp(s->startPos, s->targetPos, t);

    // Perpendicular jitter, deterministic from the cast seed and the index —
    // never from time, or the whole ridge would slide sideways every frame.
    Vector3 dir = Vector3Subtract(s->targetPos, s->startPos);
    dir.y = 0.0f;
    if (Vector3LengthSqr(dir) > 1e-6f)
    {
        Vector3 side = Vector3Normalize((Vector3){-dir.z, 0.0f, dir.x});
        unsigned int h = (unsigned int)(s->castSeed + j * 2654435761u);
        h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
        float r = ((float)(h & 0xFFFF) / 65535.0f) * 2.0f - 1.0f;
        p = Vector3Add(p, Vector3Scale(side, r * ICE_WAVE_JITTER * s->sizeScale));
    }
    return p;
}

// How many eruptions this cast's range earns, at a fixed spacing in METRES.
static int GlacialErupt_Count(const SkillInstance *s)
{
    float len = Vector3Distance(s->startPos, s->targetPos);
    int n = (int)(len / (ICE_WAVE_SPACING * s->sizeScale));
    if (n < 2) n = 2;
    if (n > ICE_WAVE_MAX) n = ICE_WAVE_MAX;
    return n;
}


void InitGlacialCannonSkill(int screenWidth, int screenHeight)
{
    for (int i = 0; i < MAX_INSTANCES; i++)
        s_instances[i].active = false;

    IceMist_InitShared();

#define GLACIAL_CANNON_TUNABLE_COUNT 6
    static SkillTunableEntry s_tunables[GLACIAL_CANNON_TUNABLE_COUNT];
    int tn = 0;
#include "glacial_cannon_skill_tunables.inl"

    s_glacialCannonSkillIndex = Skill_GetIndexByName("GLACIAL_CANNON");
    SkillTunables_LoadPersisted("skills/water/glacial_cannon_skill/glacial_cannon_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(s_glacialCannonSkillIndex, s_tunables, tn);
}

void CastGlacialCannonSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    // Dùng index đã cache thay vì GetIndexByName
    if (!SkillManager_CanCast(s_glacialCannonSkillIndex, agentId))
        return;

    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        if (s_instances[i].active)
            continue;

        SkillInstance *s = &s_instances[i];
        s->state = STATE_CASTING;
        s->startPos = startPos;
        s->targetPos = target;
        s->timer = 0.0f;
        s->sizeScale = params.sizeScale;
        s->damageAccumulator = 0.0f;
        s->ownerAgentId = agentId;
        s->lastSpawnedIdx = -1;

        // Trộn thời gian và ID để sinh seed độc nhất cho MỖI LẦN CAST
        s->castSeed = (int)((unsigned int)(GetTime() * 10000.0f) ^ (unsigned int)GetRandomValue(0, 9999999));

        s->burstSeed = 0;
        s->burstProgress = 0.0f;
        s->mistAcc = 0.0f;
        s->pathPointCount = 16;

        // TỐI ƯU: Chuyển phép chia ra ngoài vòng lặp. Nhân luôn nhanh hơn chia.
        float invPathSteps = 1.0f / (float)(s->pathPointCount - 1);
        for (int j = 0; j < s->pathPointCount; j++)
        {
            float t = (float)j * invPathSteps;
            s->pathPoints[j] = Vector3Lerp(startPos, target, t);
        }

        s->active = true;

        PlayCastSound(EFFECT_PRESET_ICE_SHATTER);

        // Dùng index đã cache
        SkillManager_TriggerCooldown(s_glacialCannonSkillIndex, agentId, Skill_CalculateCooldown(SKILL_CAT_PROJECTILE, params));
        return;
    }
}

void UpdateGlacialCannonSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    // Đấu Pháp events (peek — COMBAT_API.md: ids = skillIndex*1000 + slot).
    // Losing a clash or crashing into an agent skips the wave straight to
    // its impact burst at the clash point.
    {
        const ClashEvent *ev;
        int evCount = Combat_PeekEvents(&ev);
        for (int k = 0; k < evCount; k++)
        {
            int slot = ev[k].skillInstanceId - s_glacialCannonSkillIndex * 1000;
            if (slot < 0 || slot >= MAX_INSTANCES) continue;
            SkillInstance *s = &s_instances[slot];
            if (!s->active || s->state != STATE_CHANNELING) continue;
            if (ev[k].outcome == CLASH_B_WINS || ev[k].outcome == CLASH_MUTUAL_DESTROY ||
                ev[k].outcome == CLASH_HIT_AGENT)
            {
                PlayImpactSound(EFFECT_PRESET_ICE_SHATTER);
                s->targetPos = ev[k].clashPoint;
                s->burstSeed = (int)((unsigned int)(GetTime() * 1000.0f) ^ ((unsigned int)slot * 2891336453u));
                s->burstProgress = 0.0f;
                s->state = STATE_IMPACT_BURST;
                s->timer = 0.0f;
            }
        }
    }

    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        SkillInstance *s = &s_instances[i];
        if (!s->active)
            continue;

        s->timer += dt;

        if (s->state == STATE_CASTING)
        {
            if (s->timer >= s_castDuration)
            {
                s->state = STATE_CHANNELING;
                s->timer = 0.0f;
            }
        }
        else if (s->state == STATE_CHANNELING)
        {
            // Linh khí tan khi chủ nhân chết: a channel with a dead caster
            // fizzles instead of rolling on ownerless (Combat_SubmitProjectile
            // would read its team as NEUTRAL — hitting BOTH sides — and pool
            // slot reuse could even flip it to the wrong team).
            if (Entity_GetAgent(s->ownerAgentId) == NULL)
            {
                s->state = STATE_DONE;
                s->active = false;
                continue;
            }

            float progress = s->timer / s_waveDuration;
            if (progress > 1.0f)
                progress = 1.0f;

            int wavefrontIdx = (int)(progress * (s->pathPointCount - 1));
            if (wavefrontIdx >= s->pathPointCount)
                wavefrontIdx = s->pathPointCount - 1;

            // VFX_ComposePathMistWave(VC_MAT_ICE, s->pathPoints, s->pathPointCount, progress, s->sizeScale * 0.8f);

            // Đấu Pháp: the ice wavefront is a combat collider — combat owns
            // hit detection + agent damage now (COMBAT_API.md §5); the old
            // core ApplyAoEDamage tick (no HP bookkeeping) is gone.
            Combat_SubmitProjectile(s->ownerAgentId, ELEM_WATER,
                                    s->pathPoints[wavefrontIdx],
                                    s_aoeRadius * s->sizeScale,
                                    s_damagePerSecond * 0.5f,
                                    2.0f,
                                    s_glacialCannonSkillIndex * 1000 + i);

            // COLD MIST along the travelling front. A rate, not a count per
            // frame — Update is where dt lives, which is also why this is not in
            // the draw path. Puffs are dark and low-density on purpose: this is
            // vapour rolling off ice, and a bright one would compete with the
            // crystals for the eye.
            {
                int count = GlacialErupt_Count(s);
                s->mistAcc += dt * ICE_MIST_RATE;
                int guard = 0;
                while (s->mistAcc >= 1.0f && guard++ < 6)
                {
                    s->mistAcc -= 1.0f;
                    // Behind the front, not on it: mist is what the ice has
                    // already given off.
                    float back = progress - 0.06f * (float)GetRandomValue(0, 3);
                    if (back < 0.0f) back = 0.0f;
                    int j = (int)(back * (float)count);
                    if (j >= count) j = count - 1;
                    Vector3 at = GlacialErupt_Point(s, j, count);
                    at.y += 0.03f * s->sizeScale;   // on the floor, not above it
                    IceMist_Spawn(at, s->sizeScale, 1.0f);
                }
            }

            s->damageAccumulator += s_damagePerSecond * dt;
            if (s->damageAccumulator >= 5.0f)
            {
                s->damageAccumulator = 0.0f;
                if (GetRandomValue(0, 100) < 40)
                {
                    PlayImpactSound(EFFECT_PRESET_ICE_SHATTER);
                }
            }

            if (s->timer >= s_waveDuration)
            {
                PlayImpactSound(EFFECT_PRESET_ICE_SHATTER);
                // Final burst: real team-aware agent AoE (allowed direct
                // path for AoE per COMBAT_API.md §5).
                {
                    const Agent *owner = Entity_GetAgent(s->ownerAgentId);
                    Entity_ApplyAoEDamage(s->targetPos, s_aoeRadius * (s->sizeScale * 1.8f),
                                          s_damagePerSecond * 0.5f, 1.5f,
                                          owner ? owner->team : TEAM_NEUTRAL);
                }

                unsigned int seedBits = (unsigned int)(GetTime() * 1000.0f) ^ ((unsigned int)s->ownerAgentId * 747796405u) ^ ((unsigned int)i * 2891336453u);
                s->burstSeed = (int)seedBits;
                s->burstProgress = 0.0f;

                // The impact's own cloud, thrown out ONCE. Spread around the
                // target rather than stacked on it, so it reads as vapour
                // escaping the shatter instead of one puff at a point.
                for (int m = 0; m < ICE_MIST_BURST; m++)
                {
                    float a = ((float)m / (float)ICE_MIST_BURST) * 2.0f * PI;
                    float rr = s_aoeRadius * s->sizeScale * 0.45f;
                    Vector3 at = { s->targetPos.x + cosf(a) * rr,
                                   s->targetPos.y + 0.04f * s->sizeScale,
                                   s->targetPos.z + sinf(a) * rr };
                    IceMist_Spawn(at, s->sizeScale * 1.25f, 2.2f);
                }

                s->state = STATE_IMPACT_BURST;
                s->timer = 0.0f;
            }
        }
        else if (s->state == STATE_IMPACT_BURST)
        {
            s->burstProgress = s->timer / s_burstDuration;
            if (s->burstProgress > 1.0f)
                s->burstProgress = 1.0f;

            if (s->timer >= s_burstDuration)
            {
                s->state = STATE_DONE;
                s->active = false;
            }
        }
    }
}

void DrawGlacialCannonSkill(void)
{
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        SkillInstance *s = &s_instances[i];
        if (!s->active)
            continue;

        // STATE_CASTING draws nothing, same as before the purge. The wind-up is
        // the pause itself; a gather effect here was an invention and was cut.

        if (s->state == STATE_CHANNELING)
        {
            // THE RIDGE. The old VFX_PathWave took the whole path plus a
            // progress and grew spikes along it; this is the same idea
            // assembled from what survives.
            //
            // Two things make it read as one ridge rather than a row of lumps:
            // the eruptions are spaced in METRES so consecutive bursts overlap
            // whatever the range, and each one grows on its OWN age
            // (`growProgress` is already a shader uniform on
            // VFX_DrawIceCrystalBurst). Drawing them at full size the moment the
            // front passes makes the ice pop into being and reads as a bug.
            //
            // These are immediate-mode DRAWS: they must be issued every frame
            // for every eruption on screen. Calling once at spawn draws a single
            // frame and looks like nothing happened — exactly what the ICE
            // CRYSTAL bench entry did while it was miscategorised as one-shot.
            float progress = Clamp(s->timer / s_waveDuration, 0.0f, 1.0f);
            int count = GlacialErupt_Count(s);

            for (int j = 0; j < count; j++)
            {
                float t = ((float)j + 0.5f) / (float)count;
                if (t > progress)
                    break;                       // the front has not reached it yet
                float grow = Clamp((progress - t) / ICE_WAVE_GROW, 0.0f, 1.0f);
                VFX_DrawIceCrystalBurst(GlacialErupt_Point(s, j, count),
                                        ICE_WAVE_CRYSTALS,
                                        s->castSeed + j * 977, grow);
            }
        }
        else if (s->state == STATE_IMPACT_BURST)
        {
            // Continuous by design: `burstProgress` drives the crystals growing
            // out of the ground, so this one belongs in the draw path.
            VFX_DrawIceCrystalBurst(s->targetPos, GLACIAL_CANNON_BURST_CRYSTAL_COUNT,
                                    s->burstSeed, s->burstProgress);
        }
    }
}

void UnloadGlacialCannonSkill(void)
{
    // No-op
}

SKILL_EMPTY_PROJECTILE_API(GlacialCannon)