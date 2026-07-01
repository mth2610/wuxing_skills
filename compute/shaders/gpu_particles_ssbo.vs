#version 430 core
// Vertex shader — COMPUTE PATH
// Không nhận vertex attribute. Tự build billboard quad từ SSBO + gl_VertexID.
// gl_VertexID: mỗi particle có 6 verts (2 triangles), index particle = gl_VertexID / 6

struct GpuParticleData {
    vec4 pos_radius;
    vec4 vel_drag;
    vec4 color_start;
    vec4 color_end;
    vec4 life_data;
    vec4 ff_data; // không dùng ở VS, chỉ giữ để khớp stride với ParticleBuffer
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    GpuParticleData particles[];
};

uniform mat4 mvp;
uniform vec3 u_right;   // camera right vector
uniform vec3 u_up;      // camera up vector

out vec2 fragTexCoord;
out vec4 fragColor;

// UV lookup cho 6 vertices (2 triangles, CCW)
// Triangle 1: TL(0), BL(1), BR(2)
// Triangle 2: TL(3), BR(4), TR(5)
const vec2 UV_TABLE[6] = vec2[6](
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0)
);

const vec2 CORNER_TABLE[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2(-1.0,  1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2( 1.0, -1.0)
);

void main() {
    int particle_idx = gl_VertexID / 6;
    int corner_idx   = gl_VertexID % 6;

    GpuParticleData p = particles[particle_idx];

    // Invisible nếu inactive hoặc vừa mới chết
    if (p.life_data.w < 0.5 || p.life_data.y <= 0.0) {
        gl_Position = vec4(0.0, 0.0, -1000.0, 1.0); // clip ra ngoài frustum
        fragColor   = vec4(0.0);
        fragTexCoord = vec2(0.0);
        return;
    }

    float r      = p.pos_radius.w;
    vec3  center = p.pos_radius.xyz;
    vec2  corner = CORNER_TABLE[corner_idx];

    vec3 worldPos = center
                  + u_right * (corner.x * r)
                  + u_up    * (corner.y * r);

    gl_Position  = mvp * vec4(worldPos, 1.0);
    fragTexCoord = UV_TABLE[corner_idx];

    // Nội suy màu theo life ratio
    float t      = 1.0 - (p.life_data.x / p.life_data.y);
    fragColor    = mix(p.color_start, p.color_end, clamp(t, 0.0, 1.0));
}
