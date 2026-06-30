// ============================================================
// WUXING — GPU Vertex Displacement Utilities (Vertex Shader)
// Include trong .vs của skill SAU vs_header.glsl, khi dùng mesh
// tĩnh bake bởi ProceduralMesh_CreateBaseGrid/CreateBaseCylinder
// (core/procedural_mesh_utils.h) thay vì rebuild CPU mỗi frame.
//
// KHÔNG phụ thuộc noise.glsl — DisplaceVertex_Noise nhận noise
// value từ bên ngoài (skill tự tính bằng fbm2/fbm2N từ noise.glsl
// nếu cần) để tránh double-definition khi include cả hai, cùng
// pattern với fx.glsl's dissolveCalc().
//
// Cung cấp:
//   DisplaceVertex_Noise()       — gợn sóng bề mặt theo normal
//   DisplaceVertex_AlongPath()   — uốn mesh trụ cơ sở dọc Bezier
//   DisplaceVertex_TwistAndTaper() — xoáy + thon dần quanh trục Y local
//   DisplaceVertex_AlongPathNormal() / DisplaceVertex_TwistAndTaperNormal()
//     — normal tương ứng cho 2 hàm trên (BẮT BUỘC dùng kèm, xem ghi chú).
//
// QUAN TRỌNG — normal: AlongPath/TwistAndTaper XOAY vị trí đỉnh sang 1 frame
// mới (right/up/tangent hoặc xoay quanh Y theo twist), nhưng VS_FinalOutput()
// trong vs_header.glsl chỉ transform vertexNormal GỐC qua matModel — không
// biết về phép xoay này. Nếu không tự ghi đè fragNormal bằng
// DisplaceVertex_*Normal() tương ứng SAU KHI gọi VS_FinalOutput(), ánh sáng
// sẽ dùng normal sai → hiện vân sọc xoắn ốc rất rõ trên twist (twist xoay
// nhanh theo t) và lệch sáng nhẹ trên path-bend (path không thẳng theo +Y).
// DisplaceVertex_Noise không cần hàm normal riêng — biên độ nhỏ nên xài
// chung normal gốc là đủ, nếu cần chính xác hơn thì perturb ở fragment
// shader theo kiểu lighting.glsl's perturbNormal().
//
// Set 4 uniform u_disp* qua ProceduralMesh_SetDisplacementUniforms()
// mỗi frame trước khi Draw. Xem CORE_API.md — Procedural Mesh,
// mục "GPU Vertex Displacement" để biết cách dùng đầy đủ.
// ============================================================

uniform float u_dispAmplitude;
uniform float u_dispFrequency;
uniform float u_dispSpeed;
uniform float u_dispTwist;
uniform float u_dispTaperStart;
uniform float u_dispTaperEnd;
uniform vec3  u_dispPathP0;
uniform vec3  u_dispPathP1;
uniform vec3  u_dispPathP2;
uniform vec3  u_dispPathP3;

// ------------------------------------------------------------------
// NOISE DISPLACEMENT — đẩy đỉnh theo normal bằng noise value có sẵn.
//
// localPos/localNormal : vị trí/normal đỉnh trong local space (trước
//                         transform), tức vertexPosition/vertexNormal.
// noiseVal              : [0..1], skill tự tính trước khi gọi, ví dụ
//                          fbm2(localPos.xz * u_dispFrequency + u_time * u_dispSpeed)
//                          từ noise.glsl (include noise.glsl TRƯỚC file này
//                          nếu dùng fbm2/vnoise).
// ------------------------------------------------------------------
vec3 DisplaceVertex_Noise(vec3 localPos, vec3 localNormal, float noiseVal) {
    return localPos + localNormal * (noiseVal - 0.5) * 2.0 * u_dispAmplitude;
}

vec3 DisplaceVertex_BezierPoint(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float u = 1.0 - t;
    return u*u*u*p0 + 3.0*u*u*t*p1 + 3.0*u*t*t*p2 + t*t*t*p3;
}

vec3 DisplaceVertex_BezierTangent(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    return normalize(3.0*(1.0-t)*(1.0-t)*(p1-p0) + 6.0*(1.0-t)*t*(p2-p1) + 3.0*t*t*(p3-p2));
}

