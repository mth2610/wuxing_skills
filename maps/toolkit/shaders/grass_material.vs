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
    vec4 worldPos4 = vec4(vertexPosition, 1.0);
    fragPosition = vec3(matModel * worldPos4);
    vec3 worldNormal = mat3(matModel) * vertexNormal;
    float nLenSq = dot(worldNormal, worldNormal);
    fragNormal = (nLenSq > 1e-8) ? (worldNormal * inversesqrt(nLenSq)) : vec3(0.0, 1.0, 0.0);

    // Tính toán sẵn toàn bộ tọa độ lấy mẫu tại đỉnh
    vec2 worldXZ = vertexTexCoord;
    vec2 invTile = 1.0 / max(vec2(u_baseTileSize, u_detailTileSize), vec2(0.0001));

    v_tileUVs  = worldXZ.xyxy * vec4(invTile.x, invTile.x, invTile.y, invTile.y);
    v_noiseUVs = worldXZ.xyxy * vec4(u_maskNoiseScale, u_maskNoiseScale, u_colorVarScale, u_colorVarScale);

    gl_Position = mvp * worldPos4;
}