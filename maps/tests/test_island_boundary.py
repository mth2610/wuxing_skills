#!/usr/bin/env python3
"""Execute perimeter math and static cloud mesh generation/lifetime."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
STUBS = r'''
#include "maps/toolkit/map_props.h"
#define RAYMATH_STATIC_INLINE
#include "raymath.h"
#include <math.h>
#include <float.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
static int live,allocCalls,failAt;
void *MemAlloc(unsigned int bytes){if(++allocCalls==failAt)return NULL;live++;return malloc(bytes);}
void MemFree(void *p){if(p){live--;free(p);}}
void TraceLog(int level,const char *format,...){(void)level;(void)format;}
void UploadMesh(Mesh *m,bool dynamic){
    assert(!dynamic && m->vertexCount>0 && m->vertexCount<65536);
    for(int i=0;i<m->vertexCount;i++){
        assert(isfinite(m->vertices[i*3]) && isfinite(m->vertices[i*3+1]) && isfinite(m->vertices[i*3+2]));
        assert(m->colors[i*4]==255 && m->colors[i*4+3]<=90);
    }
    for(int i=0;i<m->triangleCount*3;i++)assert(m->indices[i]<m->vertexCount);
}
Model LoadModelFromMesh(Mesh m){
    Model model={.meshCount=1,.materialCount=1};model.meshes=malloc(sizeof(Mesh));*model.meshes=m;
    model.materials=calloc(1,sizeof(Material));model.materials[0].maps=calloc(11,sizeof(MaterialMap));return model;
}
void UnloadModel(Model m){
    MemFree(m.meshes[0].vertices);MemFree(m.meshes[0].normals);MemFree(m.meshes[0].colors);
    MemFree(m.meshes[0].texcoords);MemFree(m.meshes[0].indices);
    free(m.meshes);free(m.materials[0].maps);free(m.materials);
}
'''
MAIN = r'''
int main(void){
    MapCloudSea cloud={.ready=true,.mistTexture={.id=23}};
    Vector3 concave[]={{0,0,0},{10,0,0},{10,0,4},{4,0,4},{4,0,10},{0,0,10}};
    Vector3 island[]={{20,0,0},{24,0,0},{24,0,4},{20,0,4},{20,0,0}};
    Vector3 hole[]={{1,0,1},{1,0,2},{2,0,2},{2,0,1}};
    MapBoundaryContour loops[]={{concave,6},{island,5},{hole,4}};
    MapBoundaryMistStyle style={1.2f,4,.5f,2,.35f};
    assert(MapProp_SetCloudSeaBoundaryContours(&cloud,loops,3,&style));
    assert(live==4 && cloud.mistReady && cloud.mistModel.meshes[0].vertexCount==28);
    assert(cloud.mistModel.materials[0].maps[0].texture.id==23);
    Mesh *old=cloud.mistModel.meshes;
    for(int failure=1;failure<=4;failure++){
        allocCalls=0;failAt=failure;
        assert(!MapProp_SetCloudSeaBoundaryContours(&cloud,loops,3,&style));
        assert(live==4 && cloud.mistModel.meshes==old);
    }
    failAt=0;style.outerWidth=NAN;
    assert(!MapProp_SetCloudSeaBoundaryContours(&cloud,loops,3,&style));
    style.outerWidth=FLT_MAX;
    assert(!MapProp_SetCloudSeaBoundaryContours(&cloud,loops,3,&style));
    assert(live==4 && cloud.mistModel.meshes==old);
    style.outerWidth=4;concave[0].x=NAN;
    assert(!MapProp_SetCloudSeaBoundaryContours(&cloud,loops,3,&style));concave[0].x=0;
    // Actual terrain extraction: outer island plus an interior lake depression.
    float verts[7*7*3];unsigned short indices[6*6*6];int k=0;
    for(int z=0;z<7;z++)for(int x=0;x<7;x++){
        int v=(z*7+x)*3;verts[v]=x;verts[v+2]=z;
        verts[v+1]=(x==0||z==0||x==6||z==6)?-2:((x==3&&z==3)?-1.5f:0);
    }
    for(int z=0;z<6;z++)for(int x=0;x<6;x++){
        int a=z*7+x,b=a+7,c=b+1,d=a+1;
        indices[k++]=a;indices[k++]=b;indices[k++]=d;
        indices[k++]=d;indices[k++]=b;indices[k++]=c;
    }
    Mesh terrain={.vertexCount=49,.triangleCount=72,.vertices=verts,.indices=indices};
    MapGroundSurface ground={.ready=true,.model={.meshCount=1,.meshes=&terrain},.drawOffset={25,0,-12}};
    assert(MapProp_SetCloudSeaGroundBoundary(&cloud,&ground,-.35f,&style));
    assert(live==4);Mesh indexed=*cloud.mistModel.meshes;
    // Lake vertices cannot enter the baked outer rim.
    for(int i=0;i<indexed.vertexCount;i+=2){
        float x=(indexed.vertices[i*3]*4+indexed.vertices[(i+1)*3]*1.2f)/5.2f-25;
        float z=(indexed.vertices[i*3+2]*4+indexed.vertices[(i+1)*3+2]*1.2f)/5.2f+12;
        assert(x<2||x>4||z<2||z>4);
    }
    // Duplicated vertices produce the identical contour (unindexed Raylib meshes).
    float duplicated[72*9];for(int i=0;i<72*3;i++)for(int j=0;j<3;j++)duplicated[i*3+j]=verts[indices[i]*3+j];
    terrain.vertices=duplicated;terrain.indices=NULL;terrain.vertexCount=216;
    int oldCount=indexed.vertexCount;
    assert(MapProp_SetCloudSeaGroundBoundary(&cloud,&ground,-.35f,&style));
    assert(live==4 && cloud.mistModel.meshes[0].vertexCount==oldCount);
    for(int failure=1;failure<=8;failure++){
        old=cloud.mistModel.meshes;allocCalls=0;failAt=failure;
        assert(!MapProp_SetCloudSeaGroundBoundary(&cloud,&ground,-.35f,&style));
        assert(live==4 && cloud.mistModel.meshes==old);
    }
    failAt=0;assert(MapProp_SetCloudSeaBoundaryContours(&cloud,NULL,0,NULL));
    assert(live==0 && !cloud.mistReady);
    puts("PASS: concave/multiple/hole contours; indexed/unindexed terrain; lake exclusion; finite indexed bake; failure recovery and lifetime");
}
'''
parser = argparse.ArgumentParser()
parser.add_argument('--raylib-include', default='/tmp/rlvk_check_cache')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='island-boundary-') as directory:
    source=Path(directory)/'test.c';exe=Path(directory)/'test'
    source.write_text(STUBS+(ROOT/'maps/toolkit/map_props_boundary_contour.inl').read_text()+MAIN)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I',str(ROOT),'-I',args.raylib_include,
                    str(source),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
