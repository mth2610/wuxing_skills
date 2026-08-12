// liquid_bench.inl — three SSF liquids side by side, in ONE capture.
//
// This is the reference bench for the liquid table: water, lava and liquid
// metal drawn in the same frame, which is the whole point of the per-pixel
// material id. Until that id existed the SSF surface carried ONE material
// globally, so this fixture could only ever have rendered three bodies in
// whichever colour was bound last — which is exactly what it shows if the id
// path regresses.
//
// It deliberately uses the CPU ellipsoid path (FluidSurface_RegisterEllipsoid)
// rather than a particle emitter. Emitters spawn stochastically and settle over
// seconds; these blobs are a closed-form function of time, so a headless capture
// at a given warmup frame is reproducible and the three bodies are guaranteed to
// be the same shape as each other. The look of a real liquid body is the
// emitters' job (WATER RING), not this one's.

#define LIQUID_BENCH_SPHERES 26

/* A squat blob of overlapping spheres. Overlap is not decoration: the surface
 * only CLOSES when neighbouring kernels intersect, which is the same constraint
 * water_ring.inl's population arithmetic exists to satisfy. */
static void LiquidBench_Body(Vector3 center, float radius, float kernel, float phase)
{
    for (int i = 0; i < LIQUID_BENCH_SPHERES; ++i)
    {
        /* Fibonacci sphere: the one deterministic layout that stays even at any
         * count, so no two kernels land on top of each other and leave a hole
         * somewhere else. */
        float t = ((float)i + 0.5f) / (float)LIQUID_BENCH_SPHERES;
        float y = 1.0f - 2.0f * t;
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        float a = (float)i * 2.39996323f;           /* golden angle */
        Vector3 dir = { cosf(a) * r, y, sinf(a) * r };

        /* Breathing, not noise: the body has to move for the specular and the
         * crust to read as a liquid rather than a statue, and a closed-form
         * wobble keeps the fixture reproducible frame for frame. */
        float wobble = 0.86f + 0.14f * sinf(phase * 1.7f + (float)i * 0.9f);
        /* Squat: a sphere reconstructs beautifully and tells you nothing about
         * how the optics behave over a varying thickness. */
        Vector3 offset = { dir.x * radius * wobble,
                           dir.y * radius * 0.55f * wobble,
                           dir.z * radius * wobble };
        FluidSurface_RegisterParticle(Vector3Add(center, offset), kernel);
    }
}

/* Continuous — call every frame. `spacing` is the gap between bodies in metres.
 *
 * Each body binds its own liquid BEFORE registering its kernels, because the
 * bind is what the following registrations inherit. Binding all three up front
 * and then registering would put every kernel in the last slot. */
void VFX_ComposeLiquidBench(Vector3 center, float spacing, float t01)
{
    if (spacing <= 0.0f) spacing = 1.1f;
    t01 = Clamp(t01, 0.0f, 1.0f);
    float phase = (float)GetTime() * (0.55f + 0.45f * t01);

    const float radius = 0.30f;
    /* Kernels overlap by construction: 26 of them on a shell of radius 0.30 m
     * are about 0.21 m apart, so a 0.13 m kernel bridges every gap. */
    const float kernel = 0.13f;
    FluidSurface_SetReconstructionRadius(kernel);

    /* Airborne, like the WATER RING fixture, and for the same reason: a body
     * resting on the arena floor is judged against a receiver, which bounds its
     * optical path (`waterColumnDepth`) and pulls in the shoreline foam. The
     * bench is here to compare three liquids' OPTICS, so it holds them clear of
     * the ground where the only thing behind them is sky. */
    const float lift = 1.15f;
    const Vector3 place[3] = {
        { center.x - spacing, center.y + lift, center.z },
        { center.x,           center.y + lift, center.z },
        { center.x + spacing, center.y + lift, center.z }
    };
    FluidLiquidDesc liquid[3];
    liquid[0] = VFX_LiquidWater();
    liquid[1] = VFX_LiquidLava();
    liquid[2] = VFX_LiquidMetal();

    for (int i = 0; i < 3; ++i)
    {
        FluidSurface_BindMaterial(&liquid[i]);
        LiquidBench_Body(place[i], radius, kernel, phase + (float)i * 1.3f);
    }
}
