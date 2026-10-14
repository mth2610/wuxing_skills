#version 330

#include "core/shaders/common/vfx_lights.glsl"

in vec4 fragColor;
in vec3 fragWorldPos;

uniform sampler2D texture0;
uniform mat4      u_lightVP;
uniform float     u_shadowEnabled;
uniform float     u_shadowTexel;

out vec4 finalColor;

#define GROUND_SHADOW_PCF_RADIUS 3
#define GROUND_SHADOW_PCF_STEP   1.0

const vec2 poissonDisk[16] = vec2[]( 
   vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ), 
   vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ), 
   vec2( -0.91588401, 0.45771432 ), vec2( -0.81544232, -0.87912464 ), 
   vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ), 
   vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ), 
   vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ), 
   vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ), 
   vec2( 0.19984126, 0.78641367 ), vec2( 0.14383161, -0.14100790 ) 
);

float ShadowTapBilinear(vec2 uv, float z, float res) {
    vec2  t    = uv * res - 0.5;
    vec2  f    = fract(t);
    vec2  base = (floor(t) + 0.5) * u_shadowTexel;
    float s00 = step(z, texture(texture0, base).r);
    float s10 = step(z, texture(texture0, base + vec2(u_shadowTexel, 0.0)).r);
    float s01 = step(z, texture(texture0, base + vec2(0.0, u_shadowTexel)).r);
    float s11 = step(z, texture(texture0, base + vec2(u_shadowTexel, u_shadowTexel)).r);
    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}

float ShadowFactor(vec3 worldPos) {
    vec4 posLS = u_lightVP * vec4(worldPos, 1.0);
    vec3 proj = posLS.xyz / posLS.w;
    proj = proj * 0.5 + 0.5;
    
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;

    float z = proj.z - 0.0015;
    float res = 1.0 / u_shadowTexel;
    const float S = 1.25;

    // Tính 1 lần res và lấy mẫu đối xứng mượt
    float shadow = 0.0;
    shadow += ShadowTapBilinear(proj.xy + vec2(-S, -S) * u_shadowTexel, z, res);
    shadow += ShadowTapBilinear(proj.xy + vec2( S, -S) * u_shadowTexel, z, res);
    shadow += ShadowTapBilinear(proj.xy + vec2(-S,  S) * u_shadowTexel, z, res);
    shadow += ShadowTapBilinear(proj.xy + vec2( S,  S) * u_shadowTexel, z, res);

    return shadow * 0.25;
}

#define GROUND_SHADOW_DEBUG_PROJ 0

void main() {
#if GROUND_SHADOW_DEBUG_PROJ
    if (u_shadowEnabled > 0.5) {
        vec4 p = u_lightVP * vec4(fragWorldPos, 1.0);
        vec3 pr = p.xyz / p.w * 0.5 + 0.5;
        if (pr.x < 0.0 || pr.x > 1.0 || pr.y < 0.0 || pr.y > 1.0) {
            finalColor = vec4(0.0, 0.0, 0.4, 1.0);
            return;
        }
        float d = texture(texture0, pr.xy).r;
        finalColor = vec4(vec3(d), 1.0);
        return;
    }
#endif
    float shadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        shadow = ShadowFactor(fragWorldPos);
    }
    vec3 floorLit = fragColor.rgb * mix(0.30, 1.0, shadow);
    floorLit += VFXLights_AccumulateFlat(fragWorldPos, fragColor.rgb);
    finalColor = vec4(floorLit, fragColor.a);
}