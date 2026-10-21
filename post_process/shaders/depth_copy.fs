#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0; // scene depth texture (renderTex.depth, raw NDC depth)
uniform float u_near;
uniform float u_far;

// Snapshot the scene depth as a LINEARIZED world-space distance, stored in a
// single-channel R32F target. Storing raw NDC depth in 8-bit color crushed
// all far depths (near=0.1, far=15000) to 255, making scene vs particle
// depth indistinguishable. Linearizing + float storage preserves the real
// per-pixel distances the soft-particle fade needs.
void main() {
    float ndcDepth = texture(texture0, fragTexCoord).r;
    float ndc = ndcDepth * 2.0 - 1.0;
    float linear = (2.0 * u_near * u_far) /
                   (u_far + u_near - ndc * (u_far - u_near));
    finalColor = vec4(linear, 0.0, 0.0, 1.0);
}
