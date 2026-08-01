#version 330
in vec2 fragTexCoord;
out vec4 finalColor;

// GPU surface particles are billboards. Produce a filled circular splat here,
// independent of the artist texture and its alpha convention.
void main() {
    vec2 q = fragTexCoord * 2.0 - 1.0;
    float r2 = dot(q, q);
    if (r2 >= 1.0) discard;
    float coverage = smoothstep(1.0, 0.62, r2);
    finalColor = vec4(gl_FragCoord.z, coverage, 0.0, 1.0);
}
