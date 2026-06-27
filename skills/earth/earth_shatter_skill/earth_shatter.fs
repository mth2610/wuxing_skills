#version 330

/* Varyings */
in vec2 fragTexCoord;
in vec4 fragColor;

/* Uniforms */
uniform sampler2D texture0;
uniform vec4      colDiffuse;
uniform float     u_dissolve;
uniform float     u_time;

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
    vec3 earthBrown = vec3(0.55, 0.3, 0.1); // bright amber-brown
    vec3 glowAmber = vec3(1.0, 0.6, 0.1);   // intense glowing orange

    // Procedural crystalline facets using high-frequency noise
    float facetNoise = valueNoise(fragTexCoord * 25.0); // higher frequency for more facets
    
    // Add specular-like sparkles based on facets and time
    float sparkle = pow(sin(facetNoise * 10.0 + u_time * 4.0), 8.0) * 0.8;
    
    vec3 baseColor = earthBrown + (facetNoise * 0.25) + vec3(sparkle);

    // Glowing internal energy pulsing along the Y axis
    float pulse = sin(u_time * 3.0 - fragTexCoord.y * 8.0) * 0.5 + 0.5;
    
    // Add glowing edges at the base and top
    float edgeWeight = smoothstep(0.1, 0.0, fragTexCoord.x) + smoothstep(0.9, 1.0, fragTexCoord.x);
    edgeWeight += smoothstep(0.0, 0.2, fragTexCoord.y) + smoothstep(0.8, 1.0, fragTexCoord.y);
    edgeWeight = clamp(edgeWeight, 0.0, 1.0);
    
    // Mix in the bright amber glow
    baseColor = mix(baseColor, glowAmber * (1.5 + pulse), edgeWeight * 0.7);
    
    // Boost overall brightness to prevent it from looking dark
    baseColor *= 1.4;

    vec4 texColor = texture(texture0, fragTexCoord);
    vec4 diffuse = vec4(baseColor, 1.0) * colDiffuse * fragColor * texColor;

    if (u_dissolve < 0.001)
    {
        finalColor = diffuse;
        return;
    }

    /* Dissolve logic: shatter into fragments using noise */
    float noise = fbm(fragTexCoord * 8.0 - vec2(0.0, u_time * 0.5));

    // Dissolve front threshold
    float threshold = noise - u_dissolve;

    if (threshold < 0.0)
    {
        discard;
    }

    /* Bright white-hot gold glow on shattering boundary */
    float edgeGlow = exp(-threshold * 12.0) * u_dissolve;
    vec3 glowColor = mix(vec3(1.0, 0.9, 0.5), vec3(1.0, 0.6, 0.1), u_dissolve);

    diffuse.rgb += glowColor * edgeGlow * 4.0;
    finalColor = diffuse;
}
