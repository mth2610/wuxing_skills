#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float u_erosion;
uniform int u_emissivePass;
out vec4 finalColor;

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main()
{
    vec2 q = fragTexCoord - vec2(0.5);
    float radius = length(q) * 2.0;
    float grain = hash21(floor(fragTexCoord * 96.0));
    float noisyRadius = radius + (grain - 0.5) * 0.13;
    float edgeStart = 0.58 - u_erosion * 0.24;
    float edgeEnd = 1.02 - u_erosion * 0.45;
    float erosion = 1.0 - smoothstep(edgeStart, edgeEnd, noisyRadius);
    vec4 body = texture(texture0, fragTexCoord);
    float charMask = smoothstep(0.08, 0.85, 1.0 - dot(body.rgb, vec3(0.299, 0.587, 0.114)));
    float alpha = body.a * erosion * mix(0.72, 1.0, charMask);
    // The authored red-dominant veins are a separate material signal, not a
    // second texture guessed from a filename. Keep them absent from the char
    // pass, then draw HDR ember only in the additive pass.
    float emberSignal = max(body.r - max(body.g, body.b), 0.0);
    float emberMask = smoothstep(0.025, 0.115, emberSignal) * body.a * erosion;
    if (u_emissivePass != 0)
    {
        // BLEND_ADDITIVE applies source alpha. Keep the HDR RGB unmasked here
        // so a thin vein attenuates ONCE, rather than emberMask squared.
        vec3 ember = vec3(3.15, 0.24, 0.03);
        finalColor = vec4(ember, emberMask * 0.92);
    }
    else
    {
        // Do not draw semi-transparent grey source fringe: it is the layer
        // that reads as white fog over dark terrain. Only dense charcoal body
        // survives; the thin structure belongs to the separate ember pass.
        float denseChar = smoothstep(0.58, 0.84, charMask);
        float charOpacity = alpha * denseChar * 0.58;
        // Alpha occlusion is invariant across receiver brightness. Multiply
        // would turn source grey into a visible veil on sunlit terrain.
        vec3 charcoal = vec3(0.028, 0.016, 0.010);
        finalColor = vec4(charcoal * fragColor.rgb, charOpacity * fragColor.a);
    }
}
