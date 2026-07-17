#version 430 core
// Vertex shader — COMPUTE PATH
// Đọc thẳng SSBO particle theo gl_InstanceID (mỗi instance = 1 hạt, quad 6 verts).
// Template quad nằm ở attribute 0 (vertexPosition, góc ±1).
//
// KHÔNG dùng per-instance attribute + divisor ("VBO Instancing Bypass" cũ) nữa:
// rlvk (Vulkan backend) không hỗ trợ divisor/attribute tùy biến ngoài bộ canonical —
// attribute đọc lệch stride ra zeros → life_data.w < 0.5 → mọi hạt bị bóp tàng hình.
// Vulkan và GL 4.3 đều BẢO ĐẢM vertex-stage SSBO read (chính là lý do chọn Vulkan
// cho path này); Android GLES không chạy compute path nên không bị ảnh hưởng.
// `readonly` bắt buộc: rlvk cần NonWritable khi thiếu vertexPipelineStoresAndAtomics.

struct GpuParticleData {
    vec4 pos_radius;
    vec4 vel_drag;
    vec4 color_start;
    vec4 color_end;
    vec4 life_data;
    vec4 ff_data; // không dùng ở VS, giữ để khớp stride với compute/CPU struct
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    GpuParticleData particles[];
};

in vec3 vertexPosition;   // template quad corner, xy trong [-1, 1]

uniform mat4 mvp;
uniform vec3 u_right;   // camera right vector
uniform vec3 u_up;      // camera up vector

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
    GpuParticleData p = particles[gl_InstanceID];

    // Invisible nếu inactive hoặc vừa mới chết
    if (p.life_data.w < 0.5 || p.life_data.y <= 0.0) {
        gl_Position = vec4(0.0, 0.0, -1000.0, 1.0); // clip ra ngoài frustum
        fragColor   = vec4(0.0);
        fragTexCoord = vec2(0.0);
        return;
    }

    float r      = p.pos_radius.w;
    vec3  center = p.pos_radius.xyz;
    vec2  corner = vertexPosition.xy;

    vec3 worldPos = center
                  + u_right * (corner.x * r)
                  + u_up    * (corner.y * r);

    gl_Position  = mvp * vec4(worldPos, 1.0);
    fragTexCoord = corner * 0.5 + 0.5;   // (-1,-1)->(0,0) ... (1,1)->(1,1), khớp UV_TABLE cũ

    // Nội suy màu theo life ratio
    float t      = 1.0 - (p.life_data.x / p.life_data.y);
    fragColor    = mix(p.color_start, p.color_end, clamp(t, 0.0, 1.0));
}
