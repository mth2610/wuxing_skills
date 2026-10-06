#ifndef CORE_MOTION_FIELDS_H
#define CORE_MOTION_FIELDS_H
#include "core/force_field.h"
#include "core/motion/motion_flow.h"
#include "core/motion/physical_field.h"
#include "core/motion/motion_path.h"
#include "core/particles/particle_dynamics.h"
#include "core/wind/wind_types.h"
#include <stdint.h>
#define MOTION_FIELDS_MAX_GUIDES 16
#define MOTION_FIELDS_MAX_TARGETS 32
#define MOTION_ARRIVAL_MAX_TARGETS 3
/* Generation-checked owned field handles. Zero is invalid. Pool exhaustion
 * fails rather than replacing an unrelated live field. */
typedef uint32_t MotionFieldHandle;
#define MOTION_FIELD_INVALID ((MotionFieldHandle)0)
typedef enum {
  MOTION_RECEIVER_PARTICLE = 1,
  MOTION_RECEIVER_FOLIAGE = 2,
  MOTION_RECEIVER_ALL = 3
} MotionReceiverMask;
typedef enum {
  MOTION_FORMATION_STREAM,
  MOTION_FORMATION_SHELL
} MotionFormation;
typedef enum { MOTION_GUIDE_SUSTAINED, MOTION_GUIDE_PULSE } MotionGuideMode;
typedef enum {
  MOTION_ARRIVAL_RELEASE,
  MOTION_ARRIVAL_DESTROY,
  MOTION_ARRIVAL_HOLD,
  MOTION_ARRIVAL_ORBIT
} MotionArrivalMode;
/* Independently spawnable field. Data is copied; positions/layers are world
 * space on direct creation. Arrival templates are translated so origin is B.
 * Force layers produce Newtons; Wind/shared flow velocities use m/s.
 * All three compose; zero airflow/flow strength disables that component. */
typedef struct MotionTargetDesc {
  Vector3 center;
  float radius, duration, attackTime, fadeTime;
  unsigned int receiverMask;
  ForceField field;
  VorticleData airflow; /* Zero strength disables the Wind primitive. */
  MotionFlowDesc flow;
  Vector3 flowAxis; /* Zero defaults to world Y. */
  FieldDesc physicalField;
  bool usePhysicalField;
} MotionTargetDesc;
typedef struct MotionArrivalEvent {
  MotionFieldHandle guide;
  Vector3 target, position, velocity;
} MotionArrivalEvent;
/* Fires once per guide, after the first swept receiver arrival. Callback must
 * not reset/update the registry or recursively update particles. Templates
 * below can create independent fields without requiring a callback. */
typedef void (*MotionArrivalCallback)(const MotionArrivalEvent *event,
                                      void *userData);
typedef struct MotionArrivalProfile {
  MotionArrivalMode mode;
  float radius;
  float orbitRadius;
  MotionFlowDesc flow;
  Vector3 orbitAxis;
  Vector3 impulseNs; /* Per arriving receiver, applied by body integration. */
  /* Optional complete post-arrival body profile; copied by particle receivers.
   * Free foliage substitutes its own mass when inverseMassKg <= 0. */
  bool overrideDynamics;
  ParticleDynamicsProfile dynamics;
  int targetCount;
  MotionTargetDesc targets[MOTION_ARRIVAL_MAX_TARGETS];
  MotionArrivalCallback callback;
  void *userData;
} MotionArrivalProfile;
/* Guiding field and target fields have separate ownership/lifetimes. Count=0
 * VFX can create only this field and catch already-spawned compatible bodies.
 * SHELL follows a shared advancing centre, preserving each captured offset.
 * STREAM guides each receiver along its own local path progress. */
typedef struct MotionGuideDesc {
  MotionPath path;
  MotionFormation formation;
  MotionGuideMode mode;
  float duration, radius, pulseLength, speed;
  float maxForceNewtons; /* Legacy configuration adapter into controller. */
  MotionFlowDesc flow;
  /* Publish a bounded Wind approximation for tracers and anchored vegetation.
   * Pulse follows the advancing path head; sustained uses three path samples.
   * Uses existing gust/vortex/turbulence primitives, not exact curl noise.
   * Defaults false; expires/stops with this guide, never spawns a target blast. */
  bool affectWind;
  /* Smooth tube falloff is derived from geometry. Positive endpoint scales
   * interpolate by normalized arc length; zero means
   * 1 for backwards-compatible zero-initialized descriptors. */
  float radiusScaleStart, radiusScaleEnd;
  float speedScaleStart, speedScaleEnd;
  bool preserveStreamLanes;
  /* Stiffness derives from force budget / quarter tube radius. Critical
   * damping is computed from each receiver's actual mass; no tuned frequency,
   * steering multiplier or reference mass is needed. All guide forces are N. */
  float attackTime, fadeTime;
  unsigned int receiverMask;
  /* Additional Newton force layers travel in the sampled local path frame.
   * Origin/direction are local metres/unit vectors; use ordinary force
   * primitives for curl, radial attraction, drag, etc. */
  ForceField field;
  MotionArrivalProfile arrival;
  FieldDesc physicalField; /* Additional typed field in the local path frame. */
  bool usePhysicalField;
  GuideController controller;
} MotionGuideDesc;
/* Caller-owned per receiver; never shared across particles. All zero is free.
 * One guide captures a receiver at a time; other target fields still compose.
 */
