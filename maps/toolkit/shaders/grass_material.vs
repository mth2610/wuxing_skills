#version 330

// ============================================================
// WUXING — grass_material Vertex Shader (Optimized)
// Dời toàn bộ phép nhân/chia tọa độ UV và Noise từ Fragment sang Vertex
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;

// Khai báo thêm các uniform tỉ lệ ở Vertex Shader để tính trước UV
// (Raylib / OpenGL tự động chia sẻ chung vùng nhớ uniform trong cùng 1 Shader Program)
uniform float u_baseTileSize;
uniform float u_detailTileSize;
uniform float u_maskNoiseScale;
uniform float u_colorVarScale;

out vec3 fragPosition;
out vec3 fragNormal;
// Đóng gói 4 hệ tọa độ UV vào 2 biến vec4 để tối ưu thanh ghi nội suy
out vec4 v_tileUVs;   // .xy = baseUV,      .zw = detailUV
out vec4 v_noiseUVs;  // .xy = maskNoiseUV, .zw = colorVarUV

void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal   = normalize(mat3(matModel) * vertexNormal);

    // Tính toán sẵn toàn bộ tọa độ lấy mẫu tại đỉnh (chỉ chạy vài lần cho cả mặt đất)
    vec2 worldXZ = vertexTexCoord;
    vec2 invTile = 1.0 / max(vec2(u_baseTileSize, u_detailTileSize), vec2(0.0001));

    v_tileUVs.xy  = worldXZ * invTile.x;          // baseUV
    v_tileUVs.zw  = worldXZ * invTile.y;          // detailUV
    v_noiseUVs.xy = worldXZ * u_maskNoiseScale;   // maskNoiseUV
    v_noiseUVs.zw = worldXZ * u_colorVarScale;    // colorVarUV

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}