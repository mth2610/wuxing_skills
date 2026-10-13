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
// procedural color variation (core/shaders/common/noise.glsl)
// — no baked mask/noise texture files.
//
// All texture lookups and noise sampling use fragTexCoord (baked to world
// meters at mesh creation — see the note in main()) divided by a tile size
// or multiplied by a noise frequency: "no tiling-seam-decision, works at any
// mesh size", same reasoning as Item 37's noise sampling.
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
//
// ---- Optimization pass (uniform/varying interface UNCHANGED, so
// core/grass_material.c needs no edits) ----
//  * GRASS_LITE (auto-on under GL_ES, i.e. mobile): the broad tint
//    variation drops from a second fbm2() to a 4-sin() domain-warped
//    wave. That term is meant to be low-frequency drift only, so fbm
//    octaves were spent on detail nobody can see. Mask noise stays fbm2
//    (dirt patches must look organic, not periodic). Desktop path keeps
//    both fbm2 calls exactly as before.
//  * Color math runs in mediump on GL_ES (desktop ignores the qualifier);
//    coordinates and noise input stay highp.
//  * Per-fragment divides by tile size became multiplies by a
//    uniform-only reciprocal (hoistable by the driver).
//  * grassDetail is read as a single channel (.r), it is grayscale.
//
// ---- Visual pass ----
//  * Dirt/grass boundary is broken up by the grassDetail grain, so dirt
//    pokes through between blades instead of a smooth cross-fade (the
//    "pasted-on gradient" look). Uses the grain already fetched: no
//    extra texture read.
//  * Thin darker rim where grass meets dirt (damp/compacted soil edge)
//    gives the patches depth without a normal map.
//  Tune both with GRASS_EDGE_BREAKUP / GRASS_RIM_DARKEN below
//  (set either to 0.0 to get the old look back).
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

// 1 = cheaper path for mobile. Auto-selected from GL_ES; define GRASS_LITE
// yourself before this point to force either way (e.g. to profile on PC).
#ifndef GRASS_LITE
  #ifdef GL_ES
    #define GRASS_LITE 1
  #else
    #define GRASS_LITE 0
  #endif
#endif

// How strongly the grain perturbs the dirt/grass threshold (0 = old smooth fade).
#define GRASS_EDGE_BREAKUP 0.6
// How much the grass->dirt transition band is darkened (0 = off).
#define GRASS_RIM_DARKEN   0.15

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform sampler2D texture0;   // grassBase — real grass photo texture
uniform sampler2D texture2;   // grassDetail — fine grayscale grain, ~0.5-centered
uniform sampler2D texture3;   // dirt — dirt patch photo texture

uniform mediump vec4 colDiffuse;   // raylib standard tint uniform (DrawModelEx's tint param)

uniform float u_baseTileSize;    // world meters per grassBase/dirt tile
uniform float u_detailTileSize;  // world meters per grassDetail tile (denser repeat)
uniform float u_maskNoiseScale;  // world-space fbm2 frequency for dirt-through-grass mask
uniform float u_colorVarScale;   // world-space frequency for broad tint variation

uniform vec3 u_lightDir;               // surface -> light, world space
uniform mediump vec3 u_lightColor;     // sun color, normalized 0..1
uniform mediump vec3 u_ambientColor;   // ambient floor color, normalized 0..1

out vec4 finalColor;

#if GRASS_LITE
// Smooth, zero-octave stand-in for fbm2() on the broad tint term.
// Two incommensurate, domain-warped sine layers -> no visible lattice at
// the low frequencies u_colorVarScale is meant for. Range ~[0,1], mean 0.5
// (same convention fbm2 is used with below).
float broadVariation(vec2 p) {
    p *= 3.14159265;
    float a = sin(p.x + 1.7 * sin(p.y * 0.71 + 1.3));
    float b = sin(p.y * 1.13 + 1.9 * sin(p.x * 0.57 + 4.1));
    return 0.5 + 0.25 * (a + b);
}
#endif

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

    // Uniform-only reciprocals: hoistable once per draw instead of a
    // divide per fragment.
    vec2 invTile  = 1.0 / vec2(u_baseTileSize, u_detailTileSize);
    vec2 baseUV   = worldXZ * invTile.x;
    vec2 detailUV = worldXZ * invTile.y;

    mediump vec3  grass  = texture(texture0, baseUV).rgb;
    mediump float detail = texture(texture2, detailUV).r;   // ~0.5-centered grayscale
    mediump vec3  dirt   = texture(texture3, baseUV).rgb;

    // Fine grain modulates brightness only, doesn't shift hue — this is the
    // real-surface-detail cue the pure-procedural Item 37 version lacked.
    mediump vec3 grassDetailed = grass * (0.85 + 0.30 * detail);

    // Procedural noise (no baked mask texture) decides where dirt shows
    // through grass. The grain is added to the noise BEFORE the threshold, so
    // the patch border is ragged at blade scale rather than a smooth fade.
    // Thresholds are centered on the old 0.35..0.55 band (0.45) but narrower,
    // since the grain now supplies most of the edge softness.
    float n = fbm2(worldXZ * u_maskNoiseScale);
    mediump float edge = n + (detail - 0.5) * GRASS_EDGE_BREAKUP;
    mediump float mask = smoothstep(0.40, 0.50, edge);

    // Darker soil rim exactly where the blend is half grass / half dirt
    // (peaks at mask = 0.5, zero inside either material).
    mediump float rim = mask * (1.0 - mask) * 4.0;

    mediump vec3 blended = mix(grassDetailed, dirt, mask);
    blended *= 1.0 - GRASS_RIM_DARKEN * rim;

    // Broad, sparse regional tint variation — large-area brightness drift,
    // not small speckle (that's already covered by grassDetail above).
#if GRASS_LITE
    float colorVar = broadVariation(worldXZ * u_colorVarScale);
#else
    float colorVar = fbm2(worldXZ * u_colorVarScale);
#endif
    blended *= 0.85 + 0.30 * colorVar;

    vec3 normal = normalize(fragNormal);
    mediump float diff = calcDiffuse(normal, normalize(u_lightDir), 0.0);

    mediump vec3 lit = blended * (u_ambientColor + diff * u_lightColor);
    lit += VFXLights_Accumulate(fragPosition, normal, blended);

    finalColor = vec4(lit, 1.0) * colDiffuse;
}
