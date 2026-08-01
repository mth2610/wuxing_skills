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
    vec4 ff_data; // không dùng ở VS, giữ để khớp stride với GPU/CPU struct
    vec4 route_data;
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    GpuParticleData particles[];
};

in vec3 vertexPosition;   // template quad corner, xy trong [-1, 1]

uniform mat4 mvp;
uniform vec3 u_right;   // camera right vector
uniform vec3 u_up;      // camera up vector
uniform float u_filterEmitter; // < 0 = all
uniform float u_filterRenderMode; // < 0 = all

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
    // TỐI ƯU HÓA: Chỉ đọc vector life_data, không copy toàn bộ struct
    vec4 life = particles[gl_InstanceID].life_data;
    vec4 route = particles[gl_InstanceID].route_data;

    // Invisible nếu inactive hoặc vừa mới chết
    if (life.w < 0.5 || life.y <= 0.0 ||
        (u_filterRenderMode < 0.0 && abs(route.y - 3.0) < 0.25) ||
        (u_filterEmitter >= 0.0 && abs(route.x - u_filterEmitter) > 0.25) ||
        (u_filterRenderMode >= 0.0 && abs(route.y - u_filterRenderMode) > 0.25)) {
        // SỬA LỖI: Tạo Degenerate Vertex (w = 0.0). 
        // Bị culling tuyệt đối ở bước Perspective Divide của Rasterizer.
        gl_Position  = vec4(0.0); 
        fragColor    = vec4(0.0);
        fragTexCoord = vec2(0.0);
        return;
    }

    // TỐI ƯU HÓA: Nạp trực tiếp dữ liệu cần thiết từ SSBO
    vec4 pr     = particles[gl_InstanceID].pos_radius;
    float r     = pr.w;
    vec3 center = pr.xyz;
    vec2 corner = vertexPosition.xy;

    vec3 worldPos;
    float stretchStrength = particles[gl_InstanceID].ff_data.y; // ff_pad0
    vec3 vel = particles[gl_InstanceID].vel_drag.xyz;
    float speed = length(vel);

    if (stretchStrength > 0.0 && speed > 0.2) {
        vec3 tangent = vel / speed;
        vec3 rVec = cross(u_up, tangent);
        float rLen = length(rVec);
        if (rLen > 0.0) {
            rVec /= rLen;
        } else {
            rVec = u_right;
        }
        
        float stretchFactor = 1.0 + speed * stretchStrength;
        worldPos = center
                 + rVec * (corner.x * r)
                 + tangent * (corner.y * r * stretchFactor);
    } else {
        worldPos = center
                 + u_right * (corner.x * r)
                 + u_up    * (corner.y * r);
    }

    gl_Position  = mvp * vec4(worldPos, 1.0);
    
    // (-1,-1)->(0,0) ... (1,1)->(1,1), khớp UV_TABLE cũ
    fragTexCoord = corner * 0.5 + 0.5;   

    // TỐI ƯU HÓA: Bỏ hàm clamp() do Compute Shader đã bảo đảm life.x <= life.y
    float t = 1.0 - (life.x / life.y);
    
    vec4 col_start = particles[gl_InstanceID].color_start;
    vec4 col_end   = particles[gl_InstanceID].color_end;
    
    fragColor = mix(col_start, col_end, t);
}
