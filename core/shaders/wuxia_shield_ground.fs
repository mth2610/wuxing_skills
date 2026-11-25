#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// Wuxia Unified Shield — Ground-Contact Floor Caustics (WebGL FLOOR_FRAGMENT port)
// Uses 4-channel seamless procedural u_noiseTex:
//   R = smooth FBM, G = shifted FBM, B = sharp ridged wisps, A = fine filaments
// Dual-pass bright-background compliant:
//   u_premultiply < 0.5 : BODY pass (VFX_ResolveBody)
//   u_premultiply >= 0.5: EMISSION pass (VFX_ResolvePremultiplied + VFX_TonemapSafeEmission)

uniform int       u_preset;
uniform vec3      u_primaryColor;
uniform vec3      u_rimColor;
uniform float     u_opacity;
uniform float     u_seamIntensity;
uniform float     u_speed;
uniform float     u_shieldTime;
uniform float     u_emissionGain;
uniform float     u_premultiply;
uniform float     u_rcut;
uniform float     u_quadHalf;
uniform sampler2D u_noiseTex;

in vec2 vGroundUV;

#ifndef PI
#define PI 3.14159265359
#endif
#define TAU 6.28318530718

float grHash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec2 grHash22(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 grVoronoi2(vec2 x)
{
    vec2 n = floor(x);
    vec2 f = fract(x);
    float md = 8.0;
    vec2 bestCell = vec2(0.0);
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 g = vec2(float(i), float(j));
            vec2 o = grHash22(n + g);
            vec2 r = g + o - f;
            float d = dot(r, r);
            if (d < md) {
                md = d;
                bestCell = n + g;
            }
        }
    }
    return vec3(sqrt(md), md, grHash21(bestCell));
}

