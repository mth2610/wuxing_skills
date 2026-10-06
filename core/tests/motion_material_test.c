/* Shared thin-lamina material arithmetic and the production body integrator.
 * Broadside analytical descent is a model check, not a flutter experiment. */
#include "core/motion/physical_field.h"
#include "core/motion/motion_body.h"
#include <stdio.h>
static int failed;
#define CHECK(c,m) do { if(!(c)) { printf("FAIL: %s\n",m); ++failed; } \
  else printf("PASS: %s\n",m); } while(0)
static float TerminalDescent(BodyPhysicalProperties b,float airDensity) {
  return sqrtf(2*(b.massKg-airDensity*b.volumeM3)*9.81f/
    (airDensity*b.dragCoefficient*b.projectedAreaM2));
}
static float SimulateDescent(BodyPhysicalProperties b,int hz) {
  ParticleDynamicsProfile p={.inverseMassKg=1/b.massKg,.gravityScale=1,
    .densityKgM3=b.densityKgM3,.aerodynamicAreaM2=b.projectedAreaM2,
    .aerodynamicDragCoefficient=b.dragCoefficient,.airDensityKgM3=1.225f,
    .windSusceptibility=1};
  Vector3 v={0};
  for(int i=0;i<5*hz;++i) v=MotionBody_AdvanceVelocity(v,&p,
      (Vector3){0},(Vector3){0},(Vector3){0},1.0f/hz);
  return -v.y;
}
int main(void) {
  const float area=.012f;
  ThinLaminaMaterial dry=BodyLaminaMaterial_Preset(BODY_LAMINA_LEAF_DRY);
  ThinLaminaMaterial fresh=BodyLaminaMaterial_Preset(BODY_LAMINA_LEAF_FRESH);
  ThinLaminaMaterial petal=BodyLaminaMaterial_Preset(BODY_LAMINA_PETAL_FRESH);
  BodyPhysicalProperties d=BodyPhysicalProperties_Lamina(area,&dry);
  BodyPhysicalProperties f=BodyPhysicalProperties_Lamina(area,&fresh);
  BodyPhysicalProperties p=BodyPhysicalProperties_Lamina(area,&petal);
  CHECK(fabsf(d.massKg-.00072f)<1e-8f && fabsf(f.massKg-.0024f)<1e-8f && fabsf(p.massKg-.0018f)<1e-8f,
    "lamina mass derives from one-sided area and material areal mass");
  CHECK(fabsf(d.volumeM3-area*.00020f)<1e-10f && d.volumeM3==f.volumeM3,
    "lamina displaced volume derives from area and thickness independently of hydration mass");
  CHECK(fabsf(d.densityKgM3-300)<.001f && fabsf(f.densityKgM3-1000)<.001f && fabsf(p.densityKgM3-750)<.001f,
    "effective bulk densities are derived not independently tuned");
  CHECK(d.projectedAreaM2==area && d.dragCoefficient==1.28f,
    "broadside drag pairs one-sided projected area with shared coefficient");
  BodyPhysicalProperties large=BodyPhysicalProperties_Lamina(4*area,&dry);
  CHECK(large.massKg==4*d.massKg && large.volumeM3==4*d.volumeM3 && large.projectedAreaM2==4*area,
    "doubling linear dimensions quadruples mass volume and planform area");
  CHECK(fabsf(TerminalDescent(large,1.225f)-TerminalDescent(d,1.225f))<1e-6f,
    "size-scaled lamina preserves area-to-mass ballistic ratio within constant-Cd model");
  float speeds[3]={TerminalDescent(d,1.225f),TerminalDescent(f,1.225f),TerminalDescent(p,1.225f)};
  CHECK(speeds[0]>.85f && speeds[0]<.95f && speeds[1]>1.55f && speeds[1]<1.70f && speeds[2]>1.35f && speeds[2]<1.50f,
    "representative broadside descent speeds are plausible model outputs without artificial clamps");
  CHECK(speeds[0]<speeds[2] && speeds[2]<speeds[1],
    "increased hydrated areal mass increases terminal descent at equal area and coefficient");
  CHECK(fabsf(SimulateDescent(d,30)-speeds[0])<.002f &&
        fabsf(SimulateDescent(f,60)-speeds[1])<.002f &&
        fabsf(SimulateDescent(p,120)-speeds[2])<.002f,
    "production implicit drag converges to analytical terminal equilibrium across materials and timesteps");
  CHECK(fabsf(SimulateDescent(d,30)-SimulateDescent(d,120))<.001f,
    "representative lamina descent stable at 30 and 120 Hz");
  CHECK(BodyPhysicalProperties_Lamina(0,&dry).massKg==0 &&
    BodyPhysicalProperties_Lamina(NAN,&dry).massKg==0 &&
    BodyPhysicalProperties_Lamina(area,NULL).massKg==0,
    "invalid planform area or missing material returns an invalid zero body");
  ThinLaminaMaterial invalid=dry; invalid.thicknessM=NAN;
  CHECK(BodyPhysicalProperties_Lamina(area,&invalid).massKg==0 &&
    BodyLaminaMaterial_Preset((BodyLaminaPreset)99).arealMassKgM2==0,
    "invalid material and unknown preset cannot invent physical properties");
  BodyPhysicalProperties compat=BodyPhysicalProperties_Leaf();
  CHECK(compat.massKg==d.massKg && compat.volumeM3==d.volumeM3,
    "legacy leaf convenience delegates to shared representative dry material");
  BodyPhysicalProperties sphere=BodyPhysicalProperties_Sphere(.004f,600,1.2f);
  CHECK(sphere.massKg==.004f && sphere.densityKgM3==600 && fabsf(sphere.volumeM3-.004f/600)<1e-10f,
    "solid sphere constructor remains independent of lamina presets");
  printf("material: dry %.3fm/s fresh %.3fm/s petal %.3fm/s\n",speeds[0],speeds[1],speeds[2]);
  return failed!=0;
}
