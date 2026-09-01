/* Headless contract for guided particles.
 *
 * The arithmetic exercises the same C helper used by the CPU fallback.  The
 * source checks pin only the load-bearing GPU mirror/wiring; they cannot prove
 * a real compute dispatch or SSBO binding, which still needs runtime coverage.
 */
#include "core/particles/particle_travel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int s_failures;

#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); s_failures++; } \
} while (0)

static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    char text[131072];
    size_t n;
    if (!f) return 0;
    n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    return strstr(text, needle) != NULL;
}

static void Test_FollowsWaypointsAndArrives(void)
{
    Vector3 points[] = {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}};
    ParticleTravelPath path = {
        .points = points,
        .pointCount = 2,
        .speed = 2.0f,
        .steering = 30.0f,
        .waypointRadius = 0.08f,
        .targetRadius = 0.08f
    };
    Vector3 position = {0};
    Vector3 velocity = {0};
    int waypoint = 0;
    int arrived = 0;

    for (int i = 0; i < 240 && !arrived; ++i) {
        arrived = ParticleTravel_Step(&path, 1.0f / 120.0f,
                                      &position, &velocity, &waypoint);
    }

    CHECK(arrived, "particle reaches the final path target");
    CHECK(waypoint == 1, "particle advances through the intermediate waypoint");
    CHECK(fabsf(position.x - 1.0f) < 0.081f &&
          fabsf(position.z - 1.0f) < 0.081f,
          "arrival is reported inside the authored target radius");
}

static void Test_SweptArrivalCannotTunnel(void)
{
    Vector3 target = {1.0f, 0.0f, 0.0f};
    ParticleTravelPath path = {
        .target = &target,
        .speed = 20.0f,
        .steering = 100.0f,
        .targetRadius = 0.05f
    };
    Vector3 position = {0};
    Vector3 velocity = {20.0f, 0.0f, 0.0f};
    int waypoint = 0;

    CHECK(ParticleTravel_Step(&path, 0.1f, &position, &velocity, &waypoint),
          "swept target test catches a high-speed overshoot");
}

static void Test_ForceVelocityIsGuidedNotErased(void)
{
    Vector3 target = {4.0f, 0.0f, 0.0f};
    ParticleTravelPath path = {
        .target = &target,
        .speed = 2.0f,
        .steering = 2.0f,
        .maxAcceleration = 3.0f,
        .targetRadius = 0.1f
    };
    Vector3 position = {0};
    /* Represents velocity after this frame's force-field acceleration. */
    Vector3 velocity = {0.0f, 0.0f, 1.0f};
    int waypoint = 0;

    (void)ParticleTravel_Step(&path, 0.1f, &position, &velocity, &waypoint);
    CHECK(velocity.x > 0.0f, "path steering pulls toward the next waypoint");
    CHECK(velocity.z > 0.0f, "finite steering preserves force-field deflection");
}

static void Test_DynamicTargetIsReadFresh(void)
{
    Vector3 target = {1.0f, 0.0f, 0.0f};
    ParticleTravelPath path = {.target = &target, .speed = 1.0f};
    Vector3 got = ParticleTravel_GetWaypoint(&path, 0);
    target = (Vector3){3.0f, 2.0f, -1.0f};
    Vector3 moved = ParticleTravel_GetWaypoint(&path, 0);

    CHECK(got.x == 1.0f && moved.x == 3.0f && moved.y == 2.0f && moved.z == -1.0f,
          "a stable target pointer supports moving targets without rebuilding the path");
}

static void Test_ArrivalTransitionsIntoImpactMotion(void)
{
    Vector3 target = {1.0f, 0.0f, 0.0f};
    ParticleTravelPath path = {
        .target = &target,
        .speed = 20.0f,
        .steering = 100.0f,
        .targetRadius = 0.05f,
        .arrivalOffset = 0.10f,
        .arrivalKick = 2.0f
    };
    Vector3 position = {0};
    Vector3 velocity = {20.0f, 0.0f, 0.0f};
    int waypoint = 0;

    CHECK(ParticleTravel_Step(&path, 0.1f, &position, &velocity, &waypoint),
          "arrival transition still detects the target");
    ParticleTravel_ApplyImpactEntry(&path, &position, &velocity);
    CHECK(position.x > target.x && velocity.x > 20.0f,
          "arrived particles receive an outward impact offset and kick");
}

