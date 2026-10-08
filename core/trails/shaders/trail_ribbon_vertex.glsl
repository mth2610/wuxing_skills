struct RibbonNode {vec4 positionRest,velocity,previous;};
layout(std430,binding=0) readonly buffer RibbonNodes {RibbonNode nodes[];};
in vec3 vertexPosition;
uniform mat4 mvp;
uniform vec3 u_camera;
uniform vec3 u_right;
uniform int u_slot;
uniform int u_count;
uniform float u_width;
uniform vec4 u_color;
out vec2 fragTexCoord;
out vec4 fragColor;
#if TRAIL_RIBBON_STYLED
out vec4 vColor;
out vec2 vSegUV;
out float vPathMetres;
out vec3 fragPosition;
out vec3 fragNormal;
uniform int u_ribbonMode;
uniform vec3 u_fixedNormal;
uniform vec4 u_nodeColor[60];
uniform float u_nodeWidth[60];
#endif
void main() {
    int segment=gl_InstanceID;
    int node=segment+(vertexPosition.y>0.0?1:0),base=u_slot*60;
    vec3 p=nodes[base+node].positionRest.xyz;
    // Transport the side sign from the head so adjacent segments share their
    // joint even when the path crosses the camera direction. No state readback.
    vec3 side=u_right;
    for(int i=0;i<=node;i++) {
        vec3 center=nodes[base+i].positionRest.xyz;
        vec3 tangent=nodes[base+min(i+1,u_count-1)].positionRest.xyz
                    -nodes[base+max(i-1,0)].positionRest.xyz;
        vec3 next=cross(tangent,u_camera-center);
        float len=length(next);
        if(len>1e-6) {
            next/=len;
            if(dot(next,side)<0.0) next=-next;
            side=next;
        }
    }
    #if TRAIL_RIBBON_STYLED
    if(u_ribbonMode!=0) {
        vec3 tangent=nodes[base+min(node+1,u_count-1)].positionRest.xyz-nodes[base+max(node-1,0)].positionRest.xyz;
        vec3 normal=u_ribbonMode==1?vec3(0,1,0):u_fixedNormal;
        vec3 fixedSide=cross(tangent,normal);
        if(length(fixedSide)>1e-6) side=normalize(fixedSide);
    }
    float widthScale=u_nodeWidth[node];
    #else
    float widthScale=1.0;
    #endif
    p+=side*(vertexPosition.x*u_width*0.5*widthScale);
    gl_Position=mvp*vec4(p,1);
    float arc=0.0,total=0.0;
    for(int i=1;i<u_count;i++) {
        float segmentLength=length(nodes[base+i].positionRest.xyz-nodes[base+i-1].positionRest.xyz);
        total+=segmentLength;
        if(i<=node) arc+=segmentLength;
    }
    fragTexCoord=vec2(vertexPosition.x*0.5+0.5,total>1e-6?arc/total:0.0);
    fragColor=u_color;
    #if TRAIL_RIBBON_STYLED
    vSegUV=vec2(fragTexCoord.x,float(node)/float(u_count-1));vPathMetres=total-arc;
    vColor=u_nodeColor[node];
    fragPosition=p;fragNormal=side;
    #endif
}
