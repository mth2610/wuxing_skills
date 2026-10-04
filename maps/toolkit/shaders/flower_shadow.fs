#version 330

in vec4 fragColor;
in vec2 fragShadowUV;
uniform vec3 u_shadowTint;
uniform float u_shadowStrength;
out vec4 finalColor;

void main()
{
    // Feather both sides and round off the projected tip. The six-vertex mesh
    // stays cheap, while the shadow no longer reads as a solid black triangle.
    float sideCoverage = 1.0 - smoothstep(0.72, 1.0, abs(fragShadowUV.y));
    float tipCoverage = 1.0 - smoothstep(0.82, 1.0, fragShadowUV.x);
    float strength = fragColor.r * sideCoverage * tipCoverage;
    vec3 multiplier = mix(vec3(1.0), max(u_shadowTint, vec3(0.16)),
                          strength * 0.58 * u_shadowStrength);
    finalColor = vec4(multiplier, 1.0);
}
