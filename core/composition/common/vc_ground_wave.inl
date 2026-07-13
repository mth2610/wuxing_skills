#define ARCH_MAX_GROUNDWAVES    8

typedef struct {
    bool active;
    Vector3 origin;
    float range, speed, elapsed;
    EffectPresetType element;
} Arch_GroundWave;

static Arch_GroundWave  s_archGwaves[ARCH_MAX_GROUNDWAVES];

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

static void VC_GroundWave_Update(float dt)
{
    for (int i = 0; i < ARCH_MAX_GROUNDWAVES; i++) {
        if (!s_archGwaves[i].active) continue;
        s_archGwaves[i].elapsed += dt;
        if (s_archGwaves[i].speed * s_archGwaves[i].elapsed >= s_archGwaves[i].range)
            s_archGwaves[i].active = false;
    }
}

static void VC_GroundWave_Draw3D(Camera3D cam)
{
    (void)cam;
    for (int i = 0; i < ARCH_MAX_GROUNDWAVES; i++) {
        if (!s_archGwaves[i].active) continue;
        float r = s_archGwaves[i].speed * s_archGwaves[i].elapsed;
        Color c = Arch_ElementColor(s_archGwaves[i].element);
        DrawCoreTorus(s_archGwaves[i].origin, r * 0.92f, r, 8, 24, c);
    }
}
