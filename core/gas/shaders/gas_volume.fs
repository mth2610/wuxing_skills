#version 330
#include "core/shaders/common/vfx_composite.glsl"
#include "core/shaders/common/noise.glsl"

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
uniform int u_kind;
uniform int u_qualityTier;

/* A fixed half-step start gives neighbouring rays identical integration error;
 * after low-resolution upsampling that error reads as bands. This mobile-safe
 * interleaved hash decorrelates the phase in both screen axes. */
float Gas_RayJitter(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

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
    float travel = nearHit + stepLength * Gas_RayJitter(gl_FragCoord.xy);
    vec3 volumeSize = max(u_volumeMax - u_volumeMin, vec3(1e-5));
    vec3 accumulatedBody = vec3(0.0);
    vec3 accumulatedEmission = vec3(0.0);
    float accumulatedCoreRadiance = 0.0;
    float coverage = 0.0;

    for (int i = 0; i < 48; ++i) {
        if (i >= stepCount || coverage > 0.985) break;
        vec3 worldPosition = rayOrigin + rayDirection * travel;
        vec3 localPosition = (worldPosition - u_volumeMin) / volumeSize;
        vec4 gas = Gas_SampleVolume(localPosition);
        float rawDensity = max(gas.r, 0.0);
        float density = rawDensity;
        /* World-space 3D noise breaks voxel-soft blobs without introducing the
         * plane-wave stripes caused by sin(dot(position, k)). Multiplication
         * keeps the two bands distinct instead of averaging fbm back to grey. */
        vec3 coarseDomain = worldPosition * 1.35 + vec3(3.7, 11.2, -5.4);
        vec3 detailDomain = worldPosition * 3.10 + vec3(-8.1, 2.6, 14.7);
        float coarseNoise = (u_qualityTier >= 3) ? fbm3(coarseDomain)
                                                 : vnoise3(coarseDomain);
        float detailNoise = (u_qualityTier >= 3) ? fbm3(detailDomain)
                                                 : vnoise3(detailDomain);
        float breakup = clamp(smoothstep(0.24, 0.76, coarseNoise) *
                              mix(0.62, 1.16, detailNoise), 0.0, 1.0);
        if (u_kind == 1) {
            density = max(density - (1.0 - breakup) * 0.14, 0.0) *
                      mix(0.78, 1.35, breakup);
        } else {
            density = max(density - (1.0 - breakup) * 0.06, 0.0) *
                      mix(0.72, 1.24, breakup);
        }
        float densityAlpha = 1.0 - exp(-density * u_densityScale * stepLength);
        float heat = max(gas.g * 0.35 + gas.b, 0.0);
        float coolSmoke = 1.0 - smoothstep(0.08, 0.62, heat);
        float bodyOpacity = mix(0.32, 0.52, coolSmoke);
        if (u_kind != 1) bodyOpacity = 1.0;
        float sampleAlpha = densityAlpha * bodyOpacity;
        /* Extinction and radiance are different signals. Reaction-rich fire
         * emits through the full density footprint; cooling density keeps only
         * its smoke/body coverage. Coupling both to bodyOpacity made every
         * attempt to remove the dark carrier dim the flame by the same amount. */
        float flameTransport = smoothstep(0.08, 0.55, heat) *
                               smoothstep(0.06, 0.60, gas.b);
        float emissionAlpha = mix(sampleAlpha, densityAlpha, flameTransport);
        float transmittance = 1.0 - coverage;

        vec3 gradientStep = 1.0 / max(u_gridSize - 1.0, vec3(1.0));
        float densityAbove = Gas_SampleVolume(localPosition + vec3(0.0, gradientStep.y, 0.0)).r;
        float softLight = clamp(0.42 + (density - densityAbove) * 1.8 + localPosition.y * 0.18,
                                0.2, 1.0);
        float bodyLight = softLight;
        vec3 bodyTone = u_bodyColor * mix(0.72, 1.18, coarseNoise);
        if (u_kind == 1) {
            vec3 smokeBody = mix(u_bodyColor, vec3(0.22, 0.23, 0.25), 0.92);
            vec3 hotBody = mix(u_bodyColor, u_emissionColor * 0.55, 0.82);
            bodyTone = mix(smokeBody, hotBody, 1.0 - coolSmoke) *
                       mix(0.88, 1.16, coarseNoise);
            /* Fire is self-lit. Let cooling smoke retain volume shading, but
             * never multiply hot flame by the 0.2 shadow floor: that creates
             * a muddy brown/black rim even when its emission ramp is bright. */
            float litSmoke = max(softLight, 0.50);
            bodyLight = mix(litSmoke, 1.0, 1.0 - coolSmoke);
        }
        accumulatedBody += transmittance * sampleAlpha * bodyTone * bodyLight;
        vec3 emittedColor = u_emissionColor;
        float emittedHeat = heat;
        if (u_kind == 1) {
            float shoulderWeight = smoothstep(0.10, 0.90, heat) * 0.72;
            float hotProduct = gas.b * gas.g;
            float hotGate = smoothstep(0.20, 0.72, hotProduct);
            float densityGate = smoothstep(0.30, 0.75, rawDensity);
            float coreWeight = min(hotGate, densityGate) *
                               mix(0.35, 1.0,
                                   smoothstep(0.40, 0.75, detailNoise));
            accumulatedCoreRadiance += transmittance * densityAlpha * coreWeight;
            vec3 hotColor = mix(u_emissionColor, vec3(1.0, 0.58, 0.10),
                                shoulderWeight);
            emittedColor = mix(hotColor, vec3(1.0, 0.92, 0.72), coreWeight);
            /* Body opacity is deliberately lower for fire. Compensate energy
             * here, on the already-narrow core mask, rather than broadening
             * emission transport and washing the whole flame beige. */
            emittedHeat *= mix(0.34, 1.85, coreWeight);
        }
        accumulatedEmission += transmittance * emissionAlpha * emittedColor *
                               emittedHeat * u_emissionGain;
        coverage += transmittance * sampleAlpha;
        travel += stepLength;
    }
    if (u_kind == 1) {
        /* A ray-integrated volume can look bright while every fragment remains
         * below the post-process threshold. Preserve the hottest sparse voxel
         * as a narrow HDR seed so bloom comes from the real scene pipeline. */
        vec3 bloomColor = mix(u_emissionColor, vec3(1.0, 0.92, 0.72), 0.72);
        accumulatedEmission += bloomColor * accumulatedCoreRadiance * 7.20;
    }
    if (coverage <= 0.001) discard;
    vec3 unpremultipliedBody = accumulatedBody / max(coverage, 1e-5);
    finalColor = VFX_ResolvePremultiplied(unpremultipliedBody, 1.0, coverage,
                                          accumulatedEmission, 1.0, 1.0);
}
