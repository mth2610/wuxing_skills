---
name: audio
description: Own, optimize, debug, test, and evolve the Wuxing Skills Game Audio module under audio/. Use for sound effects (SFX) pooling, 3D spatial attenuation and panning, background music streaming, audio asset mapping, and volume management.
---

# Audio Module Agent

## Docs layout
- `docs/API.md` — pure interface (this module's public API contracts)
- `docs/LANDMINES.md` — distilled module-local debugging lessons
- Root `ENGINE_LANDMINES.md` — cross-cutting engine lessons

## Role
Manages the **Audio** module of the Wuxing Skills project. Responsible for sound device lifecycle, 3D spatial sound falloff, element-specific cast whooshes, UI feedback audio, combat impacts, and streamed background music.

## Scope
- **Read/write:** All files under `audio/` (`.c`, `.h`)
- **Read (required):** `audio/docs/API.md`, `audio/docs/LANDMINES.md`
- **Read (interface only):** `core/resource_manager.h`, `assets/audio/`

## Directories FULLY FORBIDDEN
- `build/`
- `_deps/`
- `android.wuxing_skills/`
- `unreal-engine-starter-content-main/` (off-limits without explicit permission)

## Core Responsibilities
1. **Asset-Optional Design:** Missing audio files must fail silently and gracefully without crashes or repeated expensive disk retries.
2. **Audio-Blind Gameplay:** Gameplay modules (`entities/`, `combat/`) must remain audio-blind. Audio events are translated at the system integration boundary (`main.c`, `game/`, `ui/`).
3. **Spatial Falloff & Jitter:** Sound played at 3D world positions must respect `AUDIO_SFX_MAX_DIST` with linear volume falloff and stereo panning. Pitch jitter ($\pm 6\%$) prevents rapid repetitive hits from sounding robotic.
4. **No Dynamic Allocations:** Strictly zero `malloc`/`free` during runtime. Sound handles are lazily loaded and cached via `ResourceManager_LoadSound()`.