void main()
{
    vec2 p = (vGroundUV * 2.0 - 1.0) * u_quadHalf;
    float r = length(p);
    float Rcut = max(u_rcut, 0.05);
    float radialFade = 1.0 - smoothstep(Rcut * 1.25, u_quadHalf * 0.90, r);
    if (radialFade <= 0.002) discard;

    float az = atan(p.y, p.x);
    float uFloor = az / TAU;
    float t = u_shieldTime * u_speed;

    float signedDist = r - Rcut;
    float absDist = abs(signedDist);

    // Thin contact line at r = Rcut; structured floor effects carry the wider band
    float hotRing = exp(-absDist * 42.0);
    float outerSpill = exp(-max(signedDist, 0.0) * 2.8) * smoothstep(-0.18, 0.03, signedDist);
    float innerGlow = smoothstep(Rcut + 0.05, Rcut * 0.35, r) * 0.08;

    vec3 ringCol = u_rimColor;
    vec3 spillCol = u_primaryColor;
    float extraEffect = 0.0;
    vec3 extraCol = u_primaryColor;
    vec3 causticRGB = vec3(0.0);

    if (u_preset == 0) {
        // 1) CHAN KHI: swirling ridged qi filaments (B & A channels) skimming outward
        float env = texture(u_noiseTex, vec2(uFloor * 2.0 - t * 0.14, r * 0.32 - t * 0.12)).r;
        float w1  = texture(u_noiseTex, vec2(uFloor * 3.0 - t * 0.22, r * 0.55 - t * 0.18)).b;
        float w2  = texture(u_noiseTex, vec2(uFloor * 5.0 + t * 0.15, r * 0.85 - t * 0.26)).a;
        float wisps = (smoothstep(0.38, 0.92, w1) * 0.85 + smoothstep(0.46, 0.95, w2) * 0.65)
                    * (0.45 + 0.55 * env);
        float bandEnv = exp(-max(signedDist, 0.0) * 2.1) * smoothstep(-0.20, 0.03, signedDist);
        extraEffect = wisps * bandEnv * 1.35;
        extraCol = mix(u_primaryColor, u_rimColor, 0.52);
    } else if (u_preset == 1) {
        // 2) BAGUA: 8 notched cinnabar seal ticks & outer seal ring
        float octant = pow(max(0.5 + 0.5 * cos((az + t * 0.24) * 8.0), 0.0), 10.0);
        float sealRing = exp(-abs(r - (Rcut + 0.22)) * 24.0);
        vec3 cinnabar = vec3(0.95, 0.22, 0.14);
        ringCol = mix(cinnabar, vec3(1.0, 0.62, 0.42), 0.42);
        spillCol = mix(cinnabar, u_primaryColor, 0.35);
        extraEffect = sealRing * 0.72 + octant * exp(-absDist * 4.5) * 0.95;
        extraCol = cinnabar * 1.25;
    } else if (u_preset == 2) {
        // 3) KIM CHUNG: 8-petal golden lotus caustic projection
        float lotusRay = pow(max(0.5 + 0.5 * cos(az * 8.0), 0.0), 3.0);
        float lotusCaustic = exp(-abs(r - (Rcut + 0.15 + 0.16 * lotusRay)) * 18.0) * (0.52 + 0.48 * lotusRay);
        float innerLotus = smoothstep(Rcut, 0.1, r) * exp(-abs(r - Rcut * 0.68) * 14.0) * lotusRay * 0.45;
        ringCol = mix(u_rimColor, vec3(1.0, 0.92, 0.68), 0.5);
        spillCol = u_primaryColor;
        extraEffect = lotusCaustic * 0.95 + innerLotus;
        extraCol = mix(u_primaryColor, vec3(1.0, 0.95, 0.78), 0.58);
    } else if (u_preset == 3) {
        // 4) NGOC THANH: jade cloud halo + ridged gold rim
        float cloudFloor = texture(u_noiseTex, p * 0.18 + vec2(t * 0.04, -t * 0.05)).r;
        float veinFloor  = texture(u_noiseTex, p * 0.34 - vec2(t * 0.06, t * 0.04)).b;
        ringCol = mix(u_rimColor, vec3(1.0, 0.88, 0.52), 0.52);
        extraEffect = exp(-absDist * 2.2) * (cloudFloor * 0.45 + smoothstep(0.45, 0.92, veinFloor) * 0.75);
        extraCol = mix(u_primaryColor, vec3(1.0, 0.88, 0.52), 0.48);
    } else if (u_preset == 4) {
        // 5) LOI DINH: seamless ridged electric grounding arcs radiating outward
        float snapT = floor(t * 10.0) * 0.1;
        vec2 dir = p / max(r, 0.001);
        float b1 = texture(u_noiseTex, dir * 0.35 + p * 0.18 - dir * (t * 0.25) + vec2(snapT * 0.37, -snapT * 0.29)).b;
        float b2 = texture(u_noiseTex, dir * 0.55 - p * 0.26 - dir * (t * 0.35) + vec2(-snapT * 0.23, snapT * 0.41)).a;
        float bolt = pow(smoothstep(0.45, 0.95, b1), 2.2) * 1.15 + pow(smoothstep(0.50, 0.96, b2), 2.6) * 0.85;
        extraEffect = bolt * exp(-max(signedDist, 0.0) * 2.2) * smoothstep(-0.18, 0.04, signedDist) * 1.55;
        extraCol = mix(u_primaryColor, u_rimColor, 0.72);
    } else if (u_preset == 5) {
        // 6) XICH VIEM: glowing magma cracks & ember scorch
        float m1 = texture(u_noiseTex, p * 0.35 - vec2(0.0, t * 0.12)).b;
        float m2 = texture(u_noiseTex, p * 0.65 + vec2(t * 0.08, -t * 0.10)).a;
        ringCol = vec3(1.0, 0.88, 0.55);
        extraEffect = exp(-max(signedDist, 0.0) * 2.1) * smoothstep(-0.24, 0.04, signedDist)
                    * (smoothstep(0.35, 0.90, m1) * 0.95 + smoothstep(0.45, 0.94, m2) * 0.65);
        extraCol = mix(u_primaryColor, vec3(1.0, 0.78, 0.28), 0.55);
    } else if (u_preset == 6) {
        // 7) HUYEN BANG: 2D Voronoi frost web creeping outward
        vec3 vFloor = grVoronoi2(p * 2.4);
        float frostWeb = smoothstep(0.11, 0.015, vFloor.x) * exp(-max(signedDist, 0.0) * 1.35) * smoothstep(-0.32, 0.04, signedDist);
        float frostSheet = exp(-absDist * 2.8) * (0.40 + 0.55 * vFloor.z);
        extraEffect = frostWeb * 1.05 + frostSheet * 0.35;
        extraCol = mix(u_primaryColor, u_rimColor, 0.75);
    } else if (u_preset == 7) {
        // 8) LUU LY: razor-sharp optical caustic ring with RGB dispersion
        float causticRadius = Rcut * 0.87;
        float cRipple = (texture(u_noiseTex, vec2(uFloor * 3.0 + t * 0.06, r * 0.55)).r - 0.5) * 0.045;
        float cR = exp(-abs(r - (causticRadius + 0.025 + cRipple)) * 32.0);
        float cG = exp(-abs(r - (causticRadius + cRipple)) * 34.0);
        float cB = exp(-abs(r - (causticRadius - 0.025 + cRipple)) * 32.0);
        causticRGB = vec3(cR, cG, cB) * 1.2;
        float outerCaustic = exp(-abs(r - (Rcut + 0.14)) * 24.0) * 0.42;
        causticRGB += vec3(0.85, 0.94, 1.0) * outerCaustic;
        ringCol = vec3(1.0, 0.99, 0.98);
        spillCol = u_primaryColor * 0.65;
    } else {
        // 9) PHONG TRAO: differential tangential cyclone wind-shear tails (B & A channels)
        float logR = log(max(r, 0.18));
        float envFloor = texture(u_noiseTex, vec2(uFloor * 2.0 + logR * 0.35 - t * 0.22, r * 0.22 - t * 0.28)).r;
        float streak1  = texture(u_noiseTex, vec2(uFloor * 3.0 + logR * 0.95 - t * 0.68, r * 0.42 - t * 0.52)).b;
        float streak2  = texture(u_noiseTex, vec2(uFloor * 5.0 + logR * 1.35 - t * 1.05, r * 0.65 - t * 0.78)).a;
        float windTails = (smoothstep(0.42, 0.92, streak1) * 0.85 + smoothstep(0.50, 0.95, streak2) * 0.72)
                        * smoothstep(0.24, 0.80, envFloor);
        float outerSweep = exp(-max(signedDist, 0.0) * 1.15) * smoothstep(-0.24, 0.05, signedDist);
        ringCol = mix(u_rimColor, vec3(1.0), 0.42);
        spillCol = mix(u_primaryColor, u_rimColor, 0.25);
        extraEffect = windTails * outerSweep * 1.38;
        extraCol = mix(u_primaryColor, vec3(0.95, 1.0, 0.99), 0.58);
    }

    float sInt = u_seamIntensity;
    vec3 col = ringCol * hotRing * 0.54 * sInt
             + spillCol * (outerSpill * 0.22 + innerGlow) * sInt
             + extraCol * extraEffect * sInt
             + causticRGB * sInt;

    float mask = clamp((hotRing * 0.54 + outerSpill * 0.18 + innerGlow * 0.28 + extraEffect * 0.80 + length(causticRGB) * 0.5) * sInt, 0.0, 1.0);
    float alpha = clamp(mask * u_opacity * radialFade, 0.0, 0.85);
    if (alpha <= 0.002) discard;

    if (u_premultiply > 0.5)
    {
        float emMask = pow(clamp(hotRing * 0.40 + extraEffect * 0.62 + length(causticRGB) * 0.5, 0.0, 1.0), 1.85) * u_opacity * radialFade;
        vec3 glow = VFX_TonemapSafeEmission(col, u_emissionGain * 0.72, emMask);
        finalColor = VFX_ResolvePremultiplied(vec3(0.0), 0.0, 0.0, glow, 1.0, 1.0);
    }
    else
    {
        finalColor = VFX_ResolveBody(col, 1.0, alpha);
    }
}
