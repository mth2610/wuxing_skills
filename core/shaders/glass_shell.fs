#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec3 shieldViewDir;
in float shieldFresnel;

uniform vec4 u_bodyColor;
uniform vec4 u_rimColor;
uniform float u_opacity;
uniform float u_rimStrength;
uniform float u_bodyOpacity;
uniform float u_emissionGain;
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
uniform sampler2D u_sceneTex;
uniform int u_hasScene;
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
    float filament = smoothstep(0.58, 0.82, energy) *
                     smoothstep(0.42, 0.76, noise);
    float bottomGlow = smoothstep(0.05, 0.92, -normal.y);
    // Keep the carrier readable on white backgrounds: a low mass floor plus
    // structured pattern, while the semantic Magic appearance supplies the
    // stronger radiance through the separate emission pass.
    float bodyStructure = smoothstep(0.18, 0.72, pattern);
    /* Preserve hue without laying a milky, high-luminance film over a bright
     * destination.  The carrier is deliberately darker/sparser; rim + the
     * separate additive emission pass provide the perceived brightness. */
    vec3 body = u_bodyColor.rgb * (0.055 + 0.11 * light + bodyStructure * 0.24);
    body = pow(max(body, vec3(0.0)), vec3(1.12));
    body += u_rimColor.rgb * bottomGlow * 0.75;
    vec2 matcapUV = normal.xy * 0.5 + 0.5;
    vec3 matcap = (u_hasMatcap != 0) ? texture(u_matcapTex, matcapUV).rgb
                                     : vec3(0.25 + normal.y * 0.25);
    vec3 glow = u_rimColor.rgb * (fresnel * u_rimStrength + pattern * 0.35 + ripple * 1.5);
    glow += matcap * fresnel * 0.55;
    glow += u_rimColor.rgb * bottomGlow * (0.65 + pattern * 1.15);
    glow += u_contactColor.rgb * contact * u_contactStrength;

    /* The rear optical interface must remain present, but it cannot be an
     * indistinguishable duplicate of the front shell.  Give it a darker,
     * quieter carrier so the eye reads a real inner volume instead of one
     * flat translucent disc. */
    float rearInterface = (u_wallPass == 0) ? 1.0 : 0.0;
    /* Rear coverage is now intentionally visible; do not attenuate its body
     * a second time or the 0.45 volume term collapses to a barely measurable
     * tint after the shared bodyOpacity multiplier. */
    body *= mix(1.0, 1.35, rearInterface);
    glow *= mix(1.0, 0.68, rearInterface);

    /* Safe scene-through glass: the C side binds a copy made after the 3D
     * scene is complete.  This is what gives the shield a real volume instead
     * of a flat tinted disc; the authored carrier remains on top for colour. */
    if (u_hasScene != 0 && u_emissionOnly == 0) {
        vec2 sceneUV = gl_FragCoord.xy / u_resolution;
        sceneUV += flow * 0.018 * (0.35 + fresnel);
        vec3 behind = texture(u_sceneTex, sceneUV).rgb;
        /* Scene dominates the membrane; the authored tint only colours the
         * glass, otherwise a flat QA background makes this indistinguishable
         * from the legacy opaque carrier. */
        vec3 glassTint = mix(behind * 0.92, body, 0.18 + 0.22 * pattern);
        body = mix(body, glassTint, 0.92 * softMask);
    }

    if (u_emissionOnly != 0) {
        // Emission is sparse radiance, not a second translucent body.  A
        // non-zero floor here paints the entire sphere on bright backgrounds
        // and defeats the shared Magic body/emission separation.
        float emissionMask = max(fresnel * 0.92,
                                 max(filament * 0.0,
                                     max(contact * 0.90, ripple)));
        float emissionAlpha = u_opacity * clamp(emissionMask, 0.0, 1.0);
        finalColor = VFX_ResolveEmission(glow, u_emissionGain, 1.0, emissionAlpha);
        return;
    }
    /* Keep the rear interface for thickness/parallax, but make it a light
     * optical contribution.  Removing it flattens the shell; weighting it too
     * strongly is what creates the milky full-sphere wash on bright backdrops. */
    float wallWeight = (u_wallPass == 0) ? 0.86 : 1.0;
    float alpha = u_opacity * wallWeight * softMask *
                  (u_baseAlpha + fresnel * u_fresnelAlpha +
                   contact * u_contactAlpha + ripple * 0.18);
    /* Rear glass has a deliberate interior volume term.  Without it the
     * back interface only appears at the silhouette and the sphere reads as
     * a flat rim; keep it non-emissive and subdued so the scene remains
     * visible through the centre. */
    alpha += rearInterface * 0.45 * u_opacity * softMask;
    /* Glass carrier coverage is intentionally sparse.  The previous value
     * still tinted the entire sphere; this multiplier makes the scene-through
     * window unambiguous while rim/emission remain independently bright. */
    alpha *= 0.35;
    finalColor = VFX_ResolvePremultiplied(
        body + u_rimColor.rgb * fresnel * 0.45, u_bodyOpacity, alpha,
        vec3(0.0), 0.0, 0.0);
}
