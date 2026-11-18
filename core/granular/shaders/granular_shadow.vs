#version 430 core
struct GranularGrain {
    vec4 position_radius;
    vec4 velocity_life;
    vec4 meta_ao;
    vec4 aux_pad;
};

layout(std430, binding = 0) readonly buffer GranularState { GranularGrain grains[]; };

in vec3 vertexPosition; // .z = vertex index 0..35

uniform mat4 u_lightVP;
uniform vec3 u_camPos;
uniform float u_maxDrawDist;

#include "core/granular/shaders/granular_mesh.glsl"

void main()
{
    GranularGrain g = grains[gl_InstanceID];
    float radius = g.position_radius.w;
    float life   = g.velocity_life.w;
    if (life <= 0.0 || radius <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    if (u_maxDrawDist > 0.0) {
        vec3 d = g.position_radius.xyz - u_camPos;
        if (dot(d, d) > u_maxDrawDist * u_maxDrawDist) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
    }

    int vid = int(vertexPosition.z + 0.5);
    int matId = int(g.meta_ao.y + 0.5);
    bool isClod    = (matId == 1 || matId == 2);
    bool isLamina  = (matId == 5 || matId == 6 || matId == 8);
    bool isTwig    = (matId == 7);
    bool isCrystal = (matId == 9 || matId == 11);
    bool isMetal   = (matId == 12);

    // Skip sub-texel FINE sand/snow in shadow map to prevent 30Hz shimmer
    if (!(isClod || isLamina || isTwig || isCrystal || isMetal)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    int maxVerts = isCrystal ? 36 : 24;
    if (vid >= maxVerts) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    float seed     = g.meta_ao.x;
    float flags    = g.meta_ao.w;
    float rotPhase = g.aux_pad.y;
    bool resting   = (flags > 0.5 && flags < 1.5);
    float scaleR   = radius * (isLamina ? 2.10 : (isClod ? 1.38 : (isTwig ? 1.85 : 1.70)))
                   * mix(0.80, 1.28, fract(seed * 17.31));

    vec3 spinAxis = normalize(vec3(
        fract(seed * 13.1) - 0.5,
        fract(seed * 29.7) - 0.5,
        fract(seed * 47.3) - 0.5
    ) + vec3(0.01, 0.1, 0.01));

    vec3 localPt;
    vec2 dummyUv = vec2(0.0);
    if (isLamina) {
        bool isPetal = (matId == 8);
        float yaw   = seed * 6.2831853 + (resting ? 0.0 : rotPhase * 0.7);
        float pitch = resting ? 0.10 * sin(seed * 41.0) : (rotPhase + seed * 6.283);
        float roll  = resting ? 0.08 * cos(seed * 29.0) : sin(rotPhase * 1.35 + seed * 19.0) * 0.85;
        mat3 frame = eulerFrame(yaw, pitch, roll);
        localPt = frame * leafVertex(vid, seed, isPetal, dummyUv);
    } else if (isClod) {
        const int octTris[24] = int[24](
            2, 4, 0,   2, 0, 5,   2, 5, 1,   2, 1, 4,
            3, 0, 4,   3, 5, 0,   3, 1, 5,   3, 4, 1
        );
        float ang = resting ? seed * 6.2831853 : (rotPhase + seed * 6.2831853);
        localPt = rotateAxisAngle(clodVertex(octTris[vid], seed), spinAxis, ang);
    } else if (isTwig) {
        float ang = resting ? seed * 6.2831853 : (rotPhase + seed * 6.2831853);
        vec3 twigAxis = resting ? vec3(0.0, 1.0, 0.0) : spinAxis;
        localPt = rotateAxisAngle(twigVertex(vid, seed, dummyUv), twigAxis, ang);
    } else if (isCrystal) {
        float ang = resting ? seed * 6.2831853 : (rotPhase * 1.15 + seed * 6.2831853);
        localPt = rotateAxisAngle(crystalVertex(vid, seed, dummyUv), spinAxis, ang);
    } else {
        float ang = resting ? seed * 6.2831853 : (rotPhase * 1.45 + seed * 6.2831853);
        localPt = rotateAxisAngle(metalShardVertex(vid, seed, dummyUv), spinAxis, ang);
    }

    vec3 worldPt = g.position_radius.xyz + localPt * scaleR;
    if (resting && isLamina) worldPt.y += 0.012;
    gl_Position = u_lightVP * vec4(worldPt, 1.0);
}
