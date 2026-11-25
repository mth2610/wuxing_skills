#version 330
#include "core/shaders/common/vs_header.glsl"

// DrawCoreSphere is immediate-mode geometry inside BeginMode3D: vertexPosition and
// vertexNormal arrive in VIEW space (already transformed by matView on the CPU).
// Calling VS_FinalOutput would apply matModel a second time and skew the sphere.
// Reconstruct world-space position/normal using u_camPos/u_camRight/u_camUp/u_camForward
// for stable procedural motifs and ground-cut height, while outputting gl_Position = mvp * viewPos.

uniform vec3  u_camPos;
uniform vec3  u_camRight;
uniform vec3  u_camUp;
uniform vec3  u_camForward;
uniform vec3  u_shellCenter;
uniform float u_shellRadius;
uniform float u_shieldTime;
uniform float u_speed;
uniform int   u_preset;
uniform float u_groundEnabled;
uniform float u_groundY;
uniform float u_hasImpacts;
uniform vec4  u_impacts[4]; // xyz = unit dir, w = age (seconds, < 0 = inactive)

out vec3  vUnitPos;
out vec3  vWorldPos;
out vec3  vWorldNormal;
out vec3  vViewPos;
out vec3  vViewNormal;
out float vWaveDisp;

float evalImpactDisp(vec3 unitPos)
{
    if (u_hasImpacts < 0.5) return 0.0;
    float dispSum = 0.0;
    for (int i = 0; i < 4; i++) {
        float age = u_impacts[i].w;
        if (age >= 0.0 && age < 2.6) {
            float d = acos(clamp(dot(unitPos, normalize(u_impacts[i].xyz)), -1.0, 1.0));
            float radius = age * 1.65;
            float decay = exp(-age * 1.45);
            dispSum += sin((d - radius) * 26.0) * exp(-abs(d - radius) * 6.0) * decay * 0.045;
        }
    }
    return clamp(dispSum, -0.10, 0.10);
}

void main()
{
    vec3 viewPos  = vertexPosition;
    vec3 viewN    = normalize(vertexNormal);
    vec3 worldN   = normalize(u_camRight * viewN.x + u_camUp * viewN.y - u_camForward * viewN.z);
    vec3 worldPos = u_camPos + u_camRight * viewPos.x + u_camUp * viewPos.y - u_camForward * viewPos.z;
    vec3 unitPos  = worldN;

    float impDisp = evalImpactDisp(unitPos);
    float disp = impDisp;
    float t = u_shieldTime * u_speed;

    // Kim Chung (preset 2): acoustic standing-wave harmonic vibration on impact
    if (u_preset == 2 && u_hasImpacts > 0.5) {
        float bellVib = 0.0;
        float az = atan(unitPos.z, unitPos.x);
        for (int i = 0; i < 4; i++) {
            float age = u_impacts[i].w;
            if (age >= 0.0 && age < 2.2) {
                float decay = exp(-age * 2.5);
                float modeA = sin(az * 4.0) * cos(unitPos.y * 5.0) * sin(age * 46.0);
                float modeB = cos(az * 8.0 + 0.7) * sin(unitPos.y * 8.0) * sin(age * 68.0);
                bellVib += (modeA * 0.010 + modeB * 0.005) * decay;
            }
        }
        disp = impDisp * 0.75 + bellVib;
    }
    // Thanh Phong (preset 8): subtle localized passing-gust flutter
    else if (u_preset == 8) {
        float az = atan(unitPos.z, unitPos.x);
        float poleFade = smoothstep(0.99, 0.78, abs(unitPos.y));
        float gustEnv = 0.5 + 0.5 * sin(az * 2.0 - unitPos.y * 3.0 - t * 1.9);
        float gustFlutter = (
            sin(unitPos.y * 10.5 - t * 5.4 + az * 3.0) * 0.007 +
            cos(unitPos.y * 6.2 - t * 3.8 - az * 2.0) * 0.005
        ) * gustEnv * poleFade;
        disp = impDisp * 1.08 + gustFlutter;
    }

    float hNorm = (worldPos.y - u_groundY) / max(u_shellRadius, 0.1);
    float pinFactor = (u_groundEnabled > 0.5) ? smoothstep(0.0, 0.22, max(hNorm, 0.0)) : 1.0;
    disp *= pinFactor;

    vec3 displacedView  = viewPos + viewN * (disp * u_shellRadius);
    vec3 displacedWorld = worldPos + worldN * (disp * u_shellRadius);

    vUnitPos     = unitPos;
    vWorldPos    = displacedWorld;
    vWorldNormal = worldN;
    vViewPos     = displacedView;
    vViewNormal  = viewN;
    vWaveDisp    = disp;

    gl_Position = mvp * vec4(displacedView, 1.0);
}
