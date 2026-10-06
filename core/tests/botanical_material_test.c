#include "core/composition/wood/vc_wood_botanical_profile.h"
#include "core/motion/physical_field.h"
#include <stdio.h>
static int failures;
#define CHECK(c,m) do {if(!(c)){printf("FAIL: %s\n",m);failures++;}else printf("PASS: %s\n",m);}while(0)
int main(void) {
    float plumSize=Botanical_ResolvePetalSize(0,2);
    BotanicalProfile plum=Botanical_DetachedPetalProfile(2);
    float plumLength=1.25f*plumSize;
    float plumWidth=2*Botanical_EvaluateWidth(&plum,plum.a/(plum.a+plum.b))*plumLength;
    CHECK(fabsf(plumLength-.014f)<1e-7f && fabsf(plumWidth-.009f)<1e-7f,
          "default standalone plum petal measures 14 mm by 9 mm");
    CHECK(fabsf(Botanical_ProfilePetalPlum().W-.56f)<1e-7f,
          "standalone petal width calibration preserves compound blossom geometry");
    CHECK(fabsf(Botanical_PetalDefaultSize(0)*1.25f-.060f)<1e-7f &&
          fabsf(Botanical_PetalDefaultSize(1)*1.25f-.030f)<1e-7f,
          "lotus and orchid retain distinct representative blade dimensions");
    CHECK(Botanical_ResolvePetalSize(.004f,2)==.004f &&
          Botanical_ResolveLeafSize(.01f,0)==.01f &&
          isnan(Botanical_ResolvePetalSize(NAN,2)),
          "small explicit dimensions remain valid and nonfinite dimensions are rejected");
    CHECK(fabsf(Botanical_LeafDefaultSize(0)*1.05f-.080f)<1e-7f,
          "default free oval leaf is an 8 cm blade");
    ThinLaminaMaterial petalMaterial=BodyLaminaMaterial_Preset(BODY_LAMINA_PETAL_FRESH);
    float plumArea=Botanical_PetalPlanformArea(plumSize,2);
    BodyPhysicalProperties plumBody=BodyPhysicalProperties_Lamina(plumArea,&petalMaterial);
    CHECK(plumBody.massKg>0 && plumBody.massKg<.0001f &&
          fabsf(plumBody.massKg/plumArea-.150f)<1e-6f,
          "species-sized plum petal derives material mass below 100 mg without a floor");
    for(int shape=0;shape<3;shape++) {
        float area=Botanical_LeafPlanformArea(.16f,shape);
        float doubled=Botanical_LeafPlanformArea(.32f,shape);
        CHECK(area>0 && fabsf(doubled/area-4)<1e-5f,
              "leaf mesh planform scales quadratically with blade dimensions");
        ThinLaminaMaterial material=BodyLaminaMaterial_Preset(BODY_LAMINA_LEAF_DRY);
        BodyPhysicalProperties small=BodyPhysicalProperties_Lamina(area,&material);
        BodyPhysicalProperties big=BodyPhysicalProperties_Lamina(doubled,&material);
        CHECK(fabsf(big.massKg/small.massKg-4)<1e-5f && small.massKg<.002f,
              "leaf material mass scales with geometry rather than fixed grams");
    }
    for(int type=0;type<3;type++) {
        float area=Botanical_PetalPlanformArea(.13f,type);
        ThinLaminaMaterial material=BodyLaminaMaterial_Preset(BODY_LAMINA_PETAL_FRESH);
        BodyPhysicalProperties body=BodyPhysicalProperties_Lamina(area,&material);
        CHECK(area>0 && fabsf(body.massKg/area-.150f)<1e-6f,
              "petal morphology supplies consistent broadside area and fresh sheet mass");
        ThinLaminaMaterial dry=BodyLaminaMaterial_Preset(BODY_LAMINA_LEAF_DRY);
        BodyPhysicalProperties dried=BodyPhysicalProperties_Lamina(area,&dry);
        CHECK(fabsf(dried.massKg/body.massKg-.4f)<1e-6f,
              "dry sheet selection remains distinct when applied to a petal geometry");
    }
    CHECK(Botanical_FlutterAirWeight((Vector3){0})==0 &&
          Botanical_FlutterAirWeight((Vector3){NAN,0,0})==0,
          "stationary body in still air and invalid relative flow inject no flutter");
    Vector3 movingAir={3,0,0}, movingBody={3,0,0};
    CHECK(Botanical_FlutterAirWeight(MotionVec_Sub(movingAir,movingBody))==0,
          "co-moving body has no relative aerodynamic flow to power flutter");
    CHECK(Botanical_FlutterAirWeight((Vector3){1.5f,0,0})==.5f &&
          Botanical_FlutterAirWeight((Vector3){0,3,0})==1 &&
          Botanical_FlutterAirWeight((Vector3){100,0,0})==1,
          "relative flow powers a finite bounded artistic flutter envelope");
    return failures?1:0;
}
