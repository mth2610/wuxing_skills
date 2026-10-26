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
    // Đưa phép lấy mẫu texture và fwidth(alpha) ra trước điều kiện biến thiên fragTexCoord.x >= 0.0
    // để đảm bảo đạo hàm quad 2x2 luôn hợp lệ 100% trên mọi GPU Vulkan.
    if (u_useTexture != 0) {
        vec2 safeUV = max(fragTexCoord, vec2(0.0));
        float alpha = texture(texture0, safeUV).a;
        // Derivative-based conservative coverage is the shader equivalent of
        // a restrained alpha-to-coverage footprint for this depth-as-color
        // target. It expands only the sampled atlas edge at minification.
        float conservativeEdge = fwidth(alpha) * u_alphaCoverage;
        if (fragTexCoord.x >= 0.0 && (alpha + conservativeEdge < u_alphaCutoff))
            discard;
    }
    finalColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
