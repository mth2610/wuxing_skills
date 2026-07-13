---
name: vfx_composition
description: Specialized instructions, strict file boundaries, architecture description, and rules for creating/tuning 3D/2D VFX compositions in the Raylib C-Engine.
---

# VFX Composition & Optimization Skill

## 1. Role & Scope Boundaries (Anti-Poisoning & Token Efficiency)
This agent is strictly dedicated to designing, tuning, and optimizing visual compositions, procedural meshes, and shaders. Reading unrelated systems (like logic, AI, maps, or environment internals) is highly forbidden to preserve context-window tokens and prevent code churning.

### Strict File Access Rules:
- **Allowed (Read/Write):**
  - `core/composition/visual_composer.h`
  - `core/composition/visual_composer.c`
  - `core/composition/vc_*.inl` (modular archetypes)
  - `core/shaders/` (vertex/fragment shaders including custom/common)
- **Allowed (Read Only - Interface / Documentation):**
  - `COMPOSITION_API.md`
  - `SHADER_API.md`
  - `core/material/material_system.h` (to check `EffectMaterialParams` or presets)
  - `core/geometry/procedural_mesh_utils.h` (for procedural shape structures)
- **Strictly Forbidden (Never Read, List, or Scraped):**
  - `skills/` (except `.h` headers for function signatures if making custom integrations)
  - `maps/` (unrelated map-specific meshes/logic)
  - `environment/` (environmental shadows/light calculation logic)
  - `build/`, `_deps/`, `third_party/`, `android.wuxing_skills/`

---

## 2. Visual Composition Architecture Reference
To prevent reading core rendering source files, the composition pipeline architecture is summarized below:

### Architecture Overview:
1. **Entry Points:** `VC_Archetype_Update(dt)` handles elapsed time update and pool expiration. `VC_Archetype_Draw3D()` renders active effects using `visual_composer.c` which includes specialized files like `vc_earth.inl`, `vc_water.inl`, `vc_fire.inl`, etc.
2. **Memory Management:** Zero dynamic memory (`malloc`/`free`) is permitted. Active effects are stored in fixed-size arrays (e.g. `s_archCrownSplashes[ARCH_MAX_CROWN_SPLASHES]`) with `bool active` lifecycle flags.
3. **Materials Integration:**
   - Visual effects are drawn by passing custom parameter structs `EffectMaterialParams` to `Material_LoadCustomShader(params, vsPath, fsPath)`.
   - The returned `EffectMaterial` uniform locations are managed internally by the core material system.
   - `customParam1` is standard for **Progress/Time** `(0.0 -> 1.0)`.
   - `customParam2` is standard for **Random Phase offsets** to disrupt shader synchronization.
4. **Procedural Geometry Pipeline:**
   - Raw geometries are generated in memory and drawn directly via vertex matrices using functions defined in `core/geometry/procedural_mesh_utils.h` (like `ProceduralMesh_DrawOrganicStonePillar` or `ProceduralMesh_BuildFissure`).
   - Draw calls require active batch flushes `rlDrawRenderBatchActive()` to avoid deferred rendering state hazards.

---

## 3. Common VFX Recipes (Wuxing Art Direction §6.1)
When composing visual effects, follow these multi-layered structures to ensure depth and punchiness:

1. **Projectile:** Emitter Core + Trail (Ribbon/Particles) + Light Source + Impact Burst + Decay Smoke.
2. **Beam:** Inner Hot Core (much thicker/brighter, rolls fast) + Outer Flowing Body (energy texture, slower) + Impact Point Shockwave + Endcap Sparks. (No flat additive bloom quad).
3. **Explosion:** Radial Flash + Lens Distortion/Refraction + Particle Burst + Ground Decal + Lingering Embers + Secondary Smoke Puff.
4. **Summon/Portal:** Ground Puddle (Flow map) + Outer Ring Emitter (Particles pulling inward) + Refraction Wave + Soft Emissive Glow.
5. **Aura/Qi:** Low-opacity Body Wisps (using alpha blend ribbons with Curl Noise) + Short-lived Sparkles (Additive blend) + Tiny Point Lights.
6. **Shield:** Core Sphere Geometry + Flow Map distortion + Dissolve edge highlights + Hit ripple effect.

