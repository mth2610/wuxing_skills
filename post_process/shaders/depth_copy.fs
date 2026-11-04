#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0; // scene depth texture (renderTex.depth, raw NDC depth)
uniform float u_near;
uniform float u_far;

// Snapshot the scene depth as a LINEARIZED world-space distance, stored in a
// single-channel R32F target.
void main() {
    float ndcDepth = clamp(texture(texture0, fragTexCoord).r, 0.0, 1.0);
    float ndc = ndcDepth * 2.0 - 1.0;
    float denom = max(u_far + u_near - ndc * (u_far - u_near), 1e-5);
    float linear = clamp((2.0 * u_near * u_far) / denom, u_near, u_far);
    finalColor = vec4(linear, 0.0, 0.0, 1.0);
}