#version 330

// Đợt G2 — stylized-realism surface lighting (Thiên Nhai Minh Nguyệt Đao look).
// Beauty comes from idealized material response + atmosphere, NOT physically
// correct PBR: soft half-Lambert diffuse, a Blinn "moonlight" sheen, and a cool
// Fresnel rim that traces the silhouette — the signature wuxia-night edge light.
// Writes into the HDR float scene buffer, so spec/rim can exceed 1.0 and bloom
// naturally blooms them (the G1 HDR foundation compounds here).

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragWorldPos;
in vec3 fragViewNormal; // view space — matcap lookup UV (P3c)
in mat3 fragTBN;        // world-space tangent basis — normal map + aniso (P5a/P5b)

uniform sampler2D texture0; // albedo (raylib binds MATERIAL_MAP_DIFFUSE here)
uniform vec4 colDiffuse;    // per-draw tint (DrawModelEx tint / team color)

// Real Shading — quality tier (0 UNLIT / 1 LOW / 2 MED / 3 HIGH). Gates every
// technique below via cheap runtime branches so ONE shader covers all tiers.
uniform int u_qualityTier;

// Lighting from environment_system (set per frame by SurfaceMaterial_UpdateFrame)
uniform vec3  u_sunToLight;   // normalized dir from surface TOWARD the sun
uniform vec3  u_sunColor;
uniform vec3  u_skyColor;     // hemispheric ambient — upper sky tint
uniform vec3  u_groundColor;  // hemispheric ambient — lower bounce tint
uniform vec3  u_viewPos;

// Stylized material controls
uniform vec3  u_rimColor;     // cool moonlight edge
uniform float u_rimPower;     // higher = thinner/sharper rim
uniform float u_rimStrength;
uniform float u_specStrength;
uniform float u_shininess;
uniform vec3  u_emissiveColor; // glowing runes/eyes/weapon energy (default 0)

// Đợt E / E2 — VFX point lights. The uniforms and the accumulate function live
// in the shared block so the ground, props and particles all use the same
// falloff and wrap; a per-shader copy drifts the moment one of them is edited.
#include "core/shaders/common/vfx_lights.glsl"

// Matcap / lit-sphere material (P3c) — jade/metal/energy props. Per-material,
// set via SurfaceMaterial_SetMatcapActive/ClearMatcap right around a draw
// call (default u_hasMatcap = 0, zero cost when unused).
uniform sampler2D matcapTex;
uniform float     u_hasMatcap;
uniform float     u_matcapAmount;

// HIGH-tier upgrades (P5) — each independently flag-gated, default off/0 so
// they're free when a material doesn't opt in.
uniform sampler2D normalMap;
uniform float     u_hasNormalMap;   // P5a
uniform float     u_aniso;          // P5b — >0.5 = anisotropic sheen (hair/silk)
uniform float     u_anisoShininess;
uniform float     u_sssStrength;    // P5c — fake jade/skin back-scatter, 0 = off
uniform float     u_sssPower;
uniform vec3      u_sssColor;       // Messiah SSS tint (warm flesh or jade green)
uniform float     u_sssDistortion;  // Normal refraction weight (default ~0.35)

// Distance & Physical Height Fog (Ghost of Tsushima style)
uniform vec3  u_fogColor;
uniform float u_fogStart;
uniform float u_fogEnd;
uniform float u_fogEnabled;
uniform float u_fogHeightFalloff;   // k_e along Y axis
uniform float u_fogBaseAltitude;    // reference Y ground
uniform float u_fogSigmoidEnabled;  // >0.5 enables sigmoid inversion/cloud blanket layer
uniform vec3  u_fogSigmoidParams;   // x: layerAltitude, y: layerThickness (k_s), z: layerDensity
uniform vec3  u_rayleighLMS;        // Rayleigh LMS scattering coefficients
uniform float u_mieAnisotropy;      // g (forward scattering)
uniform float u_multipleScattAmp;   // Multiple scattering boost (~2.16)

