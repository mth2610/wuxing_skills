#include "core/motion/motion_path_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void Near(Vector3 a,Vector3 b,float tolerance) {
  assert(MotionVec_Length(MotionVec_Sub(a,b))<tolerance);
}
int main(void) {
  /* Locks the shared GLSL formula to this numeric CPU test; real device parity
   * remains the renderer harness's responsibility. */
  FILE *shader=fopen("core/motion/shaders/motion_fields.glsl","rb");assert(shader);
  static char text[90000];size_t bytes=fread(text,1,sizeof(text)-1,shader);fclose(shader);text[bytes]=0;
  assert(strstr(text,"identity.x==handle && motionFields[fi].identity.y==3u"));
  assert(strstr(text,"while(hi-lo>1)"));
  assert(strstr(text,"span*(t3-2.0*t2+t)*aSlope+span*(t3-t2)*bSlope"));
  assert(strstr(text,"float blend=t2*(3.0-2.0*t);"));
  Vector3 points[]={{0,0,0},{1,1,0},{3,0,1},{4,1,1},{6,0,0}};
  MotionPath p;assert(MotionPath_Build(&p,points,5));
  MotionPathTransportSnapshot view={.path=&p,.transform=FieldTransform_Identity()};
  Near(MotionPathTransport_Sample(&view,-10,(Vector3){0}),points[0],1e-6f);
  Near(MotionPathTransport_Sample(&view,100,(Vector3){0}),points[4],1e-6f);
  Vector3 lane={.1f,.2f,.3f};
  for(int i=1;i<p.count-1;i++) {
    float d=p.distance[i],e=.001f;
    for(int offset=0;offset<2;offset++) {
      Vector3 useLane=offset?lane:(Vector3){0};
      Vector3 mid=MotionPathTransport_Sample(&view,d,useLane);
      Vector3 left=MotionVec_Scale(MotionVec_Sub(mid,MotionPathTransport_Sample(&view,d-e,useLane)),1/e);
      Vector3 right=MotionVec_Scale(MotionVec_Sub(MotionPathTransport_Sample(&view,d+e,useLane),mid),1/e);
      Near(left,right,.015f);
    }
  }
  float previous=-1;
  for(int i=0;i<=1000;i++) {
    float d=p.length*i/1000;
    Vector3 q=MotionPathTransport_Sample(&view,d,(Vector3){0});
    assert(q.x>=previous);previous=q.x;
  }
  /* Absolute phase sampling has no integration history or per-step drift. */
  float age=0;for(int i=0;i<64;i++) age+=1.0f/64;
  Near(MotionPathTransport_Sample(&view,2*age,lane),MotionPathTransport_Sample(&view,2,lane),1e-6f);
  Vector3 world=MotionPathTransport_Sample(&view,0,(Vector3){0,.2f,.3f});
  Near(MotionPathTransport_CaptureLane(&view,world),(Vector3){0,.2f,.3f},1e-6f);
  view.transform.position=(Vector3){10,20,30};
  view.transform.axisX=(Vector3){0,1,0};view.transform.axisY=(Vector3){-1,0,0};
  Vector3 q=MotionPathTransport_Sample(&view,p.length,(Vector3){0});
  Near(q,(Vector3){10,26,30},1e-6f);
  world=MotionPathTransport_Sample(&view,0,(Vector3){0,.2f,.3f});
  Near(MotionPathTransport_CaptureLane(&view,world),(Vector3){0,.2f,.3f},1e-5f);
  assert(MotionVec_Length(MotionPathTransport_Sample(NULL,1,lane))==0);
  puts("motion path transport: clamp, C1 lanes, monotonic route, stable phase, transformed capture PASS");
  return 0;
}
