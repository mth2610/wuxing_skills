// vc_archetype.inl — Stateful managed VFX archetypes
// Included once by visual_composer.c. All state is private to this TU.
//
// Public API (declared in visual_composer.h):
//   VFX_SpawnProcBeam / VFX_KillProcBeam     — ProcRay beam with element tint
//   VFX_SpawnGroundWave                       — expanding shockwave disc
//   VFX_SpawnOrbitals / (auto-expire)         — N orbs orbiting a center
//   VFX_SpawnAuraRing / VFX_KillAuraRing      — emitter ring + light
//   VFX_ChainLightning                        — staggered lightning hop chain
//   VFX_ComposeLightningBolt                  — single fixed-endpoint bolt (leader flash + decay)
//   VFX_ComposeEnergyFlow                     — smooth A→B channel, scrolling flow texture (mana stream)
//   VFX_SpawnSmokeColumn / VFX_KillSmokeColumn — long rising smoke column (cigarette-smoke style)
//
// VFX_ComposeSmokePuff / VFX_ComposeSmokeTrail lived here briefly (2026-07-10,
// as a billboard-quad diffusion-shader pool) but were reverted back to
// vc_neutral.inl's particle-burst implementation the same day — too costly
// per-pixel for effects that can fire many times a second from gameplay
// (path pillars, impacts). SmokeColumn is the opposite case — normally 1-2
// long-lived instances at once (a torch, incense, chimney) — so it uses
// vc_smoke_energy.inl's VFX_ComposeSmokeColumnFX (2-3 crossed shader quads,
// base-sourced diffusion field) instead of an emitter/particle pool: cost
// scales with (concurrent shader quads) × (pixel area), and here that's a
// small, mostly-constant number regardless of how long the column lives —
// unlike a particle stream, which keeps spawning new draw calls over time.
// Lifecycle (called by VFX_Compose_Update / VFX_Compose_Draw3D in visual_composer.c):
//   static void VC_Archetype_Update(float dt)
//   static void VC_Archetype_Draw3D(Camera3D cam)

#define ARCH_MAX_BEAMS          8
#define ARCH_MAX_GROUNDWAVES    8
#define ARCH_MAX_ORBITALS       8
#define ARCH_ORBITALS_PER_GROUP 8
#define ARCH_MAX_AURAS          8
#define ARCH_AURA_RING_K        8
#define ARCH_CHAIN_MAX_QUEUE    32
#define ARCH_MAX_BOLTS          8
#define ARCH_MAX_FLOWS          8
#define ARCH_MAX_SMOKE_COLUMNS  6

typedef struct {
    bool active;
    int  procRayId;
    Vector3 from, to;
    float duration, elapsed;
    EffectPresetType element;
} Arch_Beam;

typedef struct {
    bool active;
    Vector3 origin;
    float range, speed, elapsed;
    EffectPresetType element;
} Arch_GroundWave;

typedef struct {
    bool active;
    Vector3 center;
    EffectPresetType element;
    int count;
    float radius, duration, elapsed;
    float phases[ARCH_ORBITALS_PER_GROUP];
} Arch_Orbital;

typedef struct {
    bool active;
    Vector3 center;
    EffectPresetType element;
    float radius, duration, elapsed;
    int emitterIds[ARCH_AURA_RING_K];
} Arch_Aura;

typedef struct {
    Vector3 from, to;
    float delay, elapsed, scale;
    bool active;
} Arch_ChainEntry;

typedef struct {
    bool active;
    int  procBoltId;
    Vector3 from, to;
    float scale, duration, elapsed;
} Arch_Bolt;

typedef struct {
    bool active;
    int  energyFlowId;
    Vector3 from, to;
    float duration, elapsed;
} Arch_Flow;

typedef struct {
    bool active;
    bool dying;      // true once past duration or VFX_KillSmokeColumn'd — fading out, not yet freed
    Vector3 pos;
    float halfWidth, height;
    int   planeCount;
    float duration, elapsed;  // duration <= 0 = runs until VFX_KillSmokeColumn
    float dyingElapsed;
} Arch_SmokeColumn;

typedef struct {
    bool active;
    Vector3 position;
    Vector3 velocity;
    Vector3 rotationAxis;
    float rotationAngle;
    float rotationSpeed;
    float scale;
    float elapsed;
    float lifetime;
    int seed;
    MaterialPreset matPreset;
    VC_MaterialId matId;
} Arch_DebrisShard;

#define ARCH_MAX_DEBRIS_SHARDS 128

static ColorGradient s_shardSparkleGrad = {0};
static bool s_shardSparkleInit = false;