// Real shadow map (P6, HIGH+Shadow) — single directional PCF shadow from
// EnvShadow. Multiplies diffuse/spec only (not ambient), so shadowed areas
// stay lit by the hemispheric ambient rather than going pitch black.
uniform sampler2D shadowMap;
uniform mat4      u_lightVP;
uniform float     u_shadowEnabled;
uniform float     u_shadowTexel; // 1.0/resolution, pushed from C (map is 2048
                                 // desktop / 512 Mali) — do NOT use textureSize()
                                 // (returns 0 under rlvk -> texel INF -> NaN PCF).
uniform sampler2D staticShadowMap;
uniform mat4      u_staticLightVP;
uniform float     u_staticShadowEnabled;
uniform float     u_staticShadowTexel;

out vec4 finalColor;

// 3x3 PCF; returns 1.0 = fully lit, 0.0 = fully shadowed.
// mat*vec — TEST-PROVEN by rlvk scenario `shadow_proj` (M*v matches CPU to
// 0.002, v*M off by 0.324). Keep in lockstep with ground_shadow.fs. See
// REAL_SHADING_P6_NOTES.md; do NOT flip from in-game guessing.
float ShadowFactor(vec3 worldPos, float ndl) {
    vec4 posLS = u_lightVP * vec4(worldPos, 1.0);
    vec3 proj = posLS.xyz / posLS.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;

    float bias = max(0.0025 * (1.0 - ndl), 0.0006);
    float z = proj.z - bias;
    // Bilinear-weighted PCF (same fix as maps/toolkit/shaders/ground_shadow.fs, 2026-07-22): the
    // shadow map is sampled POINT — R32F linear filtering is an optional format feature and is not
    // assumable on Vulkan/Mali — so a plain 3x3 of raw taps quantises every sample onto a texel
    // centre and the shadow edge reads as SQUARE BLOCKS on a low-resolution map. Comparing per
    // texel and blending the RESULTS with the sub-texel fraction removes the grid without needing
    // any format feature. 4 bilinear taps = 16 fetches (was 9), on the tier that already opted
    // into real shading.
    float res = 1.0 / u_shadowTexel;
    float shadow = 0.0;
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++) {
            vec2 o = (vec2(float(x), float(y)) * 3.0 - 1.5) * u_shadowTexel; // +-1.5 texels
            vec2 t = (proj.xy + o) * res - 0.5;
            vec2 f = fract(t);
            vec2 base = (floor(t) + 0.5) * u_shadowTexel;
            float s00 = step(z, texture(shadowMap, base).r);
            float s10 = step(z, texture(shadowMap, base + vec2(u_shadowTexel, 0.0)).r);
            float s01 = step(z, texture(shadowMap, base + vec2(0.0, u_shadowTexel)).r);
            float s11 = step(z, texture(shadowMap, base + vec2(u_shadowTexel, u_shadowTexel)).r);
            shadow += mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
        }
    }
    float dynamicShadow = shadow * 0.25;
    float staticShadow = 1.0;
    if (u_staticShadowEnabled > 0.5) {
        vec4 staticPosLS = u_staticLightVP * vec4(worldPos, 1.0);
        vec3 staticProj = staticPosLS.xyz / max(staticPosLS.w, 0.00001);
        staticProj = staticProj * 0.5 + 0.5;
        if (staticProj.z > 0.0 && staticProj.z < 1.0 &&
            staticProj.x > 0.0 && staticProj.x < 1.0 &&
            staticProj.y > 0.0 && staticProj.y < 1.0) {
            float staticBias = max(0.0042 * (1.0 - ndl), 0.0012);
            float staticZ = staticProj.z - staticBias;
            vec2 tap = vec2(u_staticShadowTexel * 1.25);
            staticShadow = 0.0;
            staticShadow += step(staticZ, texture(staticShadowMap, staticProj.xy + vec2(-tap.x, -tap.y)).r);
            staticShadow += step(staticZ, texture(staticShadowMap, staticProj.xy + vec2( tap.x, -tap.y)).r);
            staticShadow += step(staticZ, texture(staticShadowMap, staticProj.xy + vec2(-tap.x,  tap.y)).r);
            staticShadow += step(staticZ, texture(staticShadowMap, staticProj.xy + vec2( tap.x,  tap.y)).r);
            staticShadow *= 0.25;
        }
    }
    return min(dynamicShadow, staticShadow);
}

