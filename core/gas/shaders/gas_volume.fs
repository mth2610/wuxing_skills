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
uniform sampler2D u_bgLuma;
uniform int u_hasBgLuma;
uniform float u_bgAdapt;
uniform float u_detailStrength;
uniform float u_shadowStrength;

const vec3 GAS_BRIGHT_BODY_GAIN = vec3(0.72, 0.78, 0.66);

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
    float backgroundLuma = 0.0;
    if (u_hasBgLuma != 0) backgroundLuma = texture(u_bgLuma, uv).r;
    float backgroundAdapt = smoothstep(0.22, 0.88, backgroundLuma) *
                            clamp(u_bgAdapt, 0.0, 1.0);
    vec3 lightDirection = normalize(vec3(0.45, 0.78, 0.32));
    float forwardScatter = pow(max(dot(rayDirection, -lightDirection), 0.0), 2.0);

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
        float coarseNoise;
        float detailNoise;
        if (u_qualityTier <= 1) {
            coarseNoise = vnoise3(coarseDomain);
            /* LOW spends one noise evaluation per step. This irrational remap
             * is decorrelation, not new spatial detail; MED/HIGH retain the
             * independent second field. */
            detailNoise = fract(coarseNoise * 1.618 +
                                dot(localPosition, vec3(0.31, 0.57, 0.83)));
        } else if (u_qualityTier >= 3) {
            coarseNoise = fbm3(coarseDomain);
            detailNoise = fbm3(detailDomain);
        } else {
            coarseNoise = vnoise3(coarseDomain);
            detailNoise = vnoise3(detailDomain);
        }
        float breakup = clamp(smoothstep(0.24, 0.76, coarseNoise) *
                              mix(0.62, 1.16, detailNoise), 0.0, 1.0);
        float shapedDensity;
        if (u_kind == 1) {
            shapedDensity = max(density - (1.0 - breakup) * 0.14, 0.0) *
                            mix(0.78, 1.35, breakup);
        } else {
            shapedDensity = max(density - (1.0 - breakup) * 0.06, 0.0) *
                            mix(0.72, 1.24, breakup);
        }
        float detailStrength = clamp(u_detailStrength, 0.0, 2.0);
        density = mix(rawDensity, shapedDensity, min(detailStrength, 1.0));
        density *= mix(1.0, mix(0.86, 1.14, breakup),
                       max(detailStrength - 1.0, 0.0));
        float brightDensityShape = mix(0.78, 1.38, smoothstep(0.08, 0.52, density));
        density *= mix(1.0, brightDensityShape, backgroundAdapt);
        float densityAlpha = 1.0 - exp(-density * u_densityScale * stepLength);
        float heat = max(gas.g * 0.35 + gas.b, 0.0);
        float flameBody = smoothstep(0.05, 0.55, heat);
        float fireActivity = smoothstep(0.10, 0.60, heat) *
                             smoothstep(0.06, 0.55, gas.b);
        float energyActivity = smoothstep(0.08, 0.52, heat) *
                               smoothstep(0.04, 0.45, gas.b);
        float bodyOpacity = 1.0;
        if (u_kind == 1) bodyOpacity = 0.26 * fireActivity;
        else if (u_kind == 2) bodyOpacity = 0.28 * energyActivity;
        float sampleAlpha = densityAlpha * bodyOpacity;
        /* Extinction and radiance are different signals. Reaction-rich gas
         * emits through the full density footprint; inactive FIRE/ENERGY
         * density contributes no smoke-like body coverage. */
        float flameTransport = smoothstep(0.08, 0.55, heat) *
                               smoothstep(0.06, 0.60, gas.b);
        float emissionAlpha = mix(sampleAlpha, densityAlpha, flameTransport);
        float transmittance = 1.0 - coverage;

        vec3 gradientStep = 1.0 / max(u_gridSize - 1.0, vec3(1.0));
        vec3 lightProbeOffset = lightDirection * gradientStep;
        float densityTowardLight = Gas_SampleVolume(localPosition + lightProbeOffset).r;
        float softLight = clamp(0.38 + (density - densityTowardLight) *
                                (2.1 * clamp(u_shadowStrength, 0.0, 2.0)) +
                                localPosition.y * 0.16 + forwardScatter * 0.12,
                                0.2, 1.0);
        float bodyLight = softLight;
        vec3 bodyTone = u_bodyColor * mix(0.72, 1.18, coarseNoise);
        if (u_kind == 0) {
            /* The smoke preset is white, and material tint is only a subtle
             * bias. A high lighting floor preserves grey internal depth without
             * turning the volume into black soot. */
            vec3 smokeBase = mix(vec3(0.84, 0.86, 0.89), u_bodyColor, 0.20);
            bodyTone = smokeBase * mix(0.92, 1.08, coarseNoise);
            bodyLight = max(softLight, 0.62);
        } else if (u_kind == 1) {
            /* GAS_FIRE is flame matter, not an implicit smoke layer. Residual
             * low-reaction density fades with a warm tint; compositions that
             * need black smoke spawn a separate GAS_SMOKE volume. */
            vec3 coolBody = mix(u_bodyColor, u_emissionColor * 0.46, 0.68);
            vec3 hotBody = mix(u_bodyColor, u_emissionColor * 0.72, 0.88);
            bodyTone = mix(coolBody, hotBody, flameBody) *
                       mix(0.94, 1.08, coarseNoise);
            float litResidue = max(softLight, 0.55);
            bodyLight = mix(litResidue, 1.0, flameBody);
        } else if (u_kind == 2) {
            /* Plasma carries its hue through self-emission. Reusing the dark
             * body preset as an absorptive shell creates a smoke rim. */
            bodyTone = mix(u_emissionColor * 0.28,
                           u_emissionColor * 0.62, energyActivity);
            bodyLight = mix(0.72, 1.0, energyActivity);
        }
        float brightBodyGain = u_kind == 0 ? GAS_BRIGHT_BODY_GAIN.x :
                               (u_kind == 1 ? GAS_BRIGHT_BODY_GAIN.y :
                                              GAS_BRIGHT_BODY_GAIN.z);
        bodyTone *= mix(1.0, brightBodyGain, backgroundAdapt);
        accumulatedBody += transmittance * sampleAlpha * bodyTone * bodyLight;
        vec3 emittedColor = u_emissionColor;
        float emittedHeat = heat;
        float visibleCoreWeight = 0.0;
        if (u_kind == 1) {
            float shoulderWeight = smoothstep(0.10, 0.72, heat);
            float hotProduct = gas.b * gas.g;
            float hotGate = smoothstep(0.34, 0.78, hotProduct);
            float densityGate = smoothstep(0.30, 0.72, rawDensity);
            float detailCore = mix(0.58, 1.0,
                                   smoothstep(0.28, 0.82, detailNoise));
            float coarseCore = mix(0.72, 1.0,
                                   smoothstep(0.32, 0.80, coarseNoise));
            /* Keep the coherent thermal mask alive at every noise value, but
             * reserve headroom for the orange/yellow carrier. A unit-strength
             * weight made every hot ray resolve to cream-white after the
             * squared ray integral, even though its spatial selection was
             * correct. */
            float coreWeight = hotGate * densityGate * detailCore * coarseCore * 0.42;
            accumulatedCoreRadiance += transmittance * densityAlpha * coreWeight;
            visibleCoreWeight = coreWeight;
            vec3 hotColor = mix(u_emissionColor, vec3(1.0, 0.62, 0.12),
                                shoulderWeight);
            emittedColor = mix(hotColor, vec3(1.0, 0.88, 0.55), coreWeight);
            /* Body opacity is deliberately lower for fire. Compensate energy
             * here, on the already-narrow core mask, rather than broadening
             * emission transport and washing the whole flame beige. */
            emittedHeat *= mix(0.34, 1.85, coreWeight);
        } else if (u_kind == 2) {
            /* ENERGY follows the same ownership rule as FIRE: simulation
             * channels select the core; procedural fields only roughen its
             * edge. A thresholded detailNoise term printed detached white
             * sparks throughout otherwise uniform plasma. */
            float energyGate = smoothstep(0.30, 0.75, heat) *
                               smoothstep(0.16, 0.58, gas.b);
            float energyDensityGate = smoothstep(0.25, 0.68, rawDensity);
            float energyDetailCore = mix(0.72, 1.0,
                                         smoothstep(0.28, 0.82, detailNoise));
            float energyCoarseCore = mix(0.80, 1.0,
                                         smoothstep(0.32, 0.80, coarseNoise));
            float energyCoreWeight = energyGate * energyDensityGate *
                                     energyDetailCore * energyCoarseCore * 0.56;
            accumulatedCoreRadiance += transmittance * densityAlpha * energyCoreWeight;
            visibleCoreWeight = energyCoreWeight;
            emittedColor = mix(u_emissionColor, vec3(0.72, 0.92, 1.0),
                               energyCoreWeight);
            emittedHeat *= mix(0.58, 1.42, energyCoreWeight);
        }
        /* Keep the carrier coloured but sub-threshold on every background.
         * The ray-integrated core below owns the HDR budget; otherwise dark
         * scenes also turn every hot voxel into one uniformly blooming mass. */
        float emissionFloor = u_kind == 1 ? 0.48 :
                              (u_kind == 2 ? 0.62 : 1.0);
        float brightEmissionScale = u_kind == 1 ? 0.50 :
                                    (u_kind == 2 ? 0.68 : 1.0);
        float broadEmissionGain = mix(emissionFloor, 1.0, visibleCoreWeight) *
                                  mix(1.0, brightEmissionScale, backgroundAdapt);
        emittedHeat *= broadEmissionGain;
        accumulatedEmission += transmittance * emissionAlpha * emittedColor *
                               emittedHeat * u_emissionGain;
        coverage += transmittance * sampleAlpha;
        travel += stepLength;
    }
    /* Squaring the bounded front-to-back integral is a coverage selector:
     * weak neighbouring rays collapse, while coherent hot filaments retain
     * enough radiance to cross the bloom threshold. */
    float resolvedCoreRadiance = accumulatedCoreRadiance * accumulatedCoreRadiance;
    if (u_kind == 1) {
        /* A ray-integrated volume can look bright while every fragment remains
         * below the post-process threshold. Preserve the hottest sparse voxel
         * as a narrow HDR seed so bloom comes from the real scene pipeline. */
        vec3 bloomColor = mix(u_emissionColor, vec3(1.0, 0.88, 0.55), 0.62);
        /* coreWeight is capped at 0.42, so only a nearly coherent hot ray can
         * cross the HDR threshold; isolated samples and shoulders stay amber. */
        accumulatedEmission += bloomColor * resolvedCoreRadiance * 8.00;
    } else if (u_kind == 2) {
        /* Energy needs a compact HDR filament, not a brighter translucent
         * carrier. Keeping this on the reaction+density core preserves hue and
         * remains visible over a bright plate without turning the cloud white. */
        vec3 energyBloomColor = mix(u_emissionColor, vec3(0.72, 0.92, 1.0), 0.62);
        accumulatedEmission += energyBloomColor * resolvedCoreRadiance * 5.00;
    }
    if (coverage <= 0.001) discard;
    vec3 unpremultipliedBody = accumulatedBody / max(coverage, 1e-5);
    finalColor = VFX_ResolvePremultiplied(unpremultipliedBody, 1.0, coverage,
                                          accumulatedEmission, 1.0, 1.0);
}
