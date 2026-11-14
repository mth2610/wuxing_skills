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
    // 5-tap bilateral cross (center + 4 cardinal taps) — 44% fewer depth/fog fetches
    vec2 off = u_lowResTexel * 1.5;
    vec2 uvMin = u_lowResTexel * 0.5;
    vec2 uvMax = vec2(1.0) - uvMin;
    const vec2 kTaps[5] = vec2[5](
        vec2( 0.0,  0.0),
        vec2(-1.0,  0.0),
        vec2( 1.0,  0.0),
        vec2( 0.0, -1.0),
        vec2( 0.0,  1.0)
    );
    const float kSpatial[5] = float[5](1.0, 0.5, 0.5, 0.5, 0.5);
    float invThresh = 1.0 / max(u_depthThreshold, 0.001);
    for (int i = 0; i < 5; ++i) {
        vec2 uv = clamp(fragTexCoord + kTaps[i] * off, uvMin, uvMax);
        float difference = abs(fullDepth - texture(u_lowResDepthTex, uv).r);
        float range = 1.0 - smoothstep(0.0, 1.0, difference * invThresh);
        float weight = kSpatial[i] * range;
        sum += texture(texture0, uv) * weight;
        total += weight;
    }
    finalColor = sum / max(total, 0.0001);
}
