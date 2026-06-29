// ============================================================
// WUXING — Common Vertex Shader Header
// Include dòng đầu tiên trong mỗi .vs của skill (sau #version).
//
// Cung cấp:
//   - Khai báo attributes, uniforms, varyings chuẩn
//   - VS_FinalOutput(): hàm xuất kết quả cuối sau displacement
//
// Uniforms u_time / viewPos / u_resolution được auto-bind bởi
// SkillManager_BeginShader() — không cần set thủ công.
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4  mvp;
uniform mat4  matModel;
uniform float u_time;        // auto-bound bởi SkillManager_BeginShader
uniform vec3  viewPos;       // auto-bound bởi SkillManager_BeginShader
uniform vec2  u_resolution;  // auto-bound bởi SkillManager_BeginShader

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

// Xuất vertex đã displaced ra pipeline.
// Gọi ở cuối main() của mỗi skill VS thay cho 4 dòng transform lặp đi lặp lại.
void VS_FinalOutput(vec3 displacedPos) {
    fragPosition = vec3(matModel * vec4(displacedPos, 1.0));
    fragNormal   = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    gl_Position  = mvp * vec4(displacedPos, 1.0);
}