---

## 4. Render State Management & Raylib Hazards (CRITICAL)
OpenGL is a state machine, and Raylib uses a deferred batch renderer. Modifying render states without flushing the active vertices leads to **Batching Hazards** where states are applied to the wrong vertices.

### Mandatory Rendering Rules:
1. **Flush Active Batch:** Always call `rlDrawRenderBatchActive();` *immediately before* calling any OpenGL state-changing functions.
   - Example: Before changing depth mask (`rlDisableDepthMask`), depth testing (`rlDisableDepthTest`), culling (`rlDisableBackfaceCulling`), or blend modes (`BeginBlendMode`).
2. **Restore State:** Always restore the modified state back to its default immediately after drawing your mesh/particles, and flush the batch again if needed.
3. **Backface Culling Rules:**
   - **Enable Culling** for solid closed 3D geometries (e.g., rocks, boulders) to improve performance.
   - **Disable Culling** (`rlDisableBackfaceCulling()`) for two-sided surfaces, flat planes, oriented quads, ribbon strips, and hollow/crater meshes (e.g., Fissure cracks, Crown Splash) to prevent them from disappearing at low camera angles.
4. **Depth Mask Rules:**
   - Turn off depth writes (`rlDisableDepthMask()`) for transparent particles, soft wisps, decals, and screen-space glows to avoid Z-fighting and solid black margins.
   - Keep depth test active (`rlEnableDepthTest()`) to ensure objects are correctly occluded behind solid structures.

---

## 5. Shader Coding Standards
All custom shaders (`.vs` and `.fs`) must adhere to project-wide guidelines for compatibility and visual consistency.

### Uniforms & Environment Mapping:
- **Never hardcode viewPos / cameraPos:** Use `viewPos` (auto-bound by the core skill manager) inside fragment shaders.
- **Never hardcode light directions:** Use the uniform `u_lightDir` (auto-bound, real sun direction) rather than static vectors like `vec3(0.5, 1.0, 0.5)`.

### Shader Utility Library (`core/shaders/common/`):
- Include `#include "core/shaders/common/lighting.glsl"` for `calcDiffuse()`, `calcFresnel()`, and `calcSpecular()`.
- Include `#include "core/shaders/common/noise.glsl"` for `vnoise()`, `fbm2()`, and `fbm2N()`.
- Include `#include "core/shaders/common/fx.glsl"` for `dissolveCalc()`, `flowBlend()`, and `emissiveMask()`.

### Aesthetic Details:
- **No Visual Popping:** Implement smooth smoothstep transitions on opacity at the start and end of the VFX lifecycle.
- **Water Specs:** Water requires strong specular highlights (Blinn-Phong) and Fresnel-weighted edges for a glass-like look. Use FBM-driven caustics scrolling XZ in world-space for organic shimmers.
- **Dissolve Edge Glow:** Use world-space noise (`hash3` or `vnoise` multiplied by position) instead of UV coordinates to dissolve meshes organically. Add a colored emissive edge highlight (`1.0 - edgeFactor`) to make the boundary pop.

---

## 6. Parameter Registration (Live-Tuning)
To allow real-time tuning in the Sandbox debug GUI, all physical and visual variables must be registrable.
- Define parameters as `static float` in `skills/[element]/[name]_skill/[name]_skill_params.inl`.
- Map them using `s_tunables` entries in `skills/[element]/[name]_skill/[name]_skill_tunables.inl`.
- Reference parameters inside functions instead of hardcoding raw numeric floats.

---

## 7. Jitter & Randomization
Ensure no two skill casts look identical by applying randomization inside the spawner (`VFX_Compose*`):
1. **Yaw/Rotation:** Rotate the model matrix randomly `(0..360)` degrees on the Y-axis.
2. **Scale Jitter:** Perturb the scale dynamically by a factor of `(0.85f - 1.15f)` on creation.
3. **Wobble/Animation Phase:** Generate a random phase offset float `(0.0f - 10.0f)` and pass it as a custom uniform (e.g., `u_customParam2`) to shift vertex shader wave functions out of synchronization.
