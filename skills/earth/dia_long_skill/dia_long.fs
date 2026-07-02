#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/fx.glsl"
#include "core/shaders/common/triplanar.glsl"

uniform float u_dissolve;
uniform float u_glow; // 0..1 inner magma heat: spikes at eruption, pulses while active

void main()
{
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);
    vec3 L = normalize(u_lightDir);

    vec3 w = triplanarWeights(N, 4.0);
    float strata = triplanarNoise(fragPosition, w, 0.02);  // large soft blotches
    float grain  = triplanarNoise(fragPosition, w, 0.09);  // crevice detail

    // Matte earth palette — low contrast, desaturated brown/gray rock.
    vec3 darkRock = vec3(0.13, 0.10, 0.08);
    vec3 ochre    = vec3(0.36, 0.27, 0.18);
    vec3 magma    = vec3(1.0, 0.38, 0.05);

    vec3 base = mix(darkRock, ochre, clamp(strata * 0.6 + 0.25, 0.0, 1.0));
    base *= 1.0 - 0.45 * smoothstep(0.60, 0.85, grain); // darkened crevices only

    float diff = calcDiffuse(N, L, 0.22);
    float spec = calcSpecular(N, L, V, 32.0) * 0.12;
    float fres = calcFresnel(N, V, 3.5) * 0.18; // subtle warm rim, rock is matte

    // Magma glow ONLY near the fissure: heat rises from ground level, fades
    // out by ~12 world units up, and follows sparse noise bands — reads as
    // lava light bleeding out of the crack, not glitter on the rock.
    float heat = clamp(1.0 - fragPosition.y / 12.0, 0.0, 1.0);
    heat *= heat;
    float veinBand = smoothstep(0.55, 0.78, triplanarNoise(fragPosition + vec3(37.0), w, 0.05));
    vec3 emissive = magma * heat * (0.25 + veinBand * 0.75) * u_glow * 1.4;

    vec3 col = base * diff + vec3(0.30, 0.20, 0.10) * fres + vec3(spec) + emissive;

    // Dissolve only while actually dissolving — dissolveCalc emits a nonzero
    // edgeFactor for ~8% of fragments even at dissolve==0 (documented in
    // CORE_API.md), which shows as color speckle if applied unconditionally.
    if (u_dissolve > 0.001) {
        float dn = hash3(floor(fragPosition * 0.35));
        float edgeFactor;
        if (dissolveCalc(dn, u_dissolve, 0.12, edgeFactor) >= 1.0) discard;
        col = mix(col, magma * 2.5, edgeFactor);
    }

    finalColor = vec4(col, 1.0);
}
