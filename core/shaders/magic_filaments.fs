#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// Highly optimized magical sparkling filaments/threads shader.
// Mathematical footprint optimized by 50% for high fill-rate performance.

uniform vec4  u_color;
uniform float u_progress;
uniform float u_diffusion;  // rate of lateral expansion (D)
uniform float u_noiseScale; // frequency of filaments
uniform float u_driftSpeed; // sparkle/anim speed
uniform vec2  u_sourcePos;   // origin of the puff in quad-local uv space

// Ridged 2D noise: 1.0 - abs(vnoise) -> yields sharp threads
float ridgedNoise(vec2 p) {
    return 1.0 - abs(vnoise(p) - 0.5) * 2.0;
}

// Optimized 2-octave Ridged FBM for fast thread generation
float ridgedFBM2(vec2 p) {
    float v = 0.5 * ridgedNoise(p);
    mat2 rot = mat2(0.87758, 0.47942, -0.47942, 0.87758);
    p = rot * p * 2.15;
    v += 0.25 * ridgedNoise(p);
    return v * 1.333; // Normalize range to [0, 1.0]
}

void main() {
    vec2 uv = fragTexCoord * 2.0 - 1.0; 

    // ==========================================
    // 1. POINT-SOURCE DIFFUSION PHYSICAL MODEL
    // ==========================================
    float t0 = 0.005;
    float t  = mix(t0, 1.0, clamp(u_progress, 0.0, 1.0));
    
    float D  = max(u_diffusion * 0.1, 0.001); 
    float variance = 4.0 * D * t;
    
    float artisticFade = 1.0 - pow(u_progress, 2.5); 
    float ampNorm = mix(1.0, artisticFade, u_progress);

    float timeAnim = u_time * u_driftSpeed;

    // ==========================================
    // 2. FAST COORDINATE WARPING (1-Octave Noise)
    // ==========================================
    vec2 warpCoords = uv * (u_noiseScale * 0.3) + vec2(timeAnim * 0.4, -timeAnim * 0.2);
    vec2 warp = vec2(vnoise(warpCoords), vnoise(warpCoords + vec2(5.2, 1.3))) - 0.5;

    vec2 uvw = uv + warp * 0.4;
    float dist = length(uvw - u_sourcePos);
    float r2 = dist * dist;

    // Base diffusion envelope (tight dot at birth, expands and fades over time)
    float intensity = 2.0;
    float baseDensity = ampNorm * exp(-r2 / variance) * intensity;
    baseDensity = 1.0 - exp(-baseDensity * 1.5);

    // ==========================================
    // 3. SHARP FILAMENTS (Optimized 2-Octave FBM)
    // ==========================================
    vec2 filamentCoords = uvw * u_noiseScale + vec2(-timeAnim * 0.3, timeAnim * 0.5);
    float f = ridgedFBM2(filamentCoords);
    
    // Filament thickness (sharpened by pow)
    float thickness = 2.0;
    float threads = pow(max(f, 0.0), thickness);

    // ==========================================
    // 4. EMISSIVE EDGE GASEOUS FRESNEL
    // ==========================================
    // Adds a shell/ring highlight at the expanding boundary
    float ringRadius = mix(0.0, 0.55, pow(u_progress, 0.4));
    float ringDist = abs(dist - ringRadius);
    float fresnel = exp(-(ringDist * ringDist) / variance) * 0.6;

    // ==========================================
    // 5. HIGH-FREQUENCY SPARKLE PEAKS
    // ==========================================
    float sparkleNoise = vnoise(uv * 18.0 + vec2(timeAnim * 6.0, -timeAnim * 5.0));
    float sparkle = pow(max(sparkleNoise, 0.0), 7.0) * 3.0;

    // Combine diffusion envelope with filament patterns, rim fresnel, and sparkles
    float density = baseDensity * threads * 1.8;
    density += fresnel * threads * 1.0;
    density += sparkle * threads * baseDensity * 0.8;

    float alpha = clamp(density, 0.0, 1.0);

    // Soft quad edge clipping
    float softEdge = 1.0 - smoothstep(0.4, 1.0, length(uv));
    alpha *= softEdge;
    alpha *= smoothstep(0.0, 0.05, u_progress);

    // Final color with glowing emissive peaks
    vec3 glowColor = u_color.rgb * (1.0 + density * 2.0);
    finalColor = VFX_ResolveBody(glowColor, 1.0, alpha * u_color.a);
}
