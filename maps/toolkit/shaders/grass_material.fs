#version 330
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
// Đợt E / E2 — spell and effect lights. The ground is the LARGEST share of the
// screen next to a floor-level effect, so a fire that lights the caster but not
// the dirt under it still reads as pasted on. Shared block, so this agrees with
// the characters and the smoke about where the light is.
#include "core/shaders/common/vfx_lights.glsl"

// ============================================================
// WUXING — grass_material Fragment Shader (CORE_ISSUES.md Item 38,
// supersedes Item 37)
//
// Texture-blend hybrid ground material: ~80% texture, ~20% shader.
// Item 37's pure-procedural (fbm2-only, no textures) version looked
// fake — smooth color-only blobs with no fine surface grain, and gave
// an illusion of drifting under camera motion (no fixed high-contrast
// detail for the eye to anchor to). This version uses real photo
// textures (grassBase, grassDetail, dirt) for actual surface detail;
// the shader ONLY blends the layers together and adds broad
// procedural color variation via fbm2() (core/shaders/common/noise.glsl)
// — no baked mask/noise texture files.
//
// All texture lookups use WORLD-SPACE fragPosition.xz (not fragTexCoord)
// divided by a tile size, same "no tiling-seam-decision, works at any
// mesh size" reasoning as Item 37's noise sampling.
//
// Lit by the same Environment sun/ambient convention as prop_lit.fs
// (u_lightDir/u_lightColor/u_ambientColor), pushed once per frame by
// core/grass_material.c's GrassMaterial_UpdateLighting() — this shader
// is drawn via plain DrawModel()/DrawModelEx() (never wrapped by
// SkillManager_BeginShader), so nothing auto-binds these uniforms.
//
// Matte only (Lambertian via lighting.glsl's calcDiffuse) — no
// specular term, unchanged from Item 37. This is a simpler sibling to
// prop_lit, not built on top of it.
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform sampler2D texture0;   // grassBase — real grass photo texture
uniform sampler2D texture2;   // grassDetail — fine grayscale grain, ~0.5-centered
uniform sampler2D texture3;   // dirt — dirt patch photo texture

uniform vec4 colDiffuse;      // raylib standard tint uniform (DrawModelEx's tint param)

uniform float u_baseTileSize;    // world meters per grassBase/dirt tile
uniform float u_detailTileSize;  // world meters per grassDetail tile (denser repeat)
uniform float u_maskNoiseScale;  // world-space fbm2 frequency for dirt-through-grass mask
uniform float u_colorVarScale;   // world-space fbm2 frequency for broad tint variation

uniform vec3 u_lightDir;      // surface -> light, world space
uniform vec3 u_lightColor;    // sun color, normalized 0..1
uniform vec3 u_ambientColor;  // ambient floor color, normalized 0..1

out vec4 finalColor;

void main() {
    // fragTexCoord is baked at mesh-creation time to equal world meters
    // (verdant_path.c's TilePlaneUVs(..., 1.0f) call) — using it instead of
    // fragPosition.xz sidesteps a suspected matModel-uniform coupling issue
    // (see CORE_ISSUES.md Item 38 follow-up): the ground pattern was
    // observed staying at a constant screen-relative offset from the player
    // regardless of true distance traveled, while prop_lit-shaded objects
    // (which only use fragPosition for a minor specular term, not primary
    // color) correctly tracked distance. fragTexCoord has zero per-frame
    // matrix dependency, so it can't carry the same bug regardless of root
    // cause.
    vec2 worldXZ  = fragTexCoord;
    vec2 baseUV   = worldXZ / u_baseTileSize;
    vec2 detailUV = worldXZ / u_detailTileSize;

    vec3 grass  = texture(texture0, baseUV).rgb;
    vec3 detail = texture(texture2, detailUV).rgb;   // ~0.5-centered grayscale
    vec3 dirt   = texture(texture3, baseUV).rgb;

    // Fine grain modulates brightness only, doesn't shift hue — this is the
    // real-surface-detail cue the pure-procedural Item 37 version lacked.
    vec3 grassDetailed = grass * mix(0.85, 1.15, detail.r);

    // Procedural mask (no baked mask texture) decides where dirt shows
    // through grass — smoothstep softens the transition into an organic
    // patch edge instead of a hard cutout.
    float mask = smoothstep(0.35, 0.55, fbm2(worldXZ * u_maskNoiseScale));
    vec3 blended = mix(grassDetailed, dirt, mask);

    // Broad, sparse regional tint variation — large-area brightness drift,
    // not small speckle (that's already covered by grassDetail above).
    float colorVar = fbm2(worldXZ * u_colorVarScale);
    blended *= mix(0.85, 1.15, colorVar);

    vec3 normal = normalize(fragNormal);
    float diff = calcDiffuse(normal, normalize(u_lightDir), 0.0);

    vec3 lit = blended * (u_ambientColor + diff * u_lightColor);
    lit += VFXLights_Accumulate(fragPosition, normal, blended);

    finalColor = vec4(lit, 1.0) * colDiffuse;
}
