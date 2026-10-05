/* Exercises the production field registry without rendering. */
#include "core/motion/motion_fields.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "raylib.h"
#include "raymath.h"

static inline Vector3 Vector3Add(Vector3 a, Vector3 b) {
  return (Vector3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline Vector3 Vector3Subtract(Vector3 a, Vector3 b) {
  return (Vector3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline Vector3 Vector3Scale(Vector3 v, float scale) {
  return (Vector3){v.x * scale, v.y * scale, v.z * scale};
}

static inline float Vector3Length(Vector3 v) {
  return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline float Vector3LengthSqr(Vector3 v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

static inline Vector3 Vector3CrossProduct(Vector3 a, Vector3 b) {
  return (Vector3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                   a.x * b.y - a.y * b.x};
}

static inline float Vector3DotProduct(Vector3 a, Vector3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

#define CORE_HEADLESS_TEST 1
#include "core/force_field.c"
#include "core/motion/motion_body.h"
#include "core/motion/motion_fields.c"
#include "core/wind/wind_system.c"
#include <math.h>
#include <stdio.h>
static int failures;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    if (!(c)) {                                                                \
      printf("FAIL: %s\n", m);                                                 \
      failures++;                                                              \
    } else                                                                     \
      printf("PASS: %s\n", m);                                                 \
  } while (0)
static int events;
static void CountEvent(const MotionArrivalEvent *event, void *user) {
  (void)event;
  (*(int *)user)++;
}
static void TestOwnedStorageAndPools(void) {
  MotionFields_Reset();
  Vector3 points[] = {{0, 1, 0}, {2, 1, 0}};
  MotionGuideDesc g = MotionGuide_Default();
  MotionPath_Build(&g.path, points, 2);
  MotionFieldHandle first = MotionFields_CreateGuide(&g);
  g.path.points[1].x = 100;
  MotionReceiver r = {0};
  MotionFieldSample sample;
  MotionFields_Sample((Vector3){1.9f, 1, 0}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, &r, &sample);
  MotionFields_AdvanceReceiver(&r, (Vector3){1.9f, 1, 0}, (Vector3){2.1f, 1, 0},
                               (Vector3){0});
  CHECK(r.arrived, "registry owns a snapshot of path and target data");
  MotionFields_Reset();
  CHECK(!MotionFields_IsAlive(first), "reset invalidates old handles");
  MotionFieldHandle next = MotionFields_CreateGuide(&g);
  CHECK(first != next, "reset does not reincarnate stale handles");
  for (int i = 1; i < MOTION_FIELDS_MAX_GUIDES; i++)
    CHECK(MotionFields_CreateGuide(&g) != 0,
          "guide pool accepts available slots");
  CHECK(MotionFields_CreateGuide(&g) == 0 && MotionFields_IsAlive(next),
        "pool exhaustion preserves live casts");
  MotionFields_Reset();
}
static void TestPulseAndReceiverMasks(void) {
  Vector3 points[] = {{0, 1, 0}, {6, 1, 0}};
  MotionGuideDesc g = MotionGuide_Default();
  MotionPath_Build(&g.path, points, 2);
  g.speed = 2;
  g.mode = MOTION_GUIDE_PULSE;
  g.pulseLength = 1;
  g.receiverMask = MOTION_RECEIVER_FOLIAGE;
  MotionFields_CreateGuide(&g);
  MotionReceiver leaf = {0};
  MotionFieldSample sample;
  MotionFields_Sample((Vector3){3, 1, 0}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_FOLIAGE, &leaf, &sample);
  CHECK(!sample.captured, "a pulse cannot catch leaves ahead of its front");
  MotionFields_Update(1.5f);
  MotionFields_Sample((Vector3){3, 1, 0}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_FOLIAGE, &leaf, &sample);
  CHECK(sample.captured,
        "a travelling pulse catches a pre-existing leaf on contact");
  MotionReceiver particle = {0};
  MotionFields_Sample((Vector3){3, 1, 0}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, &particle, &sample);
  CHECK(!sample.captured, "receiver masks isolate unrelated particle classes");
  MotionFields_Update(1);
  MotionFields_Sample((Vector3){3, 1, 0}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_FOLIAGE, &leaf, &sample);
  CHECK(!sample.captured, "leaving the pulse releases a stream receiver");
  MotionFields_Reset();
}
static void TestBodyMassAndAir(void) {
  ParticleDynamicsProfile light = {.inverseMassKg = 250,
                                   .windSusceptibility = 1,
                                   .aerodynamicAreaM2 = 0.016f,
                                   .aerodynamicDragCoefficient = 1};
  ParticleDynamicsProfile heavy = light;
  heavy.inverseMassKg = 25;
  Vector3 air = {5, 0, 0};
  Vector3 a = MotionBody_AdvanceVelocity((Vector3){0}, &light, (Vector3){0},
                                         (Vector3){0}, air, 0.1f);
  Vector3 b = MotionBody_AdvanceVelocity((Vector3){0}, &heavy, (Vector3){0},
                                         (Vector3){0}, air, 0.1f);
  CHECK(a.x > b.x && a.x < air.x,
        "air response depends on mass without overshooting airflow");
  light.windCouplingHz = 1000;
  Vector3 c = MotionBody_AdvanceVelocity((Vector3){0}, &light, (Vector3){0},
                                         (Vector3){0}, air, 0.1f);
  CHECK(fabsf(c.x - a.x) < 1e-6f,
        "quadratic drag does not duplicate linear wind coupling");
  Vector3 still = MotionBody_AdvanceVelocity(
      (Vector3){9, 0, 0}, &light, (Vector3){0}, (Vector3){0}, (Vector3){0}, 1);
  CHECK(still.x > 0 && still.x < 9,
        "large drag step dissipates without reversing velocity");
}
static void TestIndependentTargetAndNewtonGuidance(void) {
  MotionFields_Reset();
  MotionTargetDesc target = MotionTarget_Default();
  target.center = (Vector3){0, 1, 0};
  target.airflow = (VorticleData){
      .type = VORTICLE_VORTEX, .direction = {0, 1, 0}, .strength = 5};
  MotionFieldHandle h = MotionFields_CreateTarget(&target);
  MotionReceiver r = {0};
  MotionFieldSample sample;
  MotionFields_Sample((Vector3){1, 1, 0}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_FOLIAGE, &r, &sample);
  CHECK(MotionFields_GetGuideCount() == 0 &&
            MotionVec_Length(sample.airflowVelocity) > 0,
        "target vortex can exist without any guiding field or emitter");
  MotionFields_Stop(h);
  MotionGuideDesc g = MotionGuide_Default();
  Vector3 points[] = {{0, 1, 0}, {3, 1, 0}};
  MotionPath_Build(&g.path, points, 2);
  g.maxForceNewtons = 0.32f;
  MotionFields_CreateGuide(&g);
  r = (MotionReceiver){0};
  MotionFields_Sample((Vector3){0, 1, 0}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, &r, &sample);
  CHECK(MotionVec_Length(sample.forceNewtons) > 0,
        "guidance always supplies Newton forces and uses receiver mass");
  ParticleDynamicsProfile a = {.inverseMassKg = 250}, b = {.inverseMassKg = 25};
  Vector3 va = MotionBody_AdvanceVelocity(
      (Vector3){0}, &a, (Vector3){0}, sample.forceNewtons, (Vector3){0}, 0.1f);
  Vector3 vb = MotionBody_AdvanceVelocity(
      (Vector3){0}, &b, (Vector3){0}, sample.forceNewtons, (Vector3){0}, 0.1f);
  CHECK(va.x > vb.x * 9.9f,
        "same Newton guide moves light particles more strongly");
  MotionFields_Reset();
}
static Vector3 RunStream(int hz, bool coherent) {
  MotionFields_Reset();
  Vector3 points[] = {{0, 1, 0}, {3, 1, 0}, {6, 1, 0}};
  MotionGuideDesc g = MotionGuide_Default();
  MotionPath_Build(&g.path, points, 3);
  g.speed = 3;
  g.duration = 5;
  g.arrival.mode = MOTION_ARRIVAL_ORBIT;
  g.arrival.flow.swirlSpeedMps = 3;
  if (coherent) {
    g.flow = (MotionFlowDesc){0.15f, 0.3f};
    g.arrival.flow.turbulenceSpeedMps = 0.1f;
  }
  MotionFields_CreateGuide(&g);
  MotionReceiver r = {0};
  Vector3 p = {0, 1, 0.1f}, v = {0};
  ParticleDynamicsProfile body = {.inverseMassKg = 250};
  for (int frame = 0; frame < hz * 3; frame++) {
    MotionFields_Update(1.0f / hz);
    float remain = 1.0f / hz;
    while (remain > 1e-6f) {
      float dt = fminf(remain, 1.0f / 120);
      remain -= dt;
      MotionFieldSample sample;
      MotionFields_Sample(p, v, 0.004f, dt, MOTION_RECEIVER_PARTICLE, &r,
                          &sample);
      v = MotionBody_AdvanceVelocity(v, &body, (Vector3){0},
                                     sample.forceNewtons,
                                     sample.airflowVelocity, dt);
      Vector3 before = p;
      p = MotionVec_Add(p, MotionVec_Scale(v, dt));
      MotionFields_AdvanceReceiver(&r, before, p, v);
    }
  }
  CHECK(r.arrived, "stream actually reaches its target before orbiting");
  float radius = MotionVec_Length(MotionVec_Sub(p, (Vector3){6, 1, 0}));
  CHECK(radius > 0.4f && radius < 1.4f, "arrival orbit stays bounded around B");
  return p;
}
static void TestFormationAndArrivalIsolation(void) {
  MotionFields_Reset();
  Vector3 points[] = {{0, 1, 0}, {2, 2, 0}, {4, 2, 2}};
  MotionGuideDesc g = MotionGuide_Default();
  MotionPath_Build(&g.path, points, 3);
  g.formation = MOTION_FORMATION_SHELL;
  g.speed = 2;
  g.duration = 6;
  MotionFieldHandle h = MotionFields_CreateGuide(&g);
  MotionReceiver a = {0}, b = {0};
  MotionFields_Capture(h, (Vector3){0, 1.2f, 0}, &a);
  MotionFields_Capture(h, (Vector3){0, 0.8f, 0}, &b);
  MotionPathSample f = MotionPath_Sample(&g.path, 3);
  CHECK(fabsf(MotionVec_Length(
                  MotionVec_Sub(MotionPath_WorldOffset(f, a.localOffset),
                                MotionPath_WorldOffset(f, b.localOffset))) -
              0.4f) < 1e-5f,
        "transported shell offsets preserve spherical shape through bends");
  MotionFields_Reset();
  g.formation = MOTION_FORMATION_STREAM;
  g.arrival.callback = CountEvent;
  g.arrival.userData = &events;
  events = 0;
  MotionFields_CreateGuide(&g);
  MotionFields_CreateGuide(&g);
  for (int i = 0; i < 2; i++) {
    MotionReceiver r = {0};
    MotionFieldSample sample;
    MotionFields_Sample((Vector3){3.95f, 2, 1.95f}, (Vector3){0}, 0.004f, 0,
                        MOTION_RECEIVER_PARTICLE, &r, &sample);
    MotionFields_AdvanceReceiver(&r, (Vector3){3.95f, 2, 1.95f},
                                 (Vector3){4.05f, 2, 2.05f}, (Vector3){0});
    if (i == 0)
      MotionFields_Stop(r.guide);
  }
  CHECK(events == 2, "simultaneous casts trigger independent arrival events "
                     "without a global cooldown");
  CHECK(MotionFields_GetTargetCount() == 0,
        "arrival without target templates creates no blast or field");
  MotionFields_Reset();
}
static void TestDensityAndForceProfiles(void) {
  ParticleDynamicsProfile b = {.inverseMassKg = 250,
                               .gravityScale = 1,
                               .densityKgM3 = 0.6f,
                               .airDensityKgM3 = 1.2f};
  Vector3 v = MotionBody_AdvanceVelocity((Vector3){0}, &b, (Vector3){0},
                                         (Vector3){0}, (Vector3){0}, 0.1f);
  CHECK(fabsf(v.y - 0.981f) < 0.0001f,
        "lighter-than-air body rises by Archimedes buoyancy");
  b.densityKgM3 = 1.2f;
  v = MotionBody_AdvanceVelocity((Vector3){0}, &b, (Vector3){0}, (Vector3){0},
                                 (Vector3){0}, 0.1f);
  CHECK(fabsf(v.y) < 0.0001f, "equal air/body density is neutrally buoyant");
  b.densityKgM3 = 600;
  CHECK(ParticleDynamics_GravityAcceleration(&b) < -9.7f,
        "dense solid falls under gravity with small air buoyancy");
  float denseAcceleration = ParticleDynamics_GravityAcceleration(&b);
  b.inverseMassKg = 25;
  CHECK(ParticleDynamics_GravityAcceleration(&b) == denseAcceleration,
        "equal-density bodies have equal gravity/buoyancy acceleration "
        "regardless of mass");
  b.densityKgM3 = 0;
  CHECK(fabsf(ParticleDynamics_GravityAcceleration(&b) + 9.81f) < 0.0001f,
        "zero density preserves legacy gravity-only profiles");
  MotionFields_Reset();
  MotionGuideDesc g = MotionGuide_Default();
  Vector3 path[] = {{0, 1, 0}, {10, 1, 0}};
  MotionPath_Build(&g.path, path, 2);
  g.radius = 2;
  g.speed = 3;
  g.maxForceNewtons = 0.2f;
  MotionFields_CreateGuide(&g);
  MotionFieldSample core, edge, matched;
  MotionFields_Sample((Vector3){5, 1, 0}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, NULL, &core);
  MotionFields_Sample((Vector3){5, 1, 1.99f}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, NULL, &edge);
  MotionFields_Sample((Vector3){5, 1, 0}, (Vector3){3, 0, 0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, NULL, &matched);
  CHECK(MotionVec_Length(core.forceNewtons) <= 0.20001f,
        "controller respects its Newton force budget");
  CHECK(MotionVec_Length(edge.forceNewtons) < 0.001f,
        "tube force fades smoothly to zero at its outer edge");
  CHECK(MotionVec_Length(matched.forceNewtons) < 0.0001f,
        "matched desired velocity has no accelerating guide force");
  MotionFields_Reset();
  g.preserveStreamLanes = true;
  g.radiusScaleStart = 2;
  g.radiusScaleEnd = 0.5f;
  MotionFields_CreateGuide(&g);
  MotionReceiver r = {0};
  MotionFields_Sample((Vector3){1, 1, 0.5f}, (Vector3){3, 0, 0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, &r, &matched);
  CHECK(
      fabsf(matched.forceNewtons.z) < 0.0001f &&
          MotionVec_Length(r.localOffset) > 0.1f,
      "stream preserves a captured lane instead of collapsing into its centre");
  MotionReceiver outside = {0};
  MotionFields_Sample((Vector3){9, 1, 2}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, &outside, &edge);
  CHECK(!outside.guide,
        "arc-length radius profile narrows the downstream capture region");
  MotionFields_Sample((Vector3){1, 1, 0}, (Vector3){0}, 0, 0,
                      MOTION_RECEIVER_PARTICLE, NULL, &edge);
  CHECK(MotionVec_Length(edge.forceNewtons) == 0,
        "invalid mass cannot produce a field force");
  MotionFields_Reset();
  g = MotionGuide_Default();
  MotionPath_Build(&g.path, path, 2);
  g.radius = 0.1f;
  g.maxForceNewtons = 5;
  g.arrival.mode = MOTION_ARRIVAL_HOLD;
  MotionFieldHandle h = MotionFields_CreateGuide(&g);
  r = (MotionReceiver){.guide = h, .arrived = true};
  Vector3 p = {10.1f, 1, 0};
  v = (Vector3){0};
  ParticleDynamicsProfile small = {.inverseMassKg = 100000};
  for (int i = 0; i < 120; i++) {
    MotionFields_Sample(p, v, 0.00001f, 1.0f / 30, MOTION_RECEIVER_PARTICLE, &r,
                        &core);
    v = MotionBody_AdvanceVelocity(v, &small, (Vector3){0}, core.forceNewtons,
                                   (Vector3){0}, 1.0f / 30);
    p = MotionVec_Add(p, MotionVec_Scale(v, 1.0f / 30));
  }
  CHECK(
      isfinite(p.x) && fabsf(p.x - 10) < 0.001f,
      "strong guide holding a tiny mass remains stable without damping tuning");
  MotionFields_Reset();
}
static void TestSharedFlowStages(void) {
  MotionFields_Reset();
  MotionGuideDesc g = MotionGuide_Default();
  Vector3 path[] = {{0, 0, 0}, {10, 0, 0}};
  MotionPath_Build(&g.path, path, 2);
  g.radius = 2;
  g.speed = 1;
  g.maxForceNewtons = 100;
  MotionFieldHandle h = MotionFields_CreateGuide(&g);
  MotionFieldSample baseline, flowing;
  MotionFields_Sample((Vector3){5, 0, 0}, (Vector3){0}, 1, 0.1f,
                      MOTION_RECEIVER_PARTICLE, NULL, &baseline);
  float damping = 2 * sqrtf(100 / 0.5f);
  CHECK(fabsf(baseline.forceNewtons.x - damping / (1 + damping * 0.1f)) < 1e-5f,
        "stream tangent uses velocity damping without a fictitious spring");
  MotionFields_Sample((Vector3){5, 0.3f, 0.2f}, (Vector3){1, 0, 0}, 1, 0,
                      MOTION_RECEIVER_PARTICLE, NULL, &baseline);
  MotionFields_Stop(h);
  g.flow = (MotionFlowDesc){.turbulenceSpeedMps = 0.6f, .swirlSpeedMps = 1.5f};
  h = MotionFields_CreateGuide(&g);
  MotionFields_Sample((Vector3){5, 0.3f, 0.2f}, (Vector3){1, 0, 0}, 1, 0,
                      MOTION_RECEIVER_PARTICLE, NULL, &flowing);
  CHECK(isfinite(flowing.forceNewtons.x) &&
            MotionVec_Length(MotionVec_Sub(flowing.forceNewtons, baseline.forceNewtons)) > 0.001f,
        "shared coherent flow contributes to guide steering");
  MotionReceiver r = {0};
  MotionFields_Capture(h, (Vector3){9.9f, 0.3f, 0}, &r);
  r.distance = 9.9f;
  r.flowTime = 0.7f;
  MotionPathSample end = MotionPath_Sample(&g.path, g.path.length);
  Vector3 offset = Motion_RotateLane(end, r.localOffset, &g.flow, g.radius, r.flowTime);
  Vector3 goal = MotionVec_Add(end.position, offset);
  MotionFields_AdvanceReceiver(&r, goal, goal, (Vector3){0});
  CHECK(r.arrived && r.flowTime == 0 &&
            MotionVec_Length(MotionVec_Sub(MotionPath_WorldOffset(end, r.localOffset), offset)) < 1e-5f,
        "arrival resets target phase while preserving the rotated lane");
  MotionFields_Stop(h);
  MotionTargetDesc target = MotionTarget_Default();
  target.attackTime = target.fadeTime = 0;
  target.center = (Vector3){10, 0, 0};
  target.radius = 2;
  target.flow.swirlSpeedMps = 2;
  target.field.layerCount = 1;
  target.field.layers[0] = (ForceLayer){.type = FORCE_GRAVITY_DIR,
                                      .direction = {0, 1, 0}, .strength = 3};
  h = MotionFields_CreateTarget(&target);
  MotionFields_Sample((Vector3){10.5f, 0, 0}, (Vector3){0}, 1, 0,
                      MOTION_RECEIVER_PARTICLE, NULL, &flowing);
  CHECK(h && flowing.forceNewtons.y > 0 && flowing.airflowVelocity.z < 0,
        "independent target composes Newton layers and airflow after guide stops");
  target.flow.turbulenceSpeedMps = NAN;
  CHECK(!MotionFields_CreateTarget(&target), "invalid target flow is rejected before allocation");
  g.arrival.flow.swirlSpeedMps = NAN;
  CHECK(!MotionFields_CreateGuide(&g), "invalid arrival flow is rejected before allocation");
  MotionFields_Reset();
}
int main(void) {
  MotionFields_Reset();
  Vector3 points[] = {{0, 1, 0}, {3, 1, 0}, {6, 1, 0}};
  MotionGuideDesc g = MotionGuide_Default();
  CHECK(MotionPath_Build(&g.path, points, 3), "build reusable arc-length path");
  g.duration = 4;
  g.radius = 2;
  g.speed = 3;
  MotionFieldHandle h = MotionFields_CreateGuide(&g);
  MotionReceiver r = {0};
  MotionFieldSample s;
  MotionFields_Sample((Vector3){1, 1, 0.4f}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_FOLIAGE, &r, &s);
  CHECK(s.forceNewtons.x > 0 && s.forceNewtons.z < 0,
        "independent guide catches an existing leaf");
  MotionReceiver outside = {0};
  MotionFields_Sample((Vector3){1, 1, 8}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_FOLIAGE, &outside, &s);
  CHECK(MotionVec_Length(s.forceNewtons) == 0,
        "corridor excludes distant leaves");
  MotionFields_Stop(h);
  MotionFields_Sample((Vector3){1, 1, 0}, (Vector3){0}, 0.004f, 0,
                      MOTION_RECEIVER_FOLIAGE, &r, &s);
  CHECK(!MotionFields_IsAlive(h) && MotionVec_Length(s.forceNewtons) == 0,
        "stopping a guide releases captured receivers");
  h = MotionFields_CreateGuide(&g);
  CHECK(!MotionFields_IsAlive(r.guide) || r.guide == 0,
        "generation prevents a stale receiver binding");
  MotionFields_Update(4.1f);
  CHECK(!MotionFields_IsAlive(h), "guide has its own lifetime");
  MotionFields_Reset();
  g.duration = 6;
  g.arrival.mode = MOTION_ARRIVAL_DESTROY;
  g.arrival.targetCount = 1;
  g.arrival.targets[0] = MotionTarget_Default();
  g.arrival.targets[0].duration = 2;
  g.arrival.targets[0].field.layers[0] =
      (ForceLayer){.type = FORCE_VORTEX, .direction = {0, 1, 0}, .strength = 8};
  g.arrival.targets[0].field.layerCount = 1;
  h = MotionFields_CreateGuide(&g);
  r = (MotionReceiver){0};
  MotionFields_Sample((Vector3){5.9f, 1, 0}, (Vector3){3, 0, 0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, &r, &s);
  CHECK(MotionFields_AdvanceReceiver(
            &r, (Vector3){5.9f, 1, 0}, (Vector3){6.2f, 1, 0},
            (Vector3){3, 0, 0}) == MOTION_ARRIVAL_DESTROY,
        "swept arrival selects destruction");
  CHECK(MotionFields_GetTargetCount() == 1,
        "arrival starts separately timed target field");
  MotionReceiver second = {0};
  MotionFields_Sample((Vector3){5.9f, 1, 0}, (Vector3){3, 0, 0}, 0.004f, 0,
                      MOTION_RECEIVER_PARTICLE, &second, &s);
  MotionFields_AdvanceReceiver(&second, (Vector3){5.9f, 1, 0},
                               (Vector3){6.2f, 1, 0}, (Vector3){3, 0, 0});
  CHECK(MotionFields_GetTargetCount() == 1,
        "a formation triggers its target fields once per guide");
  MotionFields_Stop(h);
  MotionFields_Update(0.2f);
  CHECK(MotionFields_GetTargetCount() == 1,
        "target field survives guide destruction");
  MotionFields_Update(2);
  CHECK(MotionFields_GetTargetCount() == 0,
        "target field expires independently");
  TestSharedFlowStages();
  TestDensityAndForceProfiles();
  TestOwnedStorageAndPools();
  TestPulseAndReceiverMasks();
  TestBodyMassAndAir();
  TestIndependentTargetAndNewtonGuidance();
  TestFormationAndArrivalIsolation();
  Vector3 p30 = RunStream(30, false), p60 = RunStream(60, false), p120 = RunStream(120, false);
  CHECK(MotionVec_Length(MotionVec_Sub(p30, p60)) < 0.04f &&
            MotionVec_Length(MotionVec_Sub(p60, p120)) < 0.04f,
        "30/60/120 Hz trajectories agree with bounded substeps");
  p30 = RunStream(30, true); p60 = RunStream(60, true); p120 = RunStream(120, true);
  CHECK(MotionVec_Length(MotionVec_Sub(p30, p60)) < 0.05f &&
            MotionVec_Length(MotionVec_Sub(p60, p120)) < 0.05f,
        "coherent travel and target flows converge at 30/60/120 Hz");
  printf("motion fields: %s\n", failures ? "FAIL" : "PASS");
  return failures != 0;
}
