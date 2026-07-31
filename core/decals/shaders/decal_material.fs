#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float u_erosion;
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
    float erosion = smoothstep(1.02 - u_erosion * 0.45, 0.58 - u_erosion * 0.24,
                               radius + (grain - 0.5) * 0.13);
    vec4 body = texture(texture0, fragTexCoord);
    float charMask = smoothstep(0.08, 0.85, 1.0 - dot(body.rgb, vec3(0.299, 0.587, 0.114)));
    float alpha = body.a * erosion * mix(0.72, 1.0, charMask);
    finalColor = vec4(body.rgb * fragColor.rgb, alpha * fragColor.a);
}
