// Preset-driven cast (windup) effect — elemental casting charge / gather.
// Uses VFX_CastPreset from vfx_presets.h.

#define MAX_CONCURRENT_CAST_EFFECTS 16
static ForceField s_castPullFlds[MAX_CONCURRENT_CAST_EFFECTS];
static int s_castPullFldNextSlot = 0;

void VFX_ComposeCast(Vector3 pos, EffectPresetType preset, float scale)
{
    const VFX_CastPreset *p = VFX_Preset_GetCast(preset);
    if (p == NULL) return;

    PlayCastSound(preset);
    VFXLight_Spawn(pos, p->flashColor, p->lightRadius * scale, p->lightLifetime, VFX_PRIORITY_LOW);

    ForceField *castPullFld = &s_castPullFlds[s_castPullFldNextSlot];
    s_castPullFldNextSlot = (s_castPullFldNextSlot + 1) % MAX_CONCURRENT_CAST_EFFECTS;
    ForceField_Clear(castPullFld);

    float spawnRadius = p->spawnRadius * scale;
    float pullStrength = p->pullStrength * scale;

    ForceField_AddLayer(castPullFld, (ForceLayer){
        .type     = FORCE_GRAVITY_POINT,
        .origin   = pos,
        .strength = pullStrength * 1.5f,
        .radius   = spawnRadius * 2.0f,
        .falloff  = 1.0f,
    });
    ForceField_AddLayer(castPullFld, (ForceLayer){
        .type      = FORCE_VORTEX,
        .origin    = pos,
        .direction = (Vector3){0.0f, 1.0f, 0.0f},
        .strength  = pullStrength * 0.8f,
        .radius    = spawnRadius * 1.8f,
        .falloff   = 1.0f,
    });

    // Guard count >= 1: particleCount*scale could round to 0 at tiny scales.
    int count = (int)((float)p->particleCount * scale);
    if (count < 1) count = 1;

    for (int i = 0; i < count; i++) {
        float a = (float)i / count * 2.0f * PI + ((float)rand() / (float)RAND_MAX * 0.5f);
        float r = spawnRadius * (0.8f + 0.4f * ((float)rand() / (float)RAND_MAX));
        Vector3 spawnPos = {
            pos.x + cosf(a) * r,
            pos.y + ((float)rand() / (float)RAND_MAX) * spawnRadius * 0.4f,
            pos.z + sinf(a) * r,
        };

        Vector3 toCenter = Vector3Subtract(pos, spawnPos);
        Vector3 tangent  = Vector3Normalize((Vector3){-toCenter.z, 0.0f, toCenter.x});
        Vector3 initVel  = Vector3Add(
            Vector3Scale(Vector3Normalize(toCenter), 0.25f),
            Vector3Scale(tangent, 0.1f));

        SpawnParticle((ParticleConfig){
            .position   = spawnPos,
            .velocity   = initVel,
            .radius     = ((float)rand() / (float)RAND_MAX * 0.02f + 0.012f) * scale,
            .lifetime   = (float)rand() / (float)RAND_MAX * 0.4f + 0.3f,
            .gradient   = p->gradient,
            .forceField = castPullFld,
        });
    }
}
