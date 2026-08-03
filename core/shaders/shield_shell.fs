#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"

// Authored shield membrane. Unlike plasma fallback, this path receives its
// body/flow/mask sheets from VFX_ShieldSurface; its colour remains VFX_Material.
uniform vec4 u_bodyColor;
uniform vec4 u_glowColor;
uniform sampler2D u_bodyTex;
uniform sampler2D flowTex;
uniform sampler2D u_maskTex;
uniform int u_useMask;
uniform float uTime;
uniform float u_maskTiling;
uniform float u_opacity;

#include "core/uv/shaders/uv_field.glsl"

// A 2D sheet on equirectangular sphere UVs pinches into a visible star at each
// pole. Project the same tile from the three local normal planes instead; the
// weighted transition is continuous and has no pole or longitude seam.
//
// The flow is now a core/uv SurfaceFlow bound by SurfaceFlow_Apply(), not the
// shader's own uFlowSpeed/Strength/Tiling/u_useFlow quartet. A one-layer flow
// with twoPhase on is exactly what this shader did before — but the shield can
// now be given a second or third layer from data without touching this file,
// which is the point of the module.
//
// mat == uv here: a triplanar projection's plane coordinate IS the material
// coordinate, stamped on the geometry by its own normal and not measured from
// anything that moves.
vec4 Shield_SurfaceSample(vec2 uv)
{
    return SurfaceFlow_FieldSample(u_bodyTex, flowTex, uv, uv, uTime);
}

vec4 Shield_TriplanarBody(vec3 normal)
{
    vec3 blend = pow(abs(normal), vec3(4.0));
    blend /= max(blend.x + blend.y + blend.z, 0.0001);
    return Shield_SurfaceSample(normal.zy) * blend.x +
           Shield_SurfaceSample(normal.xz) * blend.y +
           Shield_SurfaceSample(normal.xy) * blend.z;
}

float Shield_TriplanarMask(vec3 normal)
{
    if (u_useMask == 0) return 1.0;
    vec3 blend = pow(abs(normal), vec3(4.0));
    blend /= max(blend.x + blend.y + blend.z, 0.0001);
    return texture(u_maskTex, normal.zy * u_maskTiling).r * blend.x +
           texture(u_maskTex, normal.xz * u_maskTiling).r * blend.y +
           texture(u_maskTex, normal.xy * u_maskTiling).r * blend.z;
}

void main() {
    vec3 normal = normalize(fragNormal);
    vec4 body = Shield_TriplanarBody(normal);
    float mask = Shield_TriplanarMask(normal);
    float detail = clamp(dot(body.rgb, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
    float strand = smoothstep(0.16, 0.62, detail);
    float alpha = body.a * mask * strand * u_opacity;
    vec3 color = mix(u_bodyColor.rgb * 0.28, u_glowColor.rgb, strand);
    finalColor = vec4(color, alpha);
}
