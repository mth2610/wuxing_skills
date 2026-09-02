#version 330 core
#include "core/shaders/common/vfx_composite.glsl"

in vec2 fragTexCoord;
uniform vec4 u_bodyColor;
uniform vec4 u_glowColor;
uniform float u_intensity;
uniform float u_starTime;
uniform int u_pass;
uniform float u_progress;
uniform int u_mode;
out vec4 finalColor;

void main()
{
    vec2 p = fragTexCoord * 2.0 - 1.0;
    float r2 = dot(p, p);
    if (r2 >= 1.0) discard;
    float r = sqrt(r2);
    float a = atan(p.y, p.x);
    float shimmer = u_mode == 0 ? u_progress * 0.55 : u_starTime * 0.42;
    float warp = a * 11.0 + sin(a * 3.0 + 0.7 + shimmer) * 0.76 + sin(a * 5.0 - 1.1 + shimmer * 0.6) * 0.38;
    float thin = pow(max(sin(warp), 0.0), 13.0);
    float fine = pow(max(sin(a * 17.0 - 0.8 + sin(a * 4.0) * 0.32), 0.0), 27.0) * 0.42;
    float rayLength = 0.30 + 0.61 * (0.5 + 0.5 * sin(a * 5.0 + sin(a * 2.0 + shimmer) * 0.8));
    float rayTaper = smoothstep(rayLength, 0.08, r);
    float rayPulse = 0.84 + 0.16 * sin(u_starTime * 2.1 + a * 11.0);
    float rays = (thin + fine) * rayTaper * rayPulse;
    float core = exp(-r2 * 118.0);
    float corona = exp(-r2 * 12.0) * 0.76;
    float haze = exp(-r2 * 3.1) * 0.15;
    float edge = pow(1.0 - r2, 2.4);
    float silhouette = clamp((corona + rays * 0.48 + haze) * edge, 0.0, 1.0);
    float hot = clamp(core + rays * 0.28, 0.0, 1.0);

    if (u_pass == 0)
    {
        vec3 body = mix(u_bodyColor.rgb, u_glowColor.rgb, clamp(corona * 1.2 + rays * 0.35, 0.0, 1.0));
        finalColor = VFX_ResolveBody(body, 1.0 + u_intensity * 0.40,
                                     silhouette * (0.45 + u_intensity * 0.38));
    }
    else
    {
        finalColor = VFX_ResolveEmission(mix(u_glowColor.rgb, vec3(1.0), core * 0.72),
                                          1.1 + u_intensity * 2.8, hot, hot * 0.78);
    }
}
