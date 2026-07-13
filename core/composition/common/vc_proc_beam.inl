#define ARCH_MAX_BEAMS          8
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

static Arch_Beam        s_archBeams[ARCH_MAX_BEAMS];
static Arch_Bolt        s_archBolts[ARCH_MAX_BOLTS];
static Arch_Flow        s_archFlows[ARCH_MAX_FLOWS];

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

int VFX_ComposeLightningBolt(Vector3 start, Vector3 end, float scale)
{
    for (int i = 0; i < ARCH_MAX_BOLTS; i++) {
        if (!s_archBolts[i].active) {
            s_archBolts[i].active     = true;
            s_archBolts[i].from       = start;
            s_archBolts[i].to         = end;
            s_archBolts[i].scale      = scale;
            s_archBolts[i].duration   = 0.5f;
            s_archBolts[i].elapsed    = 0.0f;
            s_archBolts[i].procBoltId = SpawnProcBolt(ProcRay_BoltLightningConfig(), scale);
            VFXLight_Spawn(end, (Color){0, 200, 255, 255}, 2.5f * scale, 0.25f, VFX_PRIORITY_HIGH_ULTIMATE);
            return i;
        }
    }
    return -1;
}

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

static void VC_ProcBeam_Update(float dt)
{
    for (int i = 0; i < ARCH_MAX_BEAMS; i++) {
        if (!s_archBeams[i].active) continue;
        s_archBeams[i].elapsed += dt;
        if (s_archBeams[i].elapsed >= s_archBeams[i].duration) {
            ProcRay_Kill(s_archBeams[i].procRayId);
            s_archBeams[i].active = false;
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
        EnergyFlow_Update(s_archFlows[i].energyFlowId, s_archFlows[i].from,
                          s_archFlows[i].to, 1.0f, dt);
    }
}

static void VC_ProcBeam_Draw3D(Camera3D cam)
{
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
    for (int i = 0; i < ARCH_MAX_BOLTS; i++) {
        if (!s_archBolts[i].active) continue;
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
