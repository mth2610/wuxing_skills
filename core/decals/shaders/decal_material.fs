#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform int u_emissivePass;
uniform vec4 u_baseTint;
uniform vec4 u_emissiveTint;
uniform float u_emissiveThreshold;
uniform float u_emissiveIntensity;
uniform float u_bodyOpacity;
out vec4 finalColor;

void main()
{
    vec2 q = fragTexCoord - vec2(0.5);
    float radius = length(q) * 2.0;
    float edgeStart = 0.58 - fragColor.r * 0.24;
    float edgeEnd = 1.02 - fragColor.r * 0.45;
    // The authored alpha owns the fractured silhouette. A quantized UV-noise
    // erosion produced visible 96x96 pixel cells while a decal shrank; use
    // derivative smoothing so this lifetime fade stays resolution-independent.
    float edgeAA = max(fwidth(radius), 0.001);
    float erosion = 1.0 - smoothstep(edgeStart - edgeAA, edgeEnd + edgeAA, radius);
    vec4 body = texture(texture0, fragTexCoord);
    float alpha = body.a * erosion;
    float brightSignal = max(body.r, max(body.g, body.b));
    float emissiveMask = smoothstep(u_emissiveThreshold, 1.0, brightSignal) * body.a * erosion;
    if (u_emissivePass != 0)
    {
        finalColor = vec4(u_emissiveTint.rgb * u_emissiveIntensity, emissiveMask);
    }
    else
    {
        float receiverLight = mix(0.64, 1.0, clamp(fragNormal.y, 0.0, 1.0));
        finalColor = vec4(body.rgb * u_baseTint.rgb * receiverLight,
                          alpha * fragColor.a * u_bodyOpacity);
    }
}
