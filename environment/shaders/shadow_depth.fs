#version 330

// Real Shading P6 — depth-only fragment shader. The framebuffer this draws
// into has no color attachment (rlActiveDrawBuffers(0) in EnvShadow_Init),
// only the depth texture we sample later; this output is never read, but a
// bound program still needs an out variable on some drivers.
out vec4 finalColor;

void main() {
    finalColor = vec4(1.0);
}
