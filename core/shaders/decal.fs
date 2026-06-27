#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;

// Output fragment color
out vec4 finalColor;

void main()
{
    // Sample texture color
    vec4 texelColor = texture(texture0, fragTexCoord);
    
    // Calculate radial distance from texture center (0.5, 0.5)
    vec2 centerDist = fragTexCoord - vec2(0.5);
    float dist = length(centerDist);
    
    // Smooth radial fade-out to mathematically eliminate square/blocky boundaries
    // Fully faded out at radius 0.5 (corners), fully solid inside 0.43
    float edgeMask = smoothstep(0.5, 0.43, dist);
    
    // Combine texture color, vertex tint color, and the radial mask
    vec4 color = texelColor * fragColor;
    color.a *= edgeMask;
    
    finalColor = color;
}
