#!/usr/bin/env python3
"""Compile the production authoring/template functions with capture-only rlgl stubs.

No GPU or game build required. --raylib-include points to external reference
headers; source extraction avoids linking unrelated map rendering systems.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r"^static[^\n]*\b" + name + r"\(", source, re.M)
    if not match:
        raise ValueError(f"Production function missing: {name}")
    opening = source.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def typedef(source, name):
    return re.search(r"typedef struct \{[^}]*\} " + name + r";", source).group()


PREAMBLE = r'''
#include "maps/toolkit/map_props.h"
#define RAYMATH_STATIC_INLINE
#include "raymath.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define RL_FLOAT 0
static bool vaoBound;
static Vector3 capturedVertices[230];
static unsigned short capturedIndices[330];
static int capturedVertexCount,capturedIndexCount;
static unsigned int rlLoadVertexArray(void) { vaoBound=false; return 1; }
static bool rlEnableVertexArray(unsigned int id) { vaoBound=id!=0; return vaoBound; }
static void rlDisableVertexArray(void) { vaoBound=false; }
static unsigned int rlLoadVertexBuffer(const void *p,int bytes,bool dynamic) {
    (void)dynamic; assert(vaoBound); assert(bytes<=(int)sizeof(capturedVertices));
    memcpy(capturedVertices,p,bytes); capturedVertexCount=bytes/sizeof(Vector3); return 2;
}
static unsigned int rlLoadVertexBufferElement(const void *p,int bytes,bool dynamic) {
    (void)dynamic; assert(vaoBound); assert(bytes<=(int)sizeof(capturedIndices));
    memcpy(capturedIndices,p,bytes); capturedIndexCount=bytes/sizeof(unsigned short); return 3;
}
static void rlSetVertexAttribute(unsigned int slot,int count,int type,bool normalized,int stride,int offset) {
    assert(vaoBound && slot==0 && count==3 && type==RL_FLOAT && !normalized);
    assert(stride==(int)sizeof(Vector3) && offset==0);
}
static void rlEnableVertexAttribute(unsigned int slot) { assert(vaoBound && slot==0); }
'''

LIFECYCLE = r'''

static int allocationCall, failAllocation, liveAllocations, liveTextures;
static int s_natureTuftLodBandsLoc, s_natureTuftLodLevelLoc, s_natureTuftLodCameraLoc;
void *MemAlloc(unsigned int bytes) {
    if(++allocationCall==failAllocation) return NULL;
    void *p=malloc(bytes); if(p) liveAllocations++; return p;
}
void MemFree(void *p) { if(p) { liveAllocations--; free(p); } }
static int GfxQuality_Get(void) { return 2; }
#define GFX_HIGH 2
static Shader NatureParametric_Shader(bool shadow) {
    (void)shadow; static int locs[32]; return (Shader){.id=42,.locs=locs};
}
static unsigned int rlGetShaderIdDefault(void) { return 99; }
int GetShaderLocation(Shader shader,const char *name) { (void)shader;(void)name; return 0; }
void TraceLog(int level,const char *text,...) { (void)level;(void)text; }
static int expectedTufts;
Texture2D LoadTextureFromImage(Image image) {
    assert(image.width==1792 && image.height<=2048);
    const float *pixels=image.data;
    int descriptors=0;
    for(int texel=0;texel<image.width*image.height;texel+=7)
        if(pixels[texel*4+3]>0) descriptors++;
    // Every source root is retained, including minX=.01 rounding-sensitive root.
    assert(descriptors==expectedTufts*5 || descriptors==expectedTufts*4 || descriptors==expectedTufts*3);
    liveTextures++;
    return (Texture2D){.id=(unsigned int)liveTextures,.width=image.width,.height=image.height};
}
void UnloadTexture(Texture2D texture) { assert(texture.id); liveTextures--; }
void SetTextureFilter(Texture2D texture,int mode) { (void)texture;(void)mode; }
void SetTextureWrap(Texture2D texture,int mode) { (void)texture;(void)mode; }
static void rlUnloadVertexArray(unsigned int id) { (void)id; }
static void rlUnloadVertexBuffer(unsigned int id) { (void)id; }
'''

TEST = r'''
static void nearVector(Vector3 a,Vector3 b) {
    assert(Vector3Distance(a,b)<0.00002f);
}
// Independent De Casteljau oracle, compared with production's cubic polynomial.
static Vector3 oracleCurve(NatureBladeDescriptor d,float t) {
    Vector3 a=Vector3Lerp(d.p0,d.p1,t), b=Vector3Lerp(d.p1,d.p2,t), c=Vector3Lerp(d.p2,d.p3,t);
    return Vector3Lerp(Vector3Lerp(a,b,t),Vector3Lerp(b,c,t),t);
}
static Vector3 zeroWindPosition(NatureBladeDescriptor d,Vector3 v,bool oracle) {
    float t=v.y, role=(v.z-floorf(v.z))*4.0f;
    Vector3 center=oracle ? oracleCurve(d,t) : Nature_EvalCubicBezier(d.p0,d.p1,d.p2,d.p3,t);
    if(role>2.5f) return center;
    Vector3 tangent=Vector3Normalize(Nature_EvalCubicBezierTangent(d.p0,d.p1,d.p2,d.p3,t));
    Vector3 side=Vector3CrossProduct(tangent,(Vector3){0,1,0});
    side=Vector3Length(side)>0.001f ? Vector3Normalize(side) : (Vector3){-sinf(d.leanAngle),0,cosf(d.leanAngle)};
    Vector3 n=Vector3Normalize(Vector3CrossProduct(side,tangent));
    if(n.y<0) n=Vector3Negate(n);
    n.y=fmaxf(n.y,0.25f); n=Vector3Normalize(n);
    float profile=d.isReed ? (0.85f+0.25f*sinf(PI*t))*(1-powf(t,1.8f)) : (0.72f+0.50f*t)*(1-t*t);
    float width=d.width*0.5f*fmaxf(profile,role<1.5f ? 0.08f : 0.03f);
    return Vector3Add(Vector3Add(center,Vector3Scale(side,width*v.x)),Vector3Scale(n,width*0.18f));
}
int main(void) {
    MapMeadowStyle style={.rootColor={22,48,17,255},.tipColor={119,174,61,255},
        .bladeWidthScale=0.19f};
    int cases=0;
    for(int blades=1;blades<=10;blades++) for(int segments=1;segments<=6;segments++) {
        NatureTuftTemplate mesh={0};
        assert(NatureParametric_BuildTemplate(&mesh,blades,segments));
        assert(!vaoBound);
        assert(capturedVertexCount==blades*((segments-1)*4+3));
        assert(capturedIndexCount==blades*((segments-1)*6+3));
        assert(mesh.blades==blades && mesh.segments==segments && mesh.indexCount==capturedIndexCount);
        style.bladesPerClump=blades; style.bladeSegments=segments;
        for(int reed=0;reed<2;reed++) {
            MapMeadowPlacement p={.position={12.3f,0.16f,7.9f},.radius=0.18f,
                .height=reed ? 1.1f : 0.6f,.rotationDeg=87.0f,.phase=1.2f};
            int index=0;
            for(int blade=0;blade<blades;blade++) {
                NatureBladeDescriptor d=Nature_DescribeMeadowBlade(&p,23,blade,style,blades,segments,1.0f);
                assert(d.width>0 && d.phase==p.phase && d.p0.y==p.position.y);
                assert(d.isReed==(reed!=0));
                for(int segment=0;segment<segments;segment++) {
                    float tipStart=segments>1 ? 0.8f : 0;
                    float t0=segment==segments-1 ? tipStart : tipStart*segment/(segments-1);
                    float t1=segment==segments-1 ? 1 : tipStart*(segment+1)/(segments-1);
                    Vector3 expected[6]={{-1,t0,blade+0.25f},{1,t0,blade+0.25f},{1,t1,blade+0.5f},
                        {-1,t0,blade+0.25f},{1,t1,blade+0.5f},{-1,t1,blade+0.5f}};
                    int n=6;
                    if(segment==segments-1) { n=3; expected[2]=(Vector3){0,1,blade+0.75f}; }
                    for(int corner=0;corner<n;corner++) {
                        unsigned short vertex=capturedIndices[index++];
                        assert(vertex<capturedVertexCount);
                        nearVector(capturedVertices[vertex],expected[corner]);
                        nearVector(zeroWindPosition(d,capturedVertices[vertex],false),zeroWindPosition(d,expected[corner],true));
                    }
                }
            }
            assert(index==capturedIndexCount); cases++;
        }
    }
    // Nested LOD botanical identities and original width compensation.
    style.bladesPerClump=5; style.bladeSegments=3;
    MapMeadowPlacement p={.position={3,0,4},.height=0.7f,.radius=0.2f,.rotationDeg=40};
    for(int blade=0;blade<4;blade++) {
        int stable=blade*4/3;
        NatureBladeDescriptor near=Nature_DescribeMeadowBlade(&p,19,stable,style,5,3,1);
        NatureBladeDescriptor mid=Nature_DescribeMeadowBlade(&p,19,blade,style,4,2,1.22f);
        nearVector(near.p0,mid.p0); nearVector(near.p3,mid.p3);
        assert(fabsf(mid.width/near.width-1.22f)<0.00001f);
    }

    // Production creation/destruction and every CPU allocation failure boundary.
    MapMeadowPlacement roots[3]={
        {.position={0.01f,0,0.01f},.height=.6f,.radius=.2f,.phase=1},
        {.position={12.01f,0,0.01f},.height=.7f,.radius=.2f,.phase=2},
        {.position={24.01f,0,0.01f},.height=.8f,.radius=.2f,.phase=3}};
    style.chunkSize=12; style.shadowDistance=20; expectedTufts=3;
    MapMeadowSurface meadow={0}; allocationCall=0;
    int *lodLocations[]={&s_natureTuftLodBandsLoc,&s_natureTuftLodLevelLoc,&s_natureTuftLodCameraLoc};
    for(int i=0;i<3;i++) {
        *lodLocations[i]=-1;
        assert(!NatureParametric_Create(&meadow,roots,3,style));
        assert(!allocationCall && !liveAllocations && !liveTextures);
        *lodLocations[i]=0;
    }
    assert(NatureParametric_Create(&meadow,roots,3,style));
    int allocations=allocationCall;
    NatureParametricMeadow *data=meadow.parametric;
    int tuftCount=0;
    for(int chunk=0;chunk<meadow.chunkCount;chunk++) tuftCount+=data->ranges[chunk].count;
    assert(tuftCount==3 && meadow.ready && data->shadow);
    NatureParametric_Destroy(&meadow); MemFree(meadow.chunks);
    assert(!meadow.parametric && !liveAllocations && !liveTextures);
    for(failAllocation=1;failAllocation<=allocations;failAllocation++) {
        memset(&meadow,0,sizeof(meadow)); allocationCall=0;
        assert(!NatureParametric_Create(&meadow,roots,3,style));
        assert(!meadow.parametric && !meadow.chunks && !meadow.ready);
        assert(!liveAllocations && !liveTextures);
    }
    failAllocation=0;
    memset(&meadow,0,sizeof(meadow)); style.hasPlumes=true;
    assert(!NatureParametric_Create(&meadow,roots,3,style));
    style.hasPlumes=false; style.texturePath="authored-cutout";
    assert(!NatureParametric_Create(&meadow,roots,3,style));
    style.texturePath=NULL;
    // Descriptor addressing exactly matches seven-texel packed rows, including the seam.
    const int ids[]={0,255,256,511,512,524287};
    for(unsigned int k=0;k<sizeof(ids)/sizeof(ids[0]);k++) for(int c=0;c<7;c++) {
        int id=ids[k], x=(id%256)*7+c,y=id/256;
        assert(x>=0 && x<1792 && y>=0 && y<2048);
        assert(y*1792+x==id*7+c);
    }
    printf("PASS: %d topology/zero-wind parity cases, stable LOD identities, explicit VAO binding, atlas seams, allocation rollback\n",cases);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--raylib-include", default="/tmp/rlvk_visual_cache/raylib/src")
    args = parser.parse_args()
    nature = (ROOT / "maps/toolkit/map_props_nature.inl").read_text()
    compact = (ROOT / "maps/toolkit/map_props_meadow_parametric.inl").read_text()
    source = PREAMBLE + "\n" + "\n".join(
        re.findall(r"^#define NATURE_[^\n]+", compact, re.M))
    source += "\n" + typedef(nature,"NatureBladeDescriptor") + "\n" + typedef(compact,"NatureTuftTemplate")
    for name in ("Nature_NextRandom","Nature_Random01","Nature_EvalCubicBezier","Nature_EvalCubicBezierTangent","Nature_DescribeMeadowBlade"):
        source += "\n" + function(nature,name)
    source += "\n" + function(compact,"NatureParametric_BuildTemplate")
    source += "\n" + typedef(compact,"NatureTuftRange") + "\n" + typedef(compact,"NatureParametricMeadow") + "\n" + LIFECYCLE
    source += "\n" + function(compact,"NatureParametric_Destroy") + "\n" + function(compact,"NatureParametric_Create") + "\n" + TEST
    with tempfile.TemporaryDirectory(prefix="meadow-parametric-") as temp:
        path = pathlib.Path(temp)
        (path / "test.c").write_text(source)
        subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror","-I",str(ROOT),"-I",args.raylib_include,
                        str(path / "test.c"),"-lm","-o",str(path / "test")],check=True)
        subprocess.run([str(path / "test")],check=True)


if __name__ == "__main__":
    main()
