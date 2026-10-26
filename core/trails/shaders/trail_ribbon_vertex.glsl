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
    int count = clamp(u_count, 2, 60);
    int segment = gl_InstanceID;
    int node = clamp(segment + (vertexPosition.y > 0.0 ? 1 : 0), 0, count - 1);
    int base = u_slot * 60;

    // Single sliding-window pass over the SSBO: computes both parallel-transported
    // side orientation and cumulative arc-length in 1x reads instead of 5x reads.
    vec3 prevPos = nodes[base].positionRest.xyz;
    vec3 currPos = prevPos;
    vec3 nodePos = prevPos;
    vec3 side = u_right;
    float arc = 0.0, total = 0.0;

    #if TRAIL_RIBBON_STYLED
    bool cameraFacing = (u_ribbonMode == 0);
    vec3 fixedPlaneNormal = (u_ribbonMode == 1) ? vec3(0.0, 1.0, 0.0) : u_fixedNormal;
    #endif

    for (int i = 0; i < count; i++) {
        vec3 nextPos = nodes[base + min(i + 1, count - 1)].positionRest.xyz;
        if (i >= 1) {
            float segLen = length(currPos - prevPos);
            total += segLen;
            if (i <= node) arc += segLen;
        }
        if (i <= node) {
            vec3 tangent = nextPos - prevPos;
            #if TRAIL_RIBBON_STYLED
            vec3 next = cameraFacing ? cross(tangent, u_camera - currPos)
                                     : cross(tangent, fixedPlaneNormal);
            #else
            vec3 next = cross(tangent, u_camera - currPos);
            #endif
            float lenSq = dot(next, next);
            if (lenSq > 1e-12) {
                next *= inversesqrt(lenSq);
                if (dot(next, side) < 0.0) next = -next;
                side = next;
            }
            if (i == node) nodePos = currPos;
        }
        prevPos = currPos;
        currPos = nextPos;
    }

    #if TRAIL_RIBBON_STYLED
    float widthScale = u_nodeWidth[node];
    #else
    float widthScale = 1.0;
    #endif

    vec3 p = nodePos + side * (vertexPosition.x * u_width * 0.5 * widthScale);
    gl_Position = mvp * vec4(p, 1.0);
    fragTexCoord = vec2(vertexPosition.x * 0.5 + 0.5, total > 1e-6 ? arc / total : 0.0);
    fragColor = u_color;
    #if TRAIL_RIBBON_STYLED
    vSegUV = vec2(fragTexCoord.x, float(node) / float(count - 1));
    vPathMetres = total - arc;
    vColor = u_nodeColor[node];
    fragPosition = p;
    fragNormal = side;
    #endif
}
