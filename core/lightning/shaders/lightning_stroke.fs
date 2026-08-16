#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

uniform float u_phase;
uniform float u_mode;
uniform float u_lineWidth;
uniform float u_travel;
uniform float u_lifeFade;
uniform float u_contactStrength;
uniform float u_travelHeadStrength;
uniform float u_endpointTaperStrength;
uniform float u_coreEmission;
uniform float u_haloEmission;
uniform vec4 u_bodyColor;
uniform vec4 u_haloColor;
uniform vec4 u_coreColor;

float LightningStroke_FilamentDistance(vec2 uv)
{
    float across = uv.x * 2.0 - 1.0;
    float along = uv.y;
    // The endpoint envelope is the 3D-specific addition that keeps source and
    // click target exact while retaining the approved FBM silhouette.
    float endpointPin = pow(sin(clamp(along, 0.0, 1.0) * 3.14159265), 0.42);
    vec2 domain = vec2(along * 4.6 + u_phase * 0.52,
                       across * 1.35 - u_phase * 0.18);
    float macro = fbm2N(domain, 4) - 0.5;
    float micro = fbm2N(domain * 3.7 + vec2(19.1, 7.3), 3) - 0.5;
    float centreline = (macro * 0.72 + micro * 0.16) * endpointPin;
    return abs(across - centreline);
}

float LightningStroke_EndpointTaper(float along)
{
    // The canvas ends exactly at the source/click point, but its SDF must not.
    // This circular taper prevents a squared-off cut while retaining contact.
    float sourceTaper = smoothstep(0.0, 0.038, along);
    float targetTaper = 1.0 - smoothstep(0.962, 1.0, along);
    return sourceTaper * targetTaper;
}

void main()
{
    float distanceToFilament = LightningStroke_FilamentDistance(fragTexCoord);
    // The alpha body only preserves hue against a bright destination. It must
    // stay inside the ion channel, never become a separate blue outline.
    float body = 1.0 - smoothstep(u_lineWidth * 0.030, u_lineWidth * 0.105,
                                  distanceToFilament);
    float core = 1.0 - smoothstep(u_lineWidth * 0.018, u_lineWidth * 0.095,
                                  distanceToFilament);
    // One continuous energy profile: white ion channel -> saturated electric
    // blue corona -> pale, faint outer field. Layer weights overlap.
    float innerCorona = 1.0 - smoothstep(u_lineWidth * 0.070, u_lineWidth * 0.48,
                                         distanceToFilament);
    float halo = 1.0 - smoothstep(u_lineWidth * 0.18, u_lineWidth * 1.52,
                                  distanceToFilament);
    halo *= halo * halo;
    float tipTaper = mix(1.0, LightningStroke_EndpointTaper(fragTexCoord.y),
                         u_endpointTaperStrength);
    float contactShape = 1.0 - smoothstep(u_lineWidth * 0.025, u_lineWidth * 0.24,
                                          distanceToFilament);
    float sourceContact = exp(-pow(fragTexCoord.y / 0.018, 2.0));
    float targetContact = exp(-pow((1.0 - fragTexCoord.y) / 0.018, 2.0)) *
                          smoothstep(0.94, 0.995, u_travel);
    float endpointContact = (sourceContact + targetContact) * contactShape * u_contactStrength;

    // Reveal a discharge, rather than placing a completed beam on screen at
    // frame zero. The coverage leaves a short anti-aliased leading edge; the
    // narrow travelling boost is the visible ionisation front.
    float travelCoverage = 1.0 - smoothstep(u_travel, u_travel + 0.035, fragTexCoord.y);
    float travelHead = smoothstep(u_travel - 0.10, u_travel, fragTexCoord.y) *
                       travelCoverage * u_travelHeadStrength;
    body *= travelCoverage * tipTaper * u_lifeFade;
    core *= travelCoverage * tipTaper * u_lifeFade;
    innerCorona *= travelCoverage * tipTaper * u_lifeFade;
    halo *= travelCoverage * tipTaper * u_lifeFade;
    endpointContact *= u_lifeFade;

    if (u_mode < 0.5)
    {
        finalColor = VFX_ResolveBody(u_bodyColor.rgb, 1.0, u_bodyColor.a * body);
    }
    else
    {
        float dischargeBoost = 1.0 + travelHead * 1.20 + endpointContact * 0.30;
        // Contacts are ionised points, not blue body coverage. This keeps the
        // bolt physically attached after the general endpoint taper reaches 0.
        float emissionCore = core + endpointContact;
        float coronaWeight = innerCorona * (1.0 - clamp(emissionCore, 0.0, 1.0));
        float fieldWeight = halo * (1.0 - innerCorona);
        // Keep saturation closest to the ionised channel. Only the weak outer
        // field trends toward pale blue, so the colour order never reverses.
        vec3 innerColour = mix(u_haloColor.rgb, vec3(0.06, 0.24, 0.94), 0.30);
        vec3 outerColour = mix(innerColour, vec3(0.22, 0.48, 1.0), 0.38);
        // Intensity only: preserve the same SDF radius/weight so this enriches
        // the near-core field without reopening the old opaque blue band.
        float coronaEnergy = 2.05;
        float energy = clamp((fieldWeight * u_haloColor.a * u_haloEmission +
                              coronaWeight * u_haloColor.a * u_haloEmission * coronaEnergy +
                              emissionCore * u_coreColor.a * u_coreEmission) * dischargeBoost,
                             0.0, 1.0);
        vec3 radiance = (outerColour * fieldWeight * u_haloColor.a * u_haloEmission +
                         innerColour * coronaWeight * u_haloColor.a * u_haloEmission * coronaEnergy +
                         u_coreColor.rgb * emissionCore * u_coreColor.a * u_coreEmission) * dischargeBoost;
        finalColor = VFX_ResolveEmission(radiance / max(energy, 0.001),
                                          1.0, 1.0, energy);
    }
}
