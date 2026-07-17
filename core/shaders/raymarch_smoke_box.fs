#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

uniform vec3 u_boxMin;
uniform vec3 u_boxMax;
uniform vec3 u_center;
uniform float u_radius;
uniform vec4 u_smokeColor;

float vnoise3(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash3(p + vec3(0.0,0.0,0.0)), hash3(p + vec3(1.0,0.0,0.0)), f.x),
            mix(hash3(p + vec3(0.0,1.0,0.0)), hash3(p + vec3(1.0,1.0,0.0)), f.x), f.y),
        mix(mix(hash3(p + vec3(0.0,0.0,1.0)), hash3(p + vec3(1.0,0.0,1.0)), f.x),
            mix(hash3(p + vec3(0.0,1.0,1.0)), hash3(p + vec3(1.0,1.0,1.0)), f.x), f.y), f.z);
}

vec2 intersectAABB(vec3 ro, vec3 rd, vec3 boxMin, vec3 boxMax) {
    vec3 tMin = (boxMin - ro) / rd;
    vec3 tMax = (boxMax - ro) / rd;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    return vec2(max(max(t1.x, t1.y), t1.z), min(min(t2.x, t2.y), t2.z));
}

void main() {
    vec3 rd = normalize(fragPosition - viewPos);
    vec2 hit = intersectAABB(viewPos, rd, u_boxMin, u_boxMax);
    
    if (hit.x > hit.y || hit.y < 0.0) discard;
    
    // TOÁN HỌC CULLING BẤT CHẤP VULKAN:
    // Ta chọn khoảng cách cần render: Nếu đứng ngoài (hit.x > 0) thì lấy mặt gần. Nếu đứng trong thì lấy mặt xa.
    float targetDist = (hit.x > 0.0) ? hit.x : hit.y;
    float distToFrag = distance(viewPos, fragPosition);
    
    // Nếu khoảng cách vật lý của pixel đang vẽ lệch quá 5cm so với mặt mong muốn -> vứt.
    // Điều này đảm bảo mỗi tia ray đi qua hộp chỉ kích hoạt shader ĐÚNG 1 LẦN, không bao giờ bị gấp đôi Alpha.
    if (abs(distToFrag - targetDist) > 0.05) discard;
    
    float t = max(hit.x, 0.0); 
    float tEnd = hit.y;
    
    const int MAX_STEPS = 15; 
    float stepSize = (tEnd - t) / float(MAX_STEPS);
    vec3 p = viewPos + rd * t;
    float accumAlpha = 0.0;
    
    for (int i = 0; i < MAX_STEPS; i++) {
        float d = distance(p, u_center);
        
        float mask = 1.0 - smoothstep(u_radius * 0.4, u_radius, d);
        
        if (mask > 0.0) {
            vec3 noiseCoord = p * 1.5 - vec3(0.0, u_time * 0.5, 0.0);
            float n = 0.50 * vnoise3(noiseCoord) + 0.25 * vnoise3(noiseCoord * 2.0);
            
            float density = max(n * mask - 0.1, 0.0) * 6.0;
            
            if (density > 0.0) {
                accumAlpha += density * stepSize * (1.0 - accumAlpha);
                if (accumAlpha >= 0.95) {
                    accumAlpha = 1.0;
                    break;
                }
            }
        }
        p += rd * stepSize;
    }
    
    if (accumAlpha <= 0.01) discard;
    
    float gradient = smoothstep(u_boxMin.y, u_boxMax.y, fragPosition.y);
    vec3 finalSmoke = mix(u_smokeColor.rgb * 0.1, u_smokeColor.rgb, gradient);
    
    finalColor = vec4(finalSmoke, accumAlpha);
}