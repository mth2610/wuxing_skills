#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/fx.glsl"

// ============================================================
// Effect Material — Generic Parametrized Material (Fragment Shader)
// Backing shader for Material_LoadCustom(). Item 8 (CORE_ISSUES.md) —
// one shared shader whose look is configured via EffectMaterialParams
// uniforms instead of writing a new shader per combination.
// ============================================================

uniform sampler2D texture1;         // optional secondary detail/mask texture
uniform int   u_hasTexture1;        // 0/1 — guards the texture1 sample
uniform vec4  u_baseColor;          // primary tint; also used for rim + dissolve edge glow
uniform float u_translucency;       // 0 = opaque (alpha = u_baseColor.a), 1 = glass/tube-style
                                     // fresnel-driven alpha (see tube.fs's mix(0.3, 0.9, fresnel))
uniform float u_dissolve;           // [0..1] fade-out, same convention as other presets
uniform float u_rimStrength;        // 0..~2, rim/edge glow brightness
uniform float u_fresnelPower;       // 1..8, rim sharpness
uniform float u_emissiveIntensity;  // 0..~3, self-illumination boost
uniform float u_distortionStrength; // 0..1, VS-side wobble amount (unused here)

void main() {
    vec3 normal   = normalize(fragNormal);
    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));

    float diffuse = calcDiffuse(normal, lightDir, 0.2);
    float fresnel = calcFresnel(normal, viewDir, u_fresnelPower);

    vec3 baseColor = u_baseColor.rgb * diffuse;
    if (u_hasTexture1 != 0) {
        // Luminance-only mask (not detail.rgb) — texture1 is meant to modulate
        // brightness (e.g. a crack/detail map authored for a flat decal), not
        // import its own hue onto a mesh with very different UV density
        // (a sphere's UV pinches hard at the poles vs. a flat quad decal).
        float detailMask = texture(texture1, fragTexCoord).r;
        baseColor *= mix(1.0, detailMask, 0.5);
    }

    baseColor += baseColor * u_emissiveIntensity;

    // Weight the rim by how much this fragment faces the light, not just the
    // camera — plain view-only Fresnel glows evenly all the way around the
    // silhouette regardless of where the light actually is, which reads as
    // "rim doesn't match the light direction". Dimmed (not zeroed) on the
    // backlit side so the edge is still visible there, just weaker.
    float lightFacing = max(dot(normal, lightDir), 0.0);
    float rim = fresnel * mix(0.3, 1.0, lightFacing);
    baseColor += u_baseColor.rgb * rim * u_rimStrength;

    // Only evaluate dissolve/edge-glow once dissolve actually starts — fx.glsl's
    // dissolveCalc() computes edgeFactor for any noiseVal < dissolve + edgeWidth,
    // which is true for ~8% of fragments even at dissolve == 0.0 (the "still
    // solid" state). Left unguarded, that shows up as scattered bright speckle
    // dots across the whole surface the instant the material appears, well
    // before anything is meant to be dissolving.
    if (u_dissolve > 0.0) {
        // hash3 input uses `normal` (bounded [-1..1]), not fragPosition — fragPosition
        // is a raw world coordinate (arena center ~600), and hash3's internal
        // sin(dot(p, largeWeights)) loses precision badly at that magnitude. Using
        // normal also makes the dissolve pattern identical regardless of where in
        // the arena this material is drawn.
        float noiseVal = hash3(floor(normal * 20.0));
        float edgeFactor;
        if (dissolveCalc(noiseVal, u_dissolve, 0.08, edgeFactor) >= 1.0) discard;
        baseColor = mix(baseColor, u_baseColor.rgb * 2.0, edgeFactor);
    }

    // translucency == 0 -> fully opaque (existing behavior). translucency == 1 ->
    // same "center see-through, edges more solid" curve as tube.fs, driven by the
    // same fresnel term as the rim above. Caller must draw with BLEND_ALPHA
    // (BeginBlendMode/EndBlendMode) for the alpha<1 case to actually blend.
    float glassAlpha = mix(0.3, 0.9, fresnel);
    float alpha = mix(u_baseColor.a, glassAlpha, u_translucency);
    finalColor = vec4(baseColor, alpha);
}
