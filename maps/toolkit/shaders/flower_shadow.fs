#version 330

in vec4 fragColor;
in vec2 fragShadowUV;
uniform vec3 u_shadowTint;
uniform float u_shadowStrength;
out vec4 finalColor;

void main()
{
    // Analytic capsule-like edge on the six-vertex contact quad. This avoids
    // both a hard fake wedge and extra cap geometry for every plant.
    float capX = max((fragShadowUV.x - 0.78) / 0.22, 0.0);
    float edgeDistance = length(vec2(capX, fragShadowUV.y));
    float coverage = 1.0 - smoothstep(0.78, 1.0, edgeDistance);
    float strength = fragColor.r * coverage;
    vec3 multiplier = mix(vec3(1.0), max(u_shadowTint, vec3(0.16)),
                          strength * 0.58 * u_shadowStrength);
    finalColor = vec4(multiplier, 1.0);
}
