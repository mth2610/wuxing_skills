/* Executes the real atmosphere receiver; graphics calls are inert stand-ins.
 * This verifies motion and recycling, not billboard appearance or terrain contact. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include "raylib.h"
typedef struct Camera3D { Vector3 position, target, up; float fovy; int projection; } Camera3D;
typedef struct Image { void *data; int width, height, mipmaps, format; } Image;
#define TEXTURE_FILTER_BILINEAR 1
#include "core/vfx_render.h"
#include "core/motion/motion_fields.h"
static Vector3 s_air;
static bool s_vortex;
Vector3 Wind_EvaluateVelocity(Vector3 p, float t) {
    (void)t;
    if (s_vortex) return (Vector3){-8.0f*p.x-12.0f*p.z,0,12.0f*p.x-8.0f*p.z};
    return s_air;
}
Vector3 Wind_EvaluateBackgroundVelocity(Vector3 p,float t) { return Wind_EvaluateVelocity(p,t); }
static bool s_absoluteFlow;
void MotionFields_SampleBody(Vector3 p,Vector3 v,const BodyPhysicalProperties *b,
    const MediumProperties *m,const ReceiverConstraints *c,float dt,unsigned int mask,
    MotionReceiver *r,FieldSample *s) {
    (void)p;(void)v;(void)b;(void)m;(void)dt;(void)mask;(void)r;
    *s=(FieldSample){0};
    if(s_absoluteFlow && c->mode==RECEIVER_TRACER)
        *s=(FieldSample){.mediumVelocityMps={2,0,0},.mediumWeight=1,.mediumIsAbsolute=true};
}
static Vector3 Vector3Add(Vector3 a, Vector3 b) { return (Vector3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static Image GenImageColor(int w, int h, Color c) { (void)w;(void)h;(void)c;return (Image){0}; }
static void ImageDrawPixel(Image *i,int x,int y,Color c) { (void)i;(void)x;(void)y;(void)c; }
static Texture2D LoadTextureFromImage(Image i) { (void)i;return (Texture2D){0}; }
static void UnloadImage(Image i) { (void)i; }
static void SetTextureFilter(Texture2D t,int f) { (void)t;(void)f; }
static void UnloadTexture(Texture2D t) { (void)t; }
static void DrawBillboard(Camera3D c,Texture2D t,Vector3 p,float s,Color col) { (void)c;(void)t;(void)p;(void)s;(void)col; }
VFXRenderScope VFXRender_BeginDraw(VFXRenderPass p,VFXSurfaceMode m,bool d) { (void)p;(void)m;(void)d;return (VFXRenderScope){0}; }
void VFXRender_EndDraw(VFXRenderScope *s) { (void)s; }
#include "core/atmosphere.c"
static int failed;
#define CHECK(c,m) do { printf("[%s] %s\n",(c)?"PASS":"FAIL",m);if(!(c)) failed++; } while(0)
static void Reset(void) {
    s_ready=true;s_count=1;s_time=0;s_mode=ATMO_MODE_MOONLIGHT_DUST;
    s_extent=(Vector3){100,100,100};s_center=(Vector3){0};
    s_motes[0]=(Mote){0};s_motes[0].size=0.06f;s_air=(Vector3){0};s_vortex=false;
}
static float Run(float dt) {
    Reset();s_air=(Vector3){3,0,0};
    for(int i=0;i<(int)lroundf(2.0f/dt);i++) Atmosphere_Update(dt,(Camera3D){0});
    return s_motes[0].pos.x;
}
static Vector3 RunVortex(float dt) {
    Reset();s_vortex=true;s_motes[0].pos=(Vector3){1,0,0.2f};
    for(int i=0;i<(int)lroundf(1.0f/dt);i++) Atmosphere_Update(dt,(Camera3D){0});
    return s_motes[0].pos;
}
int main(void) {
    Reset();s_air=(Vector3){9,0,0};s_absoluteFlow=true;
    CHECK(Atmosphere_Airflow((Vector3){0},0).x==2,"Typed absolute tracer flow replaces Wind without duplication");
    s_absoluteFlow=false;
    CHECK(Atmosphere_Airflow((Vector3){0},0).x==9,"Legacy tracer retains aggregate Wind exactly");
    Vector3 coarse=RunVortex(1.0f/30),fine=RunVortex(1.0f/240);
    float error=hypotf(coarse.x-fine.x,coarse.z-fine.z);
    printf("Converging vortex trajectory error: %.6f m\n",(double)error);
    CHECK(error<0.01f,"Converging vortex motion agrees at 30 and 240 FPS");
    Reset();Atmosphere_Update(0.01f,(Camera3D){0});
    float before=s_motes[0].pos.x;
    s_air=(Vector3){10,0,0};Atmosphere_Update(0.01f,(Camera3D){0});
    CHECK(s_motes[0].pos.x-before<0.003f,"A wind step accelerates dust rather than instantly replacing velocity");
    CHECK(fabsf(Run(1.0f/30)-Run(1.0f/120))<0.002f,"Constant-air trajectory agrees at 30 and 120 FPS");
    Reset();s_air=(Vector3){3,0,0};
    for(int i=0;i<600;i++) Atmosphere_Update(1.0f/60,(Camera3D){0});
    CHECK(fabsf(s_motes[0].velocity.x-3.0f)<0.001f,"Dust converges to the air speed without an arbitrary wind multiplier");
    s_air=(Vector3){-3,0,0};Atmosphere_Update(0.01f,(Camera3D){0});
    CHECK(s_motes[0].velocity.x>0,"A reversed gust first brakes existing momentum");
    for(int i=0;i<600;i++) Atmosphere_Update(1.0f/60,(Camera3D){0});
    CHECK(fabsf(s_motes[0].velocity.x+3.0f)<0.001f,"Dust eventually follows a reversed gust without overshoot");
    float x=s_motes[0].pos.x;
    Atmosphere_Update(-1,(Camera3D){0});Atmosphere_Update(NAN,(Camera3D){0});
    CHECK(s_motes[0].pos.x==x,"Invalid timesteps do not corrupt particle state");
    CHECK(fabsf(WrapAxis(157,0,5))<=5,"Recycling handles multiple-volume crossings");
    CHECK(fabsf(WrapAxis(-157,0,5))<=5,"Recycling also handles negative crossings");
    CHECK(WrapAxis(10,3,0)==3,"A zero-width volume remains finite");
    Reset();
    CHECK(MoteBoundaryFade((Vector3){0})==1,"Interior motes retain their brightness");
    CHECK(MoteBoundaryFade((Vector3){100,0,0})==0,"Motes disappear at the recycling boundary");
    CHECK(fabsf(MoteBoundaryFade((Vector3){99,0,0})-MoteBoundaryFade((Vector3){-99,0,0}))<0.00001f,"Opposite recycling faces fade identically");
    srand(123);s_extent=(Vector3){15,5,15};
    float settling=0;
    for(int i=0;i<100;i++) { Mote m={0};SeedMote(&m);settling+=m.drift.y; }
    CHECK(settling<0,"Moonlight dust settles rather than receiving perpetual lift");
    return failed?1:0;
}
