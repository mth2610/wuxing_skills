#version 330
#include "core/shaders/common/fs_header.glsl"

// texture0: stacked 256x1024 atlas:
//   top half    (v in 0.0..0.5): paperTex    (RGB = bamboo/mulberry grain, A = deckled edge mask)
//   bottom half (v in 0.5..1.0): runeGlowTex (R = crisp calligraphy, G = multi-pass outer bloom,
//                                             B = back-face ink bleed, A = sharp Daoist double-border)
uniform sampler2D texture0;

uniform float u_modeBlend;      // 0.0 = Handmade Paper, 1.0 = Luminous Outline
uniform float u_glowIntensity;  // 0.3 .. 2.2
uniform float u_premultiplied;  // 1.0 when drawn with VFX_SURFACE_PREMULTIPLIED
uniform vec3  u_warmLightColor;
uniform vec3  u_coolLightColor;

in vec4 v_instTraits;
in vec3 v_warmLightView;
in vec3 v_coolLightView;

void Talisman_GetPalette(
    float pid,
    out vec3 paperTint,
    out vec3 inkCol,
    out vec3 bleedCol,
    out vec3 sssCol,
    out vec3 mysticCore,
    out vec3 glowPrimary,
    out vec3 glowHot
) {
    if (pid < 0.25) {
        // 0: Hoang Phu (Imperial Yellow Paper + Cinnabar Red / Golden-Amber Glow)
        paperTint   = vec3(0.96, 0.74, 0.28);
        inkCol      = vec3(0.62, 0.09, 0.06);
        bleedCol    = vec3(0.56, 0.16, 0.09);
        sssCol      = vec3(1.00, 0.66, 0.22);
        mysticCore  = vec3(0.16, 0.09, 0.03);
        glowPrimary = vec3(1.00, 0.56, 0.12);
        glowHot     = vec3(1.00, 0.90, 0.52);
    } else if (pid < 0.75) {
        // 1: Hoa Phu (Vermilion-Orange Paper + Gold Ink / Blazing Fire-Orange Glow)
        paperTint   = vec3(0.76, 0.21, 0.11);
        inkCol      = vec3(0.95, 0.76, 0.30);
        bleedCol    = vec3(0.58, 0.36, 0.12);
        sssCol      = vec3(1.00, 0.34, 0.10);
        mysticCore  = vec3(0.15, 0.03, 0.02);
        glowPrimary = vec3(1.00, 0.30, 0.06);
        glowHot     = vec3(1.00, 0.84, 0.40);
    } else {
        // 2: Loi Phu (Aged Indigo-Tan Paper + Deep Ink / Electric Cyan-Blue Glow)
        paperTint   = vec3(0.77, 0.76, 0.72);
        inkCol      = vec3(0.09, 0.12, 0.19);
        bleedCol    = vec3(0.16, 0.19, 0.26);
        sssCol      = vec3(0.66, 0.88, 0.96);
        mysticCore  = vec3(0.03, 0.06, 0.14);
        glowPrimary = vec3(0.16, 0.76, 1.00);
        glowHot     = vec3(0.76, 0.96, 1.00);
    }
}

