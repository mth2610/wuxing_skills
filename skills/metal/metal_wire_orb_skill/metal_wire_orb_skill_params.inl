// metal_wire_orb_skill_params.inl
static float tp_orbRadius = 1.0f;
static float tp_orbSpeed = 15.0f;
static float tp_wireSpawnInterval = 0.02f; // Every 0.02s
static float tp_wireOrbitRadiusMin = 1.2f;
static float tp_wireOrbitRadiusMax = 3.0f;
static float tp_wireOrbitSpeedMin = 5.0f;
static float tp_wireOrbitSpeedMax = 15.0f;
static float tp_wireLengthMin = 30.0f;
static float tp_wireLengthMax = 60.0f;
static float tp_wireLifeMin = 0.5f;
static float tp_wireLifeMax = 1.2f;
static float tp_wireThickMin = 0.02f;
static float tp_wireThickMax = 0.08f;

static SkillForceMix s_wireForceMix = {
    .curlStrength = 50.0f,
    .curlNoiseScale = 0.8f,
    .curlNoiseSpeed = 5.0f,
    
    .windStrength = 0.0f,
    .windDirX = 0.0f,
    .windDirY = 1.0f,
    .windDirZ = 0.0f,
    .windNoiseScale = 0.2f,
    .windNoiseSpeed = 1.0f
};
static ForceField s_wireForce;

#define METAL_WIRE_ORB_TUNABLE_COUNT (13 + SKILL_FORCE_MIX_TUNABLE_COUNT)
