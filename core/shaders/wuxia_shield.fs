#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// ── Wuxia Unified Shield Fragment Shader (9 Presets + 4 Ground-Cut Modes) ──
// Dual-pass bright-background compliant:
//   u_premultiply < 0.5 : BODY pass (VFX_ResolveBody) — refracted scene + pigment/soot
//   u_premultiply >= 0.5: EMISSION pass (VFX_ResolvePremultiplied + VFX_TonemapSafeEmission)

uniform int       u_preset;      // 0..8
uniform vec3      u_primaryColor;
uniform vec3      u_rimColor;
uniform float     u_opacity;
uniform float     u_fresnel;
uniform float     u_seamIntensity;
uniform float     u_speed;
uniform float     u_backWall;
uniform float     u_passSide;    // -1.0 = far wall, +1.0 = near wall
uniform float     u_relief;
uniform float     u_refraction;
uniform float     u_emissionGain;
uniform float     u_premultiply;
uniform float     u_shieldTime;

uniform float     u_groundEnabled;
uniform float     u_groundY;
uniform float     u_shellRadius;

uniform vec3      u_camPos;
uniform sampler2D u_noiseTex;
uniform sampler2D u_sceneTex;
uniform int       u_hasScene;
uniform sampler2D u_depthTex;
uniform int       u_hasDepth;
uniform vec2      u_depthClip;   // near, far
uniform float     u_hasImpacts;
uniform vec4      u_impacts[4];  // xyz = unit dir, w = age

in vec3  vUnitPos;
in vec3  vWorldPos;
in vec3  vWorldNormal;
in vec3  vViewPos;
in vec3  vViewNormal;
in float vWaveDisp;

#ifndef PI
#define PI 3.14159265359
#endif
#define TAU 6.28318530718

// ── Shared Procedural Utilities ──────────────────────────────────────────────

float shHash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec2 shHash22(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float shHash31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 shHash33(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

float shNoise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = shHash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = shHash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = shHash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = shHash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = shHash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = shHash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = shHash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = shHash31(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);

    return mix(mix(nx00, nx10, u.y), mix(nx01, nx11, u.y), u.z);
}

float shFbm3(vec3 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * shNoise3(p);
        p = p * 2.02 + vec3(13.7, -7.3, 21.1);
        a *= 0.5;
    }
    return v;
}

float shRidgedFbm3(vec3 p)
{
    float v = 0.0;
    float a = 0.55;
    for (int i = 0; i < 4; i++) {
        float n = 1.0 - abs(shNoise3(p) * 2.0 - 1.0);
        v += a * n * n;
        p = p * 2.07 + vec3(19.3, -11.7, 7.9);
        a *= 0.5;
    }
    return v;
}

vec4 shVoronoi3(vec3 x)
{
    vec3 n = floor(x);
    vec3 f = fract(x);
    float md = 8.0;
    float md2 = 8.0;
    vec3 bestCell = vec3(0.0);
    for (int k = -1; k <= 1; k++) {
        for (int j = -1; j <= 1; j++) {
            for (int i = -1; i <= 1; i++) {
                vec3 g = vec3(float(i), float(j), float(k));
                vec3 o = shHash33(n + g);
                vec3 r = g + o - f;
                float d = dot(r, r);
                if (d < md) {
                    md2 = md;
                    md = d;
                    bestCell = n + g;
                } else if (d < md2) {
                    md2 = d;
                }
            }
        }
    }
    float f1 = sqrt(md);
    float f2 = sqrt(md2);
    return vec4(f2 - f1, f1, shHash31(bestCell), shHash31(bestCell + 17.3));
}

vec3 sampleStudioEnv(vec3 dir)
{
    vec3 d = normalize(dir);
    float y = d.y;
    vec3 base = mix(vec3(0.02, 0.03, 0.05), vec3(0.06, 0.09, 0.14), smoothstep(-0.4, 0.85, y));
    float keyLight = pow(max(dot(d, normalize(vec3(0.38, 0.82, 0.44))), 0.0), 28.0);
    float rimStrip = pow(max(dot(d, normalize(vec3(-0.78, 0.32, -0.52))), 0.0), 18.0);
    float lowBand  = exp(-pow((y + 0.18) * 5.2, 2.0)) * (0.45 + 0.55 * smoothstep(-0.3, 0.9, d.z));

    vec3 col = base;
    col += vec3(1.15, 1.08, 0.96) * keyLight * 0.42;
    col += vec3(0.55, 0.82, 1.15) * rimStrip * 0.35;
    col += vec3(0.24, 0.20, 0.16) * lowBand * 0.20;
    return col;
}

