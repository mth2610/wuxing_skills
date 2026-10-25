#version 430 core
// Vertex shader — COMPUTE PATH
// Đọc thẳng SSBO particle theo gl_InstanceID (mỗi instance = 1 hạt, quad 6 verts).
// Template quad nằm ở attribute 0 (vertexPosition, góc ±1).
//
// `readonly` bắt buộc: rlvk cần NonWritable khi thiếu vertexPipelineStoresAndAtomics.

struct GpuParticleData {
    vec4 pos_radius;
    vec4 vel_drag;
    vec4 color_start;
    vec4 color_end;
    vec4 life_data;
    vec4 ff_data;        // y = stretchStrength (ff_pad0)
    vec4 route_data;     // x=emitter id, y=render mode, z=travel slot, w=waypoint
    vec4 impact_data;    // x=impact age, y=impact active, z=wind, w=blendMode
    vec4 formation_data; // xyz = per-particle offset from formation origin
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    GpuParticleData particles[];
};

in vec3 vertexPosition;   // template quad corner, xy trong [-1, 1]

uniform mat4 mvp;
uniform vec3 u_right;   // camera right vector
uniform vec3 u_up;      // camera up vector
uniform float u_filterEmitter;    // < 0 = all
uniform float u_filterRenderMode; // < 0 = all
uniform float u_filterBlend;      // < 0 = all

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
    uint id = uint(gl_InstanceID);
    vec4 life = particles[id].life_data;
    vec4 route = particles[id].route_data;

    // Culling sớm ở Vertex Stage nếu hạt không hoạt động, đã hết đời hoặc khác bộ lọc
    if (life.w < 0.5 || life.x <= 0.0 || life.y <= 0.0 ||
        (u_filterBlend >= 0.0 && abs(particles[id].impact_data.w - u_filterBlend) > 0.25) ||
        (u_filterRenderMode < 0.0 && abs(route.y - 3.0) < 0.25) ||
        (u_filterEmitter >= 0.0 && abs(route.x - u_filterEmitter) > 0.25) ||
        (u_filterRenderMode >= 0.0 && abs(route.y - u_filterRenderMode) > 0.25)) {
        gl_Position  = vec4(2.0, 2.0, 2.0, 1.0);
        fragColor    = vec4(0.0);
        fragTexCoord = vec2(0.0);
        return;
    }

    vec4 pr = particles[id].pos_radius;
    float r = pr.w;

    // Tính màu sắc & kiểm tra độ trong suốt trước khi biến đổi ma trận để tránh
    // rasterize các quad đã mờ hoàn toàn (alpha < 0.005) hoặc bán kính <= 0.
    float t = clamp(1.0 - (life.x / life.y), 0.0, 1.0);
    vec4 col_start = particles[id].color_start;
    vec4 col_end   = particles[id].color_end;
    vec4 color     = mix(col_start, col_end, t);

    if (r <= 1e-5 || color.a < 0.005) {
        gl_Position  = vec4(2.0, 2.0, 2.0, 1.0);
        fragColor    = vec4(0.0);
        fragTexCoord = vec2(0.0);
        return;
    }

    vec3 center = pr.xyz;
    vec2 corner = vertexPosition.xy;
    vec3 worldPos;

    // Chỉ đọc vận tốc từ SSBO và chiếu lên mặt phẳng camera khi hạt thực sự có stretchStrength > 0
    float stretchStrength = particles[id].ff_data.y;
    if (stretchStrength > 0.0) {
        vec3 vel = particles[id].vel_drag.xyz;
        float vx = dot(vel, u_right);
        float vy = dot(vel, u_up);
        float screenSpeedSq = vx * vx + vy * vy;

        if (screenSpeedSq > 0.0025) { // > 0.05^2
            float invSpeed    = inversesqrt(screenSpeedSq);
            float screenSpeed = screenSpeedSq * invSpeed;
            vec2 dir2D        = vec2(vx, vy) * invSpeed;
            vec3 tangentDir   = u_right * dir2D.x + u_up * dir2D.y;
            vec3 rightDir     = u_right * dir2D.y - u_up * dir2D.x;

            float stretchFactor = min(1.0 + screenSpeed * stretchStrength, 3.5);
            worldPos = center
                     + rightDir   * (corner.x * r)
                     + tangentDir * (corner.y * r * stretchFactor);
        } else {
            worldPos = center
                     + u_right * (corner.x * r)
                     + u_up    * (corner.y * r);
        }
    } else {
        worldPos = center
                 + u_right * (corner.x * r)
                 + u_up    * (corner.y * r);
    }

    gl_Position  = mvp * vec4(worldPos, 1.0);
    fragTexCoord = corner * 0.5 + 0.5;
    fragColor    = color;
}
