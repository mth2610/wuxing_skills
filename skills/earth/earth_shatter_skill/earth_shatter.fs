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

/* Value noise generator */
float hash21(vec2 p)
{
    p  = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x),
               mix(c, d, u.x),
               u.y);
}

/* 2-Octave Fractal Brownian Motion */
float fbm(vec2 p)
{
    float v = 0.0;
    v += 0.65 * valueNoise(p);
    v += 0.35 * valueNoise(p * 2.3 + vec2(u_time * 0.4, 3.1));
    return v;
}

void main()
{
    /* Crystal Geo Base Color (rich earth amber and glowing magma) */
    vec3 earthBrown = vec3(0.48, 0.28, 0.12); // rich amber-brown
    vec3 glowAmber = vec3(1.0, 0.62, 0.08);   // intense glowing gold-orange

    // Normals and view vectors for 3D depth
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(u_camPos - fragPosition);
    vec3 L = normalize(vec3(0.4, 0.85, 0.4)); // Light source

    // Procedural crystalline facets using high-frequency noise
    float facetNoise = valueNoise(fragTexCoord * 25.0);
    
    // Add specular-like sparkles based on facets and time
    float sparkle = pow(sin(facetNoise * 10.0 + u_time * 4.0), 8.0) * 0.8;
    
    vec3 baseColor = earthBrown + (facetNoise * 0.22) + vec3(sparkle * 0.5);

    // Glowing internal energy pulsing along the Y axis
    float pulse = sin(u_time * 3.5 - fragTexCoord.y * 10.0) * 0.5 + 0.5;
    
    // Add glowing edges at the base and top
    float edgeWeight = smoothstep(0.1, 0.0, fragTexCoord.x) + smoothstep(0.9, 1.0, fragTexCoord.x);
    edgeWeight += smoothstep(0.25, 0.0, fragTexCoord.y) + smoothstep(0.75, 1.0, fragTexCoord.y);
    edgeWeight = clamp(edgeWeight, 0.0, 1.0);
    
    // 3D Diffuse shading (Lambertian + Wrap lighting)
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.0);
    
    float diff = NdotL * 0.65 + 0.35; // wrap lighting for volume
    
    // Mix in the bright amber glow
    baseColor = mix(baseColor * diff, glowAmber * (1.6 + pulse * 1.4), edgeWeight * 0.68);
    
    // Rim lighting (Fresnel)
    baseColor += vec3(0.25, 0.18, 0.08) * fresnel * 0.6;
    
    // Specular crystalline highlight
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 12.0) * 0.25;
    baseColor += vec3(spec);
    
    // Boost overall brightness to prevent it from looking dark
    baseColor *= 1.35;

    vec4 texColor = texture(texture0, fragTexCoord);
    vec4 diffuseColor = vec4(baseColor * texColor.rgb, 1.0) * colDiffuse * fragColor;

    if (u_dissolve < 0.001)
    {
        finalColor = diffuseColor;
        return;
    }

    /* Dissolve logic: shatter into fragments using noise */
    float noiseVal = fbm(fragTexCoord * 8.0 - vec2(0.0, u_time * 0.5));

    // Dissolve front threshold
    float threshold = noiseVal - u_dissolve;

    if (threshold < 0.0)
    {
        discard;
    }

    /* Bright white-hot gold glow on shattering boundary */
    float edgeGlow = exp(-threshold * 12.0) * u_dissolve;
    vec3 glowColor = mix(vec3(1.0, 0.9, 0.5), vec3(1.0, 0.6, 0.1), u_dissolve);

    diffuseColor.rgb += glowColor * edgeGlow * 4.0;
    finalColor = diffuseColor;
}