// Xoay (x,z) quanh gốc theo u_dispTwist*t — dùng chung bởi vị trí lẫn normal
// để 2 bên luôn đồng bộ góc xoay.
vec2 DisplaceVertex_TwistRotate(vec2 xz, float t) {
    float twist = u_dispTwist * t;
    float c = cos(twist), s = sin(twist);
    return vec2(xz.x * c - xz.y * s, xz.x * s + xz.y * c);
}

// ------------------------------------------------------------------
// ALONG-PATH DISPLACEMENT — uốn 1 mesh trụ cơ sở (từ
// ProceduralMesh_CreateBaseCylinder: trục local +Y trong [0,1], tiết
// diện tròn bán kính local 1) dọc theo đường Bezier u_dispPathP0..P3
// (world space). vertexTexCoord.y đóng vai trò t (0 = gốc, 1 = ngọn).
// Áp dụng taper (u_dispTaperStart/End) + twist (u_dispTwist) quanh
// trục path theo t.
// ------------------------------------------------------------------
vec3 DisplaceVertex_AlongPath(vec3 localPos, vec2 texCoord) {
    float t = clamp(texCoord.y, 0.0, 1.0);
    vec3 center  = DisplaceVertex_BezierPoint(u_dispPathP0, u_dispPathP1, u_dispPathP2, u_dispPathP3, t);
    vec3 tangent = DisplaceVertex_BezierTangent(u_dispPathP0, u_dispPathP1, u_dispPathP2, u_dispPathP3, t);

    vec3 upRef = abs(tangent.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(tangent, upRef));
    vec3 up    = normalize(cross(right, tangent));

    float taper = mix(u_dispTaperStart, u_dispTaperEnd, t);
    vec2 xz = DisplaceVertex_TwistRotate(localPos.xz, t) * taper;

    return center + right * xz.x + up * xz.y;
}

// Normal tương ứng cho DisplaceVertex_AlongPath — gọi SAU VS_FinalOutput()
// rồi gán đè fragNormal (xem ghi chú đầu file). taper là scale ĐỀU trên cả
// X/Z (bán kính) nên không cần inverse-transpose, chỉ cần xoay giống vị trí.
vec3 DisplaceVertex_AlongPathNormal(vec3 localNormal, vec2 texCoord) {
    float t = clamp(texCoord.y, 0.0, 1.0);
    vec3 tangent = DisplaceVertex_BezierTangent(u_dispPathP0, u_dispPathP1, u_dispPathP2, u_dispPathP3, t);

    vec3 upRef = abs(tangent.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(tangent, upRef));
    vec3 up    = normalize(cross(right, tangent));

    vec2 nxz = DisplaceVertex_TwistRotate(localNormal.xz, t);
    return normalize(right * nxz.x + up * nxz.y + tangent * localNormal.y);
}

// ------------------------------------------------------------------
// TWIST + TAPER — xoáy quanh trục Y local + thon dần bán kính, không
// bám theo path (dùng cho phễu xoáy / cột cuồng phong đứng yên tại
// chỗ). localPos.y trong [0,1] dọc trục.
// ------------------------------------------------------------------
vec3 DisplaceVertex_TwistAndTaper(vec3 localPos) {
    float t = clamp(localPos.y, 0.0, 1.0);
    float taper = mix(u_dispTaperStart, u_dispTaperEnd, t);
    vec2 xz = DisplaceVertex_TwistRotate(localPos.xz, t) * taper;
    return vec3(xz.x, localPos.y, xz.y);
}

// Normal tương ứng cho DisplaceVertex_TwistAndTaper — gọi SAU VS_FinalOutput()
// rồi gán đè fragNormal (xem ghi chú đầu file).
vec3 DisplaceVertex_TwistAndTaperNormal(vec3 localPos, vec3 localNormal) {
    float t = clamp(localPos.y, 0.0, 1.0);
    vec2 nxz = DisplaceVertex_TwistRotate(localNormal.xz, t);
    return normalize(vec3(nxz.x, localNormal.y, nxz.y));
}
