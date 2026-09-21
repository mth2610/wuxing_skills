/* Phase-1 numerical contract for opt-in physical particle dynamics.
 * These checks exercise the header-only math shared by future CPU/GPU mirrors. */
#include "core/particles/particle_dynamics.h"
#include "core/particles/particle_travel.h"

#include <math.h>
#include <stdio.h>

static int s_failures;

#define CHECK_NEAR(actual, expected, epsilon, message) do { \
    if (fabsf((actual) - (expected)) > (epsilon)) { \
        printf("FAIL: %s (got %.6f, expected %.6f)\n", message, \
               (double)(actual), (double)(expected)); \
        s_failures++; \
    } else { printf("PASS: %s\n", message); } \
} while (0)

static void Test_ImpulseUsesInverseMass(void)
{
    Vector3 velocity = {1.0f, 0.0f, 0.0f};
    Vector3 impulse = {4.0f, 0.0f, 0.0f};
    Vector3 light = ParticleDynamics_ApplyImpulse(velocity, impulse, 1.0f);
    Vector3 heavy = ParticleDynamics_ApplyImpulse(velocity, impulse, 0.25f);
    CHECK_NEAR(light.x, 5.0f, 1e-6f, "one kg body receives J/m velocity change");
    CHECK_NEAR(heavy.x, 2.0f, 1e-6f, "higher mass reduces impulse response");
}

static void Test_NullProfileKeepsLegacyPath(void)
{
    CHECK_NEAR(ParticleDynamics_IsEnabled(NULL), 0.0f, 0.0f,
               "omitted dynamics profile selects the legacy integrator");
}

static void Test_LinearDragIsFrameRateInvariant(void)
{
    Vector3 once = ParticleDynamics_ApplyLinearDrag((Vector3){10.0f, 0.0f, 0.0f},
                                                     2.0f, 1.0f);
    Vector3 split = {10.0f, 0.0f, 0.0f};
    for (int i = 0; i < 10; ++i)
        split = ParticleDynamics_ApplyLinearDrag(split, 2.0f, 0.1f);
    CHECK_NEAR(once.x, 10.0f * expf(-2.0f), 1e-5f,
               "linear drag coefficient is documented in per-second units");
    CHECK_NEAR(split.x, once.x, 1e-5f,
               "exponential linear drag is frame-rate invariant");
}

static void Test_ForceDependsOnMassButAccelerationDoesNot(void)
{
    Vector3 acceleration = {0.0f, 3.0f, 0.0f};
    Vector3 force = {0.0f, 8.0f, 0.0f};
    Vector3 light = ParticleDynamics_ApplyAccelerationAndForce(
        (Vector3){0}, acceleration, force, 1.0f, 0.5f);
    Vector3 heavy = ParticleDynamics_ApplyAccelerationAndForce(
        (Vector3){0}, acceleration, force, 0.25f, 0.5f);
    CHECK_NEAR(light.y, 5.5f, 1e-6f,
               "Newton force response uses inverse mass");
    CHECK_NEAR(heavy.y, 2.5f, 1e-6f,
               "acceleration fields ignore particle mass");
}

static void Test_GuidanceIsBoundedAccelerationNotTeleport(void)
{
    Vector3 target = {10.0f, 0.0f, 0.0f};
    ParticleTravelPath path = {.target = &target, .speed = 4.0f};
    Vector3 acceleration = ParticleTravel_ComputeCriticalDampedAcceleration(
        &path, (Vector3){0}, (Vector3){0}, &(int){0}, (Vector3){0}, 2.0f, 3.0f);
    Vector3 velocity = ParticleDynamics_ApplyAccelerationAndForce(
        (Vector3){0}, acceleration, (Vector3){0}, 1.0f, 0.1f);
    CHECK_NEAR(acceleration.x, 3.0f, 1e-6f,
               "guided steering acceleration is bounded");
    CHECK_NEAR(velocity.x, 0.3f, 1e-6f,
               "guided particle advances physically instead of snapping to target");
}

static void Test_PhysicalGuidanceSweepsItsTarget(void)
{
    Vector3 target = {1.0f, 0.0f, 0.0f};
    ParticleTravelPath path = {.target = &target, .targetRadius = 0.05f};
    int waypoint = 0;
    CHECK_NEAR(ParticleTravel_AdvancePhysical(&path, (Vector3){0},
               (Vector3){2.0f, 0.0f, 0.0f}, &waypoint, (Vector3){0}), 1.0f, 0.0f,
               "physical guidance detects a swept final-target crossing");
}

static void Test_TerminalSpeedAndWindCoupling(void)
{
    Vector3 clamped = ParticleDynamics_ClampTerminalSpeed((Vector3){12.0f, 0.0f, 0.0f}, 5.0f);
    Vector3 coupled = ParticleDynamics_CoupleToAirflow((Vector3){0},
        (Vector3){10.0f, 0.0f, 0.0f}, 2.0f, 1.0f);
    CHECK_NEAR(clamped.x, 5.0f, 1e-6f, "terminal-speed clamp bounds physical velocity");
    CHECK_NEAR(coupled.x, 10.0f * (1.0f - expf(-2.0f)), 1e-5f,
               "wind coupling uses exponential relaxation toward airflow");
}

int main(void)
{
    Test_NullProfileKeepsLegacyPath();
    Test_ImpulseUsesInverseMass();
    Test_LinearDragIsFrameRateInvariant();
    Test_ForceDependsOnMassButAccelerationDoesNot();
    Test_GuidanceIsBoundedAccelerationNotTeleport();
    Test_PhysicalGuidanceSweepsItsTarget();
    Test_TerminalSpeedAndWindCoupling();
    printf("particle dynamics: %s\n", s_failures ? "FAIL" : "PASS");
    return s_failures ? 1 : 0;
}
