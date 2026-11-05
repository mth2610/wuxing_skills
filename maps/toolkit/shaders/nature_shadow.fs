#version 330

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform int u_useTexture;
uniform float u_alphaCutoff;
uniform float u_alphaCoverage;

out vec4 finalColor;

void main()
{
    // [VULKAN / SPIR-V FIX & BANDWIDTH OPTIMIZATION]:
    // - Khi u_useTexture == 0 (Hoa & Cỏ 3D Zero-Discard), không đọc texture và không dùng discard,
    //   giữ 100% tốc độ phần cứng Early-Z trong lượt ghi Shadow Map!
    // - Khi u_useTexture != 0: Tính dFdx/dFdy trong nhánh đồng nhất và chỉ đọc textureGrad cho phần
    //   cánh hoa có texture (fragTexCoord.x >= 0.0), bỏ qua đọc texture trên toàn bộ thân/đài hoa.
    if (u_useTexture != 0) {
        vec2 uvDx = dFdx(fragTexCoord);
        vec2 uvDy = dFdy(fragTexCoord);
        if (fragTexCoord.x >= 0.0) {
            float alpha = textureGrad(texture0, fragTexCoord, uvDx, uvDy).a;
            float uvGrad = dot(abs(uvDx) + abs(uvDy), vec2(0.5));
            float conservativeEdge = clamp(uvGrad * 0.65, 0.0, 0.22) * u_alphaCoverage;
            if (alpha + conservativeEdge < u_alphaCutoff)
                discard;
        }
    }
    finalColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}