static void ShardSparkle_Init(void) {
    if (s_shardSparkleInit) return;
    ColorGradient_AddStop(&s_shardSparkleGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_shardSparkleGrad, 0.15f, (Color){220, 245, 255, 255});
    ColorGradient_AddStop(&s_shardSparkleGrad, 1.0f, (Color){100, 180, 255, 0});
    s_shardSparkleInit = true;
}
static Arch_Beam        s_archBeams[ARCH_MAX_BEAMS];
static Arch_GroundWave  s_archGwaves[ARCH_MAX_GROUNDWAVES];
static Arch_Orbital     s_archOrbitals[ARCH_MAX_ORBITALS];
static Arch_Aura        s_archAuras[ARCH_MAX_AURAS];
static Arch_ChainEntry  s_archChain[ARCH_CHAIN_MAX_QUEUE];
static Arch_Bolt        s_archBolts[ARCH_MAX_BOLTS];
static Arch_Flow        s_archFlows[ARCH_MAX_FLOWS];
static Arch_SmokeColumn s_archSmokeColumns[ARCH_MAX_SMOKE_COLUMNS];
static Arch_DebrisShard s_archDebrisShards[ARCH_MAX_DEBRIS_SHARDS];

static Color Arch_ElementColor(EffectPresetType e)
{
    switch (e) {
    case EFFECT_PRESET_WATER_SPLASH:  return ELEMENT_COLOR_WATER;
    case EFFECT_PRESET_WOOD_BLOOM:    return ELEMENT_COLOR_WOOD;
    case EFFECT_PRESET_FIRE_EXPLOSION:return ELEMENT_COLOR_FIRE;
    case EFFECT_PRESET_EARTH_CRACK:   return ELEMENT_COLOR_EARTH;
    case EFFECT_PRESET_METAL_SHARD:   return ELEMENT_COLOR_METAL;
    case EFFECT_PRESET_TAIJI_BURST:   return ELEMENT_COLOR_TAIJI;
    default: return WHITE;
    }
}

// ── Beam ──────────────────────────────────────────────────────────────────────

int VFX_SpawnProcBeam(Vector3 from, Vector3 to, EffectPresetType element,
                      float width, float duration)
{
    (void)width;
    for (int i = 0; i < ARCH_MAX_BEAMS; i++) {
        if (!s_archBeams[i].active) {
            s_archBeams[i].active    = true;
            s_archBeams[i].from      = from;
            s_archBeams[i].to        = to;
            s_archBeams[i].element   = element;
            s_archBeams[i].duration  = duration;
            s_archBeams[i].elapsed   = 0.0f;
            s_archBeams[i].procRayId = SpawnProcRay(ProcRay_EnergyConfig(), 1.0f);
            ProcRay_SetBrightness(s_archBeams[i].procRayId, 1.4f);
            Color c = Arch_ElementColor(element);
            VFXLight_Spawn(from, c, 2.5f, duration, VFX_PRIORITY_LOW);
            VFXLight_Spawn(to,   c, 2.5f, duration, VFX_PRIORITY_LOW);
            return i;
        }
    }
    return -1;
}

void VFX_KillProcBeam(int handle)
{
    if (handle < 0 || handle >= ARCH_MAX_BEAMS || !s_archBeams[handle].active)
        return;
    ProcRay_Kill(s_archBeams[handle].procRayId);
    s_archBeams[handle].active = false;
}

// ── Ground wave ───────────────────────────────────────────────────────────────

void VFX_SpawnGroundWave(Vector3 origin, Vector3 dir,
                         EffectPresetType element, float range, float speed)
{
    (void)dir;
    for (int i = 0; i < ARCH_MAX_GROUNDWAVES; i++) {
        if (!s_archGwaves[i].active) {
            s_archGwaves[i].active  = true;
            s_archGwaves[i].origin  = origin;
            s_archGwaves[i].range   = range;
            s_archGwaves[i].speed   = speed;
            s_archGwaves[i].elapsed = 0.0f;
            s_archGwaves[i].element = element;
            return;
        }
    }
}

// ── Orbitals ──────────────────────────────────────────────────────────────────

int VFX_SpawnOrbitals(Vector3 center, EffectPresetType element,
                      int count, float radius, float duration)
{
    if (count > ARCH_ORBITALS_PER_GROUP) count = ARCH_ORBITALS_PER_GROUP;
    for (int i = 0; i < ARCH_MAX_ORBITALS; i++) {
        if (!s_archOrbitals[i].active) {
            s_archOrbitals[i].active   = true;
            s_archOrbitals[i].center   = center;
            s_archOrbitals[i].element  = element;
            s_archOrbitals[i].count    = count;
            s_archOrbitals[i].radius   = radius;
            s_archOrbitals[i].duration = duration;
            s_archOrbitals[i].elapsed  = 0.0f;
            for (int j = 0; j < count; j++)
                s_archOrbitals[i].phases[j] = (2.0f * PI * j) / count;
            return i;
        }
    }
    return -1;
}

