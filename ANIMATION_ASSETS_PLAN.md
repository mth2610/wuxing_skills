# ANIMATION ASSETS PLAN — Player / Monster (Minion) / Boss

> Asset prep sheet for the user (Thang). The engine is already ready to accept more clips — just
> export following the conventions below then tell Claude to wire the new slot.

## Technical conventions (mandatory — per `character/character_model.c`)

- **Format**: GLB, all animations bundled in ONE model file
  (like the current `assets/characters/player.glb` — use
  `scripts/combine_character_glb.py` to merge if exported separately).
- **Sample rate**: the engine plays at `ANIM_FPS = 60` — export baked at 60fps
  (30fps still runs but will play twice as fast — avoid this).
- **Clip naming**: matched by **substring, case-insensitive** (e.g. slot "punch" matches
  `Punching_Fast` too). The suggested names below are already safe.
- One-shot clips (attacks, dodges, death) should have a start pose ≈ the idle pose to avoid a pop
  when cutting in; the engine auto-stretches clip speed to the gameplay duration
  (`CharacterModel_TriggerAttackTimed`) — just author the clip's own "good rhythm."

## 1. PLAYER (player.glb — already has: idle, walking, running*, punching, kicking, palming, casting)

(*`running` is already in the file but the engine doesn't use it yet — will be wired once real
dash/run exists.)

| Priority | Clip name | Used for | Timing notes |
|---|---|---|---|
| ★★★ | `dashing` | Movement-tech dash (Shift) | 0.3–0.4s, body lunges forward, robe/scarf flowing — engine plays it with an afterimage |
| ★★★ | `jumping` | Jump + airborne (Space) | 3 beats in 1 clip: push off ground → hang in the air (the middle segment should loop well) → land |
| ★★★ | `hitreact` | Getting hit | 0.2–0.3s flinch, keep it light since hits land repeatedly |
| ★★★ | `dying` | Out of HP / falling off the ring | 1–1.5s collapse; the ring-out death uses just the start of the clip |
| ★★ | `meditating` | Meditation (G) | Cross-legged sitting, 2–3s loop, with a breathing rhythm |
| ★★ | `casting_heavy` | Big skills / Thái Cực Lôi | Slower, heavier two-arm sweep than the normal `casting` |
| ★★ | `victory` | Match win (VICTORY screen) | Short loop, hands clasped/stroking a beard, cultivator-style |
| ★ | `strafe_left` / `strafe_right` | Sidestepping while locked on target | Prep for auto-target-lock circling |
| ★ | `taiji_enter` | Entering the Thái Cực state | 1s arms spread, timed with the screen going black-and-white |

## 2. MONSTER / MINION (no model yet — currently drawn as a procedural sphere + spin ring)

One shared `minion.glb` model, recolored per element via tint (engine handles this automatically):

| Priority | Clip name | Used for | Notes |
|---|---|---|---|
| ★★★ | `idle` | Hovering in place | Loop, pulsing like a spirit |
| ★★★ | `walk` | Trudging toward the enemy boss | Loop, speed matched to 2 m/s |
| ★★★ | `windup` | 0.3s before self-detonating | Swells up + shakes — the player's dodge cue (No Tutorial) |
| ★★ | `dying` | Killed before it can explode | Collapses/dissolves over 0.4s |
| ★ | `spawn` | Emerging from the boss | 0.5s condensing out of smoke |

Suggested design: a small will-o'-the-wisp spirit, big head, no legs (flying avoids needing legs),
matching the night "ghost lantern" mood of the art direction.

## 3. BOSS — HẮC DIỆN TÔN GIẢ (no model yet — currently procedural)

If a `boss_hac_dien.glb` model is made (one model, element swapped via tint + the existing rune VFX):

| Priority | Clip name | Used for | Notes |
|---|---|---|---|
| ★★★ | `idle` | Hovering, breathing | 3–4s loop, robe/energy flowing |
| ★★★ | `casting` | Firing a skill per phase (every 3s) | 0.8–1s arm extension — lets the player see it coming and dodge |
| ★★★ | `phaseshift` | Phase transition / element change | 1.5s tensing + energy burst, timed with the rune color change |
| ★★ | `hitreact` | Taking a heavy hit / losing a shield | Short 0.2s, the boss shouldn't flinch much |
| ★★ | `summon` | Summoning a minion wave | 1s arms spread, summoning |
| ★★ | `dying` | Defeat — VICTORY screen | 2–3s dramatic dissolution, should feel earned |
| ★ | `taiji_rage` | Below 30% HP, entering Thái Cực | A fiercer idle loop, replaces `idle` in the final phase |

Suggested design: tall, gaunt humanoid, blank black mask (matches the name Hắc Diện/"Black Face"),
long arms — the lower body could be smoke (avoids leg rigging, fits the levitating look).

## Suggested build order

1. Player: `dashing`, `jumping`, `hitreact`, `dying` — these 4 clips improve combat feel the most.
2. Minion model + `idle/walk/windup` — the monster is currently a plain sphere; replacing it early
   makes the arena feel far more alive.
3. Boss model + the ★★★ set — done last since the current procedural version is still acceptable
   for now.

Once a clip is exported, drop it into `assets/characters/` and say so — wiring it into the engine
(a new slot in `character_model.h` + triggering it in the right place) is Claude's job.
