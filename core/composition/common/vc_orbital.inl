#define ARCH_MAX_ORBITALS       8
#define ARCH_ORBITALS_PER_GROUP 8

typedef struct {
    bool active;
    Vector3 center;
    EffectPresetType element;
    int count;
    float radius, duration, elapsed;
    float phases[ARCH_ORBITALS_PER_GROUP];
} Arch_Orbital;

static Arch_Orbital     s_archOrbitals[ARCH_MAX_ORBITALS];

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

static void VC_Orbital_Update(float dt)
{
    for (int i = 0; i < ARCH_MAX_ORBITALS; i++) {
        if (!s_archOrbitals[i].active) continue;
        s_archOrbitals[i].elapsed += dt;
        if (s_archOrbitals[i].elapsed >= s_archOrbitals[i].duration)
            s_archOrbitals[i].active = false;
    }
}

static void VC_Orbital_Draw3D(Camera3D cam)
{
    (void)cam;
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
}