// ── Aura ring ─────────────────────────────────────────────────────────────────

int VFX_SpawnAuraRing(Vector3 center, EffectPresetType element,
                      float radius, float duration)
{
    for (int i = 0; i < ARCH_MAX_AURAS; i++) {
        if (!s_archAuras[i].active) {
            s_archAuras[i].active   = true;
            s_archAuras[i].center   = center;
            s_archAuras[i].element  = element;
            s_archAuras[i].radius   = radius;
            s_archAuras[i].duration = duration;
            s_archAuras[i].elapsed  = 0.0f;
            Color c = Arch_ElementColor(element);
            for (int k = 0; k < ARCH_AURA_RING_K; k++) {
                float angle = (2.0f * PI * k) / ARCH_AURA_RING_K;
                Vector3 p = { center.x + cosf(angle) * radius,
                               center.y,
                               center.z + sinf(angle) * radius };
                EmitterConfig ecfg = {0};
                ecfg.baseParticle.colorStart = c;
                ecfg.baseParticle.colorEnd   = ColorAlpha(c, 0);
                ecfg.baseParticle.radius     = 0.08f;
                ecfg.baseParticle.lifetime   = 1.2f;
                ecfg.baseParticle.velocity   = (Vector3){0, 0.5f, 0};
                ecfg.spawnRate               = 4.0f;
                ecfg.randomPosOffset         = 0.05f;
                s_archAuras[i].emitterIds[k] = CreateEmitter(ecfg, p);
            }
            VFXLight_Spawn(center, c, radius * 1.5f, duration, VFX_PRIORITY_LOW);
            return i;
        }
    }
    return -1;
}

void VFX_KillAuraRing(int handle)
{
    if (handle < 0 || handle >= ARCH_MAX_AURAS || !s_archAuras[handle].active)
        return;
    for (int k = 0; k < ARCH_AURA_RING_K; k++)
        if (s_archAuras[handle].emitterIds[k] >= 0)
            StopEmitter(s_archAuras[handle].emitterIds[k]);
    s_archAuras[handle].active = false;
}

// ── Chain lightning ───────────────────────────────────────────────────────────

void VFX_ChainLightning(const Vector3 *points, int count,
                        float scale, float hopDelay)
{
    if (!points || count < 2) return;
    for (int i = 0; i + 1 < count; i++) {
        for (int j = 0; j < ARCH_CHAIN_MAX_QUEUE; j++) {
            if (!s_archChain[j].active) {
                s_archChain[j].active  = true;
                s_archChain[j].from    = points[i];
                s_archChain[j].to      = points[i + 1];
                s_archChain[j].delay   = hopDelay * i;
                s_archChain[j].elapsed = 0.0f;
                s_archChain[j].scale   = scale;
                break;
            }
        }
    }
}

// ── Single lightning bolt (fixed endpoints, leader flash + afterglow decay) ────

int VFX_ComposeLightningBolt(Vector3 start, Vector3 end, float scale)
{
    for (int i = 0; i < ARCH_MAX_BOLTS; i++) {
        if (!s_archBolts[i].active) {
            s_archBolts[i].active     = true;
            s_archBolts[i].from       = start;
            s_archBolts[i].to         = end;
            s_archBolts[i].scale      = scale;
            s_archBolts[i].duration   = 0.5f; // ~70ms leader flash + flickering afterglow decay
            s_archBolts[i].elapsed    = 0.0f;
            s_archBolts[i].procBoltId = SpawnProcBolt(ProcRay_BoltLightningConfig(), scale);
            VFXLight_Spawn(end, (Color){0, 200, 255, 255}, 2.5f * scale, 0.25f, VFX_PRIORITY_HIGH_ULTIMATE);
            return i;
        }
    }
    return -1;
}

// ── Energy Flow (smooth A→B channel, scrolling flow texture) ──────────────────

int VFX_ComposeEnergyFlow(Vector3 from, Vector3 to, float scale, float duration)
{
    for (int i = 0; i < ARCH_MAX_FLOWS; i++) {
        if (!s_archFlows[i].active) {
            s_archFlows[i].active       = true;
            s_archFlows[i].from         = from;
            s_archFlows[i].to           = to;
            s_archFlows[i].duration     = duration;
            s_archFlows[i].elapsed      = 0.0f;
            s_archFlows[i].energyFlowId = SpawnEnergyFlow(ProcRay_EnergyFlowConfig(), scale);
            return i;
        }
    }
    return -1;
}

