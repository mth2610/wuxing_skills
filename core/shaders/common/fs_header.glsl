// ============================================================
// WUXING — Common Fragment Shader Header
// Include dòng đầu tiên trong mỗi .fs của skill (sau #version).
//
// Cung cấp:
//   - Khai báo varyings nhận từ VS
//   - Uniforms môi trường chuẩn (auto-bound)
//   - Output finalColor
//
// Uniforms u_time / viewPos / u_resolution được auto-bind bởi
// SkillManager_BeginShader() — không cần set thủ công.
// ============================================================

#ifdef GL_ES
precision mediump float;
#endif

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_time;        // auto-bound bởi SkillManager_BeginShader
uniform vec3  viewPos;       // auto-bound bởi SkillManager_BeginShader
uniform vec2  u_resolution;  // auto-bound bởi SkillManager_BeginShader

out vec4 finalColor;
