// water_ring.inl — a liquid torus held by a MESH emitter, rendered only by SSF.
//
// This exists to be looked at. Every other water body in the tree is a sphere or
// a stream, and both hide the two things screen-space fluid is actually judged
// on: whether the surface reconstructs as ONE body, and whether its thickness
// varies enough to read as liquid rather than a painted shell. A torus asks both
// questions at once — you see the far wall through the near wall, the hole shows
// the background undistorted right next to water that distorts it, and the tube
// runs from a thin silhouette edge to its thickest point within a few
// centimetres of screen space.
//
// The shape comes from the engine's MESH emitter, not a formula:
// `MeshAdjacency_SampleEdge` picks a random point on a random edge of a real
// torus mesh (the same primitive vc_converge_motes.inl uses). Swap the mesh and
// the body changes shape with no other edit — which is the point of emitting
// from geometry instead of parameterising a ring.
//
// Motion is force fields only: a vortex around the ring axis carries the water
// around, curl noise keeps the surface from looking extruded, and viscosity is
// what makes neighbouring splats travel together — without it the reconstruction
// has nothing to merge. There is no PBD here and no solver of any kind; the
// silhouette is maintained by respawning on the mesh, which costs nothing and
// never explodes.

/* The ring read as a filled disc at tube 0.22 / kernel 0.095: the splats were
 * too fat for the hole. Geometric clearance was 1.23 m, which sounds ample until
 * you count what the RECONSTRUCTION adds — each kernel inflates the tube by
 * another 0.095 of the ring radius, and the depth filter bridges what is left.
 * The fix is a PROPORTION, not a size: coverage is scale-invariant, so growing
 * the ring changes nothing. Thinning both holds the same 3.3 / 2.3 / 1.5
 * coverage and opens the hole to 1.46 m. */
#define WATER_RING_TUBE_RATIO 0.12f   /* tube radius as a fraction of the ring */
#define WATER_RING_IDLE_RELEASE 0.25f /* seconds without a call before release */
#define WATER_RING_MAX_SPAWN 48       /* per-frame ceiling after a hitch */

static MeshAdjacency s_waterRingMesh;
static bool s_waterRingMeshReady = false;
static ForceField s_waterRingField;
static MotionFieldHandle s_waterRingMotionField = MOTION_FIELD_INVALID;
static ParticleEmitterHandle s_waterRingEmitter = PARTICLE_EMITTER_INVALID;
static bool s_waterRingUsesSurface = true;
static float s_waterRingIdle = 0.0f;
static float s_waterRingAccum = 0.0f;
static ParticleConfig s_waterRingSpawn[WATER_RING_MAX_SPAWN];

static VFX_LiquidRingConfig s_liveLiquidRingConfig;
static bool s_liveLiquidRingConfigInit = false;
static VFX_LiquidRingConfig s_currentLiquidRingConfig;
static LiquidDesc s_waterRingMaterial;

VFX_LiquidRingConfig VFX_LiquidRing_DefaultConfig(void)
{
    VFX_LiquidRingConfig c = {0};
    c.center = (Vector3){6.0f, 1.2f, 4.4f};
    c.radius = 0.9f;
    c.tubeRatio = WATER_RING_TUBE_RATIO;
    c.flowSpeed = 2.4f;
    c.swirlSpeed = 1.8f;
    c.turbulenceSpeed = 0.8f;
    c.pullForce = 4.0f;
    c.motionProfile = (int)LIQUID_MOTION_WATER;
    c.renderBackend = (int)LIQUID_RENDER_METABALL3D;
    c.blobCount = 48;
    c.blobRadius = 0.18f;
    c.threshold = 0.40f;
    c.ior = 1.333f;
    c.roughness = 0.05f;
    c.opacity = 1.5f;
    return c;
}

VFX_LiquidRingConfig *VFX_LiquidRing_GetLiveConfig(void)
{
    if (!s_liveLiquidRingConfigInit) {
        s_liveLiquidRingConfig = VFX_LiquidRing_DefaultConfig();
        s_liveLiquidRingConfigInit = true;
    }
    return &s_liveLiquidRingConfig;
}

