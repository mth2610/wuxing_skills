#version 330
// Biến thể GPU-instancing của crystal.vs — dùng bởi VFX_DrawIceCrystalBurst
// (core/composition/vc_water.inl) để vẽ N viên pha lê cùng 1 mesh mẫu bằng
// ĐÚNG 1 draw call (DrawMeshInstanced) thay vì N lệnh DrawMesh riêng. Xem
// CORE_ISSUES.md Item 40.
//
// KHÔNG #include vs_header.glsl: attribute `instanceTransform` (per-instance,
// raylib tự bind khi gọi DrawMeshInstanced) cần nhân vào vị trí/normal khác
// hẳn cách vs_header.glsl dùng `matModel` uniform đơn (chung cho cả draw
// call) — trộn 2 kiểu trong cùng 1 file rủi ro (đọc attribute chưa bind khi
// không instancing là undefined behaviour tuỳ driver), nên tách file riêng.
//
// ĐÁNH ĐỔI: u_growProgress dùng CHUNG cho cả batch (không per-instance như
// crystal.vs bản DrawMesh) — mọi viên trong 1 lần vẽ instanced mọc đồng bộ.
// Vị trí/độ nghiêng/scale từng viên (thứ chính khiến mỗi lần cast trông khác
// nhau) vẫn giữ nguyên nguyên vẹn qua instanceTransform.

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in mat4 instanceTransform; // raylib DrawMeshInstanced tự bind, 1 ma trận/instance

uniform mat4 mvp;      // = matProjection * matView khi instancing (raylib KHÔNG nhân sẵn transform vào đây)
uniform mat4 matModel; // identity mặc định (SkillManager_BeginShader) — nhân thêm instanceTransform bên dưới
uniform float u_growProgress;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main()
{
    vec3 pos = vertexPosition;
    pos.y *= u_growProgress;

    mat4 instanceModel = matModel * instanceTransform;
    fragPosition = vec3(instanceModel * vec4(pos, 1.0));
    fragNormal = normalize(vec3(instanceModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;

    gl_Position = mvp * instanceTransform * vec4(pos, 1.0);
}
