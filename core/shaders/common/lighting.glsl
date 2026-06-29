// ============================================================
// WUXING — Lighting Utilities
// Include sau fs_header.glsl trong .fs của skill.
//
// Cung cấp 3 hàm dùng chung:
//   perturbNormal()  — Normal perturbation qua gradient height field
//   calcFresnel()    — Fresnel rim effect (Schlick)
//   calcSpecular()   — Specular Blinn-Phong
//
// Không phụ thuộc vào bất kỳ uniform skill-specific nào.
// ============================================================

// Tính normal bị nhiễu từ gradient của một height field.
//
//   baseNormal  — normal gốc từ mesh (fragNormal đã normalize)
//   heightDelta — vec2(h(u-eps) - h(u+eps),  h(v-eps) - h(v+eps))
//                 tức gradient theo U và V của hàm height skill tự cung cấp
//   strength    — cường độ biến dạng, thường 0.3 – 0.8
//
// Pattern dùng trong main():
//   const float eps = 0.02;
//   vec2 hDelta = vec2(
//       myHeight(fragTexCoord - vec2(eps, 0.0)) - myHeight(fragTexCoord + vec2(eps, 0.0)),
//       myHeight(fragTexCoord - vec2(0.0, eps)) - myHeight(fragTexCoord + vec2(0.0, eps))
//   );
//   vec3 normal = perturbNormal(fragNormal, hDelta, 0.5);
vec3 perturbNormal(vec3 baseNormal, vec2 heightDelta, float strength) {
    vec3 tangent = cross(vec3(0.0, 1.0, 0.0), baseNormal);
    if (length(tangent) < 0.1) tangent = cross(vec3(1.0, 0.0, 0.0), baseNormal);
    tangent    = normalize(tangent);
    vec3 bitangent = cross(baseNormal, tangent);
    return normalize(baseNormal
                     + (tangent * heightDelta.x + bitangent * heightDelta.y) * strength);
}

// Fresnel rim effect — Schlick approximation.
// Trả về [0..1]: 0 = nhìn thẳng mặt, 1 = nhìn từ cạnh.
//   power — cao hơn → viền mỏng & sắc hơn (thường 2.0 – 5.0)
float calcFresnel(vec3 normal, vec3 viewDir, float power) {
    return pow(1.0 - max(dot(normal, viewDir), 0.0), power);
}

// Specular Blinn-Phong.
// Trả về [0..1] trước khi scale bởi intensity.
//   shininess — cao hơn → điểm sáng nhỏ & tập trung hơn (thường 32 – 512)
float calcSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, float shininess) {
    vec3 halfVec = normalize(lightDir + viewDir);
    return pow(max(dot(normal, halfVec), 0.0), shininess);
}
