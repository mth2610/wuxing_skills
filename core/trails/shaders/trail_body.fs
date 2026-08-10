#version 330
#include "core/shaders/common/vfx_contrast.glsl"

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 u_contrastParams; // enabled, edgeSharpness, coreSize, coreIntensity

out vec4 finalColor;

void main()
{
    vec4 sheet = texture(texture0, fragTexCoord);
    // Compact accent-bearing sheets before the shared body compositor expands
    // their soft alpha; NONE remains the exact legacy alpha path.
    float bodyMask = VFXContrast_BodyMask(sheet.a, u_contrastParams);
    float alpha = bodyMask * fragColor.a * colDiffuse.a;
    if (alpha < 0.003) discard;
    // Preserve a darker support around a compact HDR core. A uniform 1.75x
    // lift raises both together and turns a structured ribbon into one flat
    // strip over bright scenery. NONE returns coreMask=1 and remains the exact
    // legacy full-width 1.75x path.
    float coreMask = VFXContrast_CoreMask(sheet.a, u_contrastParams);
    float coreGain = 1.75 * max(u_contrastParams.w, 1.0);
    vec3 colour = sheet.rgb * fragColor.rgb * colDiffuse.rgb *
                  mix(1.0, coreGain, coreMask);
    finalColor = vec4(colour, alpha);
}
