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

#define WATER_RING_TUBE_RATIO 0.22f   /* tube radius as a fraction of the ring */
#define WATER_RING_IDLE_RELEASE 0.25f /* seconds without a call before release */
#define WATER_RING_MAX_SPAWN 48       /* per-frame ceiling after a hitch */

static MeshAdjacency s_waterRingMesh;
static bool s_waterRingMeshReady = false;
static ForceField s_waterRingField;
static ParticleEmitterHandle s_waterRingEmitter = PARTICLE_EMITTER_INVALID;
static float s_waterRingIdle = 0.0f;
static float s_waterRingAccum = 0.0f;
static ParticleConfig s_waterRingSpawn[WATER_RING_MAX_SPAWN];

static const MeshAdjacency *WaterRing_Mesh(void)
{
    if (!s_waterRingMeshReady)
    {
        /* A UNIT ring: the caller's radius is a transform, so one mesh serves
         * every scale. Segment counts are the sampling grid the emitter draws
         * from — too few and the edges show as spokes in the reconstructed
         * surface, too many only costs build time (welded vertices, once). */
        Mesh m = GenMeshTorus(1.0f, WATER_RING_TUBE_RATIO, 40, 20);
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

/* Continuous — call every frame while the ring should exist; it releases itself
 * a quarter second after the calls stop. `radius` is the ring radius in metres
 * (the tube is 0.22 of it), `t01` drives density and flow speed.
 *
 * SSF carries ONE material at a time (FluidSurface_SetMaterialColors is global),
 * so a second fluid body on screen in the same frame shares this one's optics. */
void VFX_ComposeWaterRing(Vector3 center, float radius, float t01)
{
    if (radius <= 0.0f) radius = 0.6f;
    t01 = Clamp(t01, 0.0f, 1.0f);

    const VFX_ElementMaterial *water = VFX_Material(VC_MAT_WATER);
    const MeshAdjacency *mesh = WaterRing_Mesh();
    s_waterRingIdle = 0.0f;
    WaterRing_SetField(center, radius, t01);

    if (s_waterRingEmitter == PARTICLE_EMITTER_INVALID)
    {
        ParticleEmitterDesc desc = {0};
        desc.simulationPolicy = PARTICLE_SIM_AUTO;
        desc.renderMode = PARTICLE_RENDER_SURFACE_INPUT;
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
    const float kernel = radius * 0.095f;

    /* Framerate-independent budget carried between frames: a per-call COUNT
     * makes the body's density a function of the frame rate. */
    s_waterRingAccum += GetFrameTime() * (alive / lifetime);
    int spawn = (int)s_waterRingAccum;
    if (spawn > WATER_RING_MAX_SPAWN) spawn = WATER_RING_MAX_SPAWN;
    s_waterRingAccum -= (float)spawn;
    if (spawn <= 0) return;

    Matrix xform = MatrixMultiply(MatrixScale(radius, radius, radius),
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
            /* SSF never reads particle colour, and alpha 0 means a stray
             * billboard path could not draw these even if one appeared. */
            .colorStart = VC_WithAlpha(water->soft, 0),
            .colorEnd = VC_WithAlpha(water->body, 0),
            /* Polydisperse: equal radii reconstruct into visible rows, the same
             * trap the PBD pool documents. */
            .radius = kernel * Math_Mix(0.82f, 1.18f, Random01()),
            .lifetime = lifetime * Math_Mix(0.85f, 1.15f, Random01()),
            .forceField = &s_waterRingField,
            .forceAxisOrigin = center,
            .forceAxisDir = (Vector3){0.0f, 1.0f, 0.0f}};
    }
    ParticleManager_EmitBatch(s_waterRingEmitter, s_waterRingSpawn, spawn);

    FluidSurface_SetMaterialColors(water->body, water->glow, water->soft);
    FluidSurface_SetReconstructionRadius(kernel);
}

/* Stop feeding it and it goes away by itself; call this to drop it now. */
void VFX_WaterRing_Stop(void)
{
    if (s_waterRingEmitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_DestroyEmitter(s_waterRingEmitter);
    s_waterRingEmitter = PARTICLE_EMITTER_INVALID;
    s_waterRingAccum = 0.0f;
}

static void WaterRing_Update(float dt)
{
    if (s_waterRingEmitter == PARTICLE_EMITTER_INVALID) return;
    s_waterRingIdle += dt;
    /* Release only after the last particles have died, or the body would vanish
     * mid-flight the frame the caller stops. */
    if (s_waterRingIdle > WATER_RING_IDLE_RELEASE + 1.2f) VFX_WaterRing_Stop();
}

/* SSF submission — runs in the screen-space composite phase, before
 * FluidSurface_HasPending(). */
static void WaterRing_SubmitSurface(void)
{
    ParticleRenderStream stream;
    if (s_waterRingEmitter != PARTICLE_EMITTER_INVALID &&
        ParticleManager_GetSurfaceStream(s_waterRingEmitter, &stream))
        FluidSurface_SubmitParticleStream(&stream);
}