void main() {
    vec4 tex = texture(texture0, fragTexCoord);
    vec3 albedo = tex.rgb * colDiffuse.rgb * fragColor.rgb;
    float alpha = tex.a * colDiffuse.a;

    if (u_qualityTier == 0) { finalColor = vec4(albedo, alpha); return; } // UNLIT: cheap passthrough

    vec3 N = normalize(fragNormal);
    if (u_qualityTier >= 3 && u_hasNormalMap > 0.5) {
        vec3 nTex = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
        N = normalize(fragTBN * nTex);
    }
    vec3 L = normalize(u_sunToLight);
    vec3 V = normalize(u_viewPos - fragWorldPos);
    vec3 H = normalize(L + V);
    float ndl = dot(N, L);

    // Real shadow map (P6) — HIGH tier only; 1.0 (no-op) otherwise.
    float shadow = 1.0;
    if (u_qualityTier >= 3 && u_shadowEnabled > 0.5) {
        shadow = ShadowFactor(fragWorldPos, ndl);
    }

    // --- LOW base (all tiers >= 1) ---
    // Half-Lambert (Valve) — wraps light around the terminator for a soft,
    // stylized falloff with no harsh dark/light boundary on skin & cloth.
    float wrap = ndl * 0.5 + 0.5;
    wrap *= wrap;
    vec3 diffuse = albedo * u_sunColor * wrap * shadow;

    // Hemispheric ambient — near-free, biggest dark-scene win (no flat-black
    // undersides): sky tint above, ground bounce below, blended by N.y.
    float hemi = N.y * 0.5 + 0.5;
    vec3 ambient = albedo * mix(u_groundColor, u_skyColor, hemi);

    // Fresnel rim — the cool silhouette edge that reads as moonlight.
    float fres = pow(1.0 - max(dot(N, V), 0.0), u_rimPower);
    vec3 rim = u_rimColor * (fres * u_rimStrength);

    vec3 emissive = u_emissiveColor; // additive, BEFORE fog so bloom nurses it

    // Accumulated after the sun's half-Lambert and before the rim.
    vec3 vfxLit = VFXLights_Accumulate(fragWorldPos, N, albedo);

    vec3 color = ambient + diffuse + vfxLit + rim + emissive;

    // --- MED adds (tier >= 2): Blinn spec sheen + directional-moon rim tint ---
    if (u_qualityTier >= 2) {
        float spec = pow(max(dot(N, H), 0.0), u_shininess) * u_specStrength;
        spec *= smoothstep(0.0, 0.15, ndl);
        color += u_sunColor * spec * shadow;

        // Favour the anti-moon silhouette so the rim reads as real backlight
        // instead of a uniform halo.
        float moonFacing = smoothstep(-0.2, 0.6, dot(N, -L));
        color += rim * (moonFacing - 1.0); // rescale the LOW rim already added above

        // Matcap / lit-sphere — huge "expensive material" payoff for near-zero
        // cost; skipped entirely for materials without one (u_hasMatcap = 0).
        if (u_hasMatcap > 0.5) {
            vec2 muv = normalize(fragViewNormal).xy * 0.5 + 0.5;
            vec3 matcap = texture(matcapTex, muv).rgb;
            color = mix(color, color * matcap * 2.0, u_matcapAmount);
        }
    }

    // --- HIGH adds (tier >= 3): aniso sheen + fake SSS (normal map already
    // perturbed N above, before any of the LOW/MED terms were computed) ---
    if (u_qualityTier >= 3) {
        if (u_aniso > 0.5) {
            // Anisotropic sheen — streaks the highlight along the tangent
            // instead of dotting it (long hair / silk robes).
            vec3  T = normalize(fragTBN[0]);
            float ToH = dot(T, H);
            float aniso = sqrt(max(0.0, 1.0 - ToH * ToH));
            color += u_sunColor * pow(aniso, u_anisoShininess) * u_specStrength * shadow;
        }
        if (u_sssStrength > 0.0) {
            // Messiah Engine / Valve / Frostbite SSS Translucency model:
            // Light bends through thin geometry curved by surface normal
            float distWeight = (u_sssDistortion > 0.001) ? u_sssDistortion : 0.35;
            vec3 Lscatter = normalize(-L - N * distWeight);
            float back = pow(max(dot(V, Lscatter), 0.0), u_sssPower) * u_sssStrength;
            vec3 sssTint = (length(u_sssColor) > 0.01) ? u_sssColor : vec3(1.0, 0.48, 0.25);
            color += albedo * sssTint * u_sunColor * back;

            // Translucency from nearby VFX skill lights (chân khí hộ thể / fireballs / auras)
            for (int i = 0; i < min(u_vfxLightCount, MAX_VFX_LIGHTS); ++i) {
                vec3 vfxToPos = fragWorldPos - u_vfxLightPosRadius[i].xyz;
                float vfxDist = length(vfxToPos);
                float radius = u_vfxLightPosRadius[i].w;
                if (vfxDist < radius && vfxDist > 0.001) {
                    vec3 vfxL = -normalize(vfxToPos);
                    vec3 vfxScatter = normalize(-vfxL - N * distWeight);
                    float vfxBack = pow(max(dot(V, vfxScatter), 0.0), u_sssPower);
                    float atten = clamp(1.0 - vfxDist / radius, 0.0, 1.0);
                    color += albedo * sssTint * u_vfxLightColor[i].rgb * (vfxBack * atten * u_sssStrength * u_vfxLightGain * 0.5);
                }
            }
        }
    }

    if (u_fogEnabled > 0.5) {
        float dist = length(u_viewPos - fragWorldPos);
        float linearDistFactor = clamp((dist - u_fogStart) / max(u_fogEnd - u_fogStart, 0.001), 0.0, 1.0);

        // Analytical exponential height falloff along view ray
        float heightCoeff = 1.0;
        if (u_fogHeightFalloff > 0.0001) {
            float y1 = u_viewPos.y - u_fogBaseAltitude;
            float y2 = fragWorldPos.y - u_fogBaseAltitude;
            float dy = y2 - y1;
            if (abs(dy) > 0.001) {
                heightCoeff = (exp(-u_fogHeightFalloff * y1) - exp(-u_fogHeightFalloff * y2)) / (u_fogHeightFalloff * dy);
            } else {
                heightCoeff = exp(-u_fogHeightFalloff * (0.5 * (y1 + y2)));
            }
            heightCoeff = clamp(heightCoeff, 0.0, 4.0);
        }

        // Sigmoid thermal inversion layer / valley fog blanket
        float sigmoidDensity = 0.0;
        if (u_fogSigmoidEnabled > 0.5) {
            float diff = (fragWorldPos.y - u_fogSigmoidParams.x) / max(u_fogSigmoidParams.y, 0.001);
            // Bell-shaped density profile around the inversion layer
            sigmoidDensity = (1.0 / (1.0 + diff * diff)) * u_fogSigmoidParams.z;
        }

        float totalOpticalDepth = linearDistFactor * (heightCoeff + sigmoidDensity);
        float f = 1.0 - exp(-totalOpticalDepth);
        f = clamp(f, 0.0, 1.0);

        // Rayleigh LMS + Mie forward-scattering phase function (Patry/Schuler SIGGRAPH 2021)
        vec3 fogTone = u_fogColor;
        if (u_rayleighLMS.x > 0.0001) {
            // Forward phase alignment with sun direction (L is from surface toward sun)
            float cosTheta = clamp(dot(-V, L), -1.0, 1.0);
            float g = clamp(u_mieAnisotropy, 0.1, 0.95);
            float g2 = g * g;
            float miePhase = (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 0.001), 1.5));

            // LMS to Linear sRGB transformation matrix:
            // [ 4.0628 -3.3102  0.2474 ]
            // [-0.9693  1.8760  0.0416 ]
            // [ 0.0556 -0.2040  1.0573 ]
            vec3 scatterLMS = u_rayleighLMS * totalOpticalDepth;
            vec3 scatterRGB = clamp(mat3(
                 4.0628, -0.9693,  0.0556,
                -3.3102,  1.8760, -0.2040,
                 0.2474,  0.0416,  1.0573
            ) * scatterLMS, 0.0, 2.0);

            vec3 inScatteredSun = u_sunColor * (scatterRGB + miePhase * u_multipleScattAmp * 0.05);
            fogTone = mix(u_fogColor, inScatteredSun, clamp(totalOpticalDepth * 0.5, 0.0, 0.8));
        }

        color = mix(color, fogTone, f);
    }

    finalColor = vec4(color, alpha);
}
