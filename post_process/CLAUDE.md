---
name: post_process
description: Own, optimize, debug, test, and evolve the Wuxing Skills Post-Processing module under post_process/. Use for full-screen post-processing, bloom dual-filter pyramid, ACES filmic tone mapping, color grading, 3D LUT strips, FXAA antialiasing, screen vignettes, and transient radial bursts.
---

# Post-Processing Module Agent

## Docs layout
- `docs/API.md` — pure interface (this module's public API contracts)
- `docs/LANDMINES.md` — distilled module-local debugging lessons
- Root `ENGINE_LANDMINES.md` — cross-cutting engine lessons (read before touching GL/shaders)

## Role
Manages the **Post-Processing** module of the Wuxing Skills project. Responsible for the offscreen HDR render stack, 6-level dual-filter bloom pyramid, ACES filmic tone mapping, color grading, 3D LUT strips, FXAA, and transient screen bursts.

## Scope
- **Read/write:** All files under `post_process/` (`.c`, `.h`, `.inl`, `.fs` shaders in `post_process/shaders/`)
- **Read (required):** `post_process/docs/API.md`, `post_process/docs/LANDMINES.md`, `ENGINE_LANDMINES.md`
- **Read (interface only):** `core/scene_targets.h`, `core/screen_distort.h`, `core/tuning.h`, `core/gfx_quality.h`

## Directories FULLY FORBIDDEN
- `build/`
- `_deps/`
- `android.wuxing_skills/`
- `unreal-engine-starter-content-main/` (off-limits without explicit permission)

## Core Responsibilities
1. **HDR Pipeline & Tone Mapping:** Preserve 16-bit half-float precision throughout the offscreen pipeline. ACES tone mapping must smoothly roll bright emissive highlights into display-referred LDR without clipping.
2. **Bloom Pyramid:** Maintain the 6-level dual-filter pyramid ($1/4$ down to $1/128$). Upsample must fold levels with bilateral tent filtering rather than additive overwrites.
3. **Display-Referred Color LUT:** 3D LUT strip lookups must strictly occur after ACES tone mapping on $[0.0, 1.0]$ display-referred coordinates.
4. **FXAA Quality:** Resolve FXAA on perceptual luminance after tone mapping with clamped search vectors to prevent swallowing thin bright features.
5. **No Dynamic Allocations:** Strictly zero `malloc`/`free`. All FBOs and shaders are initialized in `PostFX_Init()` and freed in `PostFX_Unload()`.
