#version 330

/* Varyings */
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragPosition;

/* Uniforms */
uniform sampler2D texture0;
uniform float     u_dissolve;
uniform float     u_time;
uniform vec3      u_camPos;

/* Output */
out vec4 finalColor;

/* Value noise */
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float v = 0.0;
    v += 0.60 * noise(p);
    v += 0.40 * noise(p * 2.5 + vec2(u_time * 0.5, 0.0));
    return v;
}

void main()
{
    // Sample the rock noise texture (tiled vertically for rock strata look)
    vec4 texColor = texture(texture0, fragTexCoord * vec2(2.0, 6.0));

    vec3 bottomRock = vec3(0.08, 0.05, 0.03); // Darker base
    vec3 topRock = vec3(0.38, 0.28, 0.20); // Lighter top
    vec3 creviceColor = vec3(0.04, 0.02, 0.01);
    vec3 magmaColor = vec3(1.0, 0.4, 0.02);

    vec3 N = normalize(fragNormal);
    float upFactor = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 rockBase = mix(bottomRock, topRock, pow(upFactor, 0.8));

    // Fractal noise crevices, combined with the noise texture for rich details
    float crackPattern = fbm(fragTexCoord * 6.0) * (0.5 + 0.5 * texColor.r);
    vec3 baseCol = mix(rockBase, creviceColor, smoothstep(0.4, 0.6, crackPattern));

    // --- 3. Magma Emissive (Sharp, sparse veins) ---
    // Lower frequency to avoid static noise. Stretch on Y to make veins look like vertical fissures.
    vec2 worldUV = vec2(fragPosition.x * 0.08, fragPosition.y * 0.02) + vec2(fragPosition.z * 0.08, 0.0);
    float veinPattern = fbm(worldUV * 1.5);
    
    // Higher threshold means veins are sparser, restoring the 3D volume of the rock.
    // Use smoothstep instead of step for a slightly softer, glowing edge.
    float veinMask = smoothstep(0.60, 0.75, veinPattern);
    
    // Animate glowing pulse
    float pulse = 0.8 + 0.3 * sin(u_time * 5.0 - fragPosition.y * 0.5);
    
    // Super bright glowing magma!
    vec3 emissive = magmaColor * (3.5 + pulse * 1.5) * veinMask * 0.95;

    // Dynamic 3D lighting calculation for the non-emissive rock surface
    vec3 V = normalize(u_camPos - fragPosition);
    vec3 L = normalize(vec3(0.5, 0.8, 0.5)); // Directional light
    
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.0);
    
    float diff = NdotL * 0.7 + 0.3; // Softer wrap lighting
    
    // Specular highlight to simulate crystalline facets in the rock
    vec3 R = reflect(-L, N);
    float spec = pow(clamp(dot(V, R), 0.0, 1.0), 16.0) * 0.3;

    // Combine shading, applying fresnel to rim light the edges of the rock
    vec3 litColor = (baseCol * diff) * texColor.rgb + vec3(0.2, 0.15, 0.1) * fresnel * (1.0 - veinMask) + emissive + vec3(spec);
    vec4 diffuse = vec4(litColor, 1.0) * fragColor;

    // Dissolve sweep
    if (u_dissolve > 0.001) {
        float dNoise = fbm(fragTexCoord * 14.0);
        float threshold = dNoise - u_dissolve;
        if (threshold < 0.0) {
            discard;
        }
        
        // Burning edges
        float edge = exp(-threshold * 12.0);
        diffuse.rgb += vec3(1.0, 0.4, 0.0) * edge * 4.0;
    }

    finalColor = diffuse;
}
