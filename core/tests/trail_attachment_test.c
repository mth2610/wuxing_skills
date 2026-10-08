#include <assert.h>
#include <stdio.h>
#include "core/trails/trail_attachment.h"
int main(void) {
    TrailAttachmentRegistry r={0};
    Matrix m={.m0=1,.m5=1,.m10=1,.m15=1};
    TrailAttachmentHandle h=TrailAttachmentRegistry_Create(&r,m);
    assert(h);
    m.m12=2;
    assert(TrailAttachmentRegistry_Update(&r,h,m,.5f,false));
    TrailRibbonAnchor a;
    assert(TrailAttachmentRegistry_Snapshot(&r,h,(Vector3){1,0,0},&a));
    assert(a.previousPosition.x==1 && a.position.x==3 && a.velocity.x==4);
    m.m0=0;m.m1=1;m.m4=-1;m.m5=0;
    assert(TrailAttachmentRegistry_Update(&r,h,m,.5f,false));
    assert(TrailAttachmentRegistry_Snapshot(&r,h,(Vector3){1,0,0},&a));
    assert(a.position.x==2 && a.position.y==1 && a.velocity.x==-2 && a.velocity.y==2);
    TrailAttachmentRegistry_Destroy(&r,h);
    assert(!TrailAttachmentRegistry_Snapshot(&r,h,(Vector3){0},&a));
    TrailAttachmentHandle next=TrailAttachmentRegistry_Create(&r,m);
    assert(next && next!=h);
    assert(!TrailAttachmentRegistry_Get(&r,h));
    TrailAttachmentRegistry_Reset(&r);
    assert(!TrailAttachmentRegistry_Get(&r,next));
    assert(TrailAttachmentRegistry_Create(&r,m)!=next);
    for(int i=1;i<TRAIL_ATTACHMENT_CAPACITY;i++) assert(TrailAttachmentRegistry_Create(&r,m));
    assert(!TrailAttachmentRegistry_Create(&r,m));
    puts("PASS: owned transform snapshots, rotational offset velocity, generation, reset and exhaustion");
    return 0;
}
