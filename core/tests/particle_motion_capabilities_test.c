#include "raylib.h"
typedef struct { int meshCount; } Model;
typedef struct { Vector3 position,target,up;float fovy;int projection; } Camera3D;
#define WHITE ((Color){255,255,255,255})
#include "core/particles/particle_motion_capabilities.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  ParticleDynamicsProfile body={.inverseMassKg=250};
  ParticleConfig p={0};
  assert(!ParticleMotion_RequiresCpuDynamics(&p));
  p.physics.dynamics=&body;
  assert(ParticleMotion_RequiresCpuDynamics(&p));
  p.physics.spatialMotionOnly=true;p.physics.receiveMotionFields=true;
  assert(!ParticleMotion_RequiresCpuDynamics(&p));
  p.physics.initialGuide=42;assert(ParticleMotion_RequiresCpuDynamics(&p));p.physics.initialGuide=0;
  p.onTargetEmitCount=1;assert(ParticleMotion_RequiresCpuDynamics(&p));p.onTargetEmitCount=0;
  p.physics.onCollisionEmitCount=1;assert(ParticleMotion_RequiresCpuDynamics(&p));p.physics.onCollisionEmitCount=0;
  p.onDeathEmitCount=1;assert(ParticleMotion_RequiresCpuDynamics(&p));p.onDeathEmitCount=0;
  p.onLiveEmitRate=1;assert(ParticleMotion_RequiresCpuDynamics(&p));p.onLiveEmitRate=0;
  ParticleConfig leaf={.lifetime=1,.radius=.1f,.physics={.spatialMotionOnly=true}};
  p.onDeathEmit=&leaf;p.onDeathEmitCount=4;
  p.onLiveEmit=&leaf;p.onLiveEmitRate=30;
  assert(!ParticleMotion_RequiresCpuDynamics(&p));
  p.onDeathEmitCount=65;assert(ParticleMotion_RequiresCpuDynamics(&p));p.onDeathEmitCount=4;
  leaf.onDeathEmit=&leaf;assert(ParticleMotion_RequiresCpuDynamics(&p));leaf.onDeathEmit=NULL;
  leaf.lifetime=NAN;assert(ParticleMotion_RequiresCpuDynamics(&p));leaf.lifetime=1;
  leaf.render.texture.id=42;assert(ParticleMotion_RequiresCpuDynamics(&p));leaf.render.texture.id=0;
  leaf.physics.initialGuide=1;assert(ParticleMotion_RequiresCpuDynamics(&p));leaf.physics.initialGuide=0;
  leaf.render.contrastProfile=VFX_CONTRAST_FIRE;assert(ParticleMotion_RequiresCpuDynamics(&p));leaf.render.contrastProfile=0;
  p.onLiveEmitRate=INFINITY;assert(ParticleMotion_RequiresCpuDynamics(&p));
  p.onDeathEmit=NULL;p.onDeathEmitCount=0;p.onLiveEmit=NULL;p.onLiveEmitRate=0;
  p.render.blendMode=VFX_BLEND_PREMULTIPLIED;assert(ParticleMotion_RequiresCpuDynamics(&p));
  p.render.blendMode=VFX_BLEND_ALPHA;assert(!ParticleMotion_RequiresCpuDynamics(&p));
  SkillCurve curve={0};p.animation.alphaCurve=&curve;
  assert(ParticleMotion_RequiresCpuDynamics(&p));p.animation.alphaCurve=NULL;
  p.rotation=1;assert(ParticleMotion_RequiresCpuDynamics(&p));p.rotation=0;
  p.render.trailLength=4;assert(ParticleMotion_RequiresCpuDynamics(&p));p.render.trailLength=0;
  p.physics.spatialMotionOnly=false;assert(ParticleMotion_RequiresCpuDynamics(&p));
  puts("PASS: spatial GPU admission preserves legacy and callback fallback");return 0;
}
