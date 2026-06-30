#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;
uniform float u_time;         // elapsed giây kể từ lúc decal spawn
uniform float u_flowSpeed;    // tốc độ cuộn ra ngoài tâm
uniform float u_flowStrength; // trộn giữa texture gốc và texture đã cuộn [0,1]

// Output fragment color
out vec4 finalColor;

void main()
{
    vec2 centerDist = fragTexCoord - vec2(0.5);
    float dist = length(centerDist);
    vec2 dir = (dist > 0.0001) ? (centerDist / dist) : vec2(0.0);

    // Cuộn toạ độ radial ra ngoài tâm theo thời gian rồi fract về [0,1] ->
    // texture lặp lại tỏa dần ra mép (lava crawl / ripple spreading) thay vì
    // đứng yên như decal tĩnh.
    float scrollDist = fract(dist - u_time * u_flowSpeed);
    vec2 flowUV = vec2(0.5) + dir * scrollDist * 0.5;

    vec4 baseColor = texture(texture0, fragTexCoord);
    vec4 flowColor = texture(texture0, flowUV);
    vec4 texelColor = mix(baseColor, flowColor, clamp(u_flowStrength, 0.0, 1.0));

    // Smooth radial fade-out, cùng công thức với decal.fs để tránh viền vuông.
    float edgeMask = smoothstep(0.5, 0.43, dist);

    vec4 color = texelColor * fragColor;
    color.a *= edgeMask;

    finalColor = color;
}
