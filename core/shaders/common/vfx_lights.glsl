// ============================================================
// WUXING — VFX point lights, shared GLSL block (Đợt E / E2)
//
// Include in any .fs that should be lit by spell/effect lights, then pair it
// with VFXLight_BindToShader(shader, maxLights) on the C side once per frame.
//
//   (include this file)              <- see vfx_light.h for the literal line
//   color += VFXLights_Accumulate(fragWorldPos, N, albedo);
//
// NOTE: do NOT write the literal include directive anywhere in this file, even
// inside a comment. core/shader_preprocessor.c is a plain text scanner and does
// not skip comments, so an example line makes the file include ITSELF until the
// depth limit trips (8 nested copies, one WARNING, and a shader that only
// compiles because the #ifndef guard collapses the duplicates).
//
// WHY THIS IS SHARED RATHER THAN PER-SHADER. `core/vfx_light.h` has always
// maintained a pool of dynamic effect lights, and until E2 literally nothing
// consumed it — a caster stood in the dark beside their own fireball. The first
// fix wired it into surface_lit.fs alone, which reaches exactly ONE thing in the
// project (the character model). The ground and props are the larger share of
// the screen next to a floor-level effect, so a per-shader copy would have to be
// repeated in every map shader and would drift apart the moment one was edited.
//
// The falloff and the half-Lambert wrap here MUST stay identical to
// core/particles/shaders/particle_lit.fs, or smoke and the ground it sits on will disagree
// about where the light is — which reads as the effect being pasted on rather
// than being in the scene.
// ============================================================

#ifndef VFX_LIGHTS_GLSL
#define VFX_LIGHTS_GLSL

#define MAX_VFX_LIGHTS 4

// vec4 ARRAYS, NOT vec3 — and this is not a style choice. rlvk is a real Vulkan
// backend and packs uniforms into std140 blocks, where an array of vec3 has a
// stride of 16 BYTES (each element padded), while SetShaderValueV(..., VEC3, n)
// uploads 12 bytes per element packed tight. Every element after the first then
// lands at the wrong offset and the data is garbage — silently, with no error.
// The same applies to an array of float (stride 16, not 4).
//
// Packing radius into .w and using vec4 throughout sidesteps the padding
// entirely and is correct under both std140 and desktop GL. `u_vfxLightCount`
// stays a plain int (scalars are not affected).
uniform int  u_vfxLightCount;                        // 0 = disabled (LOW/UNLIT tier)
uniform vec4 u_vfxLightPosRadius[MAX_VFX_LIGHTS];    // xyz = world pos, w = radius (m)
uniform vec4 u_vfxLightColor[MAX_VFX_LIGHTS];        // rgb = colour * intensity, w unused

// Global intensity. NOT cosmetic: the physically-plausible contribution of a
// 3.5 m fire light works out to roughly 5% of surface brightness, which is
// clearly visible at night and completely invisible in daylight — and this
// project has both. The gain is what lets one light read in both, and it is
// hot-reloadable (tuning.cfg → vfx_light_gain).
uniform float u_vfxLightGain;

// Debug visualiser, honoured inside VFXLights_Accumulate so all three consumer
// shaders get it without being edited. Proving a coordinate convention by eye is
// how the rlvk real-shading work lost days; paint the quantity instead.
//   1 = fract(worldPos)      — a correct world position paints a 1 m colour grid
//                              across the surface. A FLAT single colour means the
//                              surface collapsed to one point (typically the
//                              origin, i.e. matModel never arrived).
//   2 = attenuation of light 0 — a bright disc must appear under the effect. If
//                              position is right but this is black, the falloff
//                              or radius is the fault, not the coordinates.
uniform float u_vfxLightDebug;

