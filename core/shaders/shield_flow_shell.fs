#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// Textured water-energy shield.  Unlike glass_shell.fs, this shader has no
// camera-space window: every transparent gap is read from the moving membrane
// sheet itself, so the silhouette remains organic as the camera moves.
in vec3 shieldViewDir;

uniform vec4 u_bodyColor;
uniform vec4 u_rimColor;
uniform float u_opacity;
uniform float u_bodyOpacity;
uniform float u_emissionGain;
uniform int u_emissionOnly;
// Primary sheet follows the engine's geometry-material contract: the
// immediate-mode mesh binds it through rlSetTexture(), which is texture0.
uniform sampler2D texture0;
uniform sampler2D u_flowTex;
uniform sampler2D u_sceneTex;
uniform int u_hasScene;
uniform sampler2D u_depthTex;
uniform int u_hasDepth;
uniform float u_depthEnabled;
uniform float u_contactThickness;
uniform float u_time;
uniform float u_flowSpeed;
uniform float u_flowStrength;
uniform float u_flowTiling;
uniform float u_refractionStrength;

vec2 FlowedUV(vec2 uv, vec2 direction, float phase)
{
    // Centre the phase around zero: the two samples travel equal distances in
    // opposite parts of the loop, then crossfade at the point both are quiet.
    return fract(uv + direction * ((phase * 2.0) - 1.0) * u_flowStrength);
}

float DepthContact(vec2 uv)
{
    if (u_hasDepth == 0 || u_depthEnabled < 0.5) return 0.0;
    float sceneDepth = texture(u_depthTex, uv).r;
    float fragmentDepth = max(-fragPosition.z, 0.0001);
    float gap = sceneDepth - fragmentDepth;
    float t = clamp(gap / max(u_contactThickness, 0.0001), 0.0, 1.0);
    float falloff = 1.0 - t;
    // Quadratic keeps a broad liquid transition around the terrain contact;
    // the narrow emissive core is selected separately below.
    return falloff * falloff;
}

