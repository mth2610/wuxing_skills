/* Production connected-ribbon math, no renderer or window. */
#include <assert.h>
#include <stdio.h>
#include "core/trails/trail_ribbon_solver.h"

static void EmptyField(void *user, Vector3 p, Vector3 v, float dt, int node, float offset, FieldSample *s, Vector3 *air) {
    (void)user;(void)p;(void)v;(void)dt;(void)node;(void)offset;*s=(FieldSample){0};*air=(Vector3){0};
}
int main(void) {
    TrailRibbonState s;
    TrailRibbonMaterial m=TrailRibbonMaterial_Default();
    m.body.gravityScale=0;
    assert(TrailRibbon_Initialize(&s,8,(Vector3){0},(Vector3){1,0,0},1.4f,(Vector3){2,0,0},TRAIL_RIBBON_FREE));
    assert(fabsf(s.position[7].x-1.4f)<1e-6f);
    for(int i=0;i<120;i++) TrailRibbon_Advance(&s,&m,1.f/120,NULL,EmptyField,NULL);
    assert(fabsf(s.position[0].x-2)<1e-4f);
    assert(fabsf(s.position[7].x-3.4f)<1e-4f);
    assert(!TrailRibbon_Initialize(&s,1,(Vector3){0},(Vector3){1,0,0},1,(Vector3){0},TRAIL_RIBBON_FREE));
    assert(TrailRibbon_Initialize(&s,12,(Vector3){0},(Vector3){0,-1,0},1,(Vector3){0},TRAIL_RIBBON_HEAD_ANCHORED));
    m.body.gravityScale=1;
    TrailRibbonAnchor a={.valid=true};
    for(int i=0;i<120;i++) {
        a.previousPosition=a.position;a.position.x+=1.f/120;a.velocity=(Vector3){1,0,0};
        TrailRibbon_Advance(&s,&m,1.f/120,&a,EmptyField,NULL);
        assert(MotionVec_Length(MotionVec_Sub(s.position[0],a.position))<1e-6f);
    }
    float maxError=0;
    for(int i=1;i<s.count;i++) maxError=fmaxf(maxError,fabsf(MotionVec_Length(MotionVec_Sub(s.position[i],s.position[i-1]))-s.restLength[i]));
    assert(maxError<.003f);
    Vector3 before=s.position[0];
    TrailRibbon_Release(&s,&a);
    assert(s.mode==TRAIL_RIBBON_FREE && s.velocity[0].x==1);
    TrailRibbon_Advance(&s,&m,1.f/120,NULL,EmptyField,NULL);
    assert(s.position[0].x>before.x);
    assert(TrailRibbon_Initialize(&s,8,(Vector3){0},(Vector3){1,0,0},1,(Vector3){0},TRAIL_RIBBON_FREE));
    TrailRibbonState split=s;
    TrailRibbon_Advance(&s,&m,1.f/30,NULL,EmptyField,NULL);
    for(int i=0;i<4;i++) TrailRibbon_Advance(&split,&m,1.f/120,NULL,EmptyField,NULL);
    assert(MotionVec_Length(MotionVec_Sub(s.position[7],split.position[7]))<1e-6f);
    TrailRibbon_Advance(&s,&m,100,NULL,EmptyField,NULL);
    assert(s.accumulator<TRAIL_RIBBON_FIXED_DT && Field_FiniteVector(s.position[7]));
    puts("PASS: complete chain, free inertia, pinned head, connected length, release and bounded stepping");
    return 0;
}
