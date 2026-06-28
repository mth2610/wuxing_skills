#version 330

/* Varyings */
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragPosition;

/* Uniforms */
uniform sampler2D texture0;
uniform vec4      colDiffuse;
uniform float     u_dissolve;
uniform float     u_time;
uniform vec3      u_camPos;

/* Output */
out vec4 finalColor;

/* Simple value noise */
float hash(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    v += 0.60 * noise(p);
    v += 0.40 * noise(p * 2.5 + vec2(u_time * 0.3, 0.0));
    return v;
}

void main()
{
    // Crystalline/Stony base colors (natural grey-brown rock)
    vec3 baseRock = vec3(0.35, 0.31, 0.27);
    vec3 warmCrevice = vec3(0.18, 0.14, 0.11);
    vec3 seismicGlow = vec3(0.95, 0.58, 0.10); // Warm earth orange-gold

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(u_camPos - fragPosition);
    vec3 L = normalize(vec3(0.5, 0.85, 0.5)); // Main light source

    // 3D Diffuse shading with wrap-lighting to emphasize column volumes
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.0);
    float diff = NdotL * 0.65 + 0.35; // Soft wrap lighting

    // Procedural rock noise detail
    float rNoise = noise(fragTexCoord * 16.0);
    vec3 rockCol = mix(baseRock, warmCrevice, rNoise * 0.45);

    // Mạch địa chấn phát sáng chạy theo đường nứt tự nhiên dọc thân cột (nâng ngưỡng để mạch thật mỏng mảnh)
    float veinPattern = fbm(fragTexCoord * 9.0);
    float veinMask = smoothstep(0.81, 0.83, veinPattern);
    vec3 emissive = seismicGlow * 2.5 * veinMask;
    
    // Combine base lighting with emissive veins
    vec3 finalRGB = rockCol * diff + emissive;

    // Subtle edge rim lighting for 3D depth
    finalRGB += vec3(0.18, 0.12, 0.08) * fresnel * 0.4;

    // Sample optional detail texture
    vec4 texColor = texture(texture0, fragTexCoord);
    vec4 diffuseColor = vec4(finalRGB * texColor.rgb, 1.0) * colDiffuse * fragColor;

    if (u_dissolve < 0.001)
    {
        finalColor = diffuseColor;
        return;
    }

    // Dissolve sweep logic
    float dNoise = fbm(fragTexCoord * 10.0 - vec2(0.0, u_time * 0.4));
    float threshold = dNoise - u_dissolve;
    if (threshold < 0.0)
    {
        discard;
    }

    // Glowing disintegrating edge
    float edgeGlow = exp(-threshold * 10.0) * u_dissolve;
    diffuseColor.rgb += vec3(1.0, 0.55, 0.1) * edgeGlow * 3.5;

    finalColor = diffuseColor;
}
