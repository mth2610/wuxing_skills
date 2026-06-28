#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;
in vec3 fragNormal;

uniform float u_time;
uniform vec3 u_viewPos;
uniform vec2 u_resolution;

out vec4 finalColor;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(u_viewPos - fragPosition);
    float ndotv = clamp(dot(normal, viewDir), 0.0, 1.0);
    float pulse = sin(u_time * 3.0) * 0.5 + 0.5;
    
    vec3 baseColor = fragColor.rgb * (ndotv * 0.5 + 0.5);
    baseColor.r += pulse * 0.2;
    baseColor.g += (1.0 - pulse) * 0.1;
    
    float resFactor = u_resolution.x > 0.0 ? (u_resolution.y / u_resolution.x) * 0.1 : 0.0;
    baseColor += vec3(resFactor);

    finalColor = vec4(baseColor, fragColor.a);
}
