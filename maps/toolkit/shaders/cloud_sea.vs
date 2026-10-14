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
    // Tính toán tọa độ UV và hiệu ứng trôi ngay tại đỉnh (Vertex)
    vec2 uv = vertexTexCoord * tiling;
    v_flowUVs.xy = uv + vec2(0.02, 0.008) * u_time;
    v_flowUVs.zw = uv * 1.7 + vec2(-0.015, 0.01) * u_time;

    // Tính trước vector từ camera đến điểm mây
    vec3 worldPos = vertexPosition + u_cloudOffset;
    v_viewVec = worldPos - u_viewPos;

    v_normal = vertexNormal;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}