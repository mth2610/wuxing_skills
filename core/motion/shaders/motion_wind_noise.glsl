// Shared production Wind noise; preserves legacy sampling.
vec3 hash3(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx) * 2.0 - 1.0;
}

float noiseScalar(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = dot(hash3(i + vec3(0,0,0)), f - vec3(0,0,0));
    float n100 = dot(hash3(i + vec3(1,0,0)), f - vec3(1,0,0));
    float n010 = dot(hash3(i + vec3(0,1,0)), f - vec3(0,1,0));
    float n110 = dot(hash3(i + vec3(1,1,0)), f - vec3(1,1,0));
    float n001 = dot(hash3(i + vec3(0,0,1)), f - vec3(0,0,1));
    float n101 = dot(hash3(i + vec3(1,0,1)), f - vec3(1,0,1));
    float n011 = dot(hash3(i + vec3(0,1,1)), f - vec3(0,1,1));
    float n111 = dot(hash3(i + vec3(1,1,1)), f - vec3(1,1,1));
    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);
    float nxy0 = mix(nx00, nx10, u.y);
    float nxy1 = mix(nx01, nx11, u.y);
    return mix(nxy0, nxy1, u.z); // ~[-1, 1]
}

// Curl xấp xỉ bằng finite-difference trên noiseScalar
vec3 curlNoise3(vec3 p) {
    const float e = 0.1;
    float dx  = noiseScalar(p + vec3(e,0,0)) - noiseScalar(p - vec3(e,0,0));
    float dy  = noiseScalar(p + vec3(0,e,0)) - noiseScalar(p - vec3(0,e,0));
    float dz  = noiseScalar(p + vec3(0,0,e)) - noiseScalar(p - vec3(0,0,e));
    float dx2 = noiseScalar(p + vec3(e,0,0) + vec3(31.4,0,0)) - noiseScalar(p - vec3(e,0,0) + vec3(31.4,0,0));
    float dy2 = noiseScalar(p + vec3(0,e,0) + vec3(0,47.7,0)) - noiseScalar(p - vec3(0,e,0) + vec3(0,47.7,0));
    float dz2 = noiseScalar(p + vec3(0,0,e) + vec3(0,0,58.9)) - noiseScalar(p - vec3(0,0,e) + vec3(0,0,58.9));
    return vec3(dy2 - dz, dz2 - dx, dx2 - dy) / (2.0 * e);
}

