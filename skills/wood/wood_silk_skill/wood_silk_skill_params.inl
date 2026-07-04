// Included at file scope in wood_silk_skill.c — all tunable state for wood_silk_skill.
// Static declarations here have internal linkage (same as if in wood_silk_skill.c directly).
// Do not #include from any other translation unit.

static float s_strandLength = 2.0f;
static float s_strandThick  = 0.05f;
static float s_strandLife   = 5.0f;

// Force mix — default to 0 strength except curl and wind
static SkillForceMix s_windForceMix = {
    .curlStrength = 5.0f,
    .curlNoiseScale = 0.5f,
    .curlNoiseSpeed = 2.0f,
    
    .windStrength = 3.0f,
    .windDirX = 0.5f,
    .windDirY = 1.0f,
    .windDirZ = 0.5f,
    .windNoiseScale = 0.2f,
    .windNoiseSpeed = 1.0f
};
static ForceField s_windForce;

// 3 float tunables + 1 phase × SKILL_FORCE_MIX_TUNABLE_COUNT(29) = 32
#define WOOD_SILK_TUNABLE_COUNT 32
