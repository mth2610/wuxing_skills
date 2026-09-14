// ============================================================
// WUXING — Common Signed Distance Field (SDF) GLSL Library
//
// Analytical SDF primitives for body-hugging auras (chân khí hộ thể)
// and field-based particle attractors / collision.
// ============================================================

#ifndef SDF_COMMON_GLSL
#define SDF_COMMON_GLSL

// Polynomial smooth minimum (Inigo Quilez)
// Seamlessly merges overlapping capsules into a continuous organic volume
float sdfSmin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / max(k, 1e-4), 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// Signed Distance to a 3D Sphere at origin c with radius r
float sdfSphere(vec3 p, vec3 c, float r) {
    return length(p - c) - r;
}

// Signed Distance to a 3D Capsule between segment a and b with radius r
float sdfCapsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

// Signed Distance to a 3D Box centered at origin c with half-size b
float sdfBox(vec3 p, vec3 c, vec3 b) {
    vec3 q = abs(p - c) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

#endif // SDF_COMMON_GLSL