// ── Smoke column (long rising column, cigarette-smoke style) ──────────────────
// 2-3 fixed vertical "cross billboard" quads through `pos`, shaded by
// vc_smoke_energy.inl's VFX_ComposeSmokeColumnFX (smoke_column.fs — a
// continuous age-based rising flow driven by u_time, not a looped progress
// ramp; see that file's comments). u_progress here is just a fade in/out
// mask (SMOKE_COLUMN_FADE seconds each way), not a spread/dissolve driver —
// the flow itself never stops or resets while active. See the file-header
// comment above for why this uses the shader instead of vc_neutral.inl's
// particle wisps: few concurrent long-lived instances, so the per-pixel
// cost is affordable, unlike SmokePuff/SmokeTrail.
#define SMOKE_COLUMN_FADE 0.35f

int VFX_SpawnSmokeColumn(Vector3 pos, float duration)
{
    for (int i = 0; i < ARCH_MAX_SMOKE_COLUMNS; i++) {
        if (!s_archSmokeColumns[i].active) {
            s_archSmokeColumns[i].active     = true;
            s_archSmokeColumns[i].dying      = false;
            s_archSmokeColumns[i].pos        = pos;
            s_archSmokeColumns[i].halfWidth  = 0.5f;  // bigger — easier to see/judge while tuning
            s_archSmokeColumns[i].height     = 3.5f;
            s_archSmokeColumns[i].planeCount = 1;
            s_archSmokeColumns[i].duration   = duration;
            s_archSmokeColumns[i].elapsed    = 0.0f;
            s_archSmokeColumns[i].dyingElapsed = 0.0f;
            return i;
        }
    }
    return -1;
}

// Starts the fade-out instead of an instant cut — matches smoke_column.fs's
// own u_progress-as-fade-mask design. The pool slot frees itself once the
// fade finishes (see VC_Archetype_Update).
void VFX_KillSmokeColumn(int handle)
{
    if (handle < 0 || handle >= ARCH_MAX_SMOKE_COLUMNS || !s_archSmokeColumns[handle].active)
        return;
    s_archSmokeColumns[handle].dying = true;
}

// ── Internal update / draw ────────────────────────────────────────────────────

