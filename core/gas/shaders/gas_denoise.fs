#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D u_sceneDepthTex;
uniform vec2 u_texelSize;
uniform int u_hasSceneDepth;
uniform mat4 u_inverseProjection;

out vec4 finalColor;

float Gas_ViewDepth(vec2 uv) {
    float depth = texture(u_sceneDepthTex, uv).r;
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = u_inverseProjection * clip;
    return abs(view.z) / max(abs(view.w), 1e-6);
}

void main() {
    /* Four bilinear half-texel taps are exactly a separable 3x3 tent:
     * (1 2 1 / 2 4 2 / 1 2 1) / 16. Run at raymarch resolution so the
     * one-pixel phase lattice is removed before the 3x/4x display upscale. */
    vec2 uv0 = fragTexCoord + vec2(-0.5, -0.5) * u_texelSize;
    vec2 uv1 = fragTexCoord + vec2( 0.5, -0.5) * u_texelSize;
    vec2 uv2 = fragTexCoord + vec2(-0.5,  0.5) * u_texelSize;
    vec2 uv3 = fragTexCoord + vec2( 0.5,  0.5) * u_texelSize;
    vec4 color0 = texture(texture0, uv0);
    vec4 color1 = texture(texture0, uv1);
    vec4 color2 = texture(texture0, uv2);
    vec4 color3 = texture(texture0, uv3);

    if (u_hasSceneDepth == 0) {
        finalColor = (color0 + color1 + color2 + color3) * 0.25 * fragColor;
        return;
    }

    /* Linear view-space depth keeps the edge threshold stable from near field
     * to the horizon. Half-texel depth taps match the four bilinear gas taps,
     * rejecting samples across geometry while retaining the cheap tent shape
     * on a continuous surface. */
    float centerDepth = Gas_ViewDepth(fragTexCoord);
    float sampleDepth = Gas_ViewDepth(uv0);
    float weight0 = exp2(-abs(sampleDepth - centerDepth) * 1.5);
    sampleDepth = Gas_ViewDepth(uv1);
    float weight1 = exp2(-abs(sampleDepth - centerDepth) * 1.5);
    sampleDepth = Gas_ViewDepth(uv2);
    float weight2 = exp2(-abs(sampleDepth - centerDepth) * 1.5);
    sampleDepth = Gas_ViewDepth(uv3);
    float weight3 = exp2(-abs(sampleDepth - centerDepth) * 1.5);
    float weightSum = max(weight0 + weight1 + weight2 + weight3, 1e-5);
    finalColor = (color0 * weight0 + color1 * weight1 +
                  color2 * weight2 + color3 * weight3) /
                 weightSum * fragColor;
}
