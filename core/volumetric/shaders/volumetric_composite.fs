#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;          // Low-res volumetric fog result (RGB=radiance, A=opacity)
uniform sampler2D u_volumetricTex;   // Alias
uniform sampler2D u_fullResDepthTex; // Full-res scene depth (world units)
uniform sampler2D u_lowResDepthTex;  // Low-res scene depth
uniform vec2      u_lowResTexel;     // 1.0 / lowResResolution
uniform float     u_depthThreshold;  // Bilateral depth rejection threshold (~2.0m)

void main() {
    float fullDepth = texture(u_fullResDepthTex, fragTexCoord).r;

    // 2x2 tap cross with depth-aware bilateral weights
    vec2 offsets[4] = vec2[](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2(-0.5,  0.5),
        vec2( 0.5,  0.5)
    );

    vec4 accumVolumetric = vec4(0.0);
    float totalWeight = 0.0001;

    for (int i = 0; i < 4; i++) {
        vec2 uv = fragTexCoord + offsets[i] * u_lowResTexel;
        float lowDepth = texture(u_lowResDepthTex, uv).r;

        // Depth difference penalty (bilateral range weight)
        float depthDiff = abs(fullDepth - lowDepth);
        float depthWeight = 1.0 / (1.0 + depthDiff * 2.0);

        vec4 volSample = texture(texture0, uv);
        accumVolumetric += volSample * depthWeight;
        totalWeight += depthWeight;
    }

    vec4 filtered = accumVolumetric / totalWeight;

    // Output radiance with alpha for additive blending into scene target
    finalColor = filtered;
}
