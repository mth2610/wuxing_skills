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
    vec2 d = u_lowResTexel * 0.5;

    // Unrolled 4-tap bilateral filter with hardware texture cache locality
    vec2 uv0 = fragTexCoord + vec2(-d.x, -d.y);
    vec2 uv1 = fragTexCoord + vec2( d.x, -d.y);
    vec2 uv2 = fragTexCoord + vec2(-d.x,  d.y);
    vec2 uv3 = fragTexCoord + vec2( d.x,  d.y);

    float d0 = abs(fullDepth - texture(u_lowResDepthTex, uv0).r);
    float d1 = abs(fullDepth - texture(u_lowResDepthTex, uv1).r);
    float d2 = abs(fullDepth - texture(u_lowResDepthTex, uv2).r);
    float d3 = abs(fullDepth - texture(u_lowResDepthTex, uv3).r);

    float w0 = 1.0 / (1.0 + d0 * 2.0);
    float w1 = 1.0 / (1.0 + d1 * 2.0);
    float w2 = 1.0 / (1.0 + d2 * 2.0);
    float w3 = 1.0 / (1.0 + d3 * 2.0);

    vec4 s0 = texture(texture0, uv0);
    vec4 s1 = texture(texture0, uv1);
    vec4 s2 = texture(texture0, uv2);
    vec4 s3 = texture(texture0, uv3);

    finalColor = (s0 * w0 + s1 * w1 + s2 * w2 + s3 * w3) / (w0 + w1 + w2 + w3);
}
