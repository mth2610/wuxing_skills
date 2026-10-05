#ifndef CORE_MOTION_FIELDS_H
#define CORE_MOTION_FIELDS_H
#include "core/force_field.h"
#include "core/motion/motion_flow.h"
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
  float maxForceNewtons;
  MotionFlowDesc flow;
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
} MotionFieldSample;
MotionGuideDesc MotionGuide_Default(void);
MotionTargetDesc MotionTarget_Default(void);
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
