# Candidate H — APPLIED 18/08/2026. This file is history.

> Candidate H is the shipping curve now: `core/shaders/post_process.fs`, `toneMapScene()`.
> `tonemap_shoulder` was rewritten around the new contract (achromatic surfaces still
> bit-identical, saturated shift under a stated ceiling, chroma still improves, and a new
> monotonicity check that was confirmed red on the pre-H shader). rlvk suite 28/28.
>
> Two things below are now known to be wrong or incomplete:
> * "it fails a gate by design" — the gate encoded a contract that was traded away, and
>   has been replaced rather than defeated.
> * "up to ~0.03 (about 8/255)" is the FIRST failing sample, not the worst. The real worst
>   case below the shoulder is **0.206 at peak 0.98** for a fully saturated colour at
>   `u_hueRestore` 1.0 (0.103 at the shipping 0.5). An achromatic surface moves by exactly
>   zero at every level — that is what confines the change to saturated content.
>
> Full account: root `ENGINE_LANDMINES.md`, "Bounded change and no colour banding are the
> SAME knob"; `core/docs/PROGRESS.md` 2026-08-18g. Kept for the derivation and the exact
> before/after diff.

One replacement inside `toneMapScene()` in `core/shaders/post_process.fs`.
Shader-only, hot-loads, no rebuild.

REPLACE:

    float w = smoothstep(1.0, 2.0, peak) * (1.0 - smoothstep(5.0, 9.0, peak));
    w *= clamp(u_hueRestore, 0.0, 1.0);
    if (w <= 0.0) return perChannel;

    // Tone map the PEAK and carry the channel ratios through unchanged: the hue is
    // whatever it was in scene-linear, only the level is compressed.
    vec3 hueKept = (x / peak) * acesFilmicScalar(peak);
    return mix(perChannel, hueKept, w);

WITH:

    float w = clamp(u_hueRestore, 0.0, 1.0);   // CONSTANT: no intensity-dependent bump
    if (w <= 0.0) return perChannel;
    vec3 hueKept = mix(x / peak, vec3(1.0), smoothstep(5.0, 12.0, peak)) * acesFilmicScalar(peak);
    return mix(perChannel, hueKept, w);

The whitening that used to come from ramping the weight back OUT now comes from
desaturating the hue-kept colour itself, which is monotone in intensity.

Consequence: `rlvk_visual_test` `tonemap_shoulder` goes 27/27 -> 26/27 with
"hue restoration leaked outside its shoulder at peak 0.2 (d 0.03033): the change is
NOT bounded and needs a full whole-scene approval". That is the gate working, not a
bug in it: every material below peak 1 shifts by up to ~0.03 (about 8/255).
