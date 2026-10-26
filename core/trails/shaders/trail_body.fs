#version 330
#include "core/shaders/common/vfx_contrast.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 u_contrastParams; // enabled, edgeSharpness, coreSize, coreIntensity

out vec4 finalColor;

void main()
{
    if (fragColor.a <= 0.001) discard;

    vec4 sheet = texture(texture0, fragTexCoord);
    if (sheet.a <= 0.001) discard;

    // Fallback to white if colDiffuse is unbound/zero in custom instanced draws
    vec4 diffuse = (dot(colDiffuse, colDiffuse) > 1e-6) ? colDiffuse : vec4(1.0);

    // Compact accent-bearing sheets before the shared body compositor expands
    // their soft alpha; NONE remains the exact legacy alpha path.
    float bodyMask = VFXContrast_BodyMask(sheet.a, u_contrastParams);
    float alpha = bodyMask * fragColor.a * diffuse.a;

    float alphaFootprint = max(fwidth(alpha), 1e-5);
    alpha *= smoothstep(0.003 - alphaFootprint, 0.003 + alphaFootprint, alpha);
    if (alpha < 1e-4) discard;

    // Preserve a darker support around a compact HDR core. A uniform 1.75x
    // lift raises both together and turns a structured ribbon into one flat
    // strip over bright scenery. NONE returns coreMask=1 and remains the exact
    // legacy full-width 1.75x path.
    float coreMask = VFXContrast_CoreMask(sheet.a, u_contrastParams);
    float coreGain = 1.75 * max(u_contrastParams.w, 1.0);
    vec3 colour = sheet.rgb * fragColor.rgb * diffuse.rgb *
                  mix(1.0, coreGain, coreMask);
    finalColor = VFX_ResolveBody(colour, 1.0, alpha);
}
