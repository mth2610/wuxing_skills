// Component-neutral spatial Motion ABI v1. Scene/body bindings remain 5/6.
struct MotionGpuLaw { vec4 forceType, accelerationMagnitude, centerStiffness, params, procedural; };
struct MotionGpuField {
    uvec4 identity;
    vec4 volume, halfExtents, capsuleA, capsuleB;
    vec4 position, axisX, axisY, axisZ, frameVelocity, angularVelocity;
    vec4 flowVelocity, flowAxis, flowParams;
    vec4 trajectory[64], points[64], tangents[64], normals[64];
    MotionGpuLaw laws[8];
};
struct MotionGpuBody {
    uvec4 meta;
    vec4 body0, body1, body2, body3, acceleration, force;
    uvec4 laneHandles;
    vec4 laneOffsets[4];
};
layout(std430,binding=5) readonly buffer MotionSceneBuffer {
    uvec4 motionMeta;
    vec4 zoneDirectionStrength, zoneNoise;
    MotionGpuField motionFields[];
};
layout(std430,binding=6) buffer MotionBodyBuffer { MotionGpuBody motionBodies[]; };
const int motionPerm[256]=int[256](151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
    184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
    222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180);
float mGrad(int hash,vec3 p) {
    int h=hash&15;float u=h<8?p.x:p.y;
    float v=h<4?p.y:((h==12 || h==14)?p.x:p.z);
    return ((h&1)!=0?-u:u)+((h&2)!=0?-v:v);
}
int mPerm(int i) { return motionPerm[i&255]; }
float motionPerlin(vec3 p) {
    ivec3 cell=ivec3(floor(p))&ivec3(255);vec3 q=fract(p);
    vec3 u=q*q*q*(q*(q*6.0-15.0)+10.0);
    int a=mPerm(cell.x)+cell.y,b=mPerm(cell.x+1)+cell.y;
    int aa=mPerm(a)+cell.z,ab=mPerm(a+1)+cell.z;
    int ba=mPerm(b)+cell.z,bb=mPerm(b+1)+cell.z;
    return mix(mix(mix(mGrad(mPerm(aa),q),mGrad(mPerm(ba),q-vec3(1,0,0)),u.x),
        mix(mGrad(mPerm(ab),q-vec3(0,1,0)),mGrad(mPerm(bb),q-vec3(1,1,0)),u.x),u.y),
        mix(mix(mGrad(mPerm(aa+1),q-vec3(0,0,1)),mGrad(mPerm(ba+1),q-vec3(1,0,1)),u.x),
        mix(mGrad(mPerm(ab+1),q-vec3(0,1,1)),mGrad(mPerm(bb+1),q-vec3(1,1,1)),u.x),u.y),u.z);
}
vec3 mNorm(vec3 v) { float n=length(v);return n>1e-6?v/n:vec3(0); }
vec3 mLimit(vec3 v,float cap) { float n=length(v);return n>cap && n>0.0?v*(max(cap,0.0)/n):v; }
vec3 mRotate(vec3 v,vec3 axis,float angle) {
    float c=cos(angle),s=sin(angle);
    return v*c+cross(axis,v)*s+axis*dot(axis,v)*(1.0-c);
}
vec3 mLegacyCurl(vec3 p) {
    float e=.1;vec3 dx=vec3(e,0,0),dy=vec3(0,e,0),dz=vec3(0,0,e);
    return vec3((motionPerlin(p+dy+vec3(67.234))-motionPerlin(p-dy+vec3(67.234)))
        -(motionPerlin(p+dz+vec3(31.416))-motionPerlin(p-dz+vec3(31.416))),
        (motionPerlin(p+dz)-motionPerlin(p-dz))
        -(motionPerlin(p+dx+vec3(67.234))-motionPerlin(p-dx+vec3(67.234))),
        (motionPerlin(p+dx+vec3(31.416))-motionPerlin(p-dx+vec3(31.416)))
        -(motionPerlin(p+dy)-motionPerlin(p-dy)))/(2.0*e);
}
mat3 mAxes(int fi) { return mat3(motionFields[fi].axisX.xyz,motionFields[fi].axisY.xyz,motionFields[fi].axisZ.xyz); }
struct MPath { vec3 p,t,n,b; };
MPath mPath(int fi,vec3 p) {
    int count=int(motionFields[fi].halfExtents.w);float best=1e30,dist=0.0;
    for(int i=0;i<count-1;i++) {
        vec3 a=motionFields[fi].points[i].xyz,d=motionFields[fi].points[i+1].xyz-a;
        float t=clamp(dot(p-a,d)/dot(d,d),0.0,1.0);vec3 e=p-(a+t*d);
        if(dot(e,e)<best) { best=dot(e,e);dist=mix(motionFields[fi].points[i].w,motionFields[fi].points[i+1].w,t); }
    }
    int s=0;while(s<count-2 && motionFields[fi].points[s+1].w<dist) s++;
    float t=(dist-motionFields[fi].points[s].w)/(motionFields[fi].points[s+1].w-motionFields[fi].points[s].w);
    MPath q;q.p=mix(motionFields[fi].points[s].xyz,motionFields[fi].points[s+1].xyz,t);
    q.t=motionFields[fi].tangents[s].xyz;q.n=motionFields[fi].normals[s].xyz;q.b=cross(q.t,q.n);return q;
}
float mWeight(int fi,vec3 p,vec3 nearest) {
    int shape=int(motionFields[fi].identity.y);float r=motionFields[fi].volume.x,d;
    if(shape==2) {vec3 q=abs(p)/motionFields[fi].halfExtents.xyz;d=max(q.x,max(q.y,q.z));}
    else if(shape==1) {
        vec3 a=motionFields[fi].capsuleA.xyz,v=motionFields[fi].capsuleB.xyz-a;
        float sq=dot(v,v);float t=sq>1e-10?clamp(dot(p-a,v)/sq,0.0,1.0):0.0;d=length(p-a-t*v)/r;
    } else d=length(p-(shape==3?nearest:vec3(0)))/r;
    float core=motionFields[fi].volume.y,x=clamp((d-core)/max(1.0-core,.0001),0.0,1.0);
    return 1.0-x*x*(3.0-2.0*x);
}
float mRadius(int fi) {
    vec3 h=motionFields[fi].halfExtents.xyz;
    return motionFields[fi].identity.y==2u?min(h.x,min(h.y,h.z)):motionFields[fi].volume.x;
}
vec3 mPotential(int fi,vec3 p,vec3 nearest,float age,float speed,float eddy) {
    float w=mWeight(fi,p,nearest);if(w<=0.0) return vec3(0);
    vec3 q=p/eddy+age*speed/eddy*vec3(.37,.51,.29);
    return vec3(motionPerlin(q),motionPerlin(q+vec3(31.416)),motionPerlin(q+vec3(67.234)))*speed*eddy*w;
}
vec3 mCurl(int fi,vec3 p,vec3 nearest,float age,float speed,float eddy) {
    if(speed<=0.0) return vec3(0);if(eddy<=0.0) eddy=mRadius(fi)*.25;
    float h=eddy*.02;vec3 dx=vec3(h,0,0),dy=vec3(0,h,0),dz=vec3(0,0,h);
    vec3 ax=mPotential(fi,p+dx,nearest,age,speed,eddy)-mPotential(fi,p-dx,nearest,age,speed,eddy);
    vec3 ay=mPotential(fi,p+dy,nearest,age,speed,eddy)-mPotential(fi,p-dy,nearest,age,speed,eddy);
    vec3 az=mPotential(fi,p+dz,nearest,age,speed,eddy)-mPotential(fi,p-dz,nearest,age,speed,eddy);
    return vec3(ay.z-az.y,az.x-ax.z,ax.y-ay.x)/(2.0*h);
}
void mFrame(int fi,float age,out vec3 center,out vec3 velocity) {
    center=motionFields[fi].position.xyz;velocity=motionFields[fi].frameVelocity.xyz;
    int count=int(motionFields[fi].axisX.w);if(count<2) return;
    float last=motionFields[fi].trajectory[count-1].w;
    float dist=clamp(max(age-motionFields[fi].axisY.w,0.0)*motionFields[fi].position.w,0.0,last);
    int s=0;while(s<count-2 && motionFields[fi].trajectory[s+1].w<dist) s++;
    vec4 a=motionFields[fi].trajectory[s],b=motionFields[fi].trajectory[s+1];
    float t=(dist-a.w)/(b.w-a.w);mat3 axes=mAxes(fi);
    center+=axes*mix(a.xyz,b.xyz,t);
    if(dist<last) velocity+=axes*mNorm(b.xyz-a.xyz)*motionFields[fi].position.w;
}
// Prescribed transport shares CPU Hermite arc-distance interpolation. No force
// solve, state readback or extra scene ABI. Generation identity fails closed.
int mFindPathTransport(uint handle) {
    if(handle==0u) return -1;
    for(int fi=0;fi<int(motionMeta.x);fi++)
        if(motionFields[fi].identity.x==handle && motionFields[fi].identity.y==3u &&
           motionFields[fi].halfExtents.w>=2.0) return fi;
    return -1;
}
vec3 mPathTransportDerivative(int fi,int knot,int count) {
    int a=max(knot-1,0),b=min(knot+1,count-1);
    vec4 p=motionFields[fi].points[a],q=motionFields[fi].points[b];
    return (q.xyz-p.xyz)/(q.w-p.w);
}
MPath mPathTransportFrame(int fi,float distance) {
    int count=int(motionFields[fi].halfExtents.w);
    float d=clamp(distance,0.0,motionFields[fi].points[count-1].w);
    int lo=0,hi=count-1;
    while(hi-lo>1) {int mid=(lo+hi)/2;if(motionFields[fi].points[mid].w<=d) lo=mid;else hi=mid;}
    if(lo==count-1) lo--;
    int b=lo+1;
    vec4 p=motionFields[fi].points[lo],q=motionFields[fi].points[b];
    float span=q.w-p.w,t=(d-p.w)/span,t2=t*t,t3=t2*t;
    vec3 aSlope=mPathTransportDerivative(fi,lo,count),bSlope=mPathTransportDerivative(fi,b,count);
    vec3 position=(2.0*t3-3.0*t2+1.0)*p.xyz+(-2.0*t3+3.0*t2)*q.xyz
        +span*(t3-2.0*t2+t)*aSlope+span*(t3-t2)*bSlope;
    float blend=t2*(3.0-2.0*t);
    vec3 tangent=mNorm(mix(aSlope,bSlope,blend));
    vec3 normal=mix(motionFields[fi].normals[lo].xyz,motionFields[fi].normals[b].xyz,blend);
    normal-=tangent*dot(normal,tangent);
    if(length(normal)<1e-5) {
        vec3 ref=abs(tangent.y)<.9?vec3(0,1,0):vec3(1,0,0);
        normal=ref-tangent*dot(ref,tangent);
    }
    normal=mNorm(normal);
    MPath result;result.p=position;result.t=tangent;result.n=normal;result.b=cross(tangent,normal);return result;
}
vec3 mPathTransportWorld(int fi,MPath q,vec3 lane) {
    vec3 local=q.p+q.t*lane.x+q.n*lane.y+q.b*lane.z;
    vec3 center,velocity;mFrame(fi,motionFields[fi].volume.z,center,velocity);
    return center+mAxes(fi)*local;
}
vec3 mPathTransportSample(int fi,float distance,vec3 lane) {
    return mPathTransportWorld(fi,mPathTransportFrame(fi,distance),lane);
}
// Bounded field response in a moving path frame. Body laneOffsets[0/1] are
// transport offset/velocity only in this opt-in branch; physical lanes retain
// their original ABI and ownership. No path projection or iterative solve.
vec3 mPathTransportAdvance(int fi,float distance,vec3 birthLane,float lag,float dt,inout MotionGpuBody body) {
    MPath q=mPathTransportFrame(fi,distance);
    int pathCount=int(motionFields[fi].halfExtents.w);
    if(distance<=0.0 || distance>=motionFields[fi].points[pathCount-1].w) {
        body.laneOffsets[0]=vec4(0);body.laneOffsets[1]=vec4(0);
        return mPathTransportWorld(fi,q,vec3(0));
    }
    float radius=motionFields[fi].volume.x,age=motionFields[fi].volume.z;
    if(radius<=0.0) return mPathTransportWorld(fi,q,vec3(0));
    bool flowOn=motionFields[fi].volume.w>0.5;
    float angle=flowOn?motionFields[fi].flowParams.y/radius*(age-lag):0.0;
    float c=cos(angle),sn=sin(angle);
    vec3 base=mLimit(vec3(0,birthLane.y*c-birthLane.z*sn,birthLane.y*sn+birthLane.z*c),radius);
    float room=max(0.0,radius-length(base));
    vec3 displacement=body.laneOffsets[0].xyz,velocity=body.laneOffsets[1].xyz;
    vec3 lane=base+displacement,local=q.p+q.n*lane.y+q.b*lane.z;
    vec3 localVelocity=q.n*velocity.y+q.b*velocity.z;
    float lifeAge=age-motionFields[fi].axisY.w,duration=motionFields[fi].axisZ.w;
    float life=lifeAge<0.0 || lifeAge>=duration?0.0:1.0;
    if(motionFields[fi].frameVelocity.w>0.0) life*=clamp(lifeAge/motionFields[fi].frameVelocity.w,0.0,1.0);
    if(motionFields[fi].angularVelocity.w>0.0) life*=clamp((duration-lifeAge)/motionFields[fi].angularVelocity.w,0.0,1.0);
    life=life*life*(3.0-2.0*life);
    float w=mWeight(fi,local,q.p)*life;
    vec3 gravity=transpose(mAxes(fi))*vec3(0,-9.81*body.body0.y,0);
    vec3 force=vec3(0),acc=vec3(0),air=vec3(0);float stiffness=0.0;
    if(flowOn) air=motionFields[fi].flowVelocity.xyz*w+
        mCurl(fi,local,q.p,age,motionFields[fi].flowParams.x,motionFields[fi].flowParams.z)*life;
    for(int j=0;j<int(motionFields[fi].flowParams.w);j++) {
        MotionGpuLaw law=motionFields[fi].laws[j];int type=int(law.forceType.w);
        if(type==6) stiffness=max(stiffness,law.centerStiffness.w>0.0?law.centerStiffness.w:law.accelerationMagnitude.w/radius);
        else if(type==8) {
            float speed=law.procedural.x;
            if(speed>0.0 && law.accelerationMagnitude.w>0.0 && w>0.0)
                force+=mLimit(mCurl(fi,local,q.p,age,speed,law.procedural.z)*
                    (law.accelerationMagnitude.w*life/speed),law.accelerationMagnitude.w*w);
        } else if(type==0) force+=law.forceType.xyz*w;
        else if(type==1) acc+=law.accelerationMagnitude.xyz*w;
        else if(type==2) force+=mNorm(law.centerStiffness.xyz-local)*law.accelerationMagnitude.w*w;
        else if(type==3) force+=((law.centerStiffness.xyz-local)*law.centerStiffness.w-localVelocity*law.params.x)*w;
        else if(type==4 && body.body2.w>0.0) force-=gravity*((body.body2.z>0.0?body.body2.z:1.225)/body.body2.w/body.body0.x*w);
    }
    vec3 relative=air-localVelocity;
    float rho=body.body2.z>0.0?body.body2.z:1.225;
    force+=relative*(.5*rho*body.body2.x*body.body2.y*length(relative));
    vec3 acceleration=gravity+acc+force*body.body0.x;
    vec3 lateral=vec3(0,dot(acceleration,q.n),dot(acceleration,q.b));
    float omega=sqrt(stiffness*body.body0.x);if(omega<=0.0) omega=1.0;
    float denom=1.0+2.0*omega*dt+omega*omega*dt*dt;
    velocity=(velocity+dt*(lateral-omega*omega*displacement))/denom;
    displacement+=velocity*dt;
    float len=length(displacement);
    if(len>room) {
        vec3 normal=displacement/len;displacement=normal*room;
        float outward=dot(velocity,normal);if(outward>0.0) velocity-=normal*outward;
    }
    body.laneOffsets[0]=vec4(displacement,0);body.laneOffsets[1]=vec4(velocity,0);
    int count=int(motionFields[fi].halfExtents.w);
    float total=motionFields[fi].points[count-1].w,d=clamp(distance,0.0,total);
    float edge=clamp(min(d,total-d)/radius,0.0,1.0);edge=edge*edge*(3.0-2.0*edge);
    return mPathTransportWorld(fi,q,(base+displacement)*edge);
}
// Guide resistance is isotropic, axial or transverse: R=aI+b*u*u^T.
// A zero axis means isotropic; unit axis with signed response selects axial
// (positive) or transverse (negative). Two vec4s avoid full private matrices.
struct MController { vec4 driveCap,axisResponse; };
void mCoefficients(MController r,out vec3 diagonal,out vec3 off) {
    vec3 axis=r.axisResponse.xyz;float gain=r.axisResponse.w;
    if(dot(axis,axis)<.5) {diagonal=vec3(gain);off=vec3(0);return;}
    float isotropic=gain<0.0?-gain:0.0;
    diagonal=vec3(isotropic)+gain*axis*axis;
    off=gain*axis.xxy*axis.yzz;
}
struct MSample {
    vec3 force,acc,air,dragAir;
    float weight,dragK;int priority;bool buoyancy,drag;
    int count;MController controllers[8];
};
void mController(inout MSample s,vec3 drive,vec3 axis,float response,float cap,float dt,float mass) {
    if(s.count<8) {
        s.controllers[s.count].driveCap=vec4(drive,cap);
        s.controllers[s.count].axisResponse=vec4(axis,response);s.count++;
    } else {
        float isotropic=dot(axis,axis)<.5?response:max(-response,0.0);
        float axial=dot(axis,axis)<.5?0.0:response;
        // Sherman-Morrison gives the same bounded overflow response without
        // constructing and inverting a general matrix.
        float scale=dt/mass,denom=1.0+isotropic*scale;
        vec3 force=(drive-axis*(axial*scale*dot(axis,drive)/(denom+axial*scale*dot(axis,axis))))/denom;
        s.force+=mLimit(force,cap);
    }
}
void mSampleField(int fi,vec3 pos,vec3 vel,vec3 ordinary,float rho,float mass,float dt,
    float offset,inout MotionGpuBody body,inout MSample s,bool dragPass) {
    if(dragPass && (motionFields[fi].identity.w & 8u)==0u) return;
    if((motionFields[fi].identity.z & body.meta.z)==0u) return;
    float age=max(motionFields[fi].volume.z+offset,0.0),lifeAge=age-motionFields[fi].axisY.w;
    float duration=motionFields[fi].axisZ.w;if(lifeAge<0.0 || lifeAge>=duration) return;
    float life=1.0;
    if(motionFields[fi].frameVelocity.w>0.0) life*=clamp(lifeAge/motionFields[fi].frameVelocity.w,0.0,1.0);
    if(motionFields[fi].angularVelocity.w>0.0) life*=clamp((duration-lifeAge)/motionFields[fi].angularVelocity.w,0.0,1.0);
    life=life*life*(3.0-2.0*life);
    vec3 center,frameV;mFrame(fi,age,center,frameV);mat3 axes=mAxes(fi);
    vec3 off=pos-center,p=transpose(axes)*off;int shape=int(motionFields[fi].identity.y);
    MPath path;path.p=vec3(0);path.t=vec3(1,0,0);path.n=vec3(0,1,0);path.b=vec3(0,0,1);
    if(shape==3) path=mPath(fi,p);
    float w=mWeight(fi,p,path.p)*life;if(w<=0.0) return;
    float radius=motionFields[fi].volume.x;uint flags=motionFields[fi].identity.w;
    bool flowOn=motionFields[fi].volume.w>0.5;
    vec3 flow=ordinary,axis=mNorm(motionFields[fi].flowAxis.xyz);
    if(length(axis)<.1) axis=vec3(0,1,0);
    vec3 curl=vec3(0),flowCenter=vec3(0),flowAxis=axis;
    if(shape==3) {flowCenter=path.p;flowAxis=path.t;}
    else if(shape==1) {
        vec3 delta=motionFields[fi].capsuleB.xyz-motionFields[fi].capsuleA.xyz;float sq=dot(delta,delta);
        float along=sq>1e-12?clamp(dot(p-motionFields[fi].capsuleA.xyz,delta)/sq,0.0,1.0):0.0;
        flowCenter=motionFields[fi].capsuleA.xyz+delta*along;if(sq>1e-12) flowAxis=mNorm(delta);
    }
    float swirl=motionFields[fi].flowParams.y;
    if(!dragPass && flowOn) {
        vec3 radial=p-flowCenter;radial-=flowAxis*dot(radial,flowAxis);
        vec3 local=motionFields[fi].flowVelocity.xyz*w;
        if(shape==3) local+=path.t*motionFields[fi].capsuleA.w*w;
        local+=cross(flowAxis,radial)*(swirl/mRadius(fi)*w);
        local+=mCurl(fi,p,path.p,age,motionFields[fi].flowParams.x,motionFields[fi].flowParams.z)*life;
        flow=(frameV+cross(motionFields[fi].angularVelocity.xyz,off))*w+axes*local;
        if(motionFields[fi].flowAxis.w>0.5) flow+=ordinary;
        float weight=w*motionFields[fi].flowVelocity.w;int priority=int(motionFields[fi].capsuleB.w);
        if(s.weight<=0.0 || priority>s.priority) {s.air=flow;s.weight=weight;s.priority=priority;}
        else if(priority==s.priority) {s.air=(s.air*s.weight+flow*weight)/(s.weight+weight);s.weight+=weight;}
    }
    if(dragPass) flow=s.weight>0.0?s.air:ordinary;
    vec3 lane=vec3(0);bool hasLane=false;
    bool preserve=shape==0?(flags&1u)!=0u:(shape==3 && (flags&2u)!=0u);
    if(body.meta.y!=0u && preserve) {
        bool guide=false;
        for(int j=0;j<int(motionFields[fi].flowParams.w);j++)
            if(int(motionFields[fi].laws[j].forceType.w)==(shape==0?7:6)) guide=true;
        int slot=-1;
        if(guide) {
            for(int j=0;j<4;j++) if(body.laneHandles[j]==motionFields[fi].identity.x) {slot=j;hasLane=true;break;}
            if(!hasLane) {
                for(int j=0;j<4;j++) {
                    bool alive=false;
                    for(int k=0;k<int(motionMeta.x);k++) if(body.laneHandles[j]==motionFields[k].identity.x) alive=true;
                    if(!alive) {slot=j;break;}
                }
                if(slot>=0) {
                    if(shape==0) {lane=mLimit(p,radius*.65);if(flowOn) lane=mRotate(lane,axis,-swirl/radius*age);}
                    else {vec3 radial=p-path.p;radial-=path.t*dot(radial,path.t);radial=mLimit(radial,radius*.65);
                        lane=vec3(0,dot(radial,path.n),dot(radial,path.b));
                        if((flags&4u)!=0u && flowOn) lane=mRotate(lane,vec3(1,0,0),-swirl/radius*age);}
                    body.laneHandles[slot]=motionFields[fi].identity.x;body.laneOffsets[slot]=vec4(lane,0);hasLane=true;
                }
            }
            if(hasLane) lane=body.laneOffsets[slot].xyz;
        }
    }
    for(int j=0;j<int(motionFields[fi].flowParams.w);j++) {
        MotionGpuLaw law=motionFields[fi].laws[j];int type=int(law.forceType.w);
        if((type==5)!=dragPass) continue;
        float cap=law.accelerationMagnitude.w*w,k=law.centerStiffness.w;
        vec3 goal=center+axes*law.centerStiffness.xyz;
        vec3 relative=vel-frameV-cross(motionFields[fi].angularVelocity.xyz,off);
        if(type==7) {
            if(k<=0.0) k=law.accelerationMagnitude.w/radius;
            vec3 desired=frameV+cross(motionFields[fi].angularVelocity.xyz,off);
            if(hasLane) {vec3 rotated=flowOn?mRotate(lane,axis,swirl/radius*age):lane;
                goal+=axes*rotated;if(flowOn) desired+=axes*(cross(axis,rotated)*(swirl/radius));}
            k*=w;float response=2.0*sqrt(k*mass)+k*dt;
            mController(s,(goal-pos)*k-(vel-desired)*response,vec3(0),response,cap,dt,mass);
        } else if(type==6 && shape==3) {
            if(k<=0.0) k=law.accelerationMagnitude.w/radius;
            vec3 tangent=axes*path.t,rotated=lane;
            if(hasLane && (flags&4u)!=0u && flowOn) rotated=mRotate(lane,vec3(1,0,0),swirl/radius*age);
            vec3 laneWorld=axes*(path.t*rotated.x+path.n*rotated.y+path.b*rotated.z);
            vec3 error=center+axes*path.p+(hasLane?laneWorld:vec3(0))-pos;
            error-=tangent*dot(error,tangent);
            float speed=dot(relative,tangent);vec3 dampingV=relative-tangent*speed;
            if(hasLane && (flags&4u)!=0u && flowOn) dampingV-=cross(tangent,laneWorld)*(swirl/radius);
            vec3 responseAxis=tangent;float responseSign=-1.0;
            if((flags&4u)==0u && flowOn && swirl!=0.0) {
                vec3 radial=p-path.p;radial-=path.t*dot(radial,path.t);
                if(length(radial)<1e-5 && hasLane) radial=path.t*lane.x+path.n*lane.y+path.b*lane.z;
                if(dot(radial,radial)>1e-10) {vec3 r=axes*mNorm(radial);responseAxis=r;responseSign=1.0;dampingV=r*dot(dampingV,r);}
            }
            k*=w;float response=2.0*sqrt(k*mass)+k*dt;
            mController(s,error*k-dampingV*response,responseAxis,responseSign*response,cap,dt,mass);
            float target=motionFields[fi].capsuleA.w;
            if(law.params.y>0.0 && abs(target)>1e-5) {
                float gain=law.params.y/abs(target)*w;
                mController(s,tangent*(target-speed)*gain,tangent,gain,law.params.y*w,dt,mass);
            }
        } else if(type==8) {
            float speed=law.procedural.x;
            if(speed>0.0 && cap>0.0) s.force+=axes*mLimit(mCurl(fi,p,path.p,age,speed,law.procedural.z)*(law.accelerationMagnitude.w*life/speed),cap);
        } else if(type==0) s.force+=axes*law.forceType.xyz*w;
        else if(type==1) s.acc+=axes*law.accelerationMagnitude.xyz*w;
        else if(type==2) s.force+=mNorm(goal-pos)*cap;
        else if(type==3) s.force+=((goal-pos)*k-relative*law.params.x)*w;
        else if(type==4) {s.buoyancy=true;float volume=body.body2.w>0.0?mass/body.body2.w:0.0;
            s.force+=vec3(0,9.81*body.body0.y*rho*volume*w,0);}
        else if(type==5) {s.drag=true;s.dragK+=w*.5*rho*body.body2.x*body.body2.y;s.dragAir=flow;}
    }
}
vec3 mControllerResponse(MController r,vec3 delta) {
    vec3 axis=r.axisResponse.xyz;float gain=r.axisResponse.w;
    if(dot(axis,axis)<.5) return gain*delta;
    return gain<0.0?-gain*(delta-axis*dot(axis,delta)):gain*axis*dot(axis,delta);
}
vec3 mSolveControllers(inout MSample s,vec3 acceleration,float invMass,float dt) {
    if(s.count==0) return acceleration*dt;
    if(s.count==1) {
        vec3 diagonal,offDiagonal;mCoefficients(s.controllers[0],diagonal,offDiagonal);
        if(all(equal(offDiagonal,vec3(0.0)))) {
            vec4 driveCap=s.controllers[0].driveCap;
            vec3 delta=(acceleration*dt+driveCap.xyz*(invMass*dt)) /
                (vec3(1.0)+diagonal*(invMass*dt));
            vec3 force=driveCap.xyz-diagonal*delta;float cap=driveCap.w;
            if(dot(force,force)>cap*cap) delta=acceleration*dt+mLimit(force,cap)*(invMass*dt);
            return delta;
        }
    }
    vec3 delta=vec3(0);float scale=invMass*dt;
    for(int pass=0;pass<=s.count;pass++) {
        vec3 diagonal=vec3(1.0),offDiagonal=vec3(0.0),rhs=acceleration*dt;
        for(int i=0;i<s.count;i++) {
            rhs+=s.controllers[i].driveCap.xyz*scale;
            vec3 d,o;mCoefficients(s.controllers[i],d,o);
            diagonal+=d*scale;offDiagonal+=o*scale;
        }
        // Same symmetric LDL transpose solve as MotionBody_SolveResponse.
        float l10=offDiagonal.x/diagonal.x,l20=offDiagonal.y/diagonal.x;
        float d1=diagonal.y-l10*offDiagonal.x;
        float l21=(offDiagonal.z-l20*offDiagonal.x)/d1;
        float d2=diagonal.z-l20*offDiagonal.y-l21*l21*d1;
        float y1=rhs.y-l10*rhs.x,y2=rhs.z-l20*rhs.x-l21*y1;
        float z=y2/d2,y=y1/d1-l21*z;
        delta=vec3(rhs.x/diagonal.x-l10*y-l20*z,y,z);
        bool changed=false;
        for(int i=0;i<s.count;i++) {
            float cap=s.controllers[i].driveCap.w;
            if(cap<0.0) continue;
            vec3 force=s.controllers[i].driveCap.xyz-mControllerResponse(s.controllers[i],delta);
            if(dot(force,force)>cap*cap) {
                // Reuse the record for a fixed saturated force. A negative
                // cap marks it as fixed; zero resistance keeps later passes exact.
                s.controllers[i].driveCap=vec4(mLimit(force,cap),-1.0);
                s.controllers[i].axisResponse=vec4(0.0);
                changed=true;
            }
        }
        if(!changed) break;
    }
    return delta;
}
vec3 motionZone(vec3 p,float time) {
    if(zoneNoise.w<.5) return vec3(0);
    float strength=zoneDirectionStrength.w;
    vec3 acc=zoneDirectionStrength.xyz*strength+vec3(0,0,motionPerlin(vec3(53.9,0,0))*strength*.35);
    if(zoneNoise.x>0.0) acc+=mLegacyCurl(p*zoneNoise.y+vec3(time*zoneNoise.z,0,time*zoneNoise.z))*zoneNoise.x;
    return acc;
}