int VFX_LiquidRing_GetParams(VFX_LiquidRingConfig *c, VFX_ParamDef *out, int max)
{
    if (!c || !out || max <= 0) return 0;
    int n = 0;
    static const char *const backendNames[] = {"Metaball 3D", "Screen Space Fluid"};
    static const char *const profileNames[] = {"Water", "Poison", "Mud", "Lava", "Liquid Metal"};

    #define RING_PARAM(pname, pgroup, ptype, pval, plo, phi, pstep, pminInt, pmaxInt, pnames, pcount) \
        do { if (n < max) out[n++] = (VFX_ParamDef){.name=pname, .group=pgroup, .type=ptype, \
            .valPtr=pval, .minFloat=plo, .maxFloat=phi, .stepFloat=pstep, \
            .minInt=pminInt, .maxInt=pmaxInt, .enumNames=pnames, .enumCount=pcount}; } while(0)

    RING_PARAM("Render backend", "Liquid", VFX_PARAM_ENUM, &c->renderBackend, 0, 1, 0, 0, 1, backendNames, 2);
    RING_PARAM("Motion profile", "Liquid", VFX_PARAM_ENUM, &c->motionProfile, 0, 4, 0, 0, 4, profileNames, 5);
    RING_PARAM("Ring radius m", "Liquid", VFX_PARAM_FLOAT, &c->radius, 0.20f, 3.0f, 0.05f, 0, 0, NULL, 0);
    RING_PARAM("Tube ratio", "Liquid", VFX_PARAM_FLOAT, &c->tubeRatio, 0.05f, 0.30f, 0.01f, 0, 0, NULL, 0);
    RING_PARAM("Blob count", "Liquid", VFX_PARAM_INT, &c->blobCount, 8, 64, 1, 8, 64, NULL, 0);
    RING_PARAM("Blob radius m", "Liquid", VFX_PARAM_FLOAT, &c->blobRadius, 0.03f, 0.35f, 0.01f, 0, 0, NULL, 0);
    RING_PARAM("Threshold", "Liquid", VFX_PARAM_FLOAT, &c->threshold, 0.20f, 0.85f, 0.02f, 0, 0, NULL, 0);

    RING_PARAM("Flow speed m/s", "Field", VFX_PARAM_FLOAT, &c->flowSpeed, 0.0f, 15.0f, 0.2f, 0, 0, NULL, 0);
    RING_PARAM("Swirl speed m/s", "Field", VFX_PARAM_FLOAT, &c->swirlSpeed, -10.0f, 10.0f, 0.2f, 0, 0, NULL, 0);
    RING_PARAM("Turbulence m/s", "Field", VFX_PARAM_FLOAT, &c->turbulenceSpeed, 0.0f, 10.0f, 0.2f, 0, 0, NULL, 0);
    RING_PARAM("Pull force N", "Field", VFX_PARAM_FLOAT, &c->pullForce, 0.0f, 10.0f, 0.1f, 0, 0, NULL, 0);

    RING_PARAM("Refraction IOR", "Optics", VFX_PARAM_FLOAT, &c->ior, 1.0f, 2.5f, 0.02f, 0, 0, NULL, 0);
    RING_PARAM("Roughness", "Optics", VFX_PARAM_FLOAT, &c->roughness, 0.02f, 1.0f, 0.02f, 0, 0, NULL, 0);
    RING_PARAM("Opacity /m", "Optics", VFX_PARAM_FLOAT, &c->opacity, 0.1f, 20.0f, 0.2f, 0, 0, NULL, 0);

    #undef RING_PARAM
    return n;
}

static const MeshAdjacency *WaterRing_Mesh(void)
{
    if (!s_waterRingMeshReady)
    {
        /* GenMeshTorus(radius, size, ...) is (TUBE radius, overall scale) — NOT
         * (ring radius, tube radius). par_shapes__torus fixes the major radius
         * at 1 and takes the caller's value as the MINOR one, then raylib scales
         * the result by size/2. Passing 1.0 first therefore asks for a tube as
         * fat as the ring: a torus whose hole is exactly zero, which is why this
         * body rendered as a filled disc no matter how thin the splats got. The
         * hole was never in the mesh.
         *
         * So: tube ratio first, and size 2.0 to bring the unit major radius back
         * to 1 after the size/2. Segment counts are the sampling grid the emitter
         * draws from — too few and the edges show as spokes in the reconstructed
         * surface, too many only costs build time (welded vertices, once). */
        Mesh m = GenMeshTorus(WATER_RING_TUBE_RATIO, 2.0f, 40, 20);
        MeshAdjacency_Build(&s_waterRingMesh, m);
        UnloadMesh(m);
        s_waterRingMeshReady = true;
    }
    return &s_waterRingMesh;
}

