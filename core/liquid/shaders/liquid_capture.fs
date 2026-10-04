#version 330
out vec4 finalColor;

/* R = nearest fluid depth, G = local coverage/thickness proxy, B = material slot.
 * Keeping this in a colour target works on Vulkan/MoltenVK where depth textures
 * attached to an FBO cannot reliably be sampled in a later pass.
 *
 * The slot rides the SAME fragment that wins the depth test, so the composite
 * gets the material of whichever surface it is actually shading — no second
 * pass, no separate mask, and no way for the two to disagree. */
uniform float u_materialId;

void main() {
    finalColor = vec4(gl_FragCoord.z, 1.0, u_materialId, 1.0);
}