// Additive contribution for a surface at `worldPos` with normal `N` and colour
// `albedo`. Deliberately NOT clamped — ACES in post_fx rolls the highlights off,
// and clamping here flattens the bright edge that makes a nearby explosion read
// as an explosion.
vec3 VFXLights_Accumulate(vec3 worldPos, vec3 N, vec3 albedo)
{
    if (u_vfxLightDebug > 0.5 && u_vfxLightDebug < 1.5)
        return fract(worldPos) * 4.0;   // x4 so it dominates the surface colour

    // 3 = paint the COUNT alone (grey if it arrived, black if it is 0). Isolates
    //     a scalar int from the arrays — they fail for different reasons.
    if (u_vfxLightDebug > 2.5 && u_vfxLightDebug < 3.5)
        return vec3(float(u_vfxLightCount)) * 2.0;

    // 4 = paint light 0's POSITION as a colour, independent of count. If this is
    //     black while mode 3 is grey, the array upload is the fault (std140
    //     stride), not the scalar.
    // 7 = the DISTANCE from light 0 to this fragment, 0..50 m as black..white.
    //     The one quantity modes 1-6 cannot show: fract(worldPos) proves the
    //     surface has a 1 m gradient but says NOTHING about its ORIGIN, so a
    //     world position translated by 30 m paints an identical, convincing
    //     grid. This mode's black spot is where the surface believes the light
    //     is; if that is not under the light's debug sphere, the two spaces
    //     disagree and every falloff downstream is computing from a wrong `d`.
    if (u_vfxLightDebug > 6.5)
        return vec3(length(u_vfxLightPosRadius[0].xyz - worldPos) * 0.02);

    // 6 = light 0's RADIUS alone (.w of the packed vec4), scaled so a normal
    //     1-8 m radius reads as visible grey. Black here with mode 4 (the .xyz
    //     of the SAME vec4) bright means only the packed component is missing —
    //     a distinction no amount of staring at an unlit floor will give you.
    if (u_vfxLightDebug > 5.5)
        return vec3(u_vfxLightPosRadius[0].w * 0.25);

    // 5 = a CONSTANT. The single most discriminative probe there is: it needs
    //     u_vfxLightDebug to have arrived AND the returned value to reach the
    //     shader's output. A surface that stays unchanged under mode 5 is not
    //     consuming this block at all — every value-level hypothesis (count,
    //     array stride, radius, falloff) is then irrelevant for that surface.
    if (u_vfxLightDebug > 4.5)
        return vec3(1.0, 0.0, 0.0);

    if (u_vfxLightDebug > 3.5)
        return fract(u_vfxLightPosRadius[0].xyz) * 4.0;

    if (u_vfxLightDebug > 1.5)
    {
        if (u_vfxLightCount <= 0) return vec3(0.0);
        float d = length(u_vfxLightPosRadius[0].xyz - worldPos);
        float a = clamp(1.0 - d / max(u_vfxLightPosRadius[0].w, 0.001), 0.0, 1.0);
        return vec3(a * a) * 4.0;
    }

    vec3 acc = vec3(0.0);
    for (int i = 0; i < MAX_VFX_LIGHTS; i++)
    {
        if (i >= u_vfxLightCount) break;
        vec3  toL  = u_vfxLightPosRadius[i].xyz - worldPos;
        float dist = length(toL);
        float att  = clamp(1.0 - dist / max(u_vfxLightPosRadius[i].w, 0.001), 0.0, 1.0);
        att *= att;
        if (att <= 0.0) continue;
        // Half-Lambert wrap — same convention as surface_lit.fs and
        // particle_lit.fs so every surface agrees about the light direction.
        float w = dot(N, toL / max(dist, 0.001)) * 0.5 + 0.5;
        acc += albedo * u_vfxLightColor[i].rgb * (w * w) * att * u_vfxLightGain;
    }
    return acc;
}

// For flat ground where a real normal is not available: assumes an up-facing
// surface, which is what a floor is.
vec3 VFXLights_AccumulateFlat(vec3 worldPos, vec3 albedo)
{
    return VFXLights_Accumulate(worldPos, vec3(0.0, 1.0, 0.0), albedo);
}

#endif // VFX_LIGHTS_GLSL
