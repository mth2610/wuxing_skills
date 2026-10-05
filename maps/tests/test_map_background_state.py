#!/usr/bin/env python3
"""Execute production sky/cloud/water state scopes with renderer capture stubs.

This observes active shader and depth-write state, not actual GPU rasterization.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from test_meadow_parametric import function

ROOT = Path(__file__).resolve().parents[2]
STUBS = r'''
#include "maps/toolkit/map_props.h"
#include "environment/environment_system.h"
#define RAYMATH_STATIC_INLINE
#include "raymath.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
Camera3D camera = {.position={6,8,17}};
static bool depthWrite=true,culling=true;
static int active,uploads,draws,mistDraws;
static int expectedDepth=-1;
static Vector2 uploadedCloudTiling;
static Shader cloudShader={.id=2};
static int locCloudOffset=102;
static int locCloudBank,locCloudView,locCloudSky,locCloudHaze;
static int locCloudLightDir,locCloudLightCol,locCloudAmbCol,locCloudTime,locCloudTiling;
static Texture2D s_defaultCausticTex;
void *MemAlloc(unsigned int bytes){return malloc(bytes);}
void MemFree(void *p){free(p);}
void UploadMesh(Mesh *mesh,bool dynamic){(void)dynamic;assert(mesh->vertexCount==8 && mesh->triangleCount==12);}
Model LoadModelFromMesh(Mesh mesh){
    Model m={.meshCount=1,.materialCount=1};
    m.meshes=malloc(sizeof(Mesh));*m.meshes=mesh;
    m.materials=calloc(1,sizeof(Material));return m;
}
void UnloadModel(Model m){
    free(m.meshes[0].vertices);free(m.meshes[0].normals);
    free(m.meshes[0].texcoords);free(m.meshes[0].indices);
    free(m.meshes);free(m.materials);
}
Shader ResourceManager_LoadShader(const char *vs,const char *fs){assert(vs && fs);return (Shader){.id=2};}
int GetShaderLocation(Shader s,const char *name){(void)s;(void)name;return 1;}
void UnloadShader(Shader s){assert(s.id==2);}
void BeginShaderMode(Shader s){assert(!active);active=(int)s.id;}
void EndShaderMode(void){assert(active);active=0;}
void SetShaderValue(Shader s,int loc,const void *value,int type){
    (void)loc;(void)value;(void)type;assert(active==(int)s.id);uploads++;
    if(type==SHADER_UNIFORM_VEC2 && s.id==cloudShader.id && loc==locCloudTiling) uploadedCloudTiling=*(const Vector2 *)value;
}
void DrawModel(Model m,Vector3 pos,float scale,Color tint){
    (void)m;(void)pos;(void)scale;(void)tint;
    if(expectedDepth>=0)assert(depthWrite==(bool)expectedDepth);
    if (!active) {assert(!depthWrite && !culling);mistDraws++;}
    else assert(active==2);
    draws++;
}
static void rlDrawRenderBatchActive(void){}
static void rlDisableDepthMask(void){depthWrite=false;}
static void rlEnableDepthMask(void){depthWrite=true;}
static void rlDisableBackfaceCulling(void){culling=false;}
static void rlEnableBackfaceCulling(void){culling=true;}
static void rlActiveTextureSlot(int slot){(void)slot;}
static void rlEnableTexture(unsigned int id){(void)id;}
static void rlDisableTexture(void){}
void BeginBlendMode(int mode){assert(mode==BLEND_ALPHA && active==2 && !depthWrite);}
void EndBlendMode(void){}
int GetScreenWidth(void){return 1280;}
int GetScreenHeight(void){return 720;}
double GetTime(void){return 2;}
static bool TimeFX_IsDeterministic(void){return true;}
static float TimeFX_Elapsed(void){return 2;}
Vector3 Environment_GetSunDirection(void){return (Vector3){-.5f,-.45f,.55f};}
Color Environment_GetSunColor(void){return (Color){246,232,207,255};}
Color Environment_GetAmbientColor(void){return (Color){142,157,170,255};}
Vector4 ColorNormalize(Color c){return (Vector4){c.r/255.0f,c.g/255.0f,c.b/255.0f,c.a/255.0f};}
EnvFrameLighting Environment_GetFrameLighting(void){
    return (EnvFrameLighting){.sunDirection={-.5f,-.45f,.55f},
        .sunColor={246,232,207,255},.skyAmbient={177,196,229,255},
        .atmosphere={.color={205,228,250,255}}};
}
static Shader Water_GetShader(void){return (Shader){.id=2};}
static void Water_UploadWaveField(MapWaterSurface *w){(void)w;assert(!active);}
static void SceneTargets_RequestSoftDepthRegion(Rectangle r){(void)r;}
static Texture2D SceneTargets_GetDepthTexture(void){return (Texture2D){.id=9};}
'''
MAIN = r'''
int main(void){
    MapSkyDome sky=MapProp_CreateSkyDome();assert(sky.ready);
    MapProp_DrawSkyDome(&sky);assert(!active && depthWrite && culling && uploads==4 && draws==1);
    MapProp_UnloadSkyDome(&sky);assert(!sky.ready && !sky.model.meshCount);
    MapCloudSea cloud={.ready=true,.tiling={8,7}};
    expectedDepth=1;
    MapProp_DrawCloudSea(&cloud,(Vector3){0},-12);
    assert(!active && depthWrite && culling && uploads==14 && draws==2);
    assert(uploadedCloudTiling.x==8 && uploadedCloudTiling.y==7);
    cloud.tiling=(Vector2){4,3};
    expectedDepth=1;
    MapProp_DrawCloudSea(&cloud,(Vector3){0},-12);
    assert(uploadedCloudTiling.x==4 && uploadedCloudTiling.y==3);
    expectedDepth=-1;
    for(int shape=0;shape<3;shape++)for(int waves=0;waves<2;waves++){
        MapWaterSurface water={.ready=true,.config={.shape=shape,.maxDepth=1.15f},
            .waveFieldTex={.id=waves ? 7 : 0}};
        int before=uploads;
        MapProp_DrawWaterOverlay(&water,2);
        assert(uploads>before+20 && !active && depthWrite && culling);
    }
    assert(draws==9);
    expectedDepth=0;
    cloud.mistReady=true;
    MapProp_DrawIslandMist(&cloud,(Vector3){50,0,37.5});
    assert(mistDraws==1 && depthWrite && culling && !active);
    puts("PASS: sky/cloud/water uniforms and draws share active shader; depth/culling restored");
}
'''

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--raylib-include", default="/tmp/rlvk_check_cache")
    args = parser.parse_args()
    nature = (ROOT / "maps/toolkit/map_props_nature.inl").read_text()
    overlay = function(nature, "MapProp_DrawWaterOverlay")
    declarations = "\n".join(line for line in re.findall(r"^static int s_waterLoc[^;]+;", nature, re.M)
                             if re.search(r"s_waterLoc\w+", line).group() in overlay)
    code = STUBS + declarations + "\n"
    code += (ROOT / "maps/toolkit/map_props_sky.inl").read_text()
    code += function((ROOT / "maps/toolkit/map_props_cloud.inl").read_text(), "MapProp_DrawCloudSea")
    code += function((ROOT / "maps/toolkit/map_props_cloud.inl").read_text(), "MapProp_DrawIslandMist")
    code += overlay + MAIN
    with tempfile.TemporaryDirectory(prefix="map-background-") as directory:
        work = Path(directory)
        (work / "test.c").write_text(code)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                        "-I", args.raylib_include, str(work / "test.c"), "-lm", "-o", str(work / "test")], check=True)
        subprocess.run([str(work / "test")], check=True)

if __name__ == "__main__":
    main()