static void VC_Archetype_Update(float dt)
{
    for (int i = 0; i < ARCH_MAX_BEAMS; i++) {
        if (!s_archBeams[i].active) continue;
        s_archBeams[i].elapsed += dt;
        if (s_archBeams[i].elapsed >= s_archBeams[i].duration) {
            ProcRay_Kill(s_archBeams[i].procRayId);
            s_archBeams[i].active = false;
        }
    }
    for (int i = 0; i < ARCH_MAX_GROUNDWAVES; i++) {
        if (!s_archGwaves[i].active) continue;
        s_archGwaves[i].elapsed += dt;
        if (s_archGwaves[i].speed * s_archGwaves[i].elapsed >= s_archGwaves[i].range)
            s_archGwaves[i].active = false;
    }
    for (int i = 0; i < ARCH_MAX_ORBITALS; i++) {
        if (!s_archOrbitals[i].active) continue;
        s_archOrbitals[i].elapsed += dt;
        if (s_archOrbitals[i].elapsed >= s_archOrbitals[i].duration)
            s_archOrbitals[i].active = false;
    }
    for (int i = 0; i < ARCH_MAX_AURAS; i++) {
        if (!s_archAuras[i].active) continue;
        s_archAuras[i].elapsed += dt;
        if (s_archAuras[i].elapsed >= s_archAuras[i].duration) {
            VFX_KillAuraRing(i);
            continue;
        }
        // core/emitter_system.h's spawnRate ticking lives INSIDE
        // UpdateEmitterTarget itself, not in any bulk per-frame Update — the
        // 8 ring emitters were created once at spawn and never driven again,
        // so their timeAccumulator never advanced and they emitted nothing.
        // (main.c's EmitterSystem_Update(dt) is an unrelated, same-named
        // system in skill_helper.c — a name collision, not this one.)
        for (int k = 0; k < ARCH_AURA_RING_K; k++) {
            float angle = (2.0f * PI * k) / ARCH_AURA_RING_K;
            Vector3 p = { s_archAuras[i].center.x + cosf(angle) * s_archAuras[i].radius,
                           s_archAuras[i].center.y,
                           s_archAuras[i].center.z + sinf(angle) * s_archAuras[i].radius };
            UpdateEmitterTarget(s_archAuras[i].emitterIds[k], p, dt);
        }
    }
    for (int i = 0; i < ARCH_CHAIN_MAX_QUEUE; i++) {
        if (!s_archChain[i].active) continue;
        s_archChain[i].elapsed += dt;
        if (s_archChain[i].elapsed >= s_archChain[i].delay) {
            SpawnLightningTrail(s_archChain[i].from, s_archChain[i].to,
                                s_archChain[i].scale, 8.0f);
            s_archChain[i].active = false;
        }
    }
    for (int i = 0; i < ARCH_MAX_BOLTS; i++) {
        if (!s_archBolts[i].active) continue;
        s_archBolts[i].elapsed += dt;
        if (s_archBolts[i].elapsed >= s_archBolts[i].duration) {
            ProcBolt_Kill(s_archBolts[i].procBoltId);
            s_archBolts[i].active = false;
        }
    }
    for (int i = 0; i < ARCH_MAX_FLOWS; i++) {
        if (!s_archFlows[i].active) continue;
        s_archFlows[i].elapsed += dt;
        if (s_archFlows[i].elapsed >= s_archFlows[i].duration) {
            EnergyFlow_Kill(s_archFlows[i].energyFlowId);
            s_archFlows[i].active = false;
            continue;
        }
        // Scroll offset needs real dt (that's what sells "flowing") — unlike
        // Beam/Bolt's Draw3D-side Update(dt=0.0f) calls, this can't wait for
        // Draw3D since VC_Archetype_Draw3D doesn't receive dt.
        EnergyFlow_Update(s_archFlows[i].energyFlowId, s_archFlows[i].from,
                          s_archFlows[i].to, 1.0f, dt);
    }
    for (int i = 0; i < ARCH_MAX_SMOKE_COLUMNS; i++) {
        if (!s_archSmokeColumns[i].active) continue;
        s_archSmokeColumns[i].elapsed += dt;
        if (!s_archSmokeColumns[i].dying) {
            if (s_archSmokeColumns[i].duration > 0.0f &&
                s_archSmokeColumns[i].elapsed >= s_archSmokeColumns[i].duration)
                s_archSmokeColumns[i].dying = true;
        } else {
            s_archSmokeColumns[i].dyingElapsed += dt;
            if (s_archSmokeColumns[i].dyingElapsed >= SMOKE_COLUMN_FADE)
                s_archSmokeColumns[i].active = false;
        }
    }
    for (int i = 0; i < ARCH_MAX_DEBRIS_SHARDS; i++) {
        if (!s_archDebrisShards[i].active) continue;
        s_archDebrisShards[i].elapsed += dt;
        if (s_archDebrisShards[i].elapsed >= s_archDebrisShards[i].lifetime) {
            s_archDebrisShards[i].active = false;
            continue;
        }

        // Apply physics (gravity + air drag)
        s_archDebrisShards[i].velocity.y -= 9.81f * dt;
        s_archDebrisShards[i].velocity = Vector3Scale(s_archDebrisShards[i].velocity, 1.0f - 0.4f * dt);

        // Update position
        s_archDebrisShards[i].position = Vector3Add(s_archDebrisShards[i].position, 
                                                    Vector3Scale(s_archDebrisShards[i].velocity, dt));

        // Update rotation
        s_archDebrisShards[i].rotationAngle += s_archDebrisShards[i].rotationSpeed * dt;

        // Trail emission (25% chance per update frame)
        if (GetRandomValue(0, 100) < 25) {
            const VFX_ElementMaterial *eMat = VFX_Material(s_archDebrisShards[i].matId);
            if (eMat && eMat->grad) {
                const ColorGradient *partGrad = eMat->grad;
                float partRad = s_archDebrisShards[i].scale * 0.15f + Random01() * 0.01f;
                if (s_archDebrisShards[i].matId == VC_MAT_ICE || s_archDebrisShards[i].matId == VC_MAT_METAL) {
                    ShardSparkle_Init();
                    partGrad = &s_shardSparkleGrad;
                    partRad = s_archDebrisShards[i].scale * 0.25f + Random01() * 0.02f;
                }
                SpawnParticle((ParticleConfig){
                    .position = s_archDebrisShards[i].position,
                    .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, (Random01() - 0.5f) * 0.2f, (Random01() - 0.5f) * 0.2f},
                    .radius = partRad,
                    .lifetime = 0.12f + Random01() * 0.18f,
                    .gradient = partGrad
                });
            }
        }

        // Bounce check against ground plane (Y = 0)
        if (s_archDebrisShards[i].position.y < 0.0f) {
            s_archDebrisShards[i].position.y = 0.0f;

            // Reverse velocity along normal (elastic bounce)
            s_archDebrisShards[i].velocity.y = -s_archDebrisShards[i].velocity.y * 0.45f;

            // Friction
            s_archDebrisShards[i].velocity.x *= 0.6f;
            s_archDebrisShards[i].velocity.z *= 0.6f;
            s_archDebrisShards[i].rotationSpeed *= 0.7f;

            // Bounce impact particles (2 particles)
            const VFX_ElementMaterial *eMat = VFX_Material(s_archDebrisShards[i].matId);
            if (eMat && eMat->grad) {
                const ColorGradient *partGrad = eMat->grad;
                if (s_archDebrisShards[i].matId == VC_MAT_ICE || s_archDebrisShards[i].matId == VC_MAT_METAL) {
                    ShardSparkle_Init();
                    partGrad = &s_shardSparkleGrad;
                }
                for (int p = 0; p < 2; p++) {
                    SpawnParticle((ParticleConfig){
                        .position = s_archDebrisShards[i].position,
                        .velocity = (Vector3){
                            (Random01() - 0.5f) * 1.2f,
                            Random01() * 0.8f,
                            (Random01() - 0.5f) * 1.2f
                        },
                        .radius = s_archDebrisShards[i].scale * 0.2f + Random01() * 0.015f,
                        .lifetime = 0.15f + Random01() * 0.15f,
                        .gradient = partGrad
                    });
                }
            }

            // Rest condition
            if (s_archDebrisShards[i].velocity.y < 0.25f) {
                s_archDebrisShards[i].velocity.y = 0.0f;
                s_archDebrisShards[i].velocity.x = 0.0f;
                s_archDebrisShards[i].velocity.z = 0.0f;
                s_archDebrisShards[i].rotationSpeed = 0.0f;
            }
        }
    }
}

