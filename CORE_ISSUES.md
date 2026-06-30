# Core Issues / Backlog

Tasks for Core Agent — assign when ready.

## 1. `Cast[Name]Skill` signature inconsistent across CORE_API.md §4 skeleton examples

Found while building the first real skill (Wood "Vine Snare") against the documented pipeline: the skeleton example code in §4 declares `Cast[Name]Skill` as `(Vector3 spawnPos, SkillParams params)`, but the required lifecycle header template (earlier in §4) mandates `(Vector3 startPos, Vector3 target, SkillParams params)`. Skills Agent followed the header template (required for auto-registration via `scripts/generate_registry.py`'s signature matching) and worked around the mismatch correctly this time, but it's a real trap for a less careful future implementation — two parts of the same document disagree on a required signature.

~~Action: audit all four skeleton examples in §4 (Flying, Anchored-Single-Point, Anchored-Along-Path, Entity-Attached) for signature consistency against the header template immediately above them. Fix any that drifted. Use `Edit` with precise `old_string` — `grep -n "^### \|^## " CORE_API.md` first, it's 1200+ lines, never `Write` it wholesale.~~

**RESOLVED 2026-06-30**: Audited all four §4 skeletons. Only Anchored-Single-Point (Ground-Rising) had drifted (`CastExampleRisingSkill(Vector3 spawnPos, SkillParams params)`); fixed to `(Vector3 startPos, Vector3 target, SkillParams params)`, with the rising mesh now spawning at `target` and the cast-windup effect playing at `startPos`. Flying-Projectile, Anchored-Along-Path, and Entity-Attached already matched the template.

## 2. Anchored-Single-Point skeleton's mesh example produces a "robotic" shape — contradicts the document's own Aesthetic Laws

Also found building "Vine Snare": the skeleton drew its rising mesh with a single `DrawCoreCylinder(bottom, top, radiusBottom, radiusTop, slices, color)` call — different top/bottom radii, no segmentation, no jitter. Visually this is a plain truncated cone (frustum), not remotely organic. The Skills Agent followed the skeleton example faithfully and produced exactly this — a straight tapered cylinder for a skill that was supposed to be a vine wrapping around a target.

This directly contradicts §12 "Critical Aesthetic Laws (Anti-Robotic Design)" in the same document:
- §12.2 Perpendicular Jitter — the single-cylinder example has none
- §12.3 Instance Randomization — no randomized scale/yaw/pitch anywhere in the skeleton
- The skeleton is "technically compliant" (uses `DrawCoreCylinder`, an approved procedural mesh function, not a banned raylib primitive) but still reads as robotic because it's used as a single undecorated shape

Impact: any AI agent copying this skeleton verbatim for a new skill — not just Vine Snare — will produce a similarly "robotic" look, even though it follows every individual rule in isolation. The skeleton needs to actually demonstrate the Aesthetic Laws it's sitting next to in the same document, not just satisfy the "no banned primitives" rule.

~~Action: revise the Anchored-Single-Point skeleton's example mesh-drawing code in §4 to show a multi-segment, jittered approach...~~

**RESOLVED 2026-06-30**: `DrawExampleRisingSkill` now stacks `MESH_SEGMENTS` (5) short `DrawCoreCylinder` segments instead of one straight frustum. Each segment is perpendicular-jittered using the §12.2 `perp`/`jitter` pattern (scaled down to ±3.0f for this ~80f-tall mesh vs. the ±12.0f reference value, since the reference is tuned for longer path layouts). Added per-instance `yawOffset` (random jitter axis direction) and `scaleVariance` (85-115%) fields per §12.3, applied to segment height/radius. Added a one-sentence prose note before the code pointing back at §12.2/§12.3. State machine and `SmoothStep01`/`Math_Mix` height animation logic unchanged.

---
*Logged from first real skill build (Vine Snare) against the pipeline, 2026-06-30.*