static void WaterRing_SetField(Vector3 center, float radius, float t01)
{
    ForceField_Clear(&s_waterRingField);
    /* Flow around the ring axis. This is the whole reason the body reads as
     * liquid instead of a mesh: the surface moves while the silhouette does not. */
    ForceField_AddLayer(&s_waterRingField, (ForceLayer){.type=FORCE_VORTEX_AXIS,
        .origin=center, .direction={0.0f,1.0f,0.0f},
        .strength=1.8f + 1.6f*t01, .radius=radius*2.4f, .falloff=1.0f});
    /* Surface life. Small on purpose — enough to break the extruded look, not
     * enough to tear splats out of the sheet. */
    ForceField_AddLayer(&s_waterRingField, (ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=0.30f + 0.35f*t01, .noiseScale=2.6f, .noiseSpeed=1.4f});
    /* Neighbours must travel TOGETHER or the reconstruction has nothing to
     * merge; this is the single most important layer for SSF coherence. */
    ForceField_AddLayer(&s_waterRingField, (ForceLayer){.type=FORCE_VISCOSITY,
        .strength=3.6f});
}

void VFX_ComposeLiquidRingEx(const VFX_LiquidRingConfig *config)
{
    if (!config) return;
    s_currentLiquidRingConfig = *config;
    s_waterRingIdle = 0.0f;

    float radius = (config->radius <= 0.0f) ? 0.6f : config->radius;
    Vector3 center = config->center;
    float t01 = 1.0f;

    const VFX_ElementMaterial *water = VFX_Material(VC_MAT_WATER);
    LiquidDesc material = LiquidSurface_ProfileDesc(LIQUID_MOTION_WATER);
    if (config->motionProfile != (int)LIQUID_MOTION_WATER &&
        config->motionProfile >= 0 && config->motionProfile <= 4)
        material = LiquidSurface_ProfileDesc((LiquidMotionProfile)config->motionProfile);
    if (config->ior > 1.0f) material.ior = config->ior;
    if (config->roughness > 0.0f) material.roughnessScale = config->roughness * 5.0f;
    if (config->opacity > 0.0f) material.opacityPerMetre = config->opacity;
    s_waterRingMaterial = material;

    const MeshAdjacency *mesh = WaterRing_Mesh();

    // Create physical motion field guide for orbiting liquid mass
    if (s_waterRingMotionField == MOTION_FIELD_INVALID || !MotionFields_IsAlive(s_waterRingMotionField))
    {
        if (s_waterRingMotionField != MOTION_FIELD_INVALID) MotionFields_Stop(s_waterRingMotionField);
        MotionFieldRecipe r = MotionRecipe_Orbit(center, (Vector3){0.0f, 1.0f, 0.0f},
                                                 radius, config->swirlSpeed, 60.0f);
        r.turbulenceSpeedMps = config->turbulenceSpeed;
        r.maxForceNewtons = config->pullForce;
        r.receiverMask = MOTION_RECEIVER_ALL_COMPONENTS;
        FieldDesc fieldDesc;
        if (MotionFieldRecipe_Build(&r, &fieldDesc)) {
            s_waterRingMotionField = MotionFields_CreateField(&fieldDesc);
        }
    }
    else
    {
        FieldTransform tf = {.position = center};
        MotionFields_SetTransform(s_waterRingMotionField, &tf);
    }

    bool ringRunning = (s_waterRingEmitter != PARTICLE_EMITTER_INVALID) && s_waterRingUsesSurface;
    bool useSurface = (config->renderBackend == LIQUID_RENDER_SSF) &&
                      LiquidSurface_RequestBody(LIQUID_PRIORITY_CAST, center,
                                                radius * (1.0f + WATER_RING_TUBE_RATIO),
                                                ringRunning);
    WaterRing_SetField(center, radius, t01);

    if (s_waterRingEmitter == PARTICLE_EMITTER_INVALID)
    {
        ParticleEmitterDesc desc = {0};
        bool cpuOnly = (config->renderBackend == LIQUID_RENDER_METABALL3D) ||
                       (getenv("WUXING_LIQUID_CPU_ONLY") && atoi(getenv("WUXING_LIQUID_CPU_ONLY")) != 0);
        desc.simulationPolicy = cpuOnly ? PARTICLE_SIM_CPU_ONLY : PARTICLE_SIM_AUTO;
        desc.renderMode = (useSurface || config->renderBackend == LIQUID_RENDER_METABALL3D)
                              ? PARTICLE_RENDER_SURFACE_INPUT : PARTICLE_RENDER_BILLBOARD;
        desc.moduleFlags = PARTICLE_MODULE_FORCE_FIELD;
        desc.debugName = "WaterRing SSF stream";
        desc.particle = (ParticleConfig){.forceField = &s_waterRingField};
        s_waterRingEmitter = ParticleManager_CreateEmitter(&desc);
        if (s_waterRingEmitter == PARTICLE_EMITTER_INVALID) return;
    }

    /* Population, not rate, is what decides whether the surface CLOSES: the
     * splats have to overlap. Torus surface is (2*PI*R)(2*PI*r) m², a kernel
     * covers about PI*k², and below a ratio of ~1 the reconstruction stops
     * bridging gaps and the body renders as a cluster of beads. These counts
     * hold 3.3 / 2.3 / 1.5 at HIGH / MED / LOW, so LOW thins the water rather
     * than opening holes in it. The ratio is scale-invariant (both areas go as
     * radius²), which is why the caller may pick any ring size.
     * core/tests/water_ring_coverage_test.c owns this arithmetic — it caught
     * the first pass of these numbers falling under 1 at MED and LOW. */
    GfxQuality quality = GfxQuality_Get();
    float alive = quality >= GFX_HIGH ? 1000.0f : (quality >= GFX_MED ? 700.0f : 460.0f);
    alive *= Math_Mix(0.55f, 1.0f, t01);
    const float lifetime = 0.95f;
    const float kernel = radius * 0.070f;

    /* Framerate-independent budget carried between frames: a per-call COUNT
     * makes the body's density a function of the frame rate. */
    s_waterRingAccum += TimeFX_RawDelta() * (alive / lifetime);
    int spawn = (int)s_waterRingAccum;
    if (spawn > WATER_RING_MAX_SPAWN) spawn = WATER_RING_MAX_SPAWN;
    s_waterRingAccum -= (float)spawn;
    if (spawn <= 0) return;

    /* par_shapes lays the ring in the XY plane with the tube axis along Z, so the
     * mesh stands upright. Everything downstream — the vortex axis, the tangent
     * launch, `spoke.y = 0` — assumes a ring lying flat in XZ, so rotate it
     * there rather than special-casing four different places. */
    Matrix xform = MatrixMultiply(MatrixMultiply(MatrixRotateX(-PI*0.5f),
                                                 MatrixScale(radius, radius, radius)),
                                  MatrixTranslate(center.x, center.y, center.z));
    for (int i = 0; i < spawn; ++i)
    {
        Vector3 local = MeshAdjacency_SampleEdge(mesh);
        Vector3 position = Vector3Transform(local, xform);
        /* Launch along the ring, so the vortex layer has something to keep
         * rather than something to start. The radial component is deliberately
         * tiny: pushing outward here is what turns a ring into a cloud. */
        Vector3 spoke = Vector3Subtract(position, center);
        spoke.y = 0.0f;
        Vector3 tangent = Vector3Normalize(Vector3CrossProduct((Vector3){0.0f,1.0f,0.0f}, spoke));
        Vector3 velocity = Vector3Scale(tangent, Math_Mix(0.30f, 0.62f, Random01()) * (0.5f + 0.5f*t01));
        velocity.y += (Random01() - 0.5f) * 0.10f;

        s_waterRingSpawn[i] = (ParticleConfig){
            .position = position,
            .velocity = velocity,
            /* SSF never reads particle colour, so alpha 0 there means a stray
             * billboard path could not draw these. The fallback needs the
             * opposite: these ARE the visible body. */
            .colorStart = VC_WithAlpha(water->soft, useSurface ? 0 : 190),
            .colorEnd = VC_WithAlpha(water->body, useSurface ? 0 : 0),
            /* Polydisperse: equal radii reconstruct into visible rows, the same
             * trap the PBD pool documents. */
            .radius = kernel * Math_Mix(0.82f, 1.18f, Random01()),
            .lifetime = lifetime * Math_Mix(0.85f, 1.15f, Random01()),
            .forceField = &s_waterRingField,
            .forceAxisOrigin = center,
            .forceAxisDir = (Vector3){0.0f, 1.0f, 0.0f}};
        s_waterRingSpawn[i].physics.receiveMotionFields = false;
        s_waterRingSpawn[i].physics.spatialMotionOnly = false;
    }
    ParticleManager_EmitBatch(s_waterRingEmitter, s_waterRingSpawn, spawn);

    if (useSurface) {
        LiquidSurface_BindMaterial(&material);
        LiquidSurface_SetReconstructionRadiusFor(LIQUID_PRIORITY_CAST, kernel);
    }
    s_waterRingUsesSurface = useSurface;
}

