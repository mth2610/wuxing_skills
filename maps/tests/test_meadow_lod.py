"""Execute production tuft selection and CPU sphere eligibility with captured roots."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

def function(source, name):
    start = source.index(name+'(')
    start = source.rfind('\n', 0, start)+1
    opening = source.index('{', start)
    depth = 1
    end = opening+1
    while depth:
        depth += (source[end] == '{')-(source[end] == '}')
        end += 1
    return source[start:end]

glsl = function((ROOT/'maps/toolkit/shaders/nature_parametric.glsl').read_text(),
                'NatureTuftUsesCurrentLod')
# Adapt only GLSL vector I/O and language syntax; hash, thresholds and selection
# remain copied directly from the production shader.
glsl = re.sub(r'vec3 root = texelFetch\(u_bladeParameters,.*?\)\.xyz;',
              'Vector3 root = FetchRoot(blade);', glsl, flags=re.S)
glsl = re.sub(r'vec3 worldRoot = vec3\(u_worldFromShaderSpace.*?;',
              'Vector3 worldRoot = TransformRoot(root);', glsl, flags=re.S)
glsl = re.sub(r'\buint\b', 'uint32_t', glsl)
glsl = re.sub(r'\bfloat\(', '(float)(', glsl)
culling = function((ROOT/'maps/toolkit/map_props_meadow_parametric.inl').read_text(),
                   'NatureParametric_LodIntersectsSphere')
STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct {float x,y,z;} Vector3;
typedef struct {float x,y,z,w;} Vector4;
static Vector4 u_tuftLodBands;
static int u_tuftLodLevel,u_bladeOffset,gl_InstanceID,u_bladesPerTuft,expectedBlade,addressBias;
static Vector3 u_tuftLodCamera,rootInput,worldOffset;
static Vector3 FetchRoot(int blade) {assert(blade==expectedBlade);return rootInput;}
static Vector3 TransformRoot(Vector3 r) {return (Vector3){r.x+worldOffset.x,r.y+worldOffset.y,r.z+worldOffset.z};}
static uint32_t floatBitsToUint(float f) {uint32_t u;memcpy(&u,&f,sizeof(u));return u;}
static float distance(Vector3 a,Vector3 b) {float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return sqrtf(x*x+y*y+z*z);}
static float smoothstep(float a,float b,float x) {float t=fmaxf(0,fminf(1,(x-a)/(b-a)));return t*t*(3-2*t);}
'''
TEST = r'''
static int Selected(void) {
    int count=0,result=-1;
    for(int lod=0;lod<3;lod++) {
        /* LOD topology and chunk compaction may use different blade addresses. */
        gl_InstanceID=37+lod*17+addressBias;u_bladesPerTuft=5;
        u_bladeOffset=100+lod*153;expectedBlade=u_bladeOffset+gl_InstanceID*u_bladesPerTuft;
        u_tuftLodLevel=lod;
        if(NatureTuftUsesCurrentLod()) {count++;result=lod;}
    }
    assert(count==1);return result;
}
static void SetDistance(float d) {
    Vector3 r=TransformRoot(rootInput);u_tuftLodCamera=(Vector3){r.x+d,r.y,r.z};
}
static void CheckSphere(int selected,float rootDistance,float radius,float offset) {
    /* A root within a chunk sphere may lie anywhere between its nearest and
       farthest camera distances; selected passes must never be CPU-culled. */
    float centerDistance=rootDistance+offset;
    float nearest=fmaxf(0,centerDistance-radius),farthest=centerDistance+radius;
    assert(rootDistance>=nearest-1e-4f && rootDistance<=farthest+1e-4f);
    assert(NatureParametric_LodIntersectsSphere(selected,nearest,farthest,u_tuftLodBands));
}
int main(void) {
    int total=0;
    const Vector4 modes[]={{9,23,2,5},{0,23,2,5},{0,0,0,0}};
    for(int mode=0;mode<3;mode++) {
        u_tuftLodBands=modes[mode];
        for(int i=0;i<128;i++) {
            rootInput=(Vector3){7.13f+i*.317f,.03f+(i%7)*.007f,4.29f+(i%23)*.271f};
            int previous=-1;
            for(int j=0;j<=800;j++) {
                float d=j*.1f;SetDistance(d);int selected=Selected();
                assert(selected>=previous);previous=selected;
                if(mode==1)assert(selected!=1);
                if(mode==2)assert(selected==0);
                CheckSphere(selected,d,8,4);
                CheckSphere(selected,d,8,-fminf(4,d));
                total++;
            }
        }
    }
    u_tuftLodBands=modes[0];int nearCounts[3]={0},farCounts[3]={0},tinyChanges=0;
    for(int i=0;i<8192;i++) {
        rootInput=(Vector3){7.123f+i*.0031f,0.037f,11.337f+(i%137)*.0107f};
        SetDistance(9);int base=Selected();nearCounts[base]++;
        for(int frame=0;frame<4;frame++) {addressBias=frame*127;assert(Selected()==base);}
        addressBias=0;
        /* Translating camera and geometry changes matrices but must not
           change immutable authoring-space rank or the relative distance. */
        worldOffset=(Vector3){100,-3,200};SetDistance(9);assert(Selected()==base);
        worldOffset=(Vector3){0};SetDistance(9.00001f);tinyChanges+=(Selected()!=base);
        SetDistance(23);farCounts[Selected()]++;
    }
    assert(nearCounts[2]==0 && nearCounts[0]>3500 && nearCounts[1]>3500);
    assert(farCounts[0]==0 && farCounts[1]>3500 && farCounts[2]>3500);
    assert(tinyChanges<8);
    /* Mixed LOD must occupy an extended band rather than a chunk boundary. */
    for(int step=0;step<=8;step++) {
        float d=7.4f+step*.4f;int counts[3]={0};
        for(int i=0;i<2048;i++) {
            rootInput=(Vector3){31.731f+i*.0013f,.031f,12.837f+(i%53)*.0061f};
            SetDistance(d);counts[Selected()]++;
        }
        assert(counts[0]>0 && counts[1]>0 && counts[2]==0);
    }
    assert(!NatureParametric_LodIntersectsSphere(0,12,13,modes[0]));
    assert(!NatureParametric_LodIntersectsSphere(1,30,31,modes[0]));
    assert(!NatureParametric_LodIntersectsSphere(2,4,6,modes[0]));
    assert(!NatureParametric_LodIntersectsSphere(1,0,100,modes[1]));
    printf("meadow LOD: %d selections; uniqueness, monotonicity, immutable rank, bands, two-tier/disabled and conservative CPU culling passed\n",total);
}
'''
with tempfile.TemporaryDirectory(prefix='wuxing-meadow-lod-') as directory:
    work = pathlib.Path(directory)
    c = work/'lod.c'
    exe = work/'lod'
    c.write_text(STUBS+glsl+'\n'+culling+'\n'+TEST)
    subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror',str(c),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