static void Test_MovingTargetAlwaysOwnsTheFinalSlot(void)
{
    Vector3 points[PARTICLE_TRAVEL_MAX_WAYPOINTS];
    Vector3 target = {99.0f, 1.0f, 2.0f};
    ParticleTravelPath path = {
        .points = points,
        .pointCount = PARTICLE_TRAVEL_MAX_WAYPOINTS,
        .target = &target
    };
    Vector3 final = ParticleTravel_GetWaypoint(
        &path, ParticleTravel_WaypointCount(&path) - 1);

    CHECK(ParticleTravel_WaypointCount(&path) == PARTICLE_TRAVEL_MAX_WAYPOINTS,
          "route storage remains fixed when an oversized path has a moving target");
    CHECK(final.x == target.x && final.y == target.y && final.z == target.z,
          "moving target always owns the final route slot");
}

static void Test_GPUAndManagerWiring(void)
{
    CHECK(Has("core/particles/shaders/gpu/particle_gpu.comp", "evalTravelPath"),
          "compute shader mirrors the travel solver");
    CHECK(Has("core/particles/shaders/gpu/particle_gpu.comp", "segmentDistanceSq"),
          "compute arrival uses a swept segment test");
    CHECK(Has("core/particles/gpu/particle_gpu_backend.c", "rlBindShaderBuffer(s_path_ssbo, 2)"),
          "path registry is bound once as a shared SSBO");
    CHECK(Has("core/particles/particle_manager.c", "MeshAdjacency_SampleEdge"),
          "manager resolves mesh sources before backend submission");
    CHECK(Has("core/particles/gpu/particle_gpu_backend.c", "ParticleConfig impact = *s_impactRegistry[impactIndex]"),
          "GPU arrival mirror spawns the configured target-impact effect");
    CHECK(Has("core/particles/particle_system.c", "ParticleConfig impact = p->onTargetConfig"),
          "CPU fallback spawns the same target-impact effect");
    CHECK(Has("core/particles/shaders/gpu/particle_gpu.comp", "impactActive") &&
          Has("core/particles/gpu/particle_gpu_backend.c", "impact_active"),
          "GPU and CPU mirrors retain particles for the post-arrival phase");
    CHECK(Has("core/particles/shaders/gpu/particle_gpu.comp", "arrival.y") &&
          Has("core/particles/particle_travel.h", "ParticleTravel_ApplyImpactEntry"),
          "arrival applies the shared outward impact entry impulse");
}

static void Test_VFXFixtureWiring(void)
{
    const char *fixture = "core/composition/common/vc_particle_upgrades_test.inl";
    CHECK(Has(fixture, "GuidedParticleTest_Spawn"),
          "particle-upgrades fixture includes the guided-travel demonstration");
    CHECK(Has(fixture, "VFX_ComposeGuidedParticle"),
          "guided-travel demonstration has a generator-visible public entry point");
    CHECK(Has("core/composition/visual_composer.h",
              "void VFX_ComposeGuidedParticle(Vector3 source, Vector3 target);"),
          "guided-travel public declaration matches its source/target implementation");
    CHECK(Has(fixture, "PARTICLE_SIM_AUTO") &&
          Has(fixture, "PARTICLE_SOURCE_MESH_EDGE"),
          "guided VFX defaults to GPU-capable AUTO simulation and a mesh source");
    CHECK(Has(fixture, "arrivalForceField") && Has(fixture, "travelPath") &&
          !Has(fixture, ".onTargetEmit"),
          "guided VFX keeps the same particles for path travel and target impact");
    CHECK(Has("core/composition/visual_composer.c", "GuidedParticleTest_Update(dt);"),
          "composition lifecycle keeps dynamic guided-route storage alive");
}

int main(void)
{
    Test_FollowsWaypointsAndArrives();
    Test_SweptArrivalCannotTunnel();
    Test_ForceVelocityIsGuidedNotErased();
    Test_DynamicTargetIsReadFresh();
    Test_ArrivalTransitionsIntoImpactMotion();
    Test_MovingTargetAlwaysOwnsTheFinalSlot();
    Test_GPUAndManagerWiring();
    Test_VFXFixtureWiring();
    printf("particle travel: %s\n", s_failures ? "FAIL" : "PASS");
    return s_failures ? 1 : 0;
}