typedef struct MotionReceiver {
  MotionFieldHandle guide;
  Vector3 localOffset;
  float distance, flowTime;
  int segment;
  bool arrived;
} MotionReceiver;
typedef struct MotionFieldSample {
  Vector3 forceNewtons, airflowVelocity;
  bool captured;
  Vector3 accelerationMps2;
} MotionFieldSample;
MotionGuideDesc MotionGuide_Default(void);
MotionTargetDesc MotionTarget_Default(void);
FieldDesc MotionField_Default(void);
/* Copies descriptors and paths into the existing generation-checked target
 * pool. CPU sampling only; never silently submitted to legacy GPU packing. */
MotionFieldHandle MotionFields_CreateField(const FieldDesc *desc);
/* Typed receiver boundary. Static/kinematic receivers have no dynamic response;
 * rooted receivers never capture/arrive. Tracers receive medium velocity only.
 * Field-owned flow uses explicit priority/blend; sample ordinary Wind excluding
 * Motion publication to avoid duplicate forcing. */
void MotionFields_SampleBody(Vector3 position, Vector3 velocity,
    const BodyPhysicalProperties *body, const MediumProperties *medium,
    const ReceiverConstraints *constraints, float dt, unsigned int mask,
    MotionReceiver *receiver, FieldSample *sample);
/* Stateless external sampling, excluding guide controllers/capture and legacy
 * Newton adapters. Free-body material integration can always call this even
 * when receiver capture into a guide is disabled. */
void MotionFields_SampleExternalBody(Vector3 position, Vector3 velocity,
    const BodyPhysicalProperties *body, const MediumProperties *medium,
    const ReceiverConstraints *constraints, unsigned int mask, FieldSample *sample);
void MotionFields_Reset(void);
/* Called once per simulation frame by VFX_Compose_Update, before receivers. */
void MotionFields_Update(float dt);
MotionFieldHandle MotionFields_CreateGuide(const MotionGuideDesc *desc);
MotionFieldHandle MotionFields_CreateTarget(const MotionTargetDesc *desc);
bool MotionFields_IsAlive(MotionFieldHandle handle);
void MotionFields_Stop(MotionFieldHandle handle);
int MotionFields_GetGuideCount(void);
int MotionFields_GetTargetCount(void);
/* Mass must be finite and positive; dt is the receiver substep (0 for raw
 * queries). Spring/damper uses implicit damping for timestep stability. Field
 * query usable by any CPU body; receiver may be NULL for spatial force
 * sampling without capture or arrival state. */
void MotionFields_Sample(Vector3 position, Vector3 velocity, float massKg,
                         float dt, unsigned int mask, MotionReceiver *receiver,
                         MotionFieldSample *sample);
/* Rooted receivers query the same Newton fields without capture, lanes,
 * arrival callbacks or lifetime actions. All overlapping guides compose;
 * anchoring/restoring springs belong to the receiving object's material. */
void MotionFields_SampleAnchored(Vector3 position, Vector3 velocity,
                                 float massKg, float dt, unsigned int mask,
                                 MotionFieldSample *sample);
/* Conservative world AABB of live guide domains and target spheres. Returns
 * false and zero bounds when empty. Use before expensive spatial queries. */
bool MotionFields_GetAnchoredBounds(unsigned int mask, Vector3 *minimum,
                                    Vector3 *maximum);
/* After integration. Returns action only on first actual swept arrival;
 * fields/callback trigger once per guide, actions remain per receiver. */
MotionArrivalMode MotionFields_AdvanceReceiver(MotionReceiver *receiver,
                                               Vector3 previous,
                                               Vector3 current,
                                               Vector3 velocity);
bool MotionFields_GetArrival(MotionFieldHandle guide,
                             MotionArrivalProfile *out);
/* Formation/emission are independent: caller can seed source-sampled offsets
 * on an explicitly selected guide before its first spatial query. */
bool MotionFields_Capture(MotionFieldHandle guide, Vector3 position,
                          MotionReceiver *receiver);
#endif