static void DrawUnitBoxJittered(int seed, Color color)
{
    // Deterministic random vertex offsets based on seed (fast LCG)
    float ox[8], oy[8], oz[8];
    unsigned int nextSeed = (unsigned int)seed;
    for (int i = 0; i < 8; i++)
    {
        nextSeed = nextSeed * 1103515245 + 12345;
        ox[i] = (((float)(nextSeed / 65536 % 32768) / 32767.0f) - 0.5f) * 0.35f;
        nextSeed = nextSeed * 1103515245 + 12345;
        oy[i] = (((float)(nextSeed / 65536 % 32768) / 32767.0f) - 0.5f) * 0.35f;
        nextSeed = nextSeed * 1103515245 + 12345;
        oz[i] = (((float)(nextSeed / 65536 % 32768) / 32767.0f) - 0.5f) * 0.35f;
    }

    Vector3 v[8] = {
        {-0.5f + ox[0], -0.5f + oy[0], -0.5f + oz[0]},
        { 0.5f + ox[1], -0.5f + oy[1], -0.5f + oz[1]},
        { 0.5f + ox[2],  0.5f + oy[2], -0.5f + oz[2]},
        {-0.5f + ox[3],  0.5f + oy[3], -0.5f + oz[3]},
        {-0.5f + ox[4], -0.5f + oy[4],  0.5f + oz[4]},
        { 0.5f + ox[5], -0.5f + oy[5],  0.5f + oz[5]},
        { 0.5f + ox[6],  0.5f + oy[6],  0.5f + oz[6]},
        {-0.5f + ox[7],  0.5f + oy[7],  0.5f + oz[7]}
    };

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlBegin(RL_TRIANGLES);

    // 6 faces (each quad is 2 triangles = 12 triangles total)
    int faces[6][4] = {
        {0, 3, 2, 1}, // Front
        {1, 2, 6, 5}, // Right
        {5, 6, 7, 4}, // Back
        {4, 7, 3, 0}, // Left
        {3, 7, 6, 2}, // Top
        {4, 0, 1, 5}  // Bottom
    };

    for (int f = 0; f < 6; f++)
    {
        Vector3 edge1 = Vector3Subtract(v[faces[f][1]], v[faces[f][0]]);
        Vector3 edge2 = Vector3Subtract(v[faces[f][3]], v[faces[f][0]]);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge2, edge1));

        rlNormal3f(normal.x, normal.y, normal.z);
        // Triangle 1
        rlVertex3f(v[faces[f][0]].x, v[faces[f][0]].y, v[faces[f][0]].z);
        rlVertex3f(v[faces[f][1]].x, v[faces[f][1]].y, v[faces[f][1]].z);
        rlVertex3f(v[faces[f][2]].x, v[faces[f][2]].y, v[faces[f][2]].z);
        // Triangle 2
        rlVertex3f(v[faces[f][0]].x, v[faces[f][0]].y, v[faces[f][0]].z);
        rlVertex3f(v[faces[f][2]].x, v[faces[f][2]].y, v[faces[f][2]].z);
        rlVertex3f(v[faces[f][3]].x, v[faces[f][3]].y, v[faces[f][3]].z);
    }

    rlEnd();
}

