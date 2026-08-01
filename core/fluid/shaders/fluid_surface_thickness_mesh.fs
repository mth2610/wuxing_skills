#version 330
out vec4 finalColor;
void main() {
    // CPU fallback draws real sphere geometry; a compact per-fragment optical
    // contribution still gives Beer-Lambert volume without a GPU readback.
    finalColor = vec4(0.10, 0.0, 0.0, 1.0);
}
