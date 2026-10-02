#ifndef WUXING_ENV_CLOUD_SHADOW_GLSL
#define WUXING_ENV_CLOUD_SHADOW_GLSL

// Identical world-space field for grass, substrate, rocks, and trees.
// The sampler is repeat-wrapped, bilinear, tileable low-frequency noise R.
// Only direct diffuse, specular, and transmission receive this multiplier.
uniform vec4 u_cloudUV;          // scale, driftU, driftV, attenuation
uniform vec4 u_cloudShape;       // coverage, softness, planeHeight, reserved
uniform vec2 u_cloudProjection;  // sun travel xz / max(-sun.y,0.15)

float Environment_CloudVisibility(sampler2D cloudNoise, vec3 worldPosition) {
    if (u_cloudUV.w <= 0.0) return 1.0;
    vec2 planeXZ = worldPosition.xz - max(u_cloudShape.z - worldPosition.y, 0.0) * u_cloudProjection;
    vec2 uv = planeXZ * u_cloudUV.x - u_cloudUV.yz;
    float field = texture(cloudNoise, uv).r;
    float threshold = 1.0 - u_cloudShape.x;
    float shadow = smoothstep(threshold - u_cloudShape.y, threshold + u_cloudShape.y, field);
    return 1.0 - u_cloudUV.w * shadow;
}

#endif
