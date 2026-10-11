"""Compile production heightfield generation with CPU-only allocation stubs."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
STUB=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef struct { float x,y,z; } Vector3;
typedef struct { unsigned char r,g,b,a; } Color;
typedef struct { void *data; int width,height; } Image;
typedef struct { int vertexCount,triangleCount; float *vertices,*normals,*texcoords; unsigned short *indices; } Mesh;
#define MemAlloc malloc
#define MemFree free
static Color *LoadImageColors(Image im) { Color *p=malloc(im.width*im.height*sizeof(Color));memcpy(p,im.data,im.width*im.height*sizeof(Color));return p; }
static void UnloadImageColors(Color *p) {free(p);}
static Vector3 Vector3Subtract(Vector3 a,Vector3 b) {return (Vector3){a.x-b.x,a.y-b.y,a.z-b.z};}
static Vector3 Vector3CrossProduct(Vector3 a,Vector3 b) {return (Vector3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static Vector3 Vector3Normalize(Vector3 v) {float l=sqrtf(v.x*v.x+v.y*v.y+v.z*v.z);return (Vector3){v.x/l,v.y/l,v.z/l};}
static void UploadMesh(Mesh *m,bool dynamic) {assert(!dynamic);assert(m->triangleCount>0);}
static void release(Mesh m) {free(m.vertices);free(m.normals);free(m.texcoords);free(m.indices);}
'''
MAIN=r'''
int main(void) {
 Color pixels[6]={{0,0,0,255},{0,1,0,255},{128,0,0,255},{0,128,0,255},{255,0,0,255},{255,255,0,255}};
 Image image={pixels,3,2};
 Mesh a=Ground_CreateIndexedHeightfield(image,(Vector3){4,24,3},true,true);
 Mesh b=Ground_CreateIndexedHeightfield(image,(Vector3){4,24,3},true,false);
 assert(a.vertexCount==6 && a.triangleCount==4 && b.vertexCount==12 && !b.indices);
 assert(fabsf(a.vertices[4]-24.0f/65535.0f)<1e-7f); // low byte must survive
 assert(a.vertices[15]==4 && a.vertices[16]==24 && a.vertices[17]==3);
 for(int t=0;t<a.triangleCount;t++) {
  int i=a.indices[t*3],j=a.indices[t*3+1],k=a.indices[t*3+2];
  float ax=a.vertices[j*3]-a.vertices[i*3],az=a.vertices[j*3+2]-a.vertices[i*3+2];
  float bx=a.vertices[k*3]-a.vertices[i*3],bz=a.vertices[k*3+2]-a.vertices[i*3+2];
  assert(az*bx-ax*bz>0); // upward winding
 }
 for(int i=0;i<b.vertexCount;i++) {
  int j=a.indices[i];
  assert(memcmp(&b.vertices[i*3],&a.vertices[j*3],3*sizeof(float))==0);
  assert(memcmp(&b.normals[i*3],&a.normals[j*3],3*sizeof(float))==0);
  assert(memcmp(&b.texcoords[i*2],&a.texcoords[j*2],2*sizeof(float))==0);
  assert(a.normals[j*3+1]>0);
 }
 Mesh invalid=Ground_CreateIndexedHeightfield((Image){pixels,256,256},(Vector3){1,1,1},true,true);
 assert(!invalid.vertices);
 release(a);release(b);puts("heightfield precision, winding, indexed/expanded parity: PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(STUB+(ROOT/'maps/toolkit/map_props_heightfield.inl').read_text()+MAIN)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
