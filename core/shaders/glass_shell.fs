#version 330
#include "core/shaders/common/fs_header.glsl"

in vec3 shieldViewDir;
in float shieldFresnel;

uniform vec4 u_bodyColor;
uniform vec4 u_rimColor;
uniform float u_opacity;
uniform float u_rimStrength;
uniform int u_emissionOnly;
uniform int u_wallPass;
uniform vec3 u_lightDirView;

/* One lookup: R=hexagon, G=Perlin-like scrolling noise, B=soft mask. */
uniform sampler2D u_packedTex;
uniform int u_hasPacked;
uniform sampler2D u_flowTex;
uniform int u_hasFlow;
uniform sampler2D u_matcapTex;
uniform int u_hasMatcap;
uniform float u_noiseScale;
uniform float u_noiseSpeed;
uniform float u_flowStrength;
uniform float u_flowSpeed;
uniform float u_parallaxDepth;
uniform float u_innerDepth;

/* Optional half-resolution depth path; disabled by default on low-end GPUs. */
uniform sampler2D u_depthTex;
uniform int u_hasDepth;
uniform float u_depthEnabled;
uniform float u_depthLod;
uniform float u_contactStrength;
uniform vec4 u_contactColor;
uniform float u_contactThickness;
uniform float u_baseAlpha;
uniform float u_fresnelAlpha;
uniform float u_contactAlpha;

uniform vec3 u_impactView;
uniform float u_impactAge;
uniform float u_rippleFrequency;
uniform float u_rippleSpeed;

float shieldPow4(float x) {
    float x2 = x * x;
    return x2 * x2;
}

float depthContact(vec2 uv) {
    if (u_hasDepth == 0 || u_depthEnabled < 0.5) return 0.0;
    float sceneDepth = texture(u_depthTex, uv).r;
    float gap = sceneDepth - length(fragPosition);
    if (gap <= 0.0) return 0.0;
    return 1.0 - smoothstep(0.0, u_contactThickness, gap);
}

float impactRipple() {
    if (u_impactAge > 4.0) return 0.0;
    float d = distance(fragPosition, u_impactView);
    float wave = sin(d * u_rippleFrequency - u_impactAge * u_rippleSpeed);
    return max(wave, 0.0) * exp(-d) * exp(-u_impactAge);
}

void main() {
    vec3 viewDir = normalize(shieldViewDir);
    vec3 normal = normalize(fragNormal);
    float fresnel = shieldFresnel;
    float t = u_time * u_flowSpeed;
    vec2 baseUV = fragTexCoord * u_noiseScale;
    vec3 flowSample = (u_hasFlow != 0) ? texture(u_flowTex, baseUV).rgb :
                      ((u_hasPacked != 0) ? texture(u_packedTex, baseUV).rgb : vec3(0.5, 0.5, 1.0));
    vec2 flow = (flowSample.rg * 2.0 - 1.0) * u_flowStrength;
    vec2 innerUV = baseUV + flow * (t + 1.0) + shieldViewDir.xy * u_parallaxDepth * u_innerDepth;
    vec3 packed = (u_hasPacked != 0) ? texture(u_packedTex, innerUV).rgb : vec3(0.0, 0.0, 1.0);
    float energy = max(packed.r, packed.g);
    float noise = packed.g;
    float softMask = (u_hasPacked != 0) ? packed.b : 1.0;
    float contact = depthContact(gl_FragCoord.xy / u_resolution);
    float ripple = impactRipple();
    vec3 lightDir = normalize(u_lightDirView);
    float light = max(dot(normal, lightDir), 0.0);
    float pattern = smoothstep(0.22, 0.78, energy) * (0.35 + 0.65 * noise);
    float bottomGlow = smoothstep(0.05, 0.92, -normal.y);
    vec3 body = u_bodyColor.rgb * (0.30 + 0.22 * light + pattern * 0.55);
    body += u_rimColor.rgb * bottomGlow * 0.75;
    vec2 matcapUV = normal.xy * 0.5 + 0.5;
    vec3 matcap = (u_hasMatcap != 0) ? texture(u_matcapTex, matcapUV).rgb
                                     : vec3(0.25 + normal.y * 0.25);
    vec3 glow = u_rimColor.rgb * (fresnel * u_rimStrength + pattern * 0.35 + ripple * 1.5);
    glow += matcap * fresnel * 0.55;
    glow += u_rimColor.rgb * bottomGlow * (0.65 + pattern * 1.15);
    glow += u_contactColor.rgb * contact * u_contactStrength;

    if (u_emissionOnly != 0) {
        finalColor = vec4(glow, u_opacity * (0.20 + fresnel * 0.80 + ripple));
        return;
    }
    float wallWeight = (u_wallPass == 0) ? 0.45 : 1.0;
    float alpha = u_opacity * wallWeight * softMask *
                  (u_baseAlpha + fresnel * u_fresnelAlpha +
                   contact * u_contactAlpha + ripple * 0.18);
    finalColor = vec4(body + u_rimColor.rgb * fresnel * 0.45, alpha);
}
