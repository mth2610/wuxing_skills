#version 330
#include "maps/toolkit/shaders/seismic_displacement.glsl"

// Đợt E / E2 — added so ground_splat.fs can be lit by VFX point lights.
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 u_groundOffset;
uniform mat4 u_lightVP;
uniform mat4 u_staticLightVP;

out vec2 fragTexCoord;
out vec3 fragPosition;
out vec3 fragNormal;
out vec3 fragWorldPos;
out vec3 fragWorldNormal;
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;
out vec2 v_macroPatchAndField;

void main()
{
    fragTexCoord = vertexTexCoord;

    vec3 localPos = vertexPosition;
    vec3 wp = localPos + u_groundOffset;

    // Evaluate physical seismic wave deformation at this world position
    vec2 seismicSlope = vec2(0.0);
    float seismicDY = EvaluateSeismicDisplacement(wp.xz, seismicSlope);
    bool hasSeismic = (abs(seismicDY) > 1e-5 || dot(seismicSlope, seismicSlope) > 1e-8);
    if (hasSeismic) {
        localPos.y += seismicDY;
        wp.y += seismicDY;
    }

    vec4 worldVertex = vec4(localPos, 1.0);
    vec4 shaderPos = matModel * worldVertex;
    fragPosition = shaderPos.xyz;

    fragNormal = vertexNormal;
    fragWorldNormal = vertexNormal;

    fragWorldPos = wp;
    v_lightSpace = u_lightVP * shaderPos;
    v_staticLightSpace = u_staticLightVP * shaderPos;

    float patch = sin(wp.x * 0.12 + wp.z * 0.08) * 0.55
                + cos(wp.x * -0.07 + wp.z * 0.16) * 0.45;
    float macroA = sin(wp.x * 0.042 + wp.z * 0.028);
    float macroB = sin(wp.x * -0.022 + wp.z * 0.048 + 1.35);
    float field = 0.50 + 0.32 * macroA + 0.18 * macroB;
    v_macroPatchAndField = vec2(patch, field);

    gl_Position = mvp * worldVertex;
}