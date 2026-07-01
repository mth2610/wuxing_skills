#version 330

#ifdef GL_ES
precision highp float;
#endif

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_time;
uniform vec3 viewPos;
uniform vec3 u_lightDir; // auto-bound by SkillManager_BeginShader (real environment sun direction)
uniform float u_dissolve;

out vec4 finalColor;

float getSurfaceRipple(vec2 uv) {
    return sin(uv.y * 15.0 - u_time * 8.0) * cos(uv.x * 15.0 + u_time * 6.0) * 1.5;
}

vec3 perturbNormal(vec3 baseNormal, vec2 heightDelta, float strength) {
    vec3 tangent = cross(vec3(0.0, 1.0, 0.0), baseNormal);
    if (length(tangent) < 0.1) tangent = cross(vec3(1.0, 0.0, 0.0), baseNormal);
    tangent = normalize(tangent);
    vec3 bitangent = cross(baseNormal, tangent);
    return normalize(baseNormal + (tangent * heightDelta.x + bitangent * heightDelta.y) * strength);
}

void main() {
    float eps = 0.02;
    vec2 hDelta = vec2(
        getSurfaceRipple(fragTexCoord - vec2(eps, 0.0)) - getSurfaceRipple(fragTexCoord + vec2(eps, 0.0)),
        getSurfaceRipple(fragTexCoord - vec2(0.0, eps)) - getSurfaceRipple(fragTexCoord + vec2(0.0, eps))
    );
    vec3 normal = perturbNormal(fragNormal, hDelta, 0.4);

    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(u_lightDir);

    float NdotV  = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 2.5);

    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH  = max(dot(normal, halfVec), 0.0);
    float specular = pow(NdotH, 256.0) * 6.0;

    // Real N.L diffuse — previously this material had no lightDir-driven
    // shading at all (only the tiny specular dot above used lightDir); the
    // big glow everyone actually sees was purely fresnel/view-angle-driven
    // and never responded to light direction. Ambient floor 0.2 matches the
    // project's calcDiffuse() convention (lighting.glsl) so the backlit side
    // isn't pure black.
    float diffuse = max(dot(normal, lightDir), 0.2);

    vec3 coreColor = vec3(0.01, 0.20, 0.50);
    vec3 edgeColor = vec3(0.20, 0.65, 1.00);
    vec3 baseColor = mix(coreColor, edgeColor, pow(fresnel, 1.5));
    baseColor += vec3(0.5, 0.85, 1.0) * fresnel * 0.4;
    baseColor *= diffuse;

    float alpha = mix(0.4, 0.95, fresnel) * (1.0 - u_dissolve);

    finalColor = vec4(baseColor + vec3(specular), alpha);
}
