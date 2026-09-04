#version 330

in vec4 fragColor;
uniform vec3 u_shadowTint;
out vec4 finalColor;

void main()
{
    float strength = fragColor.r;
    vec3 multiplier = mix(vec3(1.0), max(u_shadowTint, vec3(0.16)), strength * 0.58);
    finalColor = vec4(multiplier, 1.0);
}
