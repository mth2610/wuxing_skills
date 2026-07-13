#define ARCH_MAX_AURAS          8
#define ARCH_AURA_RING_K        8

typedef struct {
    bool active;
    Vector3 center;
    EffectPresetType element;
    float radius, duration, elapsed;
    int emitterIds[ARCH_AURA_RING_K];
} Arch_Aura;

static Arch_Aura        s_archAuras[ARCH_MAX_AURAS];

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

static void VC_AuraRing_Update(float dt)
{
    for (int i = 0; i < ARCH_MAX_AURAS; i++) {
        if (!s_archAuras[i].active) continue;
        s_archAuras[i].elapsed += dt;
        if (s_archAuras[i].elapsed >= s_archAuras[i].duration) {
            VFX_KillAuraRing(i);
            continue;
        }
        for (int k = 0; k < ARCH_AURA_RING_K; k++) {
            float angle = (2.0f * PI * k) / ARCH_AURA_RING_K;
            Vector3 p = { s_archAuras[i].center.x + cosf(angle) * s_archAuras[i].radius,
                           s_archAuras[i].center.y,
                           s_archAuras[i].center.z + sinf(angle) * s_archAuras[i].radius };
            UpdateEmitterTarget(s_archAuras[i].emitterIds[k], p, dt);
        }
    }
}

static void VC_AuraRing_Draw3D(Camera3D cam)
{
    (void)cam;
}
