#include <assert.h>
#include <stdio.h>
#include "core/motion/motion_frame.h"
static bool Near(float a,float b) {return fabsf(a-b)<.0001f;}
int main(void) {
    MotionFrameRegistry registry={0};
    Matrix identity={.m0=1,.m5=1,.m10=1,.m15=1};
    MotionFrameHandle handle=MotionFrameRegistry_Create(&registry,identity);
    MotionFrameSnapshot snapshot;
    assert(handle && MotionFrameRegistry_Snapshot(&registry,handle,(Vector3){1,0,0},&snapshot));
    assert(snapshot.velocity.x==0 && snapshot.transform.angularVelocityRadPerSec.z==0);
    Matrix rotation=identity;rotation.m0=0;rotation.m1=1;rotation.m4=-1;rotation.m5=0;rotation.m12=2;
    assert(MotionFrameRegistry_Update(&registry,handle,rotation,.5f,false));
    assert(MotionFrameRegistry_Snapshot(&registry,handle,(Vector3){1,0,0},&snapshot));
    assert(Near(snapshot.position.x,2) && Near(snapshot.position.y,1));
    assert(Near(snapshot.velocity.x,2) && Near(snapshot.velocity.y,2));
    assert(Near(snapshot.transform.frameVelocityMps.x,4));
    assert(Near(snapshot.transform.angularVelocityRadPerSec.z,3.14159265f));
    assert(snapshot.previous.m0==1 && snapshot.current.m1==1);
    assert(MotionFrameRegistry_Update(&registry,handle,identity,.5f,true));
    assert(MotionFrameRegistry_Snapshot(&registry,handle,(Vector3){1,0,0},&snapshot));
    assert(snapshot.discontinuity && snapshot.velocity.x==0 && snapshot.transform.angularVelocityRadPerSec.z==0);
    Matrix half=identity;half.m0=-1;half.m5=-1;
    Vector3 omega=MotionFrame_AngularVelocity(identity,half,1);
    assert(Near(fabsf(omega.z),3.14159265f));
    assert(Near(omega.x,0) && Near(omega.y,0));
    /* Half turn around a diagonal axis exercises the stable nonzero diagonal branch. */
    half=identity;half.m0=0;half.m5=0;half.m10=-1;half.m4=1;half.m1=1;
    omega=MotionFrame_AngularVelocity(identity,half,1);
    assert(Near(fabsf(omega.x),2.22144147f) && Near(fabsf(omega.y),2.22144147f));
    assert(!MotionFrameRegistry_Update(&registry,handle,identity,0,false));
    Matrix invalid=identity;invalid.m12=NAN;
    assert(!MotionFrameRegistry_Update(&registry,handle,invalid,1,false));
    MotionFrameRegistry_Destroy(&registry,handle);
    assert(!MotionFrameRegistry_Snapshot(&registry,handle,(Vector3){0},&snapshot));
    assert(!snapshot.valid);
    MotionFrameHandle replacement=MotionFrameRegistry_Create(&registry,identity);
    assert(replacement && replacement!=handle);
    MotionFrameRegistry_Reset(&registry);
    assert(!MotionFrameRegistry_Get(&registry,replacement));
    puts("PASS: shared frame translation, rotation, half-turn stability, discontinuity and stale handles");
    return 0;
}
