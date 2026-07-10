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
//
// VFX_ComposeSmokePuff / VFX_ComposeSmokeTrail lived here briefly (2026-07-10,
// as a billboard-quad diffusion-shader pool) but were reverted back to
// vc_neutral.inl's particle-burst implementation the same day — too costly
// per-pixel for effects that can fire many times a second from gameplay.
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

static Arch_Beam       s_archBeams[ARCH_MAX_BEAMS];
static Arch_GroundWave s_archGwaves[ARCH_MAX_GROUNDWAVES];
static Arch_Orbital    s_archOrbitals[ARCH_MAX_ORBITALS];
static Arch_Aura       s_archAuras[ARCH_MAX_AURAS];
static Arch_ChainEntry s_archChain[ARCH_CHAIN_MAX_QUEUE];
static Arch_Bolt       s_archBolts[ARCH_MAX_BOLTS];
static Arch_Flow       s_archFlows[ARCH_MAX_FLOWS];

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
}
