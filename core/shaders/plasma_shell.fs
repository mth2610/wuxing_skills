#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// ============================================================
// Plasma Shell — Fragment Shader
// Backing FS for PlasmaMaterial (core/material/material_system.h).
// A wispy energy membrane: fresnel-weighted, fbm-broken alpha so the
// center of the sphere is fully see-through and the surface reads as
// drifting filament wisps instead of a solid glass ball (the
// EffectMaterial translucency path has an alpha floor of 0.3 at the
// center — this shader exists specifically to avoid that).
// ShieldShell alpha-composites this membrane over the scene.
// ============================================================

uniform vec4  u_baseColor;    // deep body tint of the membrane
uniform vec4  u_wispColor;    // bright filament tint (wisp crests)
uniform float u_noiseScale;   // wisp frequency over the sphere (try 2.5-4.0)
uniform float u_noiseSpeed;   // wisp scroll speed (try 0.3-0.8)
uniform float u_fresnelPower; // rim sharpness, 1..8
uniform float u_rimStrength;  // extra rim brightness, 0..~2
uniform float u_emissive;     // overall self-illumination boost
uniform float u_opacity;      // master alpha multiplier, 0..1

// Local 3D value noise built on hash3 from noise.glsl (same pattern as
// crystal.fs — noise.glsl itself only ships 2D vnoise/fbm).
float vnoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash3(i);
    float n100 = hash3(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash3(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash3(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash3(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash3(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash3(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash3(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm3(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; i++) {
        v += a * vnoise3D(p);
        p = p * 2.1 + vec3(13.7);
        a *= 0.5;
    }
    return v;
}

void main() {
    vec3 normal  = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);

    // Noise domain = surface direction (unit-length), NOT fragPosition —
    // raw world coords lose precision at arena scale (see effect_material.fs)
    // and would also change the pattern as the orb moves.
    vec3 dom = normal * u_noiseScale;
    float t = u_time * u_noiseSpeed;

    // Two fbm sheets scrolling against each other; ridging (1-|2x-1|) turns
    // soft blobs into thin filament crests — the "wisp" read.
    float n1 = fbm3(dom + vec3(t, -t * 0.6, t * 0.8));
    float n2 = fbm3(dom * 1.7 + vec3(-t * 0.7, t, -t * 0.5));
    float ridge = 1.0 - abs(2.0 * n1 - 1.0);
    float wisp = ridge * (0.35 + 0.65 * n2);
    wisp = smoothstep(0.45, 0.9, wisp); // high floor -> thin strands, not solid patches

    // Fresnel: membrane densest edge-on (silhouette), thinnest face-on —
    // this is what empties the center of the orb.
    float fresnel = calcFresnel(normal, viewDir, u_fresnelPower);

    // A shield must retain a visible carrier face-on. At 0.06 the front-facing
    // flow is multiplied below the HDR bloom threshold in daylight, leaving
    // only a rim. The wisp still gates it, so this is flowing plasma, not glass.
    float alpha = wisp * mix(0.18, 1.0, fresnel) * u_opacity * u_baseColor.a;

    // Body -> crest color ramp; crests also get the rim boost.
    vec3 color = mix(u_baseColor.rgb, u_wispColor.rgb, wisp * wisp);
    color += u_wispColor.rgb * fresnel * u_rimStrength * wisp;
    // Only thin crest pixels enter HDR/bloom. A minimum gain over the whole
    // wisp makes ACES roll the entire membrane toward white on a lit scene.
    float crest = smoothstep(0.74, 0.94, wisp);
    float interiorCrest = crest * mix(0.35, 1.0, 1.0 - fresnel);
    color += u_wispColor.rgb * (u_emissive * interiorCrest);

    finalColor = VFX_ResolveBody(color, 1.0, alpha);
}