// One integration substep; component adapters provide background airflow and
// optional legacy acceleration. Persistent lane state remains in body.
void motionAdvanceStep(inout vec3 pos,inout vec3 vel,inout MotionGpuBody body,
    vec3 ordinary,vec3 externalAcceleration,float viscosity,float time,float dt,float offset) {
    float invMass=body.body0.x,mass=1.0/invMass,rho=body.body2.z>0.0?body.body2.z:1.225;
        MSample s;s.force=vec3(0);s.acc=vec3(0);s.air=ordinary;s.dragAir=vec3(0);
        s.weight=0.0;s.priority=0;s.dragK=0.0;s.buoyancy=false;s.drag=false;s.count=0;
        for(int pass=0;pass<2;pass++) for(int fi=0;fi<int(motionMeta.x);fi++)
            mSampleField(fi,pos,vel,ordinary,rho,mass,dt,offset,body,s,pass!=0);
        vec3 acceleration=body.acceleration.xyz+s.acc+(body.force.xyz+s.force)*invMass;
        acceleration+=externalAcceleration;
        acceleration+=motionZone(pos,time)*body.body1.x*body.body1.z;
        float buoyancy=!s.buoyancy && body.body2.w>0.0?rho/body.body2.w:0.0;
        acceleration.y+=9.81*body.body0.y*(buoyancy-1.0);
        vel+=mSolveControllers(s,acceleration,invMass,dt);
        if(body.body0.z>0.0) vel*=exp(-body.body0.z*dt);
        if(!s.drag) {
            if(body.body2.x>0.0 && body.body2.y>0.0) {
                float k=.5*rho*body.body2.x*body.body2.y*invMass*max(body.body1.z,0.0);
                vec3 relative=vel-s.air;vel=s.air+relative*2.0/(1.0+sqrt(1.0+4.0*k*length(relative)*dt));
            } else if(body.body1.y>0.0 && body.body1.z>0.0)
                vel=mix(vel,s.air,1.0-exp(-body.body1.y*body.body1.z*dt));
        }
        if(body.body0.w>0.0) vel=mLimit(vel,body.body0.w);
        if(s.drag && s.dragK>0.0) {
            vec3 relative=vel-s.dragAir;float k=s.dragK*invMass;
            vel=s.dragAir+relative*2.0/(1.0+sqrt(1.0+4.0*k*length(relative)*dt));
        }
        if(body.body0.w>0.0) vel=mLimit(vel,body.body0.w);
        if(viscosity>0.0) vel*=exp(-viscosity*dt);
        pos+=vel*dt;
}
