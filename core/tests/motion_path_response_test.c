/* Shared Motion response; production Perlin, fields and path sampler. */
#define main ExistingMotionTestsMain
#include "core/tests/motion_fields_test.c"
#undef main
#include <assert.h>
static void NearPoint(Vector3 a,Vector3 b,float epsilon) {
    assert(MotionVec_Length(MotionVec_Sub(a,b))<epsilon);
}
int main(void) {
    FieldDesc f=MotionField_Default();f.lifetime.durationSec=1000;
    f.volume.shape=FIELD_PATH_TUBE;f.volume.radiusM=.5f;
    Vector3 points[]={{0,0,0},{10,0,0}};assert(MotionPath_Build(&f.volume.path,points,2));
    MotionPathTransportSnapshot view={.path=&f.volume.path,.transform=FieldTransform_Identity(),.field=&f,.ageSec=1};
    BodyPhysicalProperties body={.massKg=.004f};
    MotionPathTransportState state={0};
    NearPoint(MotionPathTransport_Advance(&view,5,(Vector3){0},0,.02f,&body,0,(Vector3){0},&state),(Vector3){5,0,0},1e-6f);
    f.forceLawCount=1;f.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_ACCELERATION,.accelerationMps2={10000,0,0}};
    for(int i=0;i<100;i++) NearPoint(MotionPathTransport_Advance(&view,5,(Vector3){0},0,.02f,&body,0,(Vector3){0},&state),(Vector3){5,0,0},1e-6f);
    f.forceLaws[0].accelerationMps2=(Vector3){10000,10000,10000};
    for(int i=0;i<1000;i++) {
        Vector3 q=MotionPathTransport_Advance(&view,5,(Vector3){0},0,i%2?.01f:.05f,&body,0,(Vector3){0},&state);
        assert(fabsf(q.x-5)<1e-6f&&hypotf(q.y,q.z)<=.50001f);
        assert(Field_FiniteVector(q)&&Field_FiniteVector(state.velocityMps));
    }
    assert(MotionVec_Length(state.offsetM)>.1f);
    NearPoint(MotionPathTransport_Advance(&view,-1,(Vector3){0},0,.02f,&body,0,(Vector3){0},&state),points[0],1e-6f);
    NearPoint(MotionPathTransport_Advance(&view,11,(Vector3){0},0,.02f,&body,0,(Vector3){0},&state),points[1],1e-6f);
    f.forceLawCount=0;f.flow.enabled=true;f.flow.procedural.swirlSpeedMps=1;
    state=(MotionPathTransportState){0};
    Vector3 orbit=MotionPathTransport_Advance(&view,5,(Vector3){0,.2f,0},0,.02f,&body,0,(Vector3){0},&state);
    NearPoint(orbit,(Vector3){5,.2f*cosf(2),.2f*sinf(2)},1e-5f);
    Vector3 tail=MotionPathTransport_Advance(&view,5,(Vector3){0,.2f,0},.25f,.02f,&body,0,(Vector3){0},&state);
    assert(MotionVec_Length(MotionVec_Sub(orbit,tail))>.05f);
    f.forceLawCount=1;f.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_CURL_FORCE,.magnitudeNewtons=.02f,
        .procedural={.turbulenceSpeedMps=.6f,.eddyLengthM=.3f}};
    MotionPathTransportState next={0};state=(MotionPathTransportState){0};
    for(int i=0;i<120;i++) {
        view.ageSec=i/60.f;
        orbit=MotionPathTransport_Advance(&view,5,(Vector3){0,.15f,0},0,1/60.f,&body,0,(Vector3){0},&state);
        tail=MotionPathTransport_Advance(&view,5.01f,(Vector3){0,.15f,0},0,1/60.f,&body,0,(Vector3){0},&next);
        assert(fabsf(orbit.x-5)<1e-6f&&hypotf(orbit.y,orbit.z)<=.50001f);
        assert(MotionVec_Length(MotionVec_Sub(orbit,tail))<.1f); /* Spatially coherent curl. */
    }
    assert(MotionVec_Length(state.offsetM)>.00001f);
    /* Flow turbulence is a velocity channel; aerodynamic response converts it. */
    f.forceLawCount=0;f.flow.procedural.swirlSpeedMps=0;
    f.flow.procedural.turbulenceSpeedMps=.6f;f.flow.procedural.eddyLengthM=.3f;
    body.projectedAreaM2=.002f;body.dragCoefficient=.47f;state=(MotionPathTransportState){0};
    for(int i=0;i<120;i++) {
        view.ageSec=i/60.f;
        orbit=MotionPathTransport_Advance(&view,5,(Vector3){0},0,1/60.f,&body,0,(Vector3){0},&state);
        assert(fabsf(orbit.x-5)<1e-6f&&hypotf(orbit.y,orbit.z)<=.50001f);
    }
    assert(MotionVec_Length(state.offsetM)>.00001f);
    f.flow.procedural.turbulenceSpeedMps=0;body.projectedAreaM2=0;body.dragCoefficient=0;
    state=(MotionPathTransportState){0};
    Vector3 gravity=MotionPathTransport_Advance(&view,5,(Vector3){0},0,.02f,&body,0,(Vector3){100,-9.81f,0},&state);
    assert(gravity.x==5&&gravity.y<0);
    f.forceLawCount=1;
    /* Body mass controls Newton response, without changing longitudinal speed. */
    f.flow.procedural.swirlSpeedMps=0;f.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_NEWTONS,.forceNewtons={0,.001f,0}};
    state=(MotionPathTransportState){0};next=state;
    Vector3 light=MotionPathTransport_Advance(&view,5,(Vector3){0},0,.02f,&body,0,(Vector3){0},&state);
    body.massKg*=2;Vector3 heavy=MotionPathTransport_Advance(&view,5,(Vector3){0},0,.02f,&body,0,(Vector3){0},&next);
    assert(fabsf(light.y-2*heavy.y)<1e-6f);
    puts("PASS: guided field response, locked progress, orbit/history lag, curl coherence, mass, bounded drift and exact endpoints");
    return 0;
}
