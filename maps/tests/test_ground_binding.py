"""Execute production DrawGround with raylib enum aliases and capturing stubs."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
source = (ROOT/'maps/toolkit/map_props_ground.inl').read_text()
start = source.index('void MapProp_DrawGround(')
end = source.index('\nvoid MapProp_SetGroundHabitat(', start)
draw = source[start:end]
cloud_slot = re.search(r'#define GROUND_CLOUD_TEXTURE_SLOT\s+(\d+)', source).group(1)
STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {float x,y;} Vector2;
typedef struct {float x,y,z;} Vector3;
typedef struct {float x,y,z,w;} Vector4;
typedef struct {unsigned char r,g,b,a;} Color;
typedef struct {unsigned id;} Texture2D;
typedef struct {unsigned id;} Shader;
/* Raylib enum truth: names are aliases, never independent texture slots. */
enum {MATERIAL_MAP_DIFFUSE=0, MATERIAL_MAP_SPECULAR=1, MATERIAL_MAP_METALNESS=1,
      MATERIAL_MAP_NORMAL=2, MATERIAL_MAP_ROUGHNESS=3, MATERIAL_MAP_OCCLUSION=4,
      MATERIAL_MAP_EMISSION=5, MATERIAL_MAP_HEIGHT=6, MATERIAL_MAP_BRDF=10};
enum {SHADER_UNIFORM_INT,SHADER_UNIFORM_VEC2,SHADER_UNIFORM_VEC3,SHADER_UNIFORM_VEC4,SHADER_UNIFORM_FLOAT};
enum {GFX_HIGH=2};
typedef struct {Texture2D texture;} MaterialMap;
typedef struct {MaterialMap *maps;} Material;
typedef struct {Material *materials;} Model;
typedef struct {bool ready;Vector4 rect;} MapEcology;
typedef struct {bool ready;Model model;Vector3 drawOffset;Vector2 tiling;const MapEcology *ecology;} MapGroundSurface;
typedef struct {Texture2D noiseTexture;Vector4 uvTransform,shape;Vector2 projection;} EnvCloudShadowFrame;
static const Color WHITE={255,255,255,255};
static Shader groundShader={123};
static int locGroundCloudNoise=1,locGroundTiling=2,locGroundCloudUV=3,locGroundCloudShape=4,
 locGroundCloudProjection=5,locEcologyEnabled=6,locEcologyRect=7,locLightDir=8,locLightColor=9,
 locAmbientColor=10,locViewPos=11,locPathSegCount=12,locPathSegs=13,locLakeParams=14,locGroundOffset=15;
static int s_groundPathSegCount;
static Vector4 s_groundPathSegs[16],s_groundLakeParams;
static struct {Vector3 position;} camera;
static unsigned textures[11],expectedMaterialIds[11];
static int activeProgram,activeSlot,cloudSampler,tilingUploads,drawCalls;
static Vector2 expectedTiling;
static EnvCloudShadowFrame cloudFrame={{456},{1,2,3,.12f},{.48f,.16f,80,0},{0,0}};
static void BeginShaderMode(Shader s) {assert(!activeProgram);activeProgram=s.id;}
static void EndShaderMode(void) {assert(activeProgram==123);activeProgram=0;}
static EnvCloudShadowFrame Environment_GetCloudShadowFrame(void) {return cloudFrame;}
static void rlActiveTextureSlot(int s) {assert(s>=0 && s<11);activeSlot=s;}
static void rlEnableTexture(unsigned id) {textures[activeSlot]=id;}
static void rlDisableTexture(void) {textures[activeSlot]=0;}
static unsigned rlGetTextureIdDefault(void) {return 999;}
static Vector3 Environment_GetSunDirection(void) {return (Vector3){0,-1,0};}
static Color Environment_GetSunColor(void) {return WHITE;}
static Color Environment_GetAmbientColor(void) {return WHITE;}
static void SetShaderValue(Shader s,int loc,const void *p,int type) {
 assert(activeProgram==123 && s.id==123);
 if(loc==locGroundCloudNoise) {assert(type==SHADER_UNIFORM_INT);cloudSampler=*(const int *)p;}
 if(loc==locGroundTiling) {assert(type==SHADER_UNIFORM_VEC2);Vector2 t=*(const Vector2 *)p;
   assert(t.x==expectedTiling.x && t.y==expectedTiling.y);tilingUploads++;}
}
static void SetShaderValueV(Shader s,int l,const void *p,int t,int n) {(void)n;SetShaderValue(s,l,p,t);}
static void MapShadow_UpdateShader(Shader s) {assert(activeProgram==(int)s.id);}
static int GfxQuality_Get(void) {return GFX_HIGH;}
static int GetShaderLocation(Shader s,const char *n) {(void)s;(void)n;return 16;}
static void DrawModel(Model m,Vector3 pos,float scale,Color color) {
 (void)pos;(void)scale;(void)color;
 assert(activeProgram==123 && tilingUploads==drawCalls+1);
 assert(cloudSampler==GROUND_CLOUD_TEXTURE_SLOT && textures[cloudSampler]==(cloudFrame.noiseTexture.id?456:999));
 assert(activeSlot==0);
 for(int i=0;i<11;i++) assert(m.materials[0].maps[i].texture.id==expectedMaterialIds[i]);
 assert(m.materials[0].maps[MATERIAL_MAP_SPECULAR].texture.id==101);
 assert(m.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture.id==103);
 drawCalls++;
}
'''
MAIN = r'''
int main(void) {
 MaterialMap maps[11]={0};
 for(int i=0;i<11;i++) {maps[i].texture.id=100+i;expectedMaterialIds[i]=100+i;}
 Material material={maps};MapEcology ecology={true,{0,0,100,75}};
 MapGroundSurface g={true,{&material},{0,0,0},{27.777f,20.833f},&ecology};
 expectedTiling=g.tiling;MapProp_DrawGround(&g,(Vector3){50,0,37.5f});
 assert(!activeProgram && activeSlot==0 && textures[GROUND_CLOUD_TEXTURE_SLOT]==0);
 g.tiling=(Vector2){7,9};expectedTiling=g.tiling;cloudFrame.noiseTexture.id=0;
 MapProp_DrawGround(&g,(Vector3){0});assert(drawCalls==2 && tilingUploads==2);
 assert(!activeProgram && activeSlot==0 && textures[GROUND_CLOUD_TEXTURE_SLOT]==0);
 g.ready=false;MapProp_DrawGround(&g,(Vector3){0});assert(drawCalls==2);
 puts("ground: alias-safe terrain textures, reserved cloud unit, active per-ground tiling and cleanup passed");
}
'''
with tempfile.TemporaryDirectory(prefix='wuxing-ground-binding-') as directory:
    work = pathlib.Path(directory)
    prefix = '#define GROUND_CLOUD_TEXTURE_SLOT '+cloud_slot+'\n'+STUBS
    def compile_and_run(body, name):
        c = work/(name+'.c')
        exe = work/name
        c.write_text(prefix+body+MAIN)
        subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
        return subprocess.run([str(exe)],capture_output=True,text=True)
    result = compile_and_run(draw,'ground')
    if result.returncode:
        raise RuntimeError(result.stderr)
    print(result.stdout.strip())
    # Verify sensitivity to the actual former regression using real alias values.
    bad = draw.replace('EnvCloudShadowFrame cloud = Environment_GetCloudShadowFrame();',
                       'EnvCloudShadowFrame cloud = Environment_GetCloudShadowFrame();\n'
                       'ground->model.materials[0].maps[MATERIAL_MAP_METALNESS].texture = cloud.noiseTexture;')
    result = compile_and_run(bad,'bad_alias')
    assert result.returncode != 0, 'Regression test failed to reject METALNESS overwriting SPECULAR'
    print('ground: former METALNESS cloud-overwrite mutation rejected')
