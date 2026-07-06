#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

out vec4 finalColor;

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);
    
    // Boost RGB for HDR bloom glow (emissive boost of 2.2)
    vec3 rgb = texColor.rgb * fragColor.rgb * 2.2;
    float alpha = texColor.a * fragColor.a;
    
    finalColor = vec4(rgb, alpha);
}