/* Continuous — call every frame while the ring should exist; it releases itself
 * a quarter second after the calls stop. `radius` is the ring radius in metres
 * (the tube is 0.12 of it), `t01` drives density and flow speed.
 *
 * SSF binds the ring's optical material to its submitted stream, allowing
 * other liquids to retain their own optics in the same capture. */
void VFX_ComposeWaterRing(Vector3 center, float radius, float t01)
{
    VFX_LiquidRingConfig *cfg = VFX_LiquidRing_GetLiveConfig();
    cfg->center = center;
    if (cfg->radius <= 0.0f) cfg->radius = (radius <= 0.0f) ? 0.6f : radius;
    t01 = Clamp(t01, 0.0f, 1.0f);
    cfg->flowSpeed = 1.8f + 1.6f * t01;
    VFX_ComposeLiquidRingEx(cfg);
}

/* Stop feeding it and it goes away by itself; call this to drop it now. */
void VFX_WaterRing_Stop(void)
{
    if (s_waterRingEmitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_DestroyEmitter(s_waterRingEmitter);
    if (s_waterRingMotionField != MOTION_FIELD_INVALID)
        MotionFields_Stop(s_waterRingMotionField);
    s_waterRingMotionField = MOTION_FIELD_INVALID;
    s_waterRingEmitter = PARTICLE_EMITTER_INVALID;
    s_waterRingAccum = 0.0f;
}

static void WaterRing_Update(float dt)
{
    if (s_waterRingEmitter == PARTICLE_EMITTER_INVALID &&
        s_currentLiquidRingConfig.renderBackend != LIQUID_RENDER_METABALL3D) return;
    s_waterRingIdle += dt;
    if (s_currentLiquidRingConfig.renderBackend == LIQUID_RENDER_METABALL3D &&
        s_waterRingIdle <= WATER_RING_IDLE_RELEASE)
    {
        SceneTargets_RequestSceneSnapshot();
    }
    /* Release only after the last particles have died, or the body would vanish
     * mid-flight the frame the caller stops. */
    if (s_waterRingIdle > WATER_RING_IDLE_RELEASE + 1.2f) VFX_WaterRing_Stop();
}

/* Liquid surface submission — runs in the screen-space composite phase. */
static void WaterRing_SubmitSurface(void)
{
    if (s_currentLiquidRingConfig.renderBackend == LIQUID_RENDER_METABALL3D)
    {
        if (s_waterRingIdle > WATER_RING_IDLE_RELEASE) return;
        int blobCount = s_currentLiquidRingConfig.blobCount > 0 ? s_currentLiquidRingConfig.blobCount : 48;
        float R = s_currentLiquidRingConfig.radius > 0.0f ? s_currentLiquidRingConfig.radius : 0.9f;
        float tubeRatio = s_currentLiquidRingConfig.tubeRatio > 0.01f ? s_currentLiquidRingConfig.tubeRatio : WATER_RING_TUBE_RATIO;
        float tubeR = R * tubeRatio;
        float bRadius = s_currentLiquidRingConfig.blobRadius > 0.01f ? s_currentLiquidRingConfig.blobRadius : (tubeR * 1.65f);
        Vector3 center = s_currentLiquidRingConfig.center;
        float flowSpeed = s_currentLiquidRingConfig.flowSpeed;
        float swirlSpeed = s_currentLiquidRingConfig.swirlSpeed;
        float t = (float)GetTime();

        float ringThreshold = s_currentLiquidRingConfig.threshold > 0.05f ? s_currentLiquidRingConfig.threshold : 0.40f;
        LiquidMetaball3D_BeginBody(&s_waterRingMaterial, ringThreshold);
        for (int b = 0; b < blobCount; ++b) {
            float theta = ((float)b / (float)blobCount) * (2.0f * PI) + (t * flowSpeed);
            float phi = ((float)b * 1.618f) * (2.0f * PI) + (t * swirlSpeed);
            float rMinor = tubeR * (0.35f * sinf(phi));
            float curR = R + tubeR * (0.35f * cosf(phi));
            Vector3 pos = (Vector3){
                center.x + curR * cosf(theta),
                center.y + rMinor,
                center.z + curR * sinf(theta)
            };
            LiquidMetaball3D_RegisterBlob(pos, bRadius);
        }
        LiquidMetaball3D_EndBody();
    }
    else
    {
        /* Not submitting is the whole point of the gate: the frame's fixed SSF cost
         * is only paid if something is actually in the capture. */
        if (!s_waterRingUsesSurface) return;
        ParticleRenderStream stream;
        if (s_waterRingEmitter != PARTICLE_EMITTER_INVALID &&
            ParticleManager_GetSurfaceStream(s_waterRingEmitter, &stream))
            LiquidSurface_SubmitParticleStream(&stream);
    }
}
