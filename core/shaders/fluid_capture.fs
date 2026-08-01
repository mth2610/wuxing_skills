#version 330
out vec4 finalColor;

// R = nearest fluid depth, G = local coverage/thickness proxy.
// Keeping this in a colour target works on Vulkan/MoltenVK where depth textures
// attached to an FBO cannot reliably be sampled in a later pass.
void main() {
    finalColor = vec4(gl_FragCoord.z, 1.0, 0.0, 1.0);
}
