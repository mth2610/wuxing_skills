#version 330

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform int u_useTexture;
uniform float u_alphaCutoff;
uniform float u_alphaCoverage;

out vec4 finalColor;

void main()
{
    // [VULKAN / SPIR-V FIX]: Nhánh u_useTexture != 0 là đồng nhất (uniform control flow).
    // Khi u_useTexture == 0 (Hoa & Cỏ 3D Zero-Discard), shader không dùng discard,
    // giữ 100% tốc độ phần cứng Early-Z trong lượt ghi Shadow Map!
    if (u_useTexture != 0) {
        vec2 safeUV = max(fragTexCoord, vec2(0.0));
        float alpha = texture(texture0, safeUV).a;
        float conservativeEdge = fwidth(alpha) * u_alphaCoverage;
        if (fragTexCoord.x >= 0.0 && (alpha + conservativeEdge < u_alphaCutoff))
            discard;
    }
    finalColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}