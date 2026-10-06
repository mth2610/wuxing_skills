#include "core/liquid/liquid_external_field.h"
#include <math.h>
#include <stdio.h>
static int calls;
static void Sample(Vector3 p,Vector3 v,const BodyPhysicalProperties *b,
    const MediumProperties *m,const ReceiverConstraints *c,float dt,void *data,FieldSample *s)
{
    (void)p;(void)v;(void)b;(void)m;(void)dt;(void)data;
    if(c->mode!=RECEIVER_FREE) return;
    calls++;
    *s=(FieldSample){.forceNewtons={8,0,0},.accelerationMps2={0,-9.81f,0},
        .mediumVelocityMps={100,100,100},.mediumWeight=1};
}
int main(void)
{
    BodyPhysicalProperties body={.massKg=4}; MediumProperties medium={0};
    FieldSample sample;
    Vector3 a=LiquidExternalField_SampleAcceleration(Sample,NULL,(Vector3){0},
        (Vector3){0},&body,&medium,0.01f,&sample);
    if(calls!=1 || fabsf(a.x-2)>1e-6f || fabsf(a.y+9.81f)>1e-6f || a.z!=0 ||
       sample.mediumVelocityMps.z!=100) return 1;
    a=LiquidExternalField_SampleAcceleration(NULL,NULL,(Vector3){0},
        (Vector3){0},&body,&medium,0.01f,NULL);
    if(a.x!=0 || a.y!=0 || a.z!=0 || calls!=1) return 1;
    puts("Liquid boundary samples once, separates m/s, N and m/s2; does not integrate");
    return 0;
}
