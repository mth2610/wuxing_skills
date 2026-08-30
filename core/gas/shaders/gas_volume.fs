#version 330
#include "core/shaders/common/vfx_composite.glsl"

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform sampler2D u_sceneDepthTex;
uniform vec2 u_atlasInvSize;
uniform vec3 u_gridSize;
uniform int u_tilesX;
uniform int u_hasSceneDepth;
uniform mat4 u_inverseProjection;
uniform mat4 u_viewToWorld;
uniform vec3 u_cameraPosition;
uniform vec3 u_cameraForward;
uniform int u_orthographic;
uniform vec3 u_volumeMin;
uniform vec3 u_volumeMax;
uniform int u_steps;
uniform float u_densityScale;
uniform vec3 u_bodyColor;
uniform vec3 u_emissionColor;
uniform float u_emissionGain;

vec3 Gas_ReconstructWorld(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = u_inverseProjection * clip;
    float signedInverseW = (view.w < 0.0 ? -1.0 : 1.0) /
                           max(abs(view.w), 1e-6);
    return (u_viewToWorld * vec4(view.xyz * signedInverseW, 1.0)).xyz;
}

float Gas_SafeReciprocal(float value) {
    if (abs(value) < 1e-6) return value < 0.0 ? -1e6 : 1e6;
    return 1.0 / value;
}

bool Gas_IntersectBox(vec3 origin, vec3 direction, out float nearHit, out float farHit) {
    vec3 inverseDirection = vec3(Gas_SafeReciprocal(direction.x),
                                 Gas_SafeReciprocal(direction.y),
                                 Gas_SafeReciprocal(direction.z));
    vec3 first = (u_volumeMin - origin) * inverseDirection;
    vec3 second = (u_volumeMax - origin) * inverseDirection;
    vec3 lower = min(first, second);
    vec3 upper = max(first, second);
    nearHit = max(max(lower.x, lower.y), lower.z);
    farHit = min(min(upper.x, upper.y), upper.z);
    return farHit > max(nearHit, 0.0);
}

vec4 Gas_SampleSlice(vec2 cell, float slice) {
    float tileX = mod(slice, float(u_tilesX));
    float tileY = floor(slice / float(u_tilesX));
    vec2 pixel = vec2(tileX * u_gridSize.x, tileY * u_gridSize.y) + cell + 0.5;
    return texture(texture0, pixel * u_atlasInvSize);
}

vec4 Gas_SampleVolume(vec3 localPosition) {
    vec3 cell = clamp(localPosition, 0.0, 1.0) * (u_gridSize - 1.0);
    float z0 = floor(cell.z);
    float z1 = min(z0 + 1.0, u_gridSize.z - 1.0);
    return mix(Gas_SampleSlice(cell.xy, z0), Gas_SampleSlice(cell.xy, z1), fract(cell.z));
}

void main() {
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec3 farWorld = Gas_ReconstructWorld(uv, 1.0);
    vec3 rayOrigin = u_cameraPosition;
    vec3 rayDirection = normalize(farWorld - u_cameraPosition);
    if (u_orthographic != 0) {
        rayOrigin = Gas_ReconstructWorld(uv, 0.0);
        rayDirection = normalize(u_cameraForward);
    }

    float nearHit;
    float farHit;
    if (!Gas_IntersectBox(rayOrigin, rayDirection, nearHit, farHit)) discard;
    nearHit = max(nearHit, 0.0);
    if (u_hasSceneDepth != 0) {
        float sceneDepth = texture(u_sceneDepthTex, uv).r;
        if (sceneDepth < 0.99999) {
            vec3 sceneWorld = Gas_ReconstructWorld(uv, sceneDepth);
            float sceneDistance = dot(sceneWorld - rayOrigin, rayDirection);
            farHit = min(farHit, sceneDistance);
        }
    }
    if (farHit <= nearHit) discard;

    int stepCount = clamp(u_steps, 1, 48);
    float stepLength = (farHit - nearHit) / float(stepCount);
    float travel = nearHit + stepLength * 0.5;
    vec3 volumeSize = max(u_volumeMax - u_volumeMin, vec3(1e-5));
    vec3 accumulatedBody = vec3(0.0);
    vec3 accumulatedEmission = vec3(0.0);
    float coverage = 0.0;

    for (int i = 0; i < 48; ++i) {
        if (i >= stepCount || coverage > 0.985) break;
        vec3 worldPosition = rayOrigin + rayDirection * travel;
        vec3 localPosition = (worldPosition - u_volumeMin) / volumeSize;
        vec4 gas = Gas_SampleVolume(localPosition);
        float density = max(gas.r, 0.0);
        float sampleAlpha = 1.0 - exp(-density * u_densityScale * stepLength);
        float transmittance = 1.0 - coverage;

        vec3 gradientStep = 1.0 / max(u_gridSize - 1.0, vec3(1.0));
        float densityAbove = Gas_SampleVolume(localPosition + vec3(0.0, gradientStep.y, 0.0)).r;
        float softLight = clamp(0.42 + (density - densityAbove) * 1.8 + localPosition.y * 0.18,
                                0.2, 1.0);
        accumulatedBody += transmittance * sampleAlpha * u_bodyColor * softLight;
        float heat = max(gas.g * 0.35 + gas.b, 0.0);
        accumulatedEmission += transmittance * sampleAlpha * u_emissionColor *
                               heat * u_emissionGain;
        coverage += transmittance * sampleAlpha;
        travel += stepLength;
    }
    if (coverage <= 0.001) discard;
    vec3 unpremultipliedBody = accumulatedBody / max(coverage, 1e-5);
    finalColor = VFX_ResolvePremultiplied(unpremultipliedBody, 1.0, coverage,
                                          accumulatedEmission, 1.0, 1.0);
}