void main()
{
    vec3 viewDir = normalize(shieldViewDir);
    vec3 normal = normalize(fragNormal);
    float ndotv = clamp(abs(dot(normal, viewDir)), 0.0, 1.0);
    float fresnel = pow(1.0 - ndotv, 4.0);

    vec2 baseUV = fragTexCoord * u_flowTiling;
    vec2 flowVector = texture(u_flowTex, fract(baseUV * 0.53 + vec2(0.11, -0.07))).rg * 2.0 - 1.0;
    float phaseA = fract(u_time * u_flowSpeed);
    float phaseB = fract(phaseA + 0.5);
    float flowLerp = abs(phaseA * 2.0 - 1.0);
    vec4 membraneA = texture(texture0, FlowedUV(baseUV, flowVector, phaseA));
    vec4 membraneB = texture(texture0, FlowedUV(baseUV, flowVector, phaseB));
    vec4 membrane = mix(membraneA, membraneB, flowLerp);
    vec2 screenUV = gl_FragCoord.xy / max(u_resolution, vec2(1.0));
    vec2 refractionUV = clamp(screenUV +
        (flowVector + normal.xy * 0.55) * u_refractionStrength *
        (0.35 + 0.65 * fresnel), vec2(0.001), vec2(0.999));
    vec3 refractedScene = (u_hasScene != 0) ? texture(u_sceneTex, refractionUV).rgb
                                             : u_bodyColor.rgb;

    // The density texture is a moving DETAIL signal, never a cut-out mask:
    // the reference is a continuous liquid volume with internal currents,
    // not a wireframe ball. Its RGB is never emitted, preventing dark source
    // texels from making black seams in the transparent carrier.
    float filamentField = dot(membrane.rgb, vec3(0.2126, 0.7152, 0.0722));
    float fineField = texture(texture0, FlowedUV(baseUV * 2.13, flowVector, phaseB)).r;
    float liquidDensity = clamp(filamentField * 0.74 + fineField * 0.36, 0.0, 1.0);
    float flowDetail = smoothstep(0.11, 0.64, liquidDensity);
    float brightCurrent = smoothstep(0.34, 0.66, liquidDensity);
    // Density deliberately controls both colour and coverage. If it only
    // perturbs alpha, premultiplied blending compresses the variation into one
    // nearly uniform blue. This gives the flowing water dark pockets, medium
    // body, and distinct luminous crests before bloom is involved.
    float liquidCarrier = 0.34 + 1.28 * flowDetail;
    float interfaceWeight = gl_FrontFacing ? 1.0 : 0.62;
    // This is the WIDE edge gradient: it changes the water volume itself well
    // before the thin emissive silhouette takes over.
    float edgeGradient = smoothstep(0.10, 0.82, sqrt(fresnel));
    // Keep the luminous edge close to the depth-contact core's perceived
    // thickness.  The old smoothstep on raw Fresnel only lit the last few
    // pixels of the silhouette, so the white edge looked unrelated to contact.
    float rimCore = smoothstep(0.38, 0.84, sqrt(fresnel));

    // A low-opacity carrier makes the sphere read as water. Fresnel broadens
    // it naturally toward grazing angles; flow only moves internal density.
    float bodyCoverage = u_opacity * interfaceWeight *
                         (0.125 + 0.310 * edgeGradient) * liquidCarrier;
    float rimCoverage = u_opacity * interfaceWeight * smoothstep(0.42, 0.88, fresnel) * 0.22;
    float contact = DepthContact(screenUV);
    float contactCore = smoothstep(0.72, 0.98, contact);
    float coverage = clamp(bodyCoverage + rimCoverage + contact * 0.44, 0.0, 1.0);
    vec3 deepWater = u_bodyColor.rgb * 0.20;
    vec3 midWater = mix(u_bodyColor.rgb * 0.72, u_rimColor.rgb * 0.58, 0.46);
    vec3 crestWater = mix(u_rimColor.rgb, vec3(0.48, 0.82, 1.0), 0.28);
    vec3 liquidTint = mix(deepWater, midWater, smoothstep(0.18, 0.58, flowDetail));
    liquidTint = mix(liquidTint, crestWater, brightCurrent * 0.74);
    liquidTint = mix(liquidTint, crestWater, edgeGradient * 0.22 + contact * 0.36);
    vec3 body = mix(refractedScene, liquidTint,
                    0.24 + 0.60 * flowDetail + 0.14 * edgeGradient + 0.14 * contact);

    // A real bubble shield is not uniformly self-lit.  Energy collects in the
    // lower volume and the contact line, while just a few high-density folds
    // catch light elsewhere. This breaks the artificial evenly-lit ball.
    float lowerVolume = smoothstep(0.54, 0.94, fragTexCoord.y);
    float foldGlint = brightCurrent * (0.22 + 0.22 * lowerVolume);
    float emissionMask = foldGlint +
                         0.52 * rimCore +
                         lowerVolume * (0.06 + 0.16 * flowDetail) +
                         contactCore * (0.90 + 0.35 * brightCurrent);

    if (u_emissionOnly != 0) {
        // This draw is still inside MAGIC's premultiplied scope.  Emission
        // therefore needs the same resolver as the body; ResolveEmission is
        // additive-only and would expose straight-alpha dark RGB at holes.
        float glowCoverage = coverage * emissionMask;
        vec3 contactHot = mix(u_rimColor.rgb, vec3(0.72, 0.92, 1.0), contact * 0.45);
        finalColor = VFX_ResolvePremultiplied(contactHot, u_emissionGain,
                                               glowCoverage, vec3(0.0), 0.0, 0.0);
    }
    else
        finalColor = VFX_ResolvePremultiplied(body, u_bodyOpacity, coverage,
                                               u_rimColor.rgb, 0.0, 0.0);
}
