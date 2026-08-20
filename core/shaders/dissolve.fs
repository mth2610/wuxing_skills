#version 330 core
#include "core/shaders/common/vfx_composite.glsl"

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D noiseTex; // Bản đồ nhiễu phân rã ranh giới

// raylib's default vertex shader pairs with this one, and the default pipeline
// multiplies by colDiffuse — it MUST be declared here. Its absence made this
// shader fail to compile ("'colDiffuse' : undeclared identifier"), and a failed
// compile silently falls back to the default shader, so the effect drew as an
// ordinary quad with no erosion at all. Found when E5.4 became its first
// consumer: the file had existed unused, and unused meant never compiled.
uniform vec4 colDiffuse;

uniform float dissolveAmount; // Tiến trình phân rã [0.0 .. 1.0]
// No initialisers on uniforms: the backend warns and ignores them, so a caller
// that forgets to set one gets 0, not the value written here.
uniform float edgeWidth;
uniform vec4 edgeColor;       // Màu rực quanh lát cắt rã
// Grain control. Sampling the noise at the quad's raw UV means one full noise
// image per sprite, i.e. the finest grain the texture can give — which reads as
// static rather than as matter breaking into pieces. Scale below 1 magnifies the
// noise into larger clumps; the offset gives each sprite in a cluster its own
// pattern so they do not all erode identically.
uniform float noiseScale;
uniform vec2  noiseOffset;

out vec4 finalColor;

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);
    float ns = (noiseScale > 0.0) ? noiseScale : 1.0;
    float noise = texture(noiseTex, fragTexCoord * ns + noiseOffset).r;
    
    // Loại bỏ điểm pixel hoàn toàn nếu giá trị noise nhỏ hơn ngưỡng hiện tại
    if (noise < dissolveAmount) {
        discard;
    }
    
    vec4 finalTex = texColor * colDiffuse * fragColor;
    
    // Xử lý tạo dải biên rực sáng bao quanh lát cắt tan biến (Edge Glow)
    if (noise < dissolveAmount + edgeWidth) {
        float edgeLerp = (dissolveAmount + edgeWidth - noise) / edgeWidth;
        finalTex.rgb = mix(finalTex.rgb, edgeColor.rgb * 3.0, edgeLerp);
    }
    
    // BODY / ALPHA scope (vc_dissolve_exit.inl:137).
    finalColor = VFX_ResolveBody(finalTex.rgb, 1.0, finalTex.a);
}