void main() {
    vec2 uvPaper = vec2(fragTexCoord.x, fragTexCoord.y * 0.494 + 0.003);
    vec2 uvRune  = vec2(fragTexCoord.x, 0.503 + fragTexCoord.y * 0.494);
    vec4 paperSample = texture(texture0, uvPaper);
    vec4 runeSample  = texture(texture0, uvRune);

    float paperAlpha = paperSample.a;
    // Sharpen mip-filtered ink & border so crimson calligraphy stays crisp at gameplay distance
    float inkMask    = smoothstep(0.08, 0.48, runeSample.r);
    float outerGlow  = runeSample.g;
    float bleedMask  = smoothstep(0.06, 0.52, runeSample.b);
    float borderMask = smoothstep(0.10, 0.50, runeSample.a);

    // Early discard outside visible bounds for both modes
    if (u_modeBlend < 0.02) {
        if (paperAlpha < 0.42) discard;
    } else {
        if (paperAlpha < 0.04 && outerGlow < 0.008) discard;
    }

    float paletteKey = v_instTraits.r;
    float glowBoost  = v_instTraits.g * 2.0;
    float instAlpha  = v_instTraits.b;

    vec3 paperTint, inkCol, bleedCol, sssCol, mysticCore, glowPrimary, glowHot;
    Talisman_GetPalette(paletteKey, paperTint, inkCol, bleedCol, sssCol, mysticCore, glowPrimary, glowHot);

    // View-space camera is at origin (immune to matModel = model * view under rlvk)
    vec3 V = normalize(-fragPosition);
    vec3 N = normalize(fragNormal) * (gl_FrontFacing ? 1.0 : -1.0);
    float ndotv   = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - ndotv, 2.4);

    float fiberLuma = dot(paperSample.rgb, vec3(0.30, 0.59, 0.11));

    // -----------------------------------------------------------------------
    // MODE 0: HANDMADE PAPER (Realistic Bamboo Parchment + Back Bleed + SSS)
    // -----------------------------------------------------------------------
    vec3 rawPaper = paperSample.rgb * paperTint * (gl_FrontFacing ? 1.0 : 0.92);
    vec3 surfaceAlbedo;
    float inkDensity;
    if (gl_FrontFacing) {
        surfaceAlbedo = mix(rawPaper, inkCol * (0.85 + 0.22 * fiberLuma), inkMask * 0.94);
        inkDensity = inkMask;
    } else {
        surfaceAlbedo = mix(rawPaper, bleedCol, bleedMask * 0.44);
        inkDensity = bleedMask * 0.50;
    }

    vec3 toWarm = v_warmLightView - fragPosition;
    float dWarm = length(toWarm);
    vec3 Lw = toWarm / max(dWarm, 0.001);
    float attWarm  = 1.0 / (1.0 + 0.022 * dWarm * dWarm);
    float wrapWarm = max((dot(N, Lw) + 0.22) / 1.22, 0.0);

    vec3 Lc = normalize(v_coolLightView - fragPosition);
    float wrapCool = max((dot(N, Lc) + 0.18) / 1.18, 0.0);

    vec3 Hw = normalize(Lw + V);
    float inkSpec = gl_FrontFacing ? pow(max(dot(N, Hw), 0.0), 28.0) * inkMask * 0.22 : 0.0;

    // Warm analytical subsurface scattering (backlit translucency)
    float backWarm = pow(clamp(dot(V, -Lw), 0.0, 1.0), 2.5) * 0.72 + max(dot(-N, Lw), 0.0) * 0.45;
    float backCool = pow(clamp(dot(V, -Lc), 0.0, 1.0), 2.8) * 0.45 + max(dot(-N, Lc), 0.0) * 0.25;
    float transmittance = (1.0 - inkDensity * 0.74) * (0.78 + 0.35 * fiberLuma);
    vec3 sss = (u_warmLightColor * backWarm * attWarm * 0.92 + u_coolLightColor * backCool * 0.38) * sssCol * transmittance;

    vec3 ambient = vec3(0.22, 0.24, 0.30) * (0.78 + 0.22 * abs(N.y));
    vec3 handmadeCol = surfaceAlbedo * (ambient + u_warmLightColor * wrapWarm * attWarm * 1.55 + u_coolLightColor * wrapCool * 0.78)
                     + u_warmLightColor * inkSpec * attWarm
                     + sss * (0.78 + 0.32 * u_glowIntensity);
    handmadeCol += mix(u_coolLightColor, u_warmLightColor, 0.5) * fresnel * 0.09;

    // -----------------------------------------------------------------------
    // MODE 1: LUMINOUS OUTLINED SPIRIT TALISMAN (Translucent Body + Glowing Lines)
    // -----------------------------------------------------------------------
    // Crisp stroke masks (keep interior clean and transparent between lines)
    float crispInk    = smoothstep(0.24, 0.70, runeSample.r);
    float crispBorder = smoothstep(0.22, 0.66, runeSample.a);
    float runeCore    = max(crispBorder, crispInk * (gl_FrontFacing ? 1.0 : 0.78));

    // Subtle smoked-glass translucent paper interior (mostly see-through!)
    vec3 coreShade = mysticCore * (0.45 + 0.55 * fiberLuma);

    // Flowing spiritual Qi wave along strokes
    float qiWave  = 0.78 + 0.22 * sin(fragTexCoord.y * 10.5 + u_time * 3.6 + paletteKey * 4.2);
    float qiPulse = 0.90 + 0.10 * sin(u_time * 2.4 + fragTexCoord.x * 5.0);
    float energyBoost = u_glowIntensity * (1.0 + glowBoost * 0.55);

    // Incandescent stroke core: bright white-gold center -> rich elemental glow edge
    vec3 whiteHot  = mix(glowHot, vec3(1.0, 0.98, 0.88), 0.72);
    vec3 lineCol   = mix(glowPrimary, whiteHot, smoothstep(0.25, 0.88, runeCore));
    vec3 sharpGlow = lineCol * runeCore * (2.35 * qiWave);

    // Tight optical halo hugging the strokes & deckled rim without clouding the interior
    float haloTight = pow(outerGlow, 2.25);
    vec3  outerAura = mix(glowPrimary, glowHot, haloTight * 0.50) * haloTight * (0.55 * qiWave);

    // Subtle deckled paper edge rim highlight
    float edgeBand = smoothstep(0.35, 0.65, paperAlpha) * (1.0 - smoothstep(0.65, 0.98, paperAlpha));
    vec3  rimGlow  = glowPrimary * (edgeBand * 0.45 + fresnel * paperAlpha * 0.22);

    // Translucent body alpha (~22%) so background is clearly visible through the sheet;
    // strokes carry high coverage (~92%) while outer halo is pure additive light (alpha ~0).
    float bodyAlpha     = paperAlpha * 0.22;
    float strokeAlpha   = clamp(runeCore * 0.92, 0.0, 0.95);
    float glowModeAlpha = max(bodyAlpha, strokeAlpha);

    // Premultiplied RGB for Mode 1: translucent smoked tint + pure additive glowing lines/halo
    vec3 glowModePremul = coreShade * bodyAlpha + (sharpGlow + outerAura + rimGlow) * energyBoost * qiPulse;
    vec3 glowModeStraight = glowModePremul / max(glowModeAlpha, 0.001);

    // Seamless interpolation between Mode 0 (Handmade) and Mode 1 (Luminous Glow)
    float handmadeAlpha = smoothstep(0.32, 0.62, paperAlpha);
    float finalAlpha    = mix(handmadeAlpha, glowModeAlpha, u_modeBlend) * instAlpha;

    if (u_premultiplied > 0.5) {
        vec3 handmadePremul = handmadeCol * handmadeAlpha;
        vec3 finalPremul = mix(handmadePremul, glowModePremul, u_modeBlend) * instAlpha;
        finalColor = vec4(finalPremul, finalAlpha);
    } else {
        vec3 finalCol = mix(handmadeCol, glowModeStraight, u_modeBlend);
        finalColor = vec4(finalCol, finalAlpha);
    }
}
