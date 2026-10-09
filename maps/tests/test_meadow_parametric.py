#!/usr/bin/env python3
"""Compile the production authoring/template functions with capture-only rlgl stubs.

No GPU or game build required. --raylib-include points to external reference
headers; source extraction avoids linking unrelated map rendering systems.
"""
import argparse
import pathlib
import re
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r"^(?:static|void)[^\n]*\b" + name + r"\(", source, re.M)
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
#include "maps/toolkit/meadow_palette.h"
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
static int s_natureTuftLodBandsLoc, s_natureTuftLodLevelLoc, s_natureTuftLodCameraLoc, s_natureTuftFadeRangeLoc;
static int s_natureCanonicalBladesLoc[2],s_natureCanonicalLoc[2],s_natureGeometryLodLoc[2];
static int s_natureTuftOffsetLoc[2],s_natureCompactLoc[2],s_natureVisibleIdsLoc,s_natureVisibleOffsetLoc;
static int s_natureWorldOffsetLoc[2];
static const char *testSubmission;
static const char *testOrder;
static const char *TestGetenv(const char *name) {
    if (strcmp(name,"WUXING_MEADOW_SUBMISSION")==0) return testSubmission;
    if (strcmp(name,"WUXING_MEADOW_ORDER")==0) return testOrder;
    return NULL;
}
#define getenv TestGetenv
void *MemAlloc(unsigned int bytes) {
    if(++allocationCall==failAllocation) return NULL;
    void *p=malloc(bytes); if(p) liveAllocations++; return p;
}
void MemFree(void *p) { if(p) { liveAllocations--; free(p); } }
static int GfxQuality_Get(void) { return 2; }
#define GFX_HIGH 2
#define GFX_MED 1
#define GFX_LOW 0
static Shader NatureParametric_Shader(bool shadow) {
    (void)shadow; static int locs[32]; return (Shader){.id=42,.locs=locs};
}
static unsigned int rlGetShaderIdDefault(void) { return 99; }
int GetShaderLocation(Shader shader,const char *name) { (void)shader;(void)name; return 0; }
void TraceLog(int level,const char *text,...) { (void)level;(void)text; }
static int expectedTufts;
static Camera3D camera;
static int testScreenWidth=1280,testScreenHeight=720;
int GetScreenWidth(void) {return testScreenWidth;}
int GetScreenHeight(void) {return testScreenHeight;}
static float capturedAtlas[1792*4*4];
static int capturedAtlasFloats;
static int idUploads;
void UpdateTexture(Texture2D texture,const void *pixels) {
    assert(texture.width==256); (void)pixels; idUploads++;
}
Texture2D LoadTextureFromImage(Image image) {
    assert((image.width==1792 || image.width==256) && image.height<=2048);
    if (image.width==256) { liveTextures++; return (Texture2D){.id=(unsigned int)liveTextures,.width=image.width,.height=image.height}; }
    const float *pixels=image.data;
    capturedAtlasFloats=image.width*image.height*4;
    assert(capturedAtlasFloats<=(int)(sizeof(capturedAtlas)/sizeof(capturedAtlas[0])));
    memcpy(capturedAtlas,pixels,capturedAtlasFloats*sizeof(float));
    int descriptors=0;
    for(int texel=0;texel<image.width*image.height;texel+=7)
        if(pixels[texel*4+3]>0) descriptors++;
    // Every source root is retained, including minX=.01 rounding-sensitive root.
    assert(descriptors==expectedTufts*5 || descriptors==expectedTufts*15);
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
        style.botanicalVariation=(float)(blades%3)*0.5f;
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

    // Execute production GLSL canonical reconstruction against independent
    // CPU-authoring LOD descriptors, preserving far and shadow silhouettes.
    const int parityBlades[4]={5,4,3,3},paritySegments[4]={3,2,1,2};
    const float parityWidths[4]={1,1.22f,1.65f,2.10f};
    for(int form=0;form<2;form++) for(int sample=0;sample<64;sample++) for(int lod=0;lod<4;lod++) for(int blade=0;blade<parityBlades[lod];blade++) {
        style.growthForm=form ? MAP_MEADOW_GROWTH_GRASS : MAP_MEADOW_GROWTH_AUTO;
        style.botanicalVariation=(float)(sample%3)*0.5f;
        MapMeadowPlacement root={.position={3.19f+sample*.27f,.13f,5.71f},.height=(form ? 1.15f : .3f)+(sample%13)*.043f,
            .radius=.13f+(sample%7)*.012f,.rotationDeg=sample*17.3f,.phase=sample*.03f};
        int botanical=blade*(5-1)/(parityBlades[lod]-1);
        NatureBladeDescriptor base=Nature_DescribeMeadowBlade(&root,sample,botanical,style,5,3,1);
        assert(!base.isReed);
        NatureBladeDescriptor reconstructed=ReconstructCanonical(base,lod);
        NatureBladeDescriptor authored=Nature_DescribeMeadowBlade(&root,sample,blade,style,parityBlades[lod],paritySegments[lod],parityWidths[lod]);
        nearVector(reconstructed.p0,authored.p0);nearVector(reconstructed.p1,authored.p1);
        nearVector(reconstructed.p2,authored.p2);nearVector(reconstructed.p3,authored.p3);
        assert(fabsf(reconstructed.width-authored.width)<.000002f);
        assert(!memcmp(&reconstructed.rootColor,&authored.rootColor,sizeof(Color)));
        assert(!memcmp(&reconstructed.tipColor,&authored.tipColor,sizeof(Color)));
    }

    style.growthForm=MAP_MEADOW_GROWTH_AUTO;

    // Production creation/destruction and every CPU allocation failure boundary.
    MapMeadowPlacement roots[3]={
        {.position={0.01f,0,0.01f},.height=.6f,.radius=.2f,.phase=1},
        {.position={12.01f,0,0.01f},.height=.7f,.radius=.2f,.phase=2},
        {.position={24.01f,0,0.01f},.height=.8f,.radius=.2f,.phase=3}};
    style.chunkSize=12; style.shadowDistance=20; expectedTufts=3;
    MapMeadowSurface meadow={0}; allocationCall=0;
    int *lodLocations[]={&s_natureTuftLodBandsLoc,&s_natureTuftLodLevelLoc,&s_natureTuftLodCameraLoc,
        &s_natureWorldOffsetLoc[0],&s_natureWorldOffsetLoc[1]};
    for(int i=0;i<5;i++) {
        *lodLocations[i]=-1;
        assert(!NatureParametric_Create(&meadow,roots,3,style));
        assert(!allocationCall && !liveAllocations && !liveTextures);
        *lodLocations[i]=0;
    }
    // Default retains GPU rejection and the shared atlas, with no ID uploads.
    assert(NatureParametric_Create(&meadow,roots,3,style));
    NatureParametricMeadow *defaultData=meadow.parametric;
    assert(defaultData->canonical && !defaultData->compact && !defaultData->nearFirst);
    MapProp_PrepareMeadow(&meadow,(Vector3){0});
    assert(!idUploads && !defaultData->prepared);
    NatureParametric_Destroy(&meadow);MemFree(meadow.chunks);
    assert(!liveAllocations && !liveTextures);
    memset(&meadow,0,sizeof(meadow));allocationCall=0;
    testSubmission="compact";
    testOrder="sorted";
    assert(NatureParametric_Create(&meadow,roots,3,style));
    int allocations=allocationCall;
    NatureParametricMeadow *data=meadow.parametric;
    int tuftCount=0;
    for(int chunk=0;chunk<meadow.chunkCount;chunk++) tuftCount+=data->ranges[chunk].count;
    assert(tuftCount==3 && meadow.ready && data->shadow && data->canonical && data->compact);
    assert(liveTextures==2);
    for(int c=0;c<meadow.chunkCount;c++) meadow.chunks[c].visibleThisFrame=true;
    camera.position=(Vector3){0,2,0};
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},(Vector4){9,23,2,5});
    int seen[3]={0};int submitted=0;
    for(int c=0;c<meadow.chunkCount;c++) for(int lod=0;lod<3;lod++) {
        NatureTuftRange *range=&data->ranges[c];
        for(int j=0;j<range->visibleCount[lod];j++) {
            int id=(int)data->idPixels[range->visibleOffset[lod]+j];
            assert(id>=0 && id<3 && !seen[id]);seen[id]++;submitted++;
            assert(NatureParametric_SelectLod(data->roots[id],data->ranks[id],camera.position,(Vector4){9,23,2,5})==lod);
        }
    }
    assert(submitted==3 && idUploads==1);
    // Execute stable near-first production ordering, retaining the same IDs.
    camera.position=(Vector3){40,3,2};camera.target=(Vector3){0,0,2};
    data->prepared=false;
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},(Vector4){9,23,2,5});
    assert(data->drawCount==3 && data->drawOrder[0]==2 && data->drawOrder[1]==1 && data->drawOrder[2]==0);
    for(int i=1;i<data->drawCount;i++) assert(data->drawDepth[i]>=data->drawDepth[i-1]);
    memset(seen,0,sizeof(seen));
    for(int i=0;i<3;i++) {int id=(int)data->idPixels[i];assert(id>=0 && id<3 && !seen[id]);seen[id]++;}
    int sortedUploads=idUploads;
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},(Vector4){9,23,2,5});
    assert(idUploads==sortedUploads);
    camera.projection=CAMERA_ORTHOGRAPHIC;data->prepared=false;
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},(Vector4){9,23,2,5});
    assert(data->drawOrder[0]==2 && data->drawOrder[2]==0);
    // Source order survives equal depths and the explicit legacy-order path.
    camera.position=(Vector3){0,2,0};camera.target=(Vector3){0,0,0};data->prepared=false;
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},(Vector4){9,23,2,5});
    assert(data->drawOrder[0]==0 && data->drawOrder[1]==1 && data->drawOrder[2]==2);
    data->nearFirst=false;camera.position=(Vector3){40,3,2};camera.target=(Vector3){0,0,2};data->prepared=false;
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},(Vector4){9,23,2,5});
    assert(data->drawOrder[0]==0 && data->drawOrder[2]==2);
    // GPU-selected legacy submissions can also sort chunks without ID uploads.
    data->nearFirst=true;data->compact=false;data->prepared=false;sortedUploads=idUploads;
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},(Vector4){9,23,2,5});
    assert(data->drawOrder[0]==2 && data->drawOrder[2]==0 && idUploads==sortedUploads);
    data->compact=true;data->prepared=false;camera.projection=CAMERA_PERSPECTIVE;
    camera.position=(Vector3){0,2,0};camera.target=(Vector3){0,0,0};
    int uploadsBeforeCull=idUploads;
    meadow.chunks[1].visibleThisFrame=false;
    data->prepared=false;
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},(Vector4){9,23,2,5});
    int culledCount=0;
    for(int c=0;c<meadow.chunkCount;c++) for(int lod=0;lod<3;lod++) culledCount+=data->ranges[c].visibleCount[lod];
    assert(culledCount==2 && idUploads==uploadsBeforeCull+1);
    // Stage before the render passes; the same production helper called by
    // DrawMeadow must reuse the staged list without another texture upload.
    camera.fovy=45;camera.up=(Vector3){0,1,0};camera.target=(Vector3){12,0,0};
    data->prepared=false;
    MapProp_PrepareMeadow(&meadow,(Vector3){0});
    int preparedUploads=idUploads;
    NatureMeadowView frameView=NatureParametric_View(&meadow);
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},frameView.bands);
    assert(idUploads==preparedUploads);
    camera.position.x+=.3f;
    assert(!NatureParametric_IsPrepared(&meadow,(Vector3){0},frameView.bands));
    NatureParametric_PrepareVisible(&meadow,(Vector3){0},frameView.bands);
    assert(idUploads==preparedUploads+1);
    testScreenWidth++;
    assert(!NatureParametric_IsPrepared(&meadow,(Vector3){0},frameView.bands));
    MapProp_PrepareMeadow(&meadow,(Vector3){0});
    assert(idUploads==preparedUploads+2);
    assert(!NatureParametric_IsPrepared(&meadow,(Vector3){1,0,0},frameView.bands));
    camera.projection=CAMERA_ORTHOGRAPHIC;
    assert(!NatureParametric_IsPrepared(&meadow,(Vector3){0},frameView.bands));
    camera.projection=CAMERA_PERSPECTIVE;
    // The descriptor atlas and independent shadow roots cannot change when
    // camera compaction reorders the visible ID texture.
    for(int c=0;c<meadow.chunkCount;c++) {
        int offset=data->ranges[c].offset[3]*28;
        NatureBladeDescriptor desc=Nature_DescribeMeadowBlade(&roots[c],c,0,style,5,3,1);
        nearVector((Vector3){capturedAtlas[offset],capturedAtlas[offset+1],capturedAtlas[offset+2]},desc.p0);
    }
    for(int c=0;c<meadow.chunkCount;c++) for(int lod=1;lod<4;lod++)
        assert(data->ranges[c].offset[lod]==data->ranges[c].offset[0]);
    NatureParametric_Destroy(&meadow); MemFree(meadow.chunks);
    assert(!meadow.parametric && !liveAllocations && !liveTextures);
    for(failAllocation=1;failAllocation<=allocations;failAllocation++) {
        memset(&meadow,0,sizeof(meadow)); allocationCall=0;
        assert(!NatureParametric_Create(&meadow,roots,3,style));
        assert(!meadow.parametric && !meadow.chunks && !meadow.ready);
        assert(!liveAllocations && !liveTextures);
    }
    failAllocation=0;
    // Explicit tall grass retains one canonical atlas for all visible/shadow LODs.
    roots[0].height=1.6f;
    style.growthForm=MAP_MEADOW_GROWTH_GRASS;
    memset(&meadow,0,sizeof(meadow));
    assert(NatureParametric_Create(&meadow,roots,3,style));
    data=meadow.parametric;
    assert(data->canonical && liveTextures==2);
    for(int c=0;c<meadow.chunkCount;c++) {
        NatureBladeDescriptor desc=Nature_DescribeMeadowBlade(&roots[c],c,0,style,5,3,1);
        assert(!desc.isReed);
        int offset=data->ranges[c].offset[0]*28;
        assert(capturedAtlas[offset+11]==0.0f);
        for(int lod=1;lod<4;lod++)
            assert(data->ranges[c].offset[lod]==data->ranges[c].offset[0]);
    }
    NatureParametric_Destroy(&meadow);MemFree(meadow.chunks);
    assert(!liveAllocations && !liveTextures);
    // AUTO preserves historical height-based reed selection and exact variants.
    style.growthForm=MAP_MEADOW_GROWTH_AUTO;
    roots[0].height=1.1f;
    memset(&meadow,0,sizeof(meadow));
    assert(NatureParametric_Create(&meadow,roots,3,style));
    data=meadow.parametric;
    assert(!data->canonical && liveTextures==2);
    const int lodBlades[4]={5,4,3,3},lodSegments[4]={3,2,1,2};
    const float lodWidths[4]={1,1.22f,1.65f,1.30f};
    for(int lod=0;lod<4;lod++) for(int c=0;c<meadow.chunkCount;c++) for(int b=0;b<lodBlades[lod];b++) {
        NatureBladeDescriptor desc=Nature_DescribeMeadowBlade(&roots[c],c,b,style,lodBlades[lod],lodSegments[lod],lodWidths[lod]);
        assert(desc.isReed==(roots[c].height>0.95f));
        int offset=(data->ranges[c].offset[lod]+b)*28;
        nearVector((Vector3){capturedAtlas[offset],capturedAtlas[offset+1],capturedAtlas[offset+2]},desc.p0);
        nearVector((Vector3){capturedAtlas[offset+12],capturedAtlas[offset+13],capturedAtlas[offset+14]},desc.p3);
        assert(capturedAtlas[offset+7]==desc.width);
    }
    NatureParametric_Destroy(&meadow);MemFree(meadow.chunks);
    assert(!liveAllocations && !liveTextures);
    // Explicit reeds retain reed morphology even below the AUTO height threshold.
    style.growthForm=MAP_MEADOW_GROWTH_REED;
    roots[0].height=.5f;
    memset(&meadow,0,sizeof(meadow));
    assert(NatureParametric_Create(&meadow,roots,3,style));
    data=meadow.parametric;
    assert(!data->canonical && liveTextures==2);
    for(int lod=0;lod<4;lod++) for(int c=0;c<meadow.chunkCount;c++) {
        NatureBladeDescriptor desc=Nature_DescribeMeadowBlade(&roots[c],c,0,style,lodBlades[lod],lodSegments[lod],lodWidths[lod]);
        assert(desc.isReed);
        int offset=data->ranges[c].offset[lod]*28;
        assert(capturedAtlas[offset+11]==1.0f);
        nearVector((Vector3){capturedAtlas[offset+12],capturedAtlas[offset+13],capturedAtlas[offset+14]},desc.p3);
    }
    NatureParametric_Destroy(&meadow);MemFree(meadow.chunks);
    assert(!liveAllocations && !liveTextures);
    style.growthForm=MAP_MEADOW_GROWTH_AUTO;
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
    for name in ("Nature_NextRandom","Nature_Random01","Nature_LerpColor","Nature_EvalCubicBezier","Nature_EvalCubicBezierTangent","Nature_DescribeMeadowBlade"):
        source += "\n" + function(nature,name)
    for name in ("NatureParametric_Rank","NatureParametric_Smoothstep","NatureParametric_SelectLod","NatureParametric_LodIntersectsSphere"):
        source += "\n" + function(compact,name)
    source += "\n" + function(compact,"NatureParametric_BuildTemplate")
    source += "\n" + typedef(compact,"NatureTuftRange") + "\n" + typedef(compact,"NatureParametricMeadow") + "\n" + LIFECYCLE
    source += "\n" + typedef(compact,"NatureMeadowView")
    for name in ("NatureParametric_View","NatureParametric_SameVector","NatureParametric_IsPrepared"):
        source += "\n" + function(compact,name)
    shader_source = (ROOT / "maps/toolkit/shaders/nature_parametric.glsl").read_text()
    signature = "void NatureApplyCanonicalLod("
    start = shader_source.index(signature)
    opening = shader_source.index("{", start)
    depth, end = 1, opening+1
    while depth:
        depth += (shader_source[end] == "{") - (shader_source[end] == "}")
        end += 1
    body = shader_source[opening+1:end-1].replace("u_canonicalBladeData", "1").replace("u_geometryLod", "lod").replace("max(", "fmaxf(")
    source += "\n" + r"""
static NatureBladeDescriptor ReconstructCanonical(NatureBladeDescriptor d,int lod) {
    Vector4 p0={d.p0.x,d.p0.y,d.p0.z,d.phase},p1={d.p1.x,d.p1.y,d.p1.z,d.width};
    Vector4 p2={d.p2.x,d.p2.y,d.p2.z,0},p3={d.p3.x,d.p3.y,d.p3.z,0};
""" + body + r"""
    d.p1=(Vector3){p1.x,p1.y,p1.z};d.p2=(Vector3){p2.x,p2.y,p2.z};d.p3=(Vector3){p3.x,p3.y,p3.z};d.width=p1.w;return d;
}
"""
    source += "\n" + function(compact,"NatureParametric_Destroy") + "\n" + function(compact,"NatureParametric_Create") + "\n" + function(compact,"NatureParametric_PrepareVisible") + "\n" + function(nature,"Nature_IsChunkVisible") + "\n" + function(nature,"MapProp_PrepareMeadow") + "\n" + TEST
    with tempfile.TemporaryDirectory(prefix="meadow-parametric-") as temp:
        path = pathlib.Path(temp)
        (path / "test.c").write_text(source)
        subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror","-I",str(ROOT),"-I",args.raylib_include,
                        str(path / "test.c"),"-lm","-o",str(path / "test")],check=True)
        subprocess.run([str(path / "test")],check=True)


