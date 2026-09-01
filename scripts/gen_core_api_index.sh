#!/usr/bin/env bash
# Regenerates core/docs/API.md as an index-only doc straight from the core/*.h
# headers (ground-truth signatures). Run from repo root:
#     bash scripts/gen_core_api_index.sh > core/docs/API.md
#
# The preamble below (critical usage rules) is hand-authored — everything under
# "Signature Index" is generated from the headers, so signatures never drift.
set -euo pipefail
cd "$(dirname "$0")/.."

# Ordered public headers. Composition-layer prose lives in COMPOSITION_API.md;
# only the signature index for those symbols is included here.
HEADERS=(
  core/resource_manager.h core/vfx_surface_registry.h core/tuning.h
  core/skill_manager.h core/skill_helper.h core/skill_curve.h
  core/fluid/fluid_impact.h
  core/fluid/fluid_surface.h
  core/fluid/fluid_orb.h
  core/gas/gas_system.h
  core/force_field.h core/particles/particle_travel.h core/particles/particle_system.h core/particles/particle_manager.h core/mesh_adjacency.h
  core/trails/trail_system.h core/ribbon_strip.h core/decals/decal_system.h
  core/lightning/lightning_stroke.h
  core/vfx_contrast.h core/vfx_appearance.h core/vfx_render.h
  core/screen_distort.h core/metaball_fx.h core/color_gradient.h
  core/float_curve.h core/uv/flow_map.h core/deform/mesh_deform.h core/uv/uv_deform.h core/uv/surface_flow.h core/uv/uv_fx.h core/path_spline.h core/sprite_anim.h
  core/vfx_light.h core/post_fx.h core/camera_fx.h
  core/debug_draw.h core/motion_controller.h core/status_vfx.h core/afterimage.h
  core/surface_material.h core/gfx_quality.h core/audio_system.h core/atmosphere.h
  core/material/material_system.h core/geometry/procedural_mesh_utils.h
  core/composition/visual_composer.h core/composition/vfx_sequence.h core/presets/vfx_presets.h core/utils_math.h
)

