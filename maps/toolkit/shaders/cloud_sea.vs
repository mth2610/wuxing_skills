#version 330

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;

uniform mat4 mvp;
uniform vec3 u_cloudOffset;
uniform vec3 u_viewPos;
uniform vec2 tiling;
uniform float u_time;

// Đóng gói 2 bộ UV vào 1 biến vec4 duy nhất để tiết kiệm thanh ghi nội suy (varying)
out vec4 v_flowUVs; 
out vec3 v_viewVec;
out vec3 v_normal;

void main()
{
    // Tính toán tọa độ UV và dùng fract() cho độ lệch thời gian để tránh mất độ chính xác
    // dấu phẩy động (float32 precision drift) khi game chạy liên tục nhiều giờ
    vec2 uv = vertexTexCoord * tiling;
    v_flowUVs.xy = uv + fract(vec2(0.02, 0.008) * u_time);
    v_flowUVs.zw = uv * 1.7 + fract(vec2(-0.015, 0.01) * u_time);

    // Tính trước vector từ camera đến điểm mây
    vec3 worldPos = vertexPosition + u_cloudOffset;
    v_viewVec = worldPos - u_viewPos;

    // Chuẩn hóa an toàn tại đỉnh để giảm tải cho Fragment Shader
    float nLenSq = dot(vertexNormal, vertexNormal);
    v_normal = (nLenSq > 1e-8) ? (vertexNormal * inversesqrt(nLenSq)) : vec3(0.0, 1.0, 0.0);

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