def validate_shaders():
    validator = shutil.which("glslangValidator")
    if not validator:
        raise RuntimeError("glslangValidator is required for desktop/GLES meadow validation")

    def expand(path):
        return re.sub(r'^\s*#include "([^"]+)"\s*$',
                      lambda match: expand(ROOT / match.group(1)), path.read_text(), flags=re.M)

    with tempfile.TemporaryDirectory(prefix="meadow-shaders-") as directory:
        work = pathlib.Path(directory)
        for version in ("330", "300 es"):
            for name, stage in (("nature_lit_parametric.vs", "vert"),
                                ("nature_shadow_parametric.vs", "vert"),
                                ("nature_opaque.fs", "frag"), ("nature_shadow.fs", "frag")):
                text = expand(ROOT / "maps/toolkit/shaders" / name)
                text = re.sub(r'^#version[^\n]+', '#version '+version, text, count=1)
                if version.endswith("es"):
                    text = text.replace("#version 300 es", "#version 300 es\nprecision highp float;\nprecision highp int;", 1)
                shader = work / name
                shader.write_text(text)
                subprocess.run([validator, "-S", stage, str(shader)], check=True)
    print("PASS: production visible/shadow vertex/fragment shaders compile GLSL330 and GLES300")


if __name__ == "__main__":
    main()
    validate_shaders()
