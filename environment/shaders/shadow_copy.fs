#version 330

// Real Shading P6 — copies the shadow depth attachment into a plain R32F
// color texture, same reason as core/shaders/depth_copy.fs: this project's
// rlvk (Vulkan/MoltenVK) backend has a device quirk ("noSampledDepth") where
// FBO depth-attachment textures aren't reliably sampleable from an arbitrary
// 3D-scene shader — only the fullscreen-quad copy-to-color-texture pattern
// is proven to work (see core/screen_distort.c's ScreenDistort_SnapshotDepth).
// Unlike depth_copy.fs, this does NOT linearize — the light's projection is
// orthographic, so NDC depth is already linear in light-space distance, and
// ShadowFactor() in surface_lit.fs/ground_shadow.fs compares this raw
// [0,1]-mapped value directly (no near/far un-projection needed).

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0; // shadow depth attachment (raw NDC-mapped depth)

void main() {
    float d = texture(texture0, fragTexCoord).r;
    finalColor = vec4(d, 0.0, 0.0, 1.0);
}
