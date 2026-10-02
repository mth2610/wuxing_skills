"""Compile the production baker against a graphics stub; check real metric/CPU data."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = r'''
#ifndef RAYLIB_H
#define RAYLIB_H
#include <stdbool.h>
typedef struct {float x,y,z,w;} Vector4;
typedef struct {unsigned id;int width,height,mipmaps,format;} Texture2D;
typedef struct {void *data;int width,height,mipmaps,format;} Image;
#define PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 7
#define TEXTURE_FILTER_BILINEAR 1
#define TEXTURE_WRAP_CLAMP 1
#define LOG_INFO 1
Texture2D LoadTextureFromImage(Image);
void SetTextureFilter(Texture2D,int);
void SetTextureWrap(Texture2D,int);
void UnloadTexture(Texture2D);
double GetTime(void);
void TraceLog(int,const char *,...);
#endif
'''
TEST = r'''
#include "maps/toolkit/map_ecology.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static int uploads,unloads,calls;
Texture2D LoadTextureFromImage(Image i) { return (Texture2D){++uploads,i.width,i.height,1,i.format}; }
void SetTextureFilter(Texture2D t,int f) {(void)t;(void)f;}
void SetTextureWrap(Texture2D t,int f) {(void)t;(void)f;}
void UnloadTexture(Texture2D t) {(void)t;unloads++;}
double GetTime(void) {return 0;}
void TraceLog(int l,const char *fmt,...) {(void)l;(void)fmt;}
static float Density(float x,float z,void *u) {(void)x;(void)z;(void)u;calls++;return .8f;}
static bool Eligible(float x,float z,void *u) {(void)z;(void)u;return x>20;}
static void Near(float a,float b,float eps) {if(fabsf(a-b)>=eps) fprintf(stderr,"Near: actual=%f expected=%f epsilon=%f\n",a,b,eps);assert(fabsf(a-b)<eps);}
int main(void) {
    Vector4 path={30,0,30,75};
    MapEcologyConfig c={.rect={0,0,100,75},.lake={60,35,10,7},.paths=&path,.pathCount=1,.roadHalfWidth=1.15f};
    Near(MapEcology_RoadDistance(&c,30,20),-1.15f,.0001f);
    Near(MapEcology_RoadDistance(&c,32.15f,20),1,.0001f);
    Near(MapEcology_ShoreDistance(c.lake,60,35),-7,.0001f);
    Near(MapEcology_ShoreDistance(c.lake,72,35),2,.0001f);
    Near(MapEcology_ShoreDistance(c.lake,60,44),2,.0001f);
    Near(MapEcology_ShoreDistance(c.lake,70,35),0,.0001f);
    Vector4 circle={0,0,5,5};
    Near(MapEcology_ShoreDistance(circle,3,4),0,.0001f);
    Near(MapEcology_ShoreDistance(circle,1,1),sqrtf(2)-5,.0001f);
    for(int i=0;i<24;i++) {
        float x=60+(i%6)*2.4f, z=35+(i/6)*2.8f, best=1e9f;
        for(int j=0;j<8192;j++) {
            float angle=j*6.28318530718f/8192;
            float dx=x-60-10*cosf(angle), dz=z-35-7*sinf(angle);
            best=fminf(best,sqrtf(dx*dx+dz*dz));
        }
        Near(fabsf(MapEcology_ShoreDistance(c.lake,x,z)),best,.003f);
    }
    MapEcology m={0},other={0};
    assert(MapEcology_Bake(&m,&c,Density,Eligible,NULL));assert(uploads==2);
    assert(!MapEcology_Bake(&other,&c,Density,Eligible,NULL));
    int n=calls;assert(MapEcology_Bake(&m,&c,Density,Eligible,NULL));assert(calls==n);
    MapEcologySample a=MapEcology_Sample(&m,40,20),b=MapEcology_Sample(&m,30,20);
    Near(a.coverage,.8f,.004f);Near(b.roadDistance,-1.15f,.05f);
    Near(a.roadDistance,8.85f,.001f);
    Near(MapEcology_Sample(&m,60,44).shoreDistance,2,.001f);
    assert(MapEcology_Sample(&m,10,20).coverage==0);
    assert(MapEcology_Sample(&m,-1,20).coverage==0);
    assert(MapEcology_Sample(&m,60,36).moisture>.99f);
    MapEcology_Unload(&m);assert(!m.ready && unloads==2);
    assert(MapEcology_Sample(&m,40,20).coverage==0);
    assert(MapEcology_Bake(&other,&c,Density,Eligible,NULL));MapEcology_Unload(&other);
    puts("ecology: signed road/ellipse, anisotropic meters, quantization, eligibility, cache and lifecycle passed");
}
'''
with tempfile.TemporaryDirectory(prefix='wuxing-ecology-') as directory:
    work = pathlib.Path(directory)
    (work / 'raylib.h').write_text(HEADER)
    (work / 'test.c').write_text(TEST)
    binary = work / 'test'
    subprocess.run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(work), '-I'+str(ROOT),
                    str(ROOT/'maps/toolkit/map_ecology.c'), str(work/'test.c'),
                    '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