static void VC_Archetype_Draw3D(Camera3D cam)
{
    (void)cam;
    for (int i = 0; i < ARCH_MAX_BEAMS; i++) {
        if (!s_archBeams[i].active) continue;
        Vector3 dir = Vector3Subtract(s_archBeams[i].to, s_archBeams[i].from);
        float len = Vector3Length(dir);
        if (len < 0.001f) continue;
        dir = Vector3Scale(dir, 1.0f / len);
        ProcRay_Update(s_archBeams[i].procRayId,
                       s_archBeams[i].from, dir, len, 1.0f, 0.0f);
        ProcRay_Draw(s_archBeams[i].procRayId, cam);
    }
    for (int i = 0; i < ARCH_MAX_GROUNDWAVES; i++) {
        if (!s_archGwaves[i].active) continue;
        float r = s_archGwaves[i].speed * s_archGwaves[i].elapsed;
        Color c = Arch_ElementColor(s_archGwaves[i].element);
        // Shockwave ring: thin torus expanding outward
        DrawCoreTorus(s_archGwaves[i].origin, r * 0.92f, r, 8, 24, c);
    }
    for (int i = 0; i < ARCH_MAX_ORBITALS; i++) {
        if (!s_archOrbitals[i].active) continue;
        float baseAngle = s_archOrbitals[i].elapsed * 1.5f;
        Color c = Arch_ElementColor(s_archOrbitals[i].element);
        float alpha = 1.0f - s_archOrbitals[i].elapsed / s_archOrbitals[i].duration;
        c.a = (unsigned char)(c.a * alpha);
        for (int j = 0; j < s_archOrbitals[i].count; j++) {
            float ang   = baseAngle + s_archOrbitals[i].phases[j];
            float scale = 0.12f + 0.04f * sinf(ang * 2.3f);
            Vector3 p = {
                s_archOrbitals[i].center.x + cosf(ang) * s_archOrbitals[i].radius,
                s_archOrbitals[i].center.y + 0.3f + 0.1f * sinf(ang * 1.7f + j),
                s_archOrbitals[i].center.z + sinf(ang) * s_archOrbitals[i].radius };
            DrawCoreSphere(p, scale, 6, 6, c);
        }
    }
    for (int i = 0; i < ARCH_MAX_BOLTS; i++) {
        if (!s_archBolts[i].active) continue;
        // Leader flash (bright) decaying to flickering afterglow over `duration`.
        float t = s_archBolts[i].elapsed / s_archBolts[i].duration;
        float brightness = 1.9f * (1.0f - t) + 0.3f;
        ProcBolt_SetBrightness(s_archBolts[i].procBoltId, brightness);
        ProcBolt_Update(s_archBolts[i].procBoltId, s_archBolts[i].from,
                        s_archBolts[i].to, s_archBolts[i].scale, 0.0f);
        ProcBolt_Draw(s_archBolts[i].procBoltId, cam);
    }
    for (int i = 0; i < ARCH_MAX_FLOWS; i++) {
        if (!s_archFlows[i].active) continue;
        EnergyFlow_Draw(s_archFlows[i].energyFlowId, cam);
    }
    VFX_BeginSmokeColumnBatch();
    for (int i = 0; i < ARCH_MAX_SMOKE_COLUMNS; i++) {
        if (!s_archSmokeColumns[i].active) continue;
        // u_progress is a fade in/out MASK now (smoke_column.fs), not a loop
        // driver — the flow itself runs continuously off u_time.
        float progress = s_archSmokeColumns[i].dying
            ? Clamp(1.0f - s_archSmokeColumns[i].dyingElapsed / SMOKE_COLUMN_FADE, 0.0f, 1.0f)
            : Clamp(s_archSmokeColumns[i].elapsed / SMOKE_COLUMN_FADE, 0.0f, 1.0f);
        VFX_ComposeSmokeColumnFX(s_archSmokeColumns[i].pos, s_archSmokeColumns[i].halfWidth,
                                 s_archSmokeColumns[i].height, progress,
                                 s_archSmokeColumns[i].planeCount);
    }
    VFX_EndSmokeColumnBatch();

    // Draw debris shards
    rlDisableBackfaceCulling();
    MaterialPreset currentPreset = -1;
    float currentDissolve = -1.0f;

    for (int i = 0; i < ARCH_MAX_DEBRIS_SHARDS; i++) {
        if (!s_archDebrisShards[i].active) continue;

        MaterialPreset shardPreset = s_archDebrisShards[i].matPreset;
        float progress = s_archDebrisShards[i].elapsed / s_archDebrisShards[i].lifetime;

        // Grouping: Only apply dissolve during the last 20% of lifetime to allow batching for the first 80%
        float shardDissolve = 0.0f;
        if (progress >= 0.8f) {
            shardDissolve = (progress - 0.8f) / 0.2f;
        }

        // If the material or the dissolve value changed, we flush and update state
        if (shardPreset != currentPreset || fabsf(shardDissolve - currentDissolve) > 0.05f) {
            if (currentPreset != -1) {
                EffectMaterial prevMat = Material_Get(currentPreset);
                Material_SetFloat(&prevMat, "u_dissolve", 0.0f);
                Material_End();
            }
            currentPreset = shardPreset;
            currentDissolve = shardDissolve;
            EffectMaterial mat = Material_Get(currentPreset);
            Material_Begin(mat);
            Material_SetFloat(&mat, "u_dissolve", currentDissolve);
        }

        rlPushMatrix();
        rlTranslatef(s_archDebrisShards[i].position.x, 
                     s_archDebrisShards[i].position.y, 
                     s_archDebrisShards[i].position.z);
        rlRotatef(s_archDebrisShards[i].rotationAngle, 
                  s_archDebrisShards[i].rotationAxis.x, 
                  s_archDebrisShards[i].rotationAxis.y, 
                  s_archDebrisShards[i].rotationAxis.z);
        rlScalef(s_archDebrisShards[i].scale, s_archDebrisShards[i].scale, s_archDebrisShards[i].scale);

        DrawUnitBoxJittered(s_archDebrisShards[i].seed, WHITE);
        
        rlPopMatrix();
    }

    if (currentPreset != -1) {
        EffectMaterial prevMat = Material_Get(currentPreset);
        Material_SetFloat(&prevMat, "u_dissolve", 0.0f);
        Material_End();
    }
    rlEnableBackfaceCulling();
}

