# Candidate H — the monotone tone map (NOT applied: it fails a gate by design)

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