float sdBox2D(vec2 p, vec2 b)
{
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

vec3 rotY3(vec3 p, float a)
{
    float c = cos(a), s = sin(a);
    return vec3(p.x * c - p.z * s, p.y, p.x * s + p.z * c);
}

float linearizeDepth(float depthSample)
{
    float z = depthSample * 2.0 - 1.0;
    float nearP = max(u_depthClip.x, 0.01);
    float farP  = max(u_depthClip.y, nearP + 1.0);
    return (2.0 * nearP * farP) / max(farP + nearP - z * (farP - nearP), 0.0001);
}

vec4 evalImpacts(vec3 unitPos)
{
    if (u_hasImpacts < 0.5) return vec4(0.0);
    float ringSum = 0.0;
    float coreSum = 0.0;
    for (int i = 0; i < 4; i++) {
        float age = u_impacts[i].w;
        if (age >= 0.0 && age < 2.6) {
            float d = acos(clamp(dot(unitPos, normalize(u_impacts[i].xyz)), -1.0, 1.0));
            float radius = age * 1.65;
            float decay = exp(-age * 1.45);
            float width = 0.12 + age * 0.045;
            float ring = exp(-pow((d - radius) / width, 2.0)) * decay;
            float secondary = exp(-pow((d - max(radius - 0.24, 0.0)) / (width * 0.75), 2.0)) * decay * 0.48;
            float core = exp(-d * 6.5) * exp(-age * 4.2);
            ringSum += ring + secondary;
            coreSum += core;
        }
    }
    return vec4(clamp(ringSum, 0.0, 2.2), clamp(coreSum, 0.0, 2.0), 0.0, 0.0);
}

vec4 evalIntersectionSeams(vec3 unitPos, float t, out float hGround)
{
    hGround = (vWorldPos.y - u_groundY) / max(u_shellRadius, 0.1);
    if (u_groundEnabled > 0.5 && hGround < 0.0) {
        discard;
    }

    float groundHot = 0.0;
    float groundAura = 0.0;
    if (u_groundEnabled > 0.5 && hGround < 1.35) {
        float azSeam = atan(unitPos.z, unitPos.x) / TAU;
        vec4 seamTex = texture(u_noiseTex, vec2(azSeam * 3.0 - t * 0.18, hGround * 0.45 - t * 0.36));
        groundHot  = exp(-hGround * 40.0) * (0.82 + 0.36 * seamTex.r) * u_seamIntensity;
        groundAura = exp(-hGround * 7.0)  * (0.35 + 0.85 * seamTex.b) * u_seamIntensity;
    }

    float depthHot = 0.0;
    float depthAura = 0.0;
    if (u_hasDepth > 0) {
        vec2 screenUV = gl_FragCoord.xy / max(u_resolution, vec2(1.0));
        float sceneRaw = texture(u_depthTex, screenUV).r;
        if (sceneRaw < 0.9999) {
            float sceneLinear = linearizeDepth(sceneRaw);
            float fragLinear  = linearizeDepth(gl_FragCoord.z);
            float diff = sceneLinear - fragLinear;
            if (diff > -0.02 && diff < 0.65) {
                float aboveMask = (u_groundEnabled > 0.5) ? smoothstep(0.03, 0.22, hGround) : 1.0;
                float d = max(diff, 0.0);
                float dNoise = texture(u_noiseTex, vec2((vWorldPos.x + vWorldPos.z) * 0.32, vWorldPos.y * 0.42 - t * 0.28)).r;
                depthHot  = exp(-d * 24.0) * aboveMask * u_seamIntensity * (0.85 + 0.35 * dNoise);
                depthAura = exp(-d * 8.5)  * aboveMask * u_seamIntensity * (0.50 + 0.50 * dNoise);
            }
        }
    }

    return vec4(groundHot, groundAura, depthHot, depthAura);
}

// ── Preset-Specific Helpers ──────────────────────────────────────────────────

float evalTrigram(vec2 p, int idx)
{
    int bits = 7;
    if (idx == 0) bits = 7;
    else if (idx == 1) bits = 3;
    else if (idx == 2) bits = 5;
    else if (idx == 3) bits = 1;
    else if (idx == 4) bits = 6;
    else if (idx == 5) bits = 2;
    else if (idx == 6) bits = 4;
    else bits = 0;

    float mask = 0.0;
    for (int row = 0; row < 3; row++) {
        float yOff = (float(row) - 1.0) * 0.16;
        int div = (row == 0) ? 1 : ((row == 1) ? 2 : 4);
        int isYang = (bits / div) - 2 * ((bits / div) / 2);
        float d;
        if (isYang == 1) {
            d = sdBox2D(p - vec2(0.0, yOff), vec2(0.28, 0.036));
        } else {
            float dL = sdBox2D(p - vec2(-0.165, yOff), vec2(0.115, 0.036));
            float dR = sdBox2D(p - vec2( 0.165, yOff), vec2(0.115, 0.036));
            d = min(dL, dR);
        }
        mask = max(mask, smoothstep(0.018, -0.008, d));
    }
    return mask;
}

vec2 evalBellRelief(vec3 p)
{
    float y = p.y;
    float az = atan(p.z, p.x);
    float u = fract(az / TAU);
    float h = 0.0;
    float glyph = 0.0;

    if (y > 0.64) {
        float r = length(p.xz);
        float a = atan(p.z, p.x);
        float petalWave = 0.5 + 0.5 * cos(a * 8.0);
        float lotusEdge = 0.20 + 0.14 * pow(petalWave, 1.45);
        float rimLine = exp(-pow((r - lotusEdge) * 46.0, 2.0));
        float innerJewel = exp(-pow((r - 0.105) * 52.0, 2.0));
        float petalVein = exp(-pow(sin(a * 4.0) * r * 38.0, 2.0)) * smoothstep(0.11, 0.15, r) * smoothstep(lotusEdge, lotusEdge - 0.03, r);
        h += (rimLine * 0.92 + innerJewel * 0.85 + petalVein * 0.42) * smoothstep(0.64, 0.74, y);
    }

    float bandTop = exp(-pow((y - 0.68) * 68.0, 2.0)) + exp(-pow((y - 0.56) * 76.0, 2.0));
    float bandMid = exp(-pow((y - 0.19) * 76.0, 2.0)) + exp(-pow((y - 0.09) * 62.0, 2.0));
    h += (bandTop + bandMid) * 0.78;

    if (y > 0.565 && y < 0.675) {
        vec2 fv = vec2(u * 24.0, (y - 0.57) / 0.10);
        vec2 fCell = fract(fv);
        float fw = max(fwidth(fv.x), 0.03);
        float barTop = smoothstep(fw, 0.0, abs(fCell.y - 0.78) - 0.07) * step(0.12, fCell.x) * step(fCell.x, 0.88);
        float barBot = smoothstep(fw, 0.0, abs(fCell.y - 0.22) - 0.07) * step(0.28, fCell.x) * step(fCell.x, 0.72);
        float vLeft  = smoothstep(fw, 0.0, abs(fCell.x - 0.16) - 0.07) * step(0.20, fCell.y) * step(fCell.y, 0.80);
        float vRight = smoothstep(fw, 0.0, abs(fCell.x - 0.84) - 0.07) * step(0.18, fCell.y) * step(fCell.y, 0.80);
        float hook   = smoothstep(fw, 0.0, abs(fCell.x - 0.50) - 0.065) * step(0.22, fCell.y) * step(fCell.y, 0.58);
        h += clamp(max(max(barTop, barBot), max(max(vLeft, vRight), hook)), 0.0, 1.0) * 0.52;
    }

    float sec = u * 8.0;
    float secF = fract(sec);
    float secCenter = abs(secF - 0.5);
    float ribFw = max(fwidth(sec), 0.012);
    float vertRib = smoothstep(ribFw * 1.4 + 0.016, 0.004, min(secF, 1.0 - secF)) * smoothstep(0.08, 0.14, y) * smoothstep(0.69, 0.64, y);
    h += vertRib * 0.68;

    if (y > 0.35 && y < 0.54 && secCenter < 0.36) {
        vec2 studGrid = vec2((secF - 0.14) / 0.72 * 3.0, (y - 0.36) / 0.17 * 2.0);
        vec2 sg = fract(studGrid) - 0.5;
        float studDist = length(sg * vec2(1.0, 1.15));
        float studDome = sqrt(max(0.0, 0.11 - studDist * studDist)) * 3.1;
        h += studDome * 0.92;
    }

    if (y > 0.205 && y < 0.345 && secCenter < 0.33) {
        vec2 gp = vec2((secF - 0.5) * 2.8, (y - 0.275) * 14.0);
        float stroke1 = exp(-pow(abs(gp.x) * 6.5, 2.0)) * smoothstep(0.82, 0.65, abs(gp.y));
        float stroke2 = exp(-pow(abs(gp.y - 0.32) * 6.5, 2.0)) * smoothstep(0.62, 0.45, abs(gp.x));
        float stroke3 = exp(-pow(abs(gp.y + 0.28) * 6.5, 2.0)) * smoothstep(0.75, 0.55, abs(gp.x));
        float ringG   = exp(-pow((length(gp * vec2(1.25, 0.95)) - 0.48) * 7.5, 2.0));
        float corner  = exp(-pow((abs(gp.x) - 0.34) * 8.0, 2.0)) * smoothstep(0.35, 0.18, abs(gp.y));
        glyph = clamp(max(max(stroke1, stroke2), max(stroke3, max(ringG * 0.85, corner * 0.75))), 0.0, 1.0);
        h += glyph * 0.32;
    }

    if (y < 0.12 && y > -0.05) {
        float petal16 = 0.5 + 0.5 * cos(az * 16.0);
        float skirtCurve = 0.02 + 0.068 * pow(petal16, 1.25);
        h += exp(-pow((y - skirtCurve) * 64.0, 2.0)) * 0.76;
    }

    return vec2(clamp(h, 0.0, 1.6), glyph);
}

// ── Main Shader ──────────────────────────────────────────────────────────────

void main()
{
    vec3 unitPos = normalize(vUnitPos);
    float t = u_shieldTime * u_speed;
    float hGround;
    vec4 seam = evalIntersectionSeams(unitPos, t, hGround);

    vec3 Vview = normalize(-vViewPos);
    vec3 Nview = normalize(vViewNormal);
    float ndotv = abs(dot(Nview, Vview));
    float fresnel = pow(1.0 - ndotv, 2.75) * u_fresnel;

    vec3 V = normalize(u_camPos - vWorldPos);
    float faceSign = (u_passSide < 0.0) ? -1.0 : 1.0;
    vec3 N = normalize(vWorldNormal) * faceSign;
    vec4 imp = evalImpacts(unitPos);

    float detailScale = clamp(u_relief, 0.25, 2.0);
    float seamHot  = seam.x + seam.z;
    float seamAura = seam.y + seam.w;

    vec3 col = vec3(0.0);
    vec3 glowCol = u_rimColor;
    float glowMask = 0.0;
    float alpha = 0.08;
    vec3 backTint = u_primaryColor;
    float refrScale = u_refraction;

    if (u_preset == 0) {
        // 1) HỘ THỂ CHÂN KHÍ
        vec3 pA = rotY3(unitPos, unitPos.y * 2.4 - t * 0.72);
        vec3 pB = rotY3(unitPos, -unitPos.y * 1.9 + t * 0.52);
        float silkA = shRidgedFbm3(vec3(pA.x * 2.0, pA.y * 4.4 - t * 0.55, pA.z * 2.0));
        float silkB = shFbm3(vec3(pB.x * 2.8, pB.y * 3.6 - t * 0.42, pB.z * 2.8));
        float ribbons = pow(smoothstep(0.56, 0.94, silkA), 1.65) * (0.45 + 0.55 * silkB) * detailScale;

        vec3 R = reflect(-V, normalize(N + vec3(silkA - 0.5, silkB - 0.5, 0.0) * 0.08));
        vec3 envRefl = sampleStudioEnv(R);

        col = mix(u_primaryColor * 0.22, u_primaryColor * 0.55, 1.0 - ndotv) * 0.16;
        col += mix(u_primaryColor, u_rimColor, 0.55) * ribbons * (0.85 + fresnel * 0.55);
        col += u_rimColor * fresnel * 0.92 + envRefl * (0.04 + fresnel * 0.18);

        float hitBoost = 1.0 + imp.x * 1.4 + imp.y * 1.2;
        col += u_rimColor * seamHot * 1.35 * hitBoost;
        col += u_primaryColor * seamAura * (0.55 + silkB * 0.45) * hitBoost;
        col += mix(u_primaryColor, vec3(1.0), 0.62) * (imp.x * 1.05 + imp.y * 1.4);

        glowMask = clamp(fresnel * 0.72 + ribbons * 0.65 + seamHot * 1.15 + imp.x * 0.9 + imp.y, 0.0, 1.0);
        alpha = 0.015 + (1.0 - ndotv) * 0.035 + ribbons * 0.32 + fresnel * 0.52 + seamHot * 0.80 + seamAura * 0.22 + clamp(imp.x * 0.42 + imp.y * 0.5, 0.0, 0.65);
        backTint = vec3(0.68, 0.92, 0.86);
    } else if (u_preset == 1) {
        // 2) THỦY MẶC BÁT QUÁI
        float az = atan(unitPos.z, unitPos.x) + t * 0.24;
        float sectorFloat = fract(az / TAU) * 8.0;
        int sectorIdx = int(floor(sectorFloat));
        vec2 trigUV = vec2(fract(sectorFloat) - 0.5, (unitPos.y - 0.36) * 1.55);
        float trigMask = (abs(trigUV.y) < 0.42) ? evalTrigram(trigUV, sectorIdx) : 0.0;

        float ringTop = exp(-pow((unitPos.y - 0.62) * 58.0, 2.0));
        float ringBot = exp(-pow((unitPos.y - 0.10) * 58.0, 2.0));
        float baguaArt = clamp((trigMask * 1.45 + (ringTop + ringBot) * 0.72) * detailScale, 0.0, 1.6);

        float ink1 = shFbm3(unitPos * 3.2 + vec3(t * 0.28, -t * 0.42, t * 0.18));
        float ink2 = shRidgedFbm3(unitPos * 4.5 - vec3(t * 0.22, t * 0.35, -t * 0.25));
        float inkWash = smoothstep(0.32, 0.84, ink1) * 0.72 + smoothstep(0.54, 0.92, ink2) * 0.38;

        float crownMark = 0.0;
        if (unitPos.y > 0.62) {
            float rTop = length(unitPos.xz);
            float aTop = atan(unitPos.z, unitPos.x) - t * 0.35;
            float outerCirc = exp(-pow((rTop - 0.36) * 32.0, 2.0));
            float sCurve = exp(-pow((sin(aTop) * rTop - cos(aTop) * (rTop - 0.18) * 0.65) * 18.0, 2.0)) * smoothstep(0.36, 0.30, rTop);
            crownMark = (outerCirc * 0.72 + sCurve * 0.45) * smoothstep(0.62, 0.80, unitPos.y);
        }

        vec3 inkDark = vec3(0.05, 0.055, 0.065);
        vec3 cinnabar = vec3(0.95, 0.20, 0.14);

        col = mix(inkDark, u_primaryColor * 0.45, inkWash * 0.68);
        col += mix(u_primaryColor, u_rimColor, 0.5) * (baguaArt * 0.92 + crownMark * 0.75);
        col += u_rimColor * fresnel * 0.85;

        float cinnabarHit = clamp(imp.x * 1.25 + imp.y * 1.55, 0.0, 1.8);
        col = mix(col, cinnabar * 1.55, clamp(cinnabarHit * 0.58 * (0.45 + baguaArt), 0.0, 0.88));
        col += mix(cinnabar, vec3(1.0, 0.65, 0.45), 0.42) * seamHot * 1.48 * (1.0 + cinnabarHit * 0.8);
        col += mix(cinnabar * 0.85, u_primaryColor * 0.4, 0.35) * seamAura * 0.85;

        glowCol = mix(u_rimColor, cinnabar, clamp(seamHot * 0.7 + cinnabarHit * 0.8, 0.0, 1.0));
        glowMask = clamp(baguaArt * 0.65 + fresnel * 0.55 + seamHot * 1.15 + cinnabarHit * 0.9, 0.0, 1.0);
        alpha = 0.025 + inkWash * 0.06 + baguaArt * 0.68 + crownMark * 0.42 + fresnel * 0.50 + seamHot * 0.85 + seamAura * 0.28 + clamp(cinnabarHit * 0.42, 0.0, 0.65);
        backTint = vec3(0.55, 0.52, 0.48);
    } else if (u_preset == 2) {
        // 3) KIM CHUNG TRÁO
        vec3 upRef = abs(unitPos.y) < 0.985 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 T = normalize(cross(upRef, unitPos));
        vec3 B = normalize(cross(unitPos, T));

        float eps = 0.0055;
        vec2 cSample = evalBellRelief(unitPos);
        float hC = cSample.x * detailScale;
        float glyphMask = cSample.y;
        float hT = evalBellRelief(normalize(unitPos + T * eps)).x * detailScale;
        float hB = evalBellRelief(normalize(unitPos + B * eps)).x * detailScale;
        vec3 bumpedN = normalize(N - (T * ((hT - hC) / eps) + B * ((hB - hC) / eps)) * 0.0115);

        vec3 R = reflect(-V, bumpedN);
        vec3 envRefl = sampleStudioEnv(R);
        float specKey = pow(max(dot(R, normalize(vec3(0.42, 0.80, 0.45))), 0.0), 68.0);
        float specRim = pow(max(dot(R, normalize(vec3(-0.75, 0.35, -0.52))), 0.0), 34.0);
        float bumpFresnel = pow(1.0 - clamp(dot(bumpedN, V), 0.0, 1.0), 2.55) * u_fresnel;

        vec3 smokedAmber   = u_primaryColor * vec3(0.28, 0.18, 0.07);
        vec3 antiqueBronze = mix(u_primaryColor * vec3(0.72, 0.48, 0.16), u_primaryColor, 0.55);
        vec3 champagne     = mix(u_rimColor, vec3(1.0, 0.97, 0.88), 0.45);

        col = mix(smokedAmber, antiqueBronze, (1.0 - ndotv) * 0.65) * 0.24;
        float reliefMask = smoothstep(0.08, 0.85, hC);
        col += antiqueBronze * reliefMask * (0.85 + specKey * 0.95);
        col += champagne * (specKey * (0.45 + reliefMask * 1.25) + specRim * (0.28 + reliefMask * 0.55));
        col += envRefl * mix(u_primaryColor, champagne, 0.42) * (0.18 + bumpFresnel * 0.46);
        col += mix(u_primaryColor, champagne, 0.62) * bumpFresnel * 0.88;

        float resonance = clamp(imp.x * 1.45 + imp.y * 1.65, 0.0, 2.0);
        float glyphGlow = glyphMask * (0.14 + resonance * 1.85);
        col += mix(u_primaryColor, vec3(1.0, 0.96, 0.78), 0.72) * glyphGlow * 1.25;
        float ringShimmer = imp.x * (0.75 + reliefMask * 0.85) + imp.y * 1.25;
        col += champagne * ringShimmer * 0.95;

        float hitBoost = 1.0 + resonance * 0.85;
        col += champagne * seamHot * 1.38 * hitBoost;
        col += mix(antiqueBronze, u_primaryColor, 0.6) * seamAura * 0.75 * hitBoost;

        glowCol = champagne;
        glowMask = clamp(bumpFresnel * 0.65 + specKey * 0.85 + glyphGlow * 0.85 + seamHot * 1.15 + ringShimmer * 0.7, 0.0, 1.0);
        alpha = 0.025 + (1.0 - ndotv) * 0.06 + reliefMask * 0.52 + glyphGlow * 0.36 + bumpFresnel * 0.54 + specKey * 0.32 + seamHot * 0.88 + seamAura * 0.32 + clamp(ringShimmer * 0.35, 0.0, 0.58);
        backTint = vec3(0.86, 0.62, 0.28);
    } else if (u_preset == 3) {
        // 4) NGỌC THANH TIÊN TRÁO
        vec3 q = unitPos * 2.5;
        vec3 warp = vec3(
            shFbm3(q + vec3(0.0, -t * 0.25, t * 0.15)),
            shFbm3(q + vec3(4.2, t * 0.22, -t * 0.18)),
            shFbm3(q + vec3(-3.1, -t * 0.20, 2.7))
        );
        float cloud = shFbm3(q * 1.35 + warp * 1.45 - vec3(0.0, t * 0.28, 0.0));
        float cloudScroll = smoothstep(0.38, 0.82, cloud);
        float veinRaw = shRidgedFbm3(unitPos * 3.8 + warp * 0.75 + vec3(0.0, -t * 0.18, 0.0));
        float goldVein = pow(smoothstep(0.74, 0.96, veinRaw), 2.4) * detailScale;

        vec3 jadeN = normalize(N + (warp - 0.5) * 0.12);
        vec3 R = reflect(-V, jadeN);
        vec3 envRefl = sampleStudioEnv(R);
        float spec = pow(max(dot(R, normalize(vec3(0.4, 0.82, 0.42))), 0.0), 64.0);

        vec3 deepJade = u_primaryColor * vec3(0.28, 0.56, 0.48);
        vec3 goldThread = vec3(1.0, 0.86, 0.48);

        col = mix(deepJade, u_primaryColor, cloudScroll * 0.75) * (0.44 + (1.0 - ndotv) * 0.48);
        col += u_rimColor * fresnel * 0.92 + goldThread * goldVein * 1.35;
        col += envRefl * mix(u_primaryColor, vec3(1.0), 0.5) * (0.20 + fresnel * 0.35) + vec3(1.0) * spec * 0.58;

        float hitBoost = 1.0 + imp.x * 1.4 + imp.y * 1.25;
        col += mix(u_rimColor, goldThread, 0.48) * seamHot * 1.45 * hitBoost;
        col += u_primaryColor * seamAura * 0.78 * hitBoost;
        col += mix(u_rimColor, goldThread, 0.42) * (imp.x * 1.05 + imp.y * 1.45);

        glowCol = mix(u_rimColor, goldThread, clamp(goldVein + seamHot * 0.5, 0.0, 1.0));
        glowMask = clamp(fresnel * 0.72 + goldVein * 0.95 + seamHot * 1.25 + imp.x * 0.8 + imp.y, 0.0, 1.0);
        alpha = 0.08 + (1.0 - ndotv) * 0.18 + cloudScroll * 0.24 * detailScale + goldVein * 0.48 + fresnel * 0.58 + spec * 0.24 + seamHot * 0.86 + seamAura * 0.40 + clamp(imp.x * 0.42 + imp.y * 0.48, 0.0, 0.65);
        backTint = vec3(0.32, 0.84, 0.68);
    } else if (u_preset == 4) {
        // 5) LÔI ĐÌNH ĐỘ KIẾP
        float snapT = floor(t * 9.0) * 0.11;
        float stormCloud = shFbm3(unitPos * 3.0 + vec3(t * 0.35, -t * 0.65, -t * 0.25));
        float boltA = shRidgedFbm3(unitPos * 4.4 + vec3(snapT * 2.1, -t * 1.9, snapT * 1.3));
        float boltB = shRidgedFbm3(unitPos * 7.8 + vec3(-t * 1.4, t * 2.4, snapT * 2.7));
        float flicker = 0.55 + 0.45 * sin(t * 24.0 + unitPos.x * 9.0);

        float arcMain = pow(smoothstep(0.74, 0.965, boltA), 3.2) * (0.75 + 0.55 * flicker);
        float arcFine = pow(smoothstep(0.79, 0.975, boltB), 3.6);
        float arcs = (arcMain * 1.55 + arcFine * 1.15) * detailScale;
        float groundArc = (u_groundEnabled > 0.5) ? exp(-hGround * 5.2) * pow(smoothstep(0.60, 0.94, boltB), 2.4) * 1.45 : 0.0;

        vec3 stormIndigo = u_primaryColor * vec3(0.24, 0.18, 0.56);
        vec3 boltCore = mix(u_rimColor, vec3(1.0), 0.72);

        col = stormIndigo * (0.35 + stormCloud * 0.55) * (0.55 + (1.0 - ndotv) * 0.65);
        col += u_primaryColor * fresnel * 0.85 + u_rimColor * pow(max(fresnel, 0.0), 1.35) * 0.75;
        col += mix(u_primaryColor, boltCore, 0.72) * (arcs + groundArc);

        float hitBoost = 1.0 + imp.x * 1.65 + imp.y * 1.85;
        col += boltCore * seamHot * 1.55 * hitBoost;
        col += mix(u_primaryColor, u_rimColor, 0.52) * seamAura * (0.8 + arcFine) * hitBoost;
        float strikeArc = imp.x * (0.8 + boltA * 1.5) + imp.y * 1.85;
        col += boltCore * strikeArc * 1.25;

        glowCol = boltCore;
        glowMask = clamp(arcs * 0.95 + groundArc * 0.9 + fresnel * 0.62 + seamHot * 1.2 + strikeArc, 0.0, 1.0);
        alpha = 0.025 + stormCloud * 0.06 + (1.0 - ndotv) * 0.06 + arcs * 0.65 + groundArc * 0.45 + fresnel * 0.54 + seamHot * 0.88 + seamAura * 0.32 + clamp(strikeArc * 0.45, 0.0, 0.70);
        backTint = vec3(0.45, 0.48, 0.95);
    } else if (u_preset == 5) {
        // 6) XÍCH VIÊM HỎA TRÁO
        vec3 flameP = vec3(unitPos.x * 3.1, unitPos.y * 2.2 - t * 1.35, unitPos.z * 3.1);
        float f1 = shFbm3(flameP);
        float f2 = shRidgedFbm3(vec3(unitPos.x * 4.8, unitPos.y * 3.6 - t * 2.05, unitPos.z * 4.8) + f1 * 0.85);
        float tongues = smoothstep(0.46, 0.92, f2) * detailScale;

        vec4 emCell = shVoronoi3(vec3(unitPos.x * 8.5, unitPos.y * 6.5 - t * 1.85, unitPos.z * 8.5));
        float embers = smoothstep(0.16, 0.02, emCell.y) * smoothstep(0.48, 0.92, emCell.z) * (0.55 + 0.45 * sin(t * 7.0 + emCell.w * 20.0));
        float baseHeat = (u_groundEnabled > 0.5) ? exp(-hGround * 2.4) * (0.55 + 0.45 * f1) : 0.22;

        vec3 obsidianRed = u_primaryColor * vec3(0.32, 0.08, 0.03);
        vec3 brightAmber = u_rimColor;
        vec3 whiteHot = vec3(1.0, 0.96, 0.82);

        col = obsidianRed * (0.40 + (1.0 - ndotv) * 0.55);
        col += mix(u_primaryColor, brightAmber, tongues) * (tongues * 0.92 + baseHeat * 0.72);
        col += mix(brightAmber, whiteHot, 0.55) * embers * 1.65;
        col += mix(u_primaryColor, brightAmber, 0.65) * fresnel * 0.95;

        float hitBoost = 1.0 + imp.x * 1.55 + imp.y * 1.65;
        col += mix(brightAmber, whiteHot, 0.55) * seamHot * 1.52 * hitBoost;
        col += mix(u_primaryColor, brightAmber, 0.35) * seamAura * (0.82 + f1 * 0.55) * hitBoost;
        col += mix(brightAmber, whiteHot, 0.60) * (imp.x * 1.15 + imp.y * 1.65);

        glowCol = mix(brightAmber, whiteHot, clamp(embers + seamHot * 0.5, 0.0, 1.0));
        glowMask = clamp(tongues * 0.55 + embers * 1.1 + fresnel * 0.75 + seamHot * 1.3 + imp.x + imp.y, 0.0, 1.0);
        alpha = 0.075 + (1.0 - ndotv) * 0.16 + tongues * 0.38 + baseHeat * 0.28 + embers * 0.52 + fresnel * 0.58 + seamHot * 0.88 + seamAura * 0.45 + clamp(imp.x * 0.42 + imp.y * 0.55, 0.0, 0.68);
        backTint = vec3(0.85, 0.34, 0.14);
    } else if (u_preset == 6) {
        // 7) HUYỀN BĂNG KÍNH
        vec4 vor = shVoronoi3(unitPos * 4.4);
        float azIce = atan(unitPos.z, unitPos.x) / TAU;
        float microShard = texture(u_noiseTex, vec2(azIce * 5.0, unitPos.y * 3.2)).r;

        float crackMain = smoothstep(0.085, 0.008, vor.x);
        float crackFine = smoothstep(0.68, 0.95, microShard) * 0.48;
        float cracks = clamp((crackMain + crackFine) * detailScale, 0.0, 1.4);

        vec3 facetTilt = normalize(vec3(vor.z - 0.5, vor.w - 0.5, fract(vor.z * 7.3) - 0.5));
        vec3 crystalN = normalize(N + facetTilt * (0.24 * detailScale));

        vec3 R = reflect(-V, crystalN);
        vec3 envRefl = sampleStudioEnv(R);
        float glint  = pow(max(dot(R, normalize(vec3(0.45, 0.82, 0.38))), 0.0), 86.0);
        float glint2 = pow(max(dot(R, normalize(vec3(-0.70, 0.42, -0.58))), 0.0), 48.0);
        float crystalFresnel = pow(1.0 - clamp(dot(crystalN, V), 0.0, 1.0), 2.5) * u_fresnel;
        float frostCrawl = (u_groundEnabled > 0.5) ? exp(-hGround * 3.0) * (0.45 + 0.55 * crackMain) : 0.0;

        vec3 deepIce = u_primaryColor * vec3(0.28, 0.56, 0.78);
        vec3 frostWhite = mix(u_rimColor, vec3(1.0), 0.55);

        col = mix(deepIce, u_primaryColor, vor.z * 0.55 + (1.0 - ndotv) * 0.45) * 0.48;
        col += envRefl * mix(u_primaryColor, frostWhite, 0.52) * (0.24 + crystalFresnel * 0.48);
        col += frostWhite * (glint * 1.45 + glint2 * 0.75);
        col += mix(u_primaryColor, frostWhite, 0.65) * (cracks * 0.62 + frostCrawl * 0.65);
        col += u_rimColor * crystalFresnel * 0.88;

        float impactCrack = imp.x * (0.65 + cracks * 1.65) + imp.y * 1.45;
        col += frostWhite * impactCrack * 1.15;
        float hitBoost = 1.0 + imp.x * 1.3 + imp.y * 1.2;
        col += frostWhite * seamHot * 1.45 * hitBoost;
        col += mix(u_primaryColor, frostWhite, 0.42) * seamAura * (0.72 + crackMain * 0.65) * hitBoost;

        glowCol = frostWhite;
        glowMask = clamp(crystalFresnel * 0.75 + glint * 1.1 + cracks * 0.45 + seamHot * 1.2 + impactCrack * 0.8, 0.0, 1.0);
        alpha = 0.08 + (1.0 - ndotv) * 0.16 + cracks * 0.36 + frostCrawl * 0.30 + crystalFresnel * 0.58 + glint * 0.42 + seamHot * 0.86 + seamAura * 0.40 + clamp(impactCrack * 0.38, 0.0, 0.62);
        backTint = vec3(0.55, 0.85, 1.0);
        refrScale *= 1.35;
    } else if (u_preset == 7) {
        // 8) LƯU LY BẢO CHÂU (Optical Hollow Glass IOR 1.52)
        float micro = (shFbm3(unitPos * 3.0 + vec3(0.0, t * 0.12, 0.0)) - 0.5) * 0.018;
        vec3 gN = normalize(N + vec3(micro, vWaveDisp * 1.8, -micro));
        float cosTheta = clamp(dot(gN, V), 0.0, 1.0);

        float F0 = 0.0426;
        float schlick = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
        float rimGlass = pow(1.0 - cosTheta, 3.1) * u_fresnel;

        vec3 refrR = refract(-V, gN, 1.0 / 1.505);
        vec3 refrG = refract(-V, gN, 1.0 / 1.522);
        vec3 refrB = refract(-V, gN, 1.0 / 1.545);
        vec3 R = reflect(-V, gN);

        vec3 dispRefr = vec3(
            sampleStudioEnv(length(refrR) > 0.01 ? refrR : R).r,
            sampleStudioEnv(length(refrG) > 0.01 ? refrG : R).g,
            sampleStudioEnv(length(refrB) > 0.01 ? refrB : R).b
        );
        vec3 envRefl = sampleStudioEnv(R);

        float filmPhase = (1.0 - cosTheta) * 5.6 + t * 0.35 + imp.x * 2.2;
        vec3 irid = 0.5 + 0.5 * cos(TAU * (filmPhase * 0.28 + vec3(0.0, 0.33, 0.67)));
        float spec1 = pow(max(dot(R, normalize(vec3(0.40, 0.84, 0.42))), 0.0), 160.0);
        float spec2 = pow(max(dot(R, normalize(vec3(-0.74, 0.36, -0.56))), 0.0), 72.0);

        col = dispRefr * mix(vec3(0.85), u_primaryColor, 0.45) * 0.62;
        col += envRefl * (0.28 + schlick * 1.45);
        col += mix(u_rimColor, irid, 0.32 * detailScale) * rimGlass * 0.86;
        col += vec3(1.0, 0.99, 0.97) * (spec1 * 1.85 + spec2 * 0.65);

        float hitBoost = 1.0 + imp.x * 1.35 + imp.y * 1.3;
        col += mix(vec3(1.0), irid, 0.25) * seamHot * 1.55 * hitBoost;
        col += mix(u_primaryColor, irid, 0.28) * seamAura * 0.58 * hitBoost;
        col += mix(vec3(1.0), irid, 0.45) * (imp.x * 0.95 + imp.y * 1.4);

        glowCol = mix(u_rimColor, irid, 0.28);
        glowMask = clamp(rimGlass * 0.7 + spec1 * 1.4 + seamHot * 1.3 + imp.x * 0.8 + imp.y, 0.0, 1.0);
        alpha = 0.055 + (1.0 - cosTheta) * 0.14 + schlick * 0.62 + rimGlass * 0.52 + spec1 * 0.55 + spec2 * 0.24 + seamHot * 0.88 + seamAura * 0.34 + clamp(imp.x * 0.38 + imp.y * 0.45, 0.0, 0.62);
        backTint = vec3(0.76, 0.90, 1.0);
        refrScale *= 1.85;
    } else {
        // 9) THANH PHONG HỘ TRÁO
        float az = atan(unitPos.z, unitPos.x);
        float u = az / TAU;
        float y = unitPos.y;
        float poleMask = smoothstep(0.985, 0.72, abs(y));

        float eddyA1 = texture(u_noiseTex, vec2(u * 2.0 - t * 0.14, y * 1.25 - t * 0.26)).r;
        float eddyA2 = texture(u_noiseTex, vec2(u * 2.0 - t * 0.14 + 0.37, y * 1.25 - t * 0.26 + 0.53)).r;
        float eddyB1 = texture(u_noiseTex, vec2(u * 3.0 + t * 0.09, y * 1.85 - t * 0.42)).r;
        float eddyB2 = texture(u_noiseTex, vec2(u * 3.0 + t * 0.09 + 0.41, y * 1.85 - t * 0.42 + 0.29)).r;
        vec2 flowWarp = (vec2(eddyA1, eddyA2) - 0.5) * 0.22 + (vec2(eddyB1, eddyB2) - 0.5) * 0.12;

        float pitch1 = y * 0.62 + flowWarp.y * 0.35;
        vec2 uvStream1 = vec2(u * 2.0 + pitch1 - t * 0.56 + flowWarp.x * 0.45, y * 2.35 - t * 0.48 + flowWarp.y);
        vec2 uvStream2 = vec2(u * 3.0 + y * 1.15 - t * 0.92 - flowWarp.x * 0.32, y * 3.60 - t * 0.82 - flowWarp.y * 0.7);

        float gustEnvA = texture(u_noiseTex, vec2(u * 1.0 + y * 0.28 - t * 0.19, y * 0.95 - t * 0.32)).r;
        float gustEnvB = texture(u_noiseTex, vec2(u * 1.0 + y * 0.28 - t * 0.19 + 0.5, y * 0.95 - t * 0.32 + 0.25)).r;
        float gustWindowA = smoothstep(0.28, 0.78, gustEnvA);
        float gustWindowB = smoothstep(0.32, 0.82, gustEnvB);

        float s1 = texture(u_noiseTex, uvStream1).r;
        float s2 = texture(u_noiseTex, uvStream2).r;
        float ribbonA = smoothstep(0.36, 0.88, s1) * gustWindowA * poleMask;
        float ribbonB = smoothstep(0.42, 0.92, s2) * (0.42 + 0.78 * gustWindowB) * poleMask;

        float bladeCoord1 = u * 3.0 + y * 0.92 - t * 0.78 + (eddyA1 - 0.5) * 0.24;
        float bladeBand1  = texture(u_noiseTex, vec2(bladeCoord1, y * 3.2 - t * 0.55)).r;
        float bladeMask1  = pow(smoothstep(0.56, 0.95, bladeBand1), 2.4) * smoothstep(0.35, 0.85, gustEnvA);

        float bladeCoord2 = u * 4.0 + y * 1.48 - t * 1.24 - (eddyB2 - 0.5) * 0.20;
        float bladeBand2  = texture(u_noiseTex, vec2(bladeCoord2, y * 4.6 - t * 0.95)).r;
        float bladeMask2  = pow(smoothstep(0.62, 0.96, bladeBand2), 2.8) * smoothstep(0.40, 0.88, eddyA2);
        float windBlades  = clamp((bladeMask1 * 1.25 + bladeMask2 * 1.15) * poleMask * detailScale, 0.0, 1.85);

        float crownVortex = 0.0;
        if (y > 0.56) {
            float rTop = length(unitPos.xz);
            float aTop = atan(unitPos.z, unitPos.x);
            float logR = log(max(rTop, 0.05));
            vec2 eyeUV = vec2(aTop / TAU * 3.0 - logR * 0.65 - t * 0.65, rTop * 1.9 - t * 0.55);
            float eyeWisp = texture(u_noiseTex, eyeUV).r;
            float eyeSpiral = smoothstep(0.38, 0.90, eyeWisp) * smoothstep(0.05, 0.22, rTop) * smoothstep(0.68, 0.24, rTop);
            float eyeRim = exp(-pow((rTop - 0.20) * 14.0, 2.0)) * (0.45 + 0.55 * eddyA1);
            crownVortex = (eyeSpiral * 0.95 + eyeRim * 0.45) * smoothstep(0.56, 0.84, y);
        }

        float baseDraft = 0.0;
        if (u_groundEnabled > 0.5 && hGround < 1.35) {
            float draftWisp = texture(u_noiseTex, vec2(u * 4.0 + hGround * 0.55 - t * 0.88, hGround * 1.1 - t * 0.95)).r;
            baseDraft = exp(-hGround * 2.85) * smoothstep(0.28, 0.88, draftWisp);
        }

        float gustStrength = clamp(ribbonA * 0.6 + ribbonB * 0.7 + windBlades * 0.8, 0.0, 1.5);
        vec3 airN = normalize(N + vec3(flowWarp.x, (s1 - 0.5) * 0.25, flowWarp.y) * (0.28 + gustStrength * 0.35));
        vec3 R = reflect(-V, airN);
        vec3 envRefl = sampleStudioEnv(R);
        float airSpec = pow(max(dot(R, normalize(vec3(0.42, 0.80, 0.42))), 0.0), 72.0);

        vec3 deepGale   = u_primaryColor * vec3(0.22, 0.42, 0.40);
        vec3 celadonAir = mix(u_primaryColor, u_rimColor, 0.42);
        vec3 silverEdge = mix(u_rimColor, vec3(1.0), 0.62);

        col = mix(deepGale, u_primaryColor * 0.55, 1.0 - ndotv) * (0.20 + (1.0 - ndotv) * 0.32);
        col += celadonAir * (ribbonA * 0.58 + ribbonB * 0.62) * (0.65 + fresnel * 0.55) * detailScale;
        col += silverEdge * (windBlades * 1.28 + crownVortex * 0.88);
        col += mix(u_primaryColor, silverEdge, 0.52) * baseDraft * 0.78;
        col += u_rimColor * fresnel * 0.90 + envRefl * (0.10 + fresnel * 0.26) + silverEdge * airSpec * (0.42 + gustStrength * 0.55);

        float hitBoost = 1.0 + imp.x * 1.45 + imp.y * 1.25;
        col += silverEdge * seamHot * 1.48 * hitBoost;
        col += mix(u_primaryColor, u_rimColor, 0.45) * seamAura * (0.72 + baseDraft * 0.65) * hitBoost;
        float gustBurst = imp.x * (0.95 + s2 * 1.15) + imp.y * 1.35;
        col += mix(celadonAir, silverEdge, 0.68) * gustBurst * 1.15;

        glowCol = silverEdge;
        glowMask = clamp(windBlades * 0.85 + crownVortex * 0.7 + fresnel * 0.7 + seamHot * 1.25 + gustBurst * 0.8, 0.0, 1.0);
        alpha = 0.048 + (1.0 - ndotv) * 0.14 + (ribbonA * 0.30 + ribbonB * 0.28) * detailScale + windBlades * 0.56 + crownVortex * 0.38 + baseDraft * 0.34 + fresnel * 0.52 + airSpec * 0.22 + seamHot * 0.88 + seamAura * 0.42 + clamp(gustBurst * 0.42, 0.0, 0.64);
        backTint = vec3(0.44, 0.86, 0.78);
    }

    float backGain = clamp(u_backWall * 1.35, 0.42, 0.72);
    float passMult = (u_passSide < 0.0) ? backGain : 1.0;
    vec3 sideCol = (u_passSide < 0.0) ? col * mix(vec3(0.75), backTint, 0.45) : col;
    float finalAlpha = clamp(alpha * u_opacity * passMult, 0.0, 0.95);

    if (u_premultiply > 0.5)
    {
        // EMISSION PASS: tone-map-safe bloom strictly on rim, seam hot-glow, lightning, runes & impact rings.
        float emSide = (u_passSide < 0.0) ? clamp(u_backWall * 1.25, 0.38, 0.65) : 1.0;
        float emMask = pow(clamp(glowMask, 0.0, 1.0), 2.2) * u_opacity * emSide;
        vec3 glow = VFX_TonemapSafeEmission(glowCol, u_emissionGain, emMask);
        finalColor = VFX_ResolvePremultiplied(vec3(0.0), 0.0, 0.0, glow, 1.0, 1.0);
    }
    else
    {
        // BODY PASS: blend crisp motifs & rim cleanly over the scene (preserving back wall & characters behind dome).
        finalColor = VFX_ResolveBody(sideCol, 1.0, finalAlpha);
    }
}