protos() {
  perl -0777 -pe 's{/\*.*?\*/}{}gs; s{//[^\n]*}{}g; s{^[ \t]*#[^\n]*}{}mg; 1 while s/\{[^{}]*\}//gs; s/\bstatic\s+inline\b[^;()]*\([^()]*\)//gs;' "$1" \
  | awk 'BEGIN{RS=";"} { s=$0; gsub(/\n[ \t]*/," ",s); gsub(/^[ \t\n]+/,"",s); gsub(/[ \t]+/," ",s);
      if (s ~ /\(/ && s !~ /[{}]/ && s !~ /^#/ && s !~ /^typedef/ && s ~ /^[A-Za-z_].*[A-Za-z_0-9] *\(/) print "  " s ";"; }'
}
enums() { perl -0777 -ne 's{/\*.*?\*/}{}gs; s{//[^\n]*}{}g; while(/typedef\s+enum\s*\{([^}]*)\}\s*(\w+)/gs){my($v,$n)=($1,$2); $v=~s/\s+//g; $v=~s/=[^,]*//g; $v=~s/,$//; print "$n { $v }\n";}' "$1"; }
structs() { perl -0777 -ne 's{/\*.*?\*/}{}gs; s{//[^\n]*}{}g; while(/typedef\s+struct\s*(?:\w+\s*)?\{[^{}]*\}\s*(\w+)/gs){print "$1 ";} while(/typedef\s+struct\s+(\w+)\s+(\w+)\s*;/gs){print "$2 ";}' "$1"; }

cat <<'PREAMBLE'
# Core Engine API — Index

> **Index-only, generated from `core/**/*.h` by `scripts/gen_core_api_index.sh`** (ground-truth signatures — never hand-edit the Signature Index; edit the headers and regenerate). This file is the map; the headers are the territory.
>
> - **How to USE these APIs** (patterns, worked examples, contracts, the "why"): [`API_GUIDE.md`](API_GUIDE.md) — the prose companion to this index.
> - **Struct fields / enum semantics:** open the header named in each section (structs are listed by name).
> - **Composition layer** (`VFX_Compose*`, material/motion split): guidance in [`COMPOSITION_API.md`](COMPOSITION_API.md).
> - **Authoring a skill** (recipes, archetype skeletons): [`RECIPE.md`](../../skills/docs/RECIPE.md), [`SKELETONS.md`](../../skills/docs/SKELETONS.md).
> - **Traps:** [`LANDMINES.md`](LANDMINES.md) (core-local) + [`ENGINE_LANDMINES.md`](../../ENGINE_LANDMINES.md) (cross-cutting — read before touching GL/shaders).
> - **Code standards** (C99, memory, scale, shaders, auto-registry, aesthetic laws): [`AGENT_CODE_STANDARD.md`](../../AGENT_CODE_STANDARD.md). The rules below are the API-usage subset; that file is the full checklist.

## Critical usage rules (what bare signatures don't tell you)
- **No `malloc`/`free`** — static pools only. Load assets via `ResourceManager_Load*`; **never** call `UnloadShader`/`UnloadTexture` in skill code (the manager owns lifetimes).
- **Meter scale** (1 unit = 1 m): mesh radii ~0.10–0.20, force/gravity 3.0–7.0 (vs real 9.81), particle speed 1.0–3.0.
- **A system's `*_Init`/`*_Update`/`*_Draw`/`*_Unload` are the engine-loop lifecycle** — skill code does not call them; it calls the spawn/add entry points only. Exceptions are called out below.
- **Trails:** `SpawnTrailEntity` returns an id. `TRAIL_TYPE_PROJECTILE` self-terminates; manually-driven trails use `KillTrail` when their lifecycle ends. Curved lightning uses `LightningStroke_SpawnPath` so it retains the stroke shader's continuous field and HDR profile.
- **`VFXLight_Spawn` requires a `VFXPriority`** — a full pool evicts the lowest priority. Use `VFX_PRIORITY_HIGH_ULTIMATE` for casts that must not drop.
- **Metaballs:** call `MetaballFX_RegisterBlob` every frame per blob (1-frame lifetime); never call `MetaballFX_Prepare`, `MetaballFX_Composite`, or `MetaballFX_DrawRegistered` from skill code.
- **ScreenDistort:** skills only call `ScreenDistort_Add` (auto-expires after `lifetime`); the rest is engine lifecycle.
- **VFX rendering:** manager batches enter `VFXRender_BeginPass(BODY/EMISSION)` once per frame-wide pass. Standalone particle/ribbon/mesh draws use `VFXRender_BeginAppearance` or `VFXRender_BeginDraw`; the scope owns target, blend, depth-write and mandatory flushes. Never call `ScreenDistort_BeginVFX*` or hand-roll blend/depth state in feature code.
- **Custom shader textures:** bind via `SetShaderValueTexture`, not `rlActiveTextureSlot`/`rlEnableTexture` — see `LANDMINES.md`.
- **Cooldowns** are keyed `(skillIndex, agentId)`; call `SkillManager_TriggerCooldown` at cast, `SkillManager_CanCast` to gate.
- **Composition rule:** element colors/gradients/force-fields come from `VFX_Material(VC_MAT_*)`; motion math (orbit/ring/jitter/breathe) from `vc_motion.h`. Assemble new `VFX_Compose*` from material + motion + primitives — hard-coded colors only for deliberate identity breaks, with a comment.
- **`CameraFX_Shake` defaults to 0** — never add camera shake to a default skill; expose it as a tunable defaulting to 0.

## Element colors
`ELEMENT_COLOR_{WATER,WOOD,FIRE,EARTH,METAL,TAIJI}` (see `core/presets/vfx_presets.h`). Use these, not ad-hoc RGB, except for deliberate identity breaks.

---

## Signature Index (by header)
PREAMBLE

for h in "${HEADERS[@]}"; do
  [ -f "$h" ] || continue
  echo ""
  echo "### \`$h\`"
  p=$(protos "$h")
  if [ -n "$p" ]; then printf '```c\n%s\n```\n' "$p"; else echo "_Inline helpers / macros only — see header._"; fi
  e=$(enums "$h"); [ -n "$e" ] && echo "**Enums:** $(echo "$e" | paste -sd '; ' -)"
  s=$(structs "$h"); [ -n "$s" ] && echo "**Structs** (fields in header): $(echo $s | sed 's/ *$//; s/ /, /g')"
done
