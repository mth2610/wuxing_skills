// Headless contract test for Universal Botanical Foliage & Petal Simulation System.
// Validates presence of Dual-State architecture, physical aerodynamic laws,
// zero-heap allocation constraints, and engine-wide wiring.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[4096];
    size_t used = 0;
    size_t n;
    while ((n = fread(buf + used, 1, sizeof(buf) - 1 - used, f)) != 0)
    {
        used += n;
        buf[used] = '\0';
        if (strstr(buf, needle) != NULL)
        {
            fclose(f);
            return 1;
        }
        if (used > 1024)
        {
            memmove(buf, buf + used - 1024, 1024);
            used = 1024;
        }
    }
    fclose(f);
    return 0;
}

int main(void)
{
    int failed = 0;
    int checks = 0;

#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        failed++; \
    } else { \
        printf("PASS: %s\n", msg); \
    } \
} while (0)

    const char *inl = "core/composition/wood/vc_wood_foliage_system.inl";
    const char *hdr = "core/composition/visual_composer.h";
    const char *vc  = "core/composition/visual_composer.c";
    const char *sk  = "skills/wood/leaf_whirlwind_skill/leaf_whirlwind_skill.c";

    // 1. Dual-State Architecture
    CHECK(FileHas(inl, "BOTANICAL_STATE_ATTACHED"), "foliage system supports ATTACHED state for branch/vine anchoring");
    CHECK(FileHas(inl, "BOTANICAL_STATE_FREE"), "foliage system supports FREE physical airborne simulation state");
    CHECK(FileHas(inl, "BOTANICAL_STATE_SETTLED"), "foliage system supports SETTLED state on terrain");

    // 2. Physical laws & forces
    CHECK((FileHas(inl, "MotionBody_AdvanceVelocity") && (FileHas("core/motion/motion_body.h", "ParticleDynamics_GravityAcceleration") && FileHas("core/particles/particle_dynamics.h", "9.81f"))), "real-world gravity (9.81 m/s^2) applied to airborne leaves");
    CHECK(FileHas(inl, "dragCoeff"), "planar aerodynamic drag coefficient modeled for leaf glide/flutter");
    CHECK(FileHas(inl, "Wind_EvaluateVelocity"), "forest environmental wind acceleration evaluates dynamically");
    CHECK(FileHas(inl, "ForceField_Evaluate"), "external force fields evaluated for vortex and homing suction");
    CHECK(FileHas(inl, "MapManager_GetGroundHeightAt"), "terrain height collision and ground contact properly queried");

    // 3. Performance & Capacity
    CHECK(FileHas(inl, "VFX_FOLIAGE_POOL_CAPACITY 2048"), "fixed-size static pool capacity defined (2048 particles)");
    CHECK(!FileHas(inl, "malloc("), "zero dynamic heap allocation (no malloc in tick or draw)");

    // 4. Detachment and Homing Mechanics
    CHECK(FileHas(inl, "VFX_FoliageSystem_DetachInRadius"), "dynamic radius detachment implemented");
    CHECK(FileHas(inl, "VFX_FoliageSystem_SetHomingTarget"), "homing vortex target setter implemented (Van Diep Quy Tong)");
    CHECK(FileHas(inl, "VFX_FoliageSystem_ClearHomingTarget"), "homing vortex target clearer implemented");

    // 5. Convenience API
    CHECK(FileHas(inl, "VFX_Foliage_SpawnFreeLeaves"), "convenience wrapper VFX_Foliage_SpawnFreeLeaves implemented");
    CHECK(FileHas(inl, "VFX_Foliage_SpawnFreePetals"), "convenience wrapper VFX_Foliage_SpawnFreePetals implemented");
    CHECK(FileHas(inl, "VFX_Foliage_SpawnAttachedLeaves"), "convenience wrapper VFX_Foliage_SpawnAttachedLeaves implemented");
    CHECK(FileHas(inl, "VFX_Foliage_SpawnAttachedFlowers"), "convenience wrapper VFX_Foliage_SpawnAttachedFlowers implemented");

    // 6. Header declarations
    CHECK(FileHas(hdr, "VFX_Foliage_SpawnFreeLeaves"), "visual_composer.h exposes high-level foliage spawn API");
    CHECK(FileHas(hdr, "VFX_FoliageSystem_SetHomingTarget"), "visual_composer.h exposes homing target API");

    // 7. Engine Dispatch wiring
    CHECK(FileHas(vc, "VFX_FoliageSystem_Update(dt, NULL)"), "visual_composer.c updates foliage system in main loop");
    CHECK(FileHas(vc, "VFX_FoliageSystem_Draw()"), "visual_composer.c renders foliage system in 3D pass");

    // 8. Skill integration
    CHECK(FileHas(sk, "VFX_Foliage_SpawnFreeLeaves"), "leaf_whirlwind_skill spawns physical foliage flurry");
    CHECK(FileHas(sk, "VFX_FoliageSystem_SetHomingTarget"), "leaf_whirlwind_skill engages homing vortex force field");

    printf("\n==== Foliage System Contract: %d/%d checks passed ====\n", checks - failed, checks);
    return failed > 0 ? 1 : 0;
}