void VFX_ComposeShardDebris(Vector3 pos, int count, float speed, VC_MaterialId matId)
{
    if (count <= 0)
        return;
    if (count > 12)
        count = 12;

    MaterialPreset matPreset = MAT_ROCK;
    switch (matId)
    {
        case VC_MAT_FIRE:      matPreset = MAT_FIRE; break;
        case VC_MAT_ICE:       matPreset = MAT_ICE; break;
        case VC_MAT_WATER:     matPreset = MAT_WATER; break;
        case VC_MAT_METAL:     matPreset = MAT_METAL; break;
        case VC_MAT_VOID:      matPreset = MAT_PORTAL; break;
        default:               matPreset = MAT_ROCK; break;
    }

    int spawned = 0;
    for (int i = 0; i < ARCH_MAX_DEBRIS_SHARDS && spawned < count; i++)
    {
        if (s_archDebrisShards[i].active)
            continue;

        s_archDebrisShards[i].active = true;
        s_archDebrisShards[i].position = pos;
        
        // Launch direction: random upward cone (hemisphere)
        float yaw = (float)rand() / (float)RAND_MAX * 2.0f * PI;
        float pitch = ((float)rand() / (float)RAND_MAX * 70.0f + 20.0f) * DEG2RAD; // 20 to 90 degrees up
        Vector3 dir = {
            cosf(yaw) * cosf(pitch),
            sinf(pitch),
            sinf(yaw) * cosf(pitch)
        };
        dir = Vector3Normalize(dir);
        
        float randSpeed = speed * (0.6f + ((float)rand() / (float)RAND_MAX * 0.8f));
        s_archDebrisShards[i].velocity = Vector3Scale(dir, randSpeed);

        // Rotation axes & rates
        s_archDebrisShards[i].rotationAxis = Vector3Normalize((Vector3){
            (float)rand() / (float)RAND_MAX * 2.0f - 1.0f,
            (float)rand() / (float)RAND_MAX * 2.0f - 1.0f,
            (float)rand() / (float)RAND_MAX * 2.0f - 1.0f
        });
        s_archDebrisShards[i].rotationAngle = (float)rand() / (float)RAND_MAX * 360.0f;
        s_archDebrisShards[i].rotationSpeed = 180.0f + ((float)rand() / (float)RAND_MAX * 540.0f); // 180..720 deg/sec

        // Size & age limits
        s_archDebrisShards[i].scale = 0.02f + ((float)rand() / (float)RAND_MAX * 0.04f); // 0.02m to 0.06m scale (small shards)
        s_archDebrisShards[i].lifetime = 0.4f + ((float)rand() / (float)RAND_MAX * 0.4f); // 0.4s to 0.8s (fast decay)
        s_archDebrisShards[i].elapsed = 0.0f;
        s_archDebrisShards[i].seed = rand() % 500;
        s_archDebrisShards[i].matPreset = matPreset;
        s_archDebrisShards[i].matId = matId;

        spawned++;
    }
}

