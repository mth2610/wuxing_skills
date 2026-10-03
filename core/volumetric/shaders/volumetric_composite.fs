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
    vec4 sum = vec4(0.0);
    float total = 0.0;
    // Filter radiance and extinction together. A wider spatial footprint
    // suppresses static integration grain; reject unrelated depth layers.
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 uv = clamp(fragTexCoord + vec2(x, y) * u_lowResTexel * 1.5,
                            u_lowResTexel * 0.5, vec2(1.0) - u_lowResTexel * 0.5);
            float difference = abs(fullDepth - texture(u_lowResDepthTex, uv).r);
            float spatial = (x == 0 ? 1.0 : 0.5) * (y == 0 ? 1.0 : 0.5);
            float range = 1.0 - smoothstep(0.0, max(u_depthThreshold, 0.001), difference);
            float weight = spatial * range;
            sum += texture(texture0, uv) * weight;
            total += weight;
        }
    }
    finalColor = sum / max(total, 0.0001);
}
