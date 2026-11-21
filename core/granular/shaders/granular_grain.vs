#version 430 core
struct GranularGrain {
    vec4 position_radius;
    vec4 velocity_life;
    vec4 meta_ao;
    vec4 aux_pad; // x=shearWork, y=rotPhase, z=angSpeed, w=reserved
};

layout(std430, binding = 0) readonly buffer GranularState { GranularGrain grains[]; };

in vec3 vertexPosition; // .xy = quad corner (0..5), .z = vertex index 0..35

uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat4 u_lightVP;
uniform vec3 u_camPos;
uniform float u_aspectRatio;
uniform float u_time;
uniform float u_maxDrawDist;

out vec2 v_corner;
out vec3 v_worldNormal;
out vec3 v_worldPos;
out vec3 v_localPos;
out vec4 v_shadowCoord;
out float v_life;
out float v_seed;
out float v_contactAO;
out float v_materialId;
out float v_speed;
out float v_flags;
out float v_tiltShade;

#include "core/granular/shaders/granular_mesh.glsl"

void main()
{
    GranularGrain g = grains[gl_InstanceID];
    float radius = g.position_radius.w;
    v_life = g.velocity_life.w;
    v_seed = g.meta_ao.x;
    v_materialId = g.meta_ao.y;
    v_contactAO = g.meta_ao.z;
    v_flags = g.meta_ao.w;
    v_speed = length(g.velocity_life.xyz);
    v_tiltShade = 1.0;
    v_worldNormal = vec3(0.0, 1.0, 0.0);
    v_worldPos = g.position_radius.xyz;
    v_localPos = vec3(0.0);
    v_shadowCoord = vec4(0.0);
    v_corner = vertexPosition.xy;

    if (v_life <= 0.0 || radius <= 0.0) {
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
    int matId = int(v_materialId + 0.5);
    bool isClod       = (matId == 1 || matId == 2);
    bool isLamina     = (matId == 5 || matId == 6 || matId == 8);
    bool isTwig       = (matId == 7);
    bool isCrystal    = (matId == 9 || matId == 11);
    bool isMetal      = (matId == 12);
    bool isShale      = (matId == 13);
    bool isFineSand   = !(isClod || isLamina || isTwig || isCrystal || isMetal || isShale);

    // Cull unused vertices for morphologies with fewer triangles
    int maxVerts = isFineSand ? 6 : (isCrystal ? 36 : 24);
    if (vid >= maxVerts) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    if (isFineSand) {
        vec3 centerView = (u_view * vec4(g.position_radius.xyz, 1.0)).xyz;
        float r = g.position_radius.w * 1.48 * mix(0.80, 1.25, fract(v_seed * 17.31));
        vec3 velView = mat3(u_view) * g.velocity_life.xyz;
        float speedXY = length(velView.xy);
        float stretch = clamp(1.0 + speedXY * 0.24, 1.0, 2.2);
        float crossScale = inversesqrt(stretch) * mix(0.76, 1.0, u_aspectRatio);

        float rotAngle = v_seed * 6.2831853;
        vec2 axis = speedXY > 0.2 ? velView.xy / speedXY : vec2(cos(rotAngle), sin(rotAngle));
        vec2 perp = vec2(-axis.y, axis.x);
        vec2 offsetView = (axis * (vertexPosition.x * stretch) + perp * (vertexPosition.y * crossScale)) * r;

        // Subpixel AA floor so distant grains never shimmer/pop
        float distCam = max(-centerView.z, 1.0);
        float minViewR = distCam * 0.0014;
        if (length(offsetView) < minViewR) {
            offsetView = normalize(offsetView + vec2(1e-5)) * minViewR;
        }
        v_worldPos = g.position_radius.xyz;
        v_shadowCoord = u_lightVP * vec4(v_worldPos, 1.0);
        gl_Position = u_projection * vec4(centerView + vec3(offsetView, 0.0), 1.0);
        return;
    }

    // ── True 3D Procedural Meshes (Clods, Leaves, Twigs, Crystals, Metal) ──
    float rotPhase = g.aux_pad.y;
    bool resting = (v_flags > 0.5 && v_flags < 1.5);
    float scaleR = g.position_radius.w * (isLamina ? 2.10 : (isClod ? 1.38 : (isTwig ? 1.85 : (isShale ? 1.62 : 1.70))))
                 * mix(0.80, 1.28, fract(v_seed * 17.31));

    // Deterministic 3D rotation frame
    vec3 spinAxis = normalize(vec3(
        fract(v_seed * 13.1) - 0.5,
        fract(v_seed * 29.7) - 0.5,
        fract(v_seed * 47.3) - 0.5
    ) + vec3(0.01, 0.1, 0.01));

    int triBase = (vid / 3) * 3;
    vec3 pA, pB, pC;
    vec2 uvA = vec2(0.0), uvB = vec2(0.0), uvC = vec2(0.0);
    int cornerIdx = vid % 3;

    if (isLamina) {
        bool isPetal = (matId == 8);
        float yaw   = v_seed * 6.2831853 + (resting ? 0.0 : rotPhase * 0.7);
        float pitch = resting ? 0.10 * sin(v_seed * 41.0) : (rotPhase + v_seed * 6.283);
        float roll  = resting ? 0.08 * cos(v_seed * 29.0) : sin(rotPhase * 1.35 + v_seed * 19.0) * 0.85;
        mat3 frame = eulerFrame(yaw, pitch, roll);

        pA = frame * leafVertex(triBase + 0, v_seed, isPetal, uvA);
        pB = frame * leafVertex(triBase + 1, v_seed, isPetal, uvB);
        pC = frame * leafVertex(triBase + 2, v_seed, isPetal, uvC);

        vec3 viewUp = mat3(u_view) * frame[2];
        v_tiltShade = 0.68 + 0.32 * abs(viewUp.z);
    } else if (isClod) {
        const int octTris[24] = int[24](
            2, 4, 0,   2, 0, 5,   2, 5, 1,   2, 1, 4,
            3, 0, 4,   3, 5, 0,   3, 1, 5,   3, 4, 1
        );
        float ang = resting ? v_seed * 6.2831853 : (rotPhase + v_seed * 6.2831853);
        pA = rotateAxisAngle(clodVertex(octTris[triBase + 0], v_seed), spinAxis, ang);
        pB = rotateAxisAngle(clodVertex(octTris[triBase + 1], v_seed), spinAxis, ang);
        pC = rotateAxisAngle(clodVertex(octTris[triBase + 2], v_seed), spinAxis, ang);
    } else if (isTwig) {
        float ang = resting ? v_seed * 6.2831853 : (rotPhase + v_seed * 6.2831853);
        vec3 twigAxis = resting ? vec3(0.0, 1.0, 0.0) : spinAxis;
        pA = rotateAxisAngle(twigVertex(triBase + 0, v_seed, uvA), twigAxis, ang);
        pB = rotateAxisAngle(twigVertex(triBase + 1, v_seed, uvB), twigAxis, ang);
        pC = rotateAxisAngle(twigVertex(triBase + 2, v_seed, uvC), twigAxis, ang);
    } else if (isCrystal) {
        float ang = resting ? v_seed * 6.2831853 : (rotPhase * 1.15 + v_seed * 6.2831853);
        pA = rotateAxisAngle(crystalVertex(triBase + 0, v_seed, uvA), spinAxis, ang);
        pB = rotateAxisAngle(crystalVertex(triBase + 1, v_seed, uvB), spinAxis, ang);
        pC = rotateAxisAngle(crystalVertex(triBase + 2, v_seed, uvC), spinAxis, ang);
    } else if (isShale) {
        float ang = resting ? v_seed * 6.2831853 : (rotPhase * 1.25 + v_seed * 6.2831853);
        vec3 shaleAxis = resting ? vec3(0.0, 1.0, 0.0) : spinAxis;
        pA = rotateAxisAngle(shaleVertex(triBase + 0, v_seed, uvA), shaleAxis, ang);
        pB = rotateAxisAngle(shaleVertex(triBase + 1, v_seed, uvB), shaleAxis, ang);
        pC = rotateAxisAngle(shaleVertex(triBase + 2, v_seed, uvC), shaleAxis, ang);
    } else {
        // Metal shard
        float ang = resting ? v_seed * 6.2831853 : (rotPhase * 1.45 + v_seed * 6.2831853);
        pA = rotateAxisAngle(metalShardVertex(triBase + 0, v_seed, uvA), spinAxis, ang);
        pB = rotateAxisAngle(metalShardVertex(triBase + 1, v_seed, uvB), spinAxis, ang);
        pC = rotateAxisAngle(metalShardVertex(triBase + 2, v_seed, uvC), spinAxis, ang);
    }

    vec3 localPt = (cornerIdx == 0) ? pA : ((cornerIdx == 1) ? pB : pC);
    v_corner     = (cornerIdx == 0) ? uvA : ((cornerIdx == 1) ? uvB : uvC);
    v_localPos   = localPt;

    vec3 rawN = cross(pB - pA, pC - pA);
    float nLen2 = dot(rawN, rawN);
    v_worldNormal = (nLen2 > 1e-8) ? (rawN * inversesqrt(nLen2)) : vec3(0.0, 1.0, 0.0);

    vec3 worldPt = g.position_radius.xyz + localPt * scaleR;
    if (resting && isLamina) worldPt.y += 0.012;
    v_worldPos = worldPt;
    v_shadowCoord = u_lightVP * vec4(worldPt, 1.0);

    vec3 pView = (u_view * vec4(worldPt, 1.0)).xyz;
    gl_Position = u_projection * vec4(pView, 1.0);
}
