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
    /* Wood Base Color (deep jungle forest green & gnarled wood brown) */
    vec3 woodBrown = vec3(0.35, 0.28, 0.18);
    vec3 leafGreen = vec3(0.12, 0.52, 0.16);
    
    // Mix green and brown based on height to make it look like a sprouting plant/vine
    vec3 baseColor = mix(woodBrown, leafGreen, fragTexCoord.y);
    
    // Add vertical wood grain stripes using coordinate noise
    float grain = valueNoise(vec2(fragTexCoord.x * 24.0, fragTexCoord.y * 3.0));
    baseColor = mix(baseColor, baseColor * 0.75, step(0.68, grain));

    vec4 texColor = texture(texture0, fragTexCoord);
    vec4 diffuse = vec4(baseColor, 1.0) * colDiffuse * fragColor * texColor;

    /* Continuous shader rendering when intact */
    if (u_dissolve < 0.001)
    {
        // Add a gentle organic breathing glow to the edges of the green thorn and boost brightness
        float edgeGlow = sin(u_time * 3.5) * 0.1f + 1.35f;
        finalColor = vec4(diffuse.rgb * edgeGlow, diffuse.a);
        return;
    }

    /* Dissolve logic: crumble from top (V = 1) down to base (V = 0) */
    float heightFromTop = 1.0 - fragTexCoord.y;
    float noise = fbm(fragTexCoord * 6.5 + vec2(u_time * 0.1, 0.0));

    // Dissolve front threshold
    float front = u_dissolve - heightFromTop * (1.0 - u_dissolve * 0.35);
    float threshold = noise - front;

    if (threshold < 0.0)
    {
        discard;
    }

    /* Neon Emerald green glow on crumbling boundary */
    float edgeGlow = exp(-threshold * 9.0) * u_dissolve;
    vec3 glowColor = mix(vec3(0.1, 1.0, 0.2), vec3(0.02, 0.5, 0.05), u_dissolve);

    diffuse.rgb += glowColor * edgeGlow * 3.0;

    finalColor = diffuse;
}
