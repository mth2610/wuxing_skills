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
// SkillManager_BeginShader() — VÀ CHỈ BỞI NÓ.
//
// Ai dùng raw BeginShaderMode() phải TỰ SET, và cái hay bị quên nhất là
// `viewPos`, vì nó không hỏng theo kiểu dễ thấy: uniform chưa set đọc ra
// (0,0,0), nên `normalize(viewPos - fragPosition)` trở thành hướng tới GỐC THẾ
// GIỚI. Kết quả vẫn là một vector đơn vị hợp lệ, vẫn cho ra một dải fresnel
// trông hợp lý — chỉ là nó nằm sai chỗ và ĐỨNG YÊN khi camera xoay. Không có
// cảnh báo nào cả.
//
// Đã mất một buổi vào đúng cái này. VÀ CÓ MỘT CÁI BẪY THỨ HAI Ở NGAY SAU:
// `fragPosition` mà VS_FinalOutput dựng ra KHÔNG phải world space. MyBeginMode3D
// nhân matView vào transform của rlgl, nên matModel = model × view cho MỌI draw
// trong pass 3D — kể cả immediate mode (ENGINE_LANDMINES §9). Set đúng viewPos
// rồi vẫn sai, vì lúc đó là world trừ view.
//
// Trong view space camera Ở GỐC, nên vector nhìn là normalize(-fragPosition) và
// không cần uniform nào. Cách kiểm dứt điểm: vẽ length(fragPosition) — nó phải
// bằng khoảng cách từ vật tới GỐC nếu là world, và tới CAMERA nếu là view. Hai
// giá trị chênh nhau hàng chục mét nên không thể nhầm; fract(fragPosition) thì
// vẽ ra lưới đẹp trong cả hai trường hợp và không phân biệt được gì.
//
// u_lightDir cũng vậy: set = normalize hướng đối của
// Environment_GetSunDirection(), xem CORE_ISSUES.md Item 10 và ví dụ trong
// tube_skill.c/stone_prison_skill.c.
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
