"""A limited foliage budget must represent both ends of the habitat domain."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
props=(ROOT/'maps/toolkit/map_props.c').read_text()
a=props.index('static unsigned int Nature_NextRandom');b=props.index('#include "maps/toolkit/map_props_texture.inl"',a)
rng=props[a:b]
meadow_path = ROOT/'maps/toolkit/map_props_meadow.inl' if (ROOT/'maps/toolkit/map_props_meadow.inl').exists() else ROOT/'maps/toolkit/map_props_nature.inl'
s=meadow_path.read_text()
a=s.index('int MapProp_GenerateMeadowPlacements');b=s.index('static inline Vector3 Nature_EvalCubicBezier',a)
production=s[a:b]
stub=r'''
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct {float x,y;} Vector2;
typedef struct {float x,y,z;} Vector3;
typedef struct {bool ready;} MapGroundSurface;
typedef struct {Vector2 minBounds,maxBounds;float spacing,jitter,minRadius,maxRadius,minHeight,maxHeight,yOffset;unsigned int seed;} MapMeadowDistribution;
typedef struct {Vector3 position;float radius,height,rotationDeg,phase;} MapMeadowPlacement;
typedef float (*MapFoliageDensityFn)(float,float,void *);
static float MapProp_SampleGroundHeight(const MapGroundSurface *g,Vector3 c,float x,float z) {(void)g;(void)c;return x+z;}
static float density(float x,float z,void *data) {(void)z;(void)data;return x>5.0f && x<15.0f ? 0 : 1;}
'''
main=r'''
int main(void) {
 MapMeadowPlacement a[32],b[32];
 MapMeadowDistribution d={.minBounds={0,0},.maxBounds={20,20},.spacing=0.5f,.jitter=0.5f,
 .minRadius=.2f,.maxRadius=.3f,.minHeight=.4f,.maxHeight=.7f,.seed=123};
 assert(MapProp_GenerateMeadowPlacements(a,32,NULL,(Vector3){0},d,density,NULL)==32);
 assert(MapProp_GenerateMeadowPlacements(b,32,NULL,(Vector3){0},d,density,NULL)==32);
 assert(memcmp(a,b,sizeof(a))==0);
 int north=0,south=0;float sum=0;
 for(int i=0;i<32;i++) {float x=a[i].position.x,z=a[i].position.z;
  assert(x<=5 || x>=15);if(z<5)north++;if(z>15)south++;sum+=z;
 }
 assert(north>=3 && south>=3 && sum/32>7 && sum/32<13);
 printf("budgeted meadow: north=%d south=%d mean_z=%.2f deterministic and habitat-safe\n",north,south,sum/32);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(stub+rng+production+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
