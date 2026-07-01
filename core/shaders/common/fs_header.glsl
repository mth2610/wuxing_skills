// ============================================================
// WUXING — Common Fragment Shader Header
// Include dòng đầu tiên trong mỗi .fs của skill (sau #version).
//
// Cung cấp:
//   - Khai báo varyings nhận từ VS
//   - Uniforms môi trường chuẩn (auto-bound)
//   - Output finalColor
//
// Uniforms u_time / viewPos / u_resolution / u_lightDir được auto-bind bởi
// SkillManager_BeginShader() — không cần set thủ công. (Skill dùng raw
// BeginShaderMode() thay vì SkillManager_BeginShader() phải tự set
// u_lightDir = normalize hướng đối của Environment_GetSunDirection(), xem
// CORE_ISSUES.md Item 10 và ví dụ trong tube_skill.c/stone_prison_skill.c.)
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_time;        // auto-bound bởi SkillManager_BeginShader
uniform vec3  viewPos;       // auto-bound bởi SkillManager_BeginShader
uniform vec2  u_resolution;  // auto-bound bởi SkillManager_BeginShader
uniform vec3  u_lightDir;    // auto-bound bởi SkillManager_BeginShader (real environment sun direction)

out vec4 finalColor;
