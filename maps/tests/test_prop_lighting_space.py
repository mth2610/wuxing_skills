#!/usr/bin/env python3
"""Run production prop uniform uploads under translated, rotated cameras."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def function(source, name):
    start = source.index("void " + name + "(")
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


STUBS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct {float x,y,z;} Vector3;
typedef struct {float x,y,z,w;} Vector4;
typedef struct {float m0,m4,m8,m12,m1,m5,m9,m13,m2,m6,m10,m14,m3,m7,m11,m15;} Matrix;
typedef struct {unsigned char r,g,b,a;} Color;
typedef struct {unsigned int id;} Shader;
enum {SHADER_UNIFORM_VEC3};
static struct {Vector3 position;} camera;
static Matrix shaderFromWorld,uploadedWorldFromShader;
static Vector3 uploadedLight,uploadedCamera,uploadedSunColor,uploadedAmbient;
static int activeShader,cloudBindings,shadowBindings;
static Vector3 Vector3Transform(Vector3 p,Matrix m) {
    return (Vector3){m.m0*p.x+m.m4*p.y+m.m8*p.z+m.m12,
                    m.m1*p.x+m.m5*p.y+m.m9*p.z+m.m13,
                    m.m2*p.x+m.m6*p.y+m.m10*p.z+m.m14};
}
static Vector3 Vector3Subtract(Vector3 a,Vector3 b) {return (Vector3){a.x-b.x,a.y-b.y,a.z-b.z};}
static Vector3 Vector3Negate(Vector3 p) {return (Vector3){-p.x,-p.y,-p.z};}
static float Vector3Length(Vector3 p) {return sqrtf(p.x*p.x+p.y*p.y+p.z*p.z);}
static Vector3 Vector3Normalize(Vector3 p) {float d=Vector3Length(p);return (Vector3){p.x/d,p.y/d,p.z/d};}
static Matrix MatrixInvert(Matrix m) {
    Matrix r={.m0=m.m0,.m4=m.m1,.m8=m.m2,.m1=m.m4,.m5=m.m5,.m9=m.m6,
              .m2=m.m8,.m6=m.m9,.m10=m.m10,.m15=1};
    Vector3 t=Vector3Transform((Vector3){m.m12,m.m13,m.m14},r);
    r.m12=-t.x;r.m13=-t.y;r.m14=-t.z;return r;
}
static Matrix rlGetMatrixTransform(void) {return shaderFromWorld;}
static Shader PropLit_GetShader(void) {return (Shader){17};}
static void rlDrawRenderBatchActive(void) {}
static void BeginShaderMode(Shader s) {assert(!activeShader);activeShader=s.id;}
static void EndShaderMode(void) {assert(activeShader==17);activeShader=0;}
static void Environment_BindCloudShadowShader(Shader s) {assert(activeShader==(int)s.id);cloudBindings++;}
static void MapShadow_UpdateShader(Shader s) {assert(activeShader==(int)s.id);shadowBindings++;}
static int GetShaderLocation(Shader s,const char *name) {
    (void)s;
    if(!strcmp(name,"u_lightDir"))return 1;
    if(!strcmp(name,"u_viewPos"))return 2;
    if(!strcmp(name,"u_lightColor"))return 6;
    if(!strcmp(name,"u_ambientColor"))return 7;
    return 3;
}
static void SetShaderValue(Shader s,int location,const void *data,int type) {
    assert(activeShader==(int)s.id && type==SHADER_UNIFORM_VEC3);
    if(location==1)uploadedLight=*(const Vector3 *)data;
    if(location==2)uploadedCamera=*(const Vector3 *)data;
    if(location==6)uploadedSunColor=*(const Vector3 *)data;
    if(location==7)uploadedAmbient=*(const Vector3 *)data;
}
static void SetShaderValueMatrix(Shader s,int location,Matrix value) {
    assert(activeShader==(int)s.id && location==3);uploadedWorldFromShader=value;
}
static Vector3 Environment_GetSunDirection(void) {return (Vector3){-.5f,-.45f,.55f};}
static Color Environment_GetSunColor(void) {return (Color){246,232,207,255};}
static Color Environment_GetAmbientColor(void) {return (Color){142,157,170,255};}
static Vector4 ColorNormalize(Color c) {return (Vector4){c.r/255.f,c.g/255.f,c.b/255.f,c.a/255.f};}
static float clamp(float value,float low,float high) {return fminf(high,fmaxf(low,value));}
static void nearVector(Vector3 a,Vector3 b) {assert(Vector3Length(Vector3Subtract(a,b))<.00002f);}
'''

MAIN = r'''
int main(void) {
    const Vector3 rock={11,.2f,12};
    for(int i=0;i<2;i++) {
        camera.position=i ? (Vector3){-14,4.5f,6} : (Vector3){9,3,20};
        float sign=i ? -1.f : 1.f;
        shaderFromWorld=(Matrix){.m8=sign,.m5=1,.m2=-sign,.m15=1,
            .m12=-sign*camera.position.z,.m13=-camera.position.y,.m14=sign*camera.position.x};
        PropLit_UpdateLighting();
        assert(!activeShader && cloudBindings==i+1 && shadowBindings==i+1);
        Vector3 sun=Vector3Negate(Environment_GetSunDirection());
        Vector3 expectedLight=Vector3Normalize((Vector3){sign*sun.z,sun.y,-sign*sun.x});
        nearVector(uploadedLight,expectedLight);
        nearVector(uploadedCamera,(Vector3){0});
        nearVector(uploadedSunColor,(Vector3){246/255.f,232/255.f,207/255.f});
        nearVector(uploadedAmbient,(Vector3){142/255.f,157/255.f,170/255.f});
        Vector3 shaderRock=Vector3Transform(rock,shaderFromWorld);
        nearVector(Vector3Transform(shaderRock,uploadedWorldFromShader),rock);
        float shaderDistance=Vector3Length(Vector3Subtract(uploadedCamera,shaderRock));
        float worldDistance=Vector3Length(Vector3Subtract(camera.position,rock));
        assert(fabsf(shaderDistance-worldDistance)<.00002f);
        assert(fabsf(ProductionGroundAO(shaderRock)-.82f)<.00002f);
        /* The same sun/normal angle must survive camera rotation. */
        Vector3 normal=Vector3Normalize((Vector3){.2f,.8f,.4f});
        Vector3 shaderNormal=(Vector3){sign*normal.z,normal.y,-sign*normal.x};
        sun=Vector3Normalize(sun);
        float worldDot=normal.x*sun.x+normal.y*sun.y+normal.z*sun.z;
        float shaderDot=shaderNormal.x*uploadedLight.x+shaderNormal.y*uploadedLight.y+shaderNormal.z*uploadedLight.z;
        assert(fabsf(worldDot-shaderDot)<.00002f);
        assert(fabsf(fmaxf(worldDot,0)-fmaxf(shaderDot,0))<.00002f);
    }
    puts("prop lighting: active uploads, direction/point transforms, distance, ground AO, diffuse and exposure invariance passed");
}
'''


source = (ROOT / "maps/toolkit/prop_lit.c").read_text()
shader = (ROOT / "maps/toolkit/shaders/prop_lit.fs").read_text()
ground_ao = re.search(r"float groundAO = ([^;]+);", shader).group(1)
assert "length(u_viewPos - fragPosition)" in shader
assert "VFXLights_Accumulate(fragPosition, normal, albedo.rgb)" in shader
assert "MapShadowVisibility(fragPosition, normal, lightDir)" in shader
assert "Environment_CloudVisibility(u_cloudNoise, worldPosition)" in shader
assert "vec3 worldPosition = vec3(u_cloudWorldFromShader * vec4(fragPosition, 1.0))" in shader
ao = ("\nstatic float ProductionGroundAO(Vector3 fragPosition) {\n"
      "Vector3 worldPosition=Vector3Transform(fragPosition,uploadedWorldFromShader);\n"
      "(void)worldPosition;return " + ground_ao + ";\n}\n")
with tempfile.TemporaryDirectory(prefix="wuxing-prop-lighting-") as directory:
    work = pathlib.Path(directory)
    code = work / "prop.c"
    binary = work / "prop"
    code.write_text(STUBS + function(source, "PropLit_UpdateLighting") + ao + MAIN)
    subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", str(code), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
