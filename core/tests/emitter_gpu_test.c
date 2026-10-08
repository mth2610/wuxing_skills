#include "raylib.h"
typedef struct {int meshCount;} Model;
typedef struct {Vector3 position,target,up;float fovy;int projection;} Camera3D;
#define WHITE ((Color){255,255,255,255})
#include "core/emitter/emitter_gpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static void Bytes(const char *dir,const char *name,const void *p,size_t size)
{
    char path[1024];snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(p,1,size,f)==size);fclose(f);
}
int main(int argc,char **argv)
{
    assert(sizeof(EmissionGpuParticle)==144);
    assert(sizeof(MotionGpuBody)==192);
    assert(sizeof(EmissionGpuTemplate)==352);
    assert(sizeof(EmissionGpuParent)==32);
    if(argc<3) {puts("PASS: emitter GPU ABI; fixture mode DIR CASE (0..7) [MOTION_FIXTURE_DIR]");return 0;}
    int scenario=atoi(argv[2]);assert(scenario>=0 && scenario<=7);
    EmissionGpuParticle parent={0},children[4]={0};
    MotionGpuBody bodies[4]={0};
    EmissionGpuParent event={.meta={17,1,1,3},.timing={30,.5f,1,0}};
    EmissionGpuTemplate leaf={0};unsigned int counters[4]={0};
    parent.data[0]=(Vector4){1,2,3,.1f};parent.data[1]=(Vector4){4,5,6,0};
    parent.data[4]=(Vector4){1,1,0,1};parent.data[6]=(Vector4){21,0,-1,0};
    leaf.particle.data[0].w=.2f;leaf.particle.data[1]=(Vector4){.1f,.2f,.3f,0};
    leaf.particle.data[4]=(Vector4){2,2,0,1};leaf.body.meta[0]=1;leaf.body.body0.x=2;
    leaf.inheritance.x=.5f;
    unsigned int due=1,steps=1;
    if(scenario==1) {parent.data[4].w=0;due=3;}
    if(scenario==2) {parent.data[4].w=0;event.timing.z=0;due=0;}
    if(scenario==3) {event.timing.x=10000;event.timing.y=0;due=64;}
    if(scenario==4) {event.timing.x=120;event.timing.y=0;counters[0]=3;due=2;}
    if(scenario==5) {event.meta[0]=18;event.timing.y=0;due=0;}
    if(scenario==6) {event.timing.y=0;due=2;steps=4;}
    if(scenario==7) {parent.data[4].w=0;due=3;steps=4;}
    Bytes(argv[1],"ssbo0.bin",&parent,sizeof(parent));
    Bytes(argv[1],"ssbo1.bin",children,sizeof(children));
    Bytes(argv[1],"ssbo2.bin",bodies,sizeof(bodies));
    Bytes(argv[1],"ssbo3.bin",&event,sizeof(event));
    Bytes(argv[1],"ssbo4.bin",&leaf,sizeof(leaf));
    Bytes(argv[1],"ssbo5.bin",counters,sizeof(counters));
    for(unsigned int i=0;i<due;i++) {
        unsigned int ticket=counters[1]++;
        if(ticket>=4) {counters[2]++;continue;}
        unsigned int slot=counters[0]++%4;
        children[slot]=leaf.particle;bodies[slot]=leaf.body;
        children[slot].data[0].x=1;children[slot].data[0].y=2;children[slot].data[0].z=3;
        children[slot].data[1].x+=2;children[slot].data[1].y+=2.5f;children[slot].data[1].z+=3;
        children[slot].data[6].x=21;
    }
    if(event.timing.z>.5f) {
        if(parent.data[4].w<.5f) event.timing.z=0;
        else {float carry=event.timing.y+event.timing.x*(float)steps/60;event.timing.y=carry-floorf(carry);}
    }
    Bytes(argv[1],"expected1.bin",children,sizeof(children));
    Bytes(argv[1],"expected2.bin",bodies,sizeof(bodies));
    Bytes(argv[1],"expected3.bin",&event,sizeof(event));
    Bytes(argv[1],"expected5.bin",counters,sizeof(counters));
    /* Follow-up production particle compute dispatch: these fixture bodies
     * have zero gravity, damping, airflow coupling and forces, hence the exact
     * reference is free ballistic transport with the same two 120 Hz steps. */
    for(unsigned int i=0;i<4;i++) if(children[i].data[4].w>.5f) {
        children[i].data[4].x-=1.f/60;
        for(int step=0;step<2;step++) {
            children[i].data[0].x+=children[i].data[1].x/120;
            children[i].data[0].y+=children[i].data[1].y/120;
            children[i].data[0].z+=children[i].data[1].z/120;
        }
    }
    Bytes(argv[1],"expected_integrated1.bin",children,sizeof(children));
    /* Optional validated Motion fixture supplies ABI snapshots for the second
     * resident dispatch. No machine-specific path lives in the harness. */
    if(argc>=4) {
        char directory[1024];snprintf(directory,sizeof(directory),"%s/integration",argv[1]);
        mkdir(directory,0777);
        for(int binding=1;binding<=5;binding++) {
            char source[1024],target[1024];
            snprintf(source,sizeof(source),"%s/ssbo%d.bin",argv[3],binding);
            snprintf(target,sizeof(target),"%s/ssbo%d.bin",directory,binding);
            FILE *in=fopen(source,"rb"),*out=fopen(target,"wb");assert(in && out);
            unsigned char block[4096];size_t n;bool first=true;
            while((n=fread(block,1,sizeof(block),in))>0) {
                if(first && binding==5) {assert(n>=4);memset(block,0,4);}
                assert(fwrite(block,1,n,out)==n);first=false;
            }
            fclose(in);fclose(out);
        }
        unsigned int zero[4]={0};Bytes(directory,"ssbo7.bin",zero,sizeof(zero));
    }
    char path[1024];snprintf(path,sizeof(path),"%s/config.txt",argv[1]);
    FILE *f=fopen(path,"w");assert(f);fprintf(f,"1 0.0166666667 0.0001 %u\n",steps);fclose(f);
    puts("PASS: emitter GPU ABI and bounded expected births");return 0;
}
