#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// ── FILAMENT TRAIL — a swept VOLUME with strands inside it ──────────────────
//
// WHAT THIS REPLACES AND WHY IT IS A REWRITE RATHER THAN A FIX.
//
// VFX_ComposeVolumeTrail put its structure in an authored sheet stretched over
// the droplet's surface. Three things were wrong with that at once and only the
// first is a bug:
//
//   1. All three tube sheets declared `A:opacity` and shipped with NO alpha
//      channel (palette / greyscale PNGs). raylib expands the missing channel
//      to a constant 255, so the sheet painted strands onto a surface that was
//      opaque everywhere. The effect read as crumpled foil because there were
//      no gaps. scripts/validate_vfx_surface_registry.py now catches this class.
//   2. A sheet on a surface is a SURFACE. However good the strands are, they
//      sit on a skin, and the near wall and the far wall carry the same picture
//      at the same place — so nothing reads as being INSIDE anything.
//   3. Repairing (1) alone measured WORSE, repeatedly, and five separate models
//      of why were each disproved by the next measurement. The two-pass layer
//      stack the old effect drew through is a machine I could not predict, and
//      the honest conclusion was to stop reverse-engineering it and own the
//      fragment stage instead.
//
// SO: same mesh, same UVs, same swept path — `ResolveShader` in trail_system.c
// says a trail carrying its own shader keeps it, and that caller owns the
// pairing. One layer, one blend (BLEND_ALPHA_PREMULTIPLY), no sheet at all.
//
// WHAT MAKES IT A VOLUME RATHER THAN A PAINTED TUBE.
//
//   - The strands live in a 3D field sampled along a CYLINDER in noise space,
//     so the near wall and the far wall cut that field at different depths and
//     show DIFFERENT strands. Two sheets of the same picture cannot do this;
//     it is the whole reason the structure had to leave the texture.
//   - Both walls are drawn (tubeSingleSided = false), and the far one is
//     dimmed rather than culled, so looking through the near wall shows the far
//     one behind it.
//   - Optical depth comes from |N.V| — thin where the surface faces you,
//     thick at grazing — which is thickness DERIVED FROM THE GEOMETRY rather
//     than faked by stacking a second wider mesh on top, which is what the old
//     effect's two-layer alpha budget was doing.
//
// UNIFORMS: every value here is one the trail system already pushes to whatever
// shader a trail carries (trail_system.c, the per-group block). Nothing else
// reaches this stage, so the knobs are deliberately mapped onto that set rather
// than invented — an invented uniform would silently read zero.

in vec4 vColor;

// THE NAMES ARE NOT NEGOTIABLE AND THEY ARE NOT `u_*`. trail_system.c caches
// its flow uniforms by literal string in CacheShaderLocs(), and the flow set
// predates the `u_` convention the volume-tube block next to it uses:
//
//     uSpeed  uStrength  uTiling  uDissolve  uMaskTiling  flowTex
//
// Declaring `u_flowSpeed` and friends — which is what the neighbouring
// `u_volRim`/`u_volErode` lookups suggest — compiles, links, and silently reads
// ZERO forever. Verified with a binary probe: the field came out flat because
// its scale uniform was never written, and four different values of the
// composition's knob rendered byte-identically.
uniform float u_flowTime;      // trail clock, already folded by the caller
uniform float uSpeed;          // -> how fast strands travel along the volume
uniform float uStrength;       // -> domain warp: how much the strands curl
uniform float uTiling;         // -> strand frequency along the volume
uniform float uDissolve;       // -> erosion: how much of the field survives
uniform float uMaskTiling;     // -> strand thinness (the ridge power)

const float FIL_TAU = 6.28318530718;

void main()
{
    vec2 uv = fragTexCoord;

    // THE NOISE DOMAIN IS A CYLINDER, not the UV plane. u wraps at 1.0, so
    // sampling 2D noise on the raw UV puts a seam down one side of the tube;
    // walking u around a circle closes it by construction and leaves the
    // length free on the third axis. Same construction the rune circle and the
    // flow shield use for a disc and a sphere.
    float ang = uv.x * FIL_TAU;
    // v grows without bound as the emitter travels, and an unbounded coordinate
    // loses float precision. Folded at a power of two far larger than anything
    // on screen: the field jumps once per ~4 km of travel, which is a trade
    // rather than an artefact.
    float along = mod(uv.y, 4096.0);

    float scale = max(uTiling, 0.25);
    float drift = u_flowTime * uSpeed;
    vec3 dom = vec3(cos(ang), sin(ang), along) * scale
             + vec3(0.0, 0.0, -drift);

    // filaments3 = domain-warped ridged noise (core/shaders/common/noise.glsl).
    // Ridging folds a smooth field's mid-level contour into a sharp crest, so
    // the maxima are connected branching LINES rather than blobs; the warp
    // makes those lines curl instead of running statistically straight.
    float warp = 1.0 + uStrength * 6.0;
    float field = filaments3(dom, warp);

    // Thinness and erosion, both on the trail's own knobs. A ridge crest is
    // broad by nature: the threshold takes its top and the power thins what is
    // left to a line.
    float erode = clamp(uDissolve, 0.0, 0.9);
    float thin = 1.0 + max(uMaskTiling, 0.0) * 3.0;
    float strand = pow(smoothstep(0.34 + erode * 0.45, 1.0, field), thin);
    float haze = pow(smoothstep(0.10, 0.95, field), 1.6);

    // ── VOLUME ──────────────────────────────────────────────────────────────
    // View space, camera at the origin — see the note in filament_trail.vs.
    vec3 nrm = normalize(fragNormal);
    vec3 viewDir = normalize(-fragPosition);
    float ndotv = clamp(abs(dot(nrm, viewDir)), 0.0, 1.0);
    // ABS, not a clamp to zero: the far wall's normal points away, so a signed
    // dot pins the whole rear hemisphere to 0 and it loses its own gradient.
    // What the shading wants is the OBLIQUITY, which is the same on both walls.
    float grazing = 1.0 - ndotv;

    // Optical depth: how much volume a ray crosses at this obliquity. Capped so
    // the silhouette does not go singular.
    float depth = clamp(0.55 / max(ndotv, 0.16), 0.0, 3.2);
    // The far wall is dimmed rather than culled. Culling it is what makes a
    // swept tube read as a solid skin.
    float wall = gl_FrontFacing ? 1.0 : 0.68;
    float rim = pow(grazing, 2.6);

    // ── COVERAGE: transparent carrier, present strands ──────────────────────
    // The base is deliberately small. Both walls composite, so a value of a
    // here lands at roughly 1-(1-a)^2 through the middle of the volume, and the
    // budget is spent on the strands and the rim rather than on bulk opacity.
    float coverage = clamp((0.030 * depth
                          + strand * 0.46
                          + haze * 0.07
                          + rim * 0.26) * wall, 0.0, 1.0);
    coverage *= vColor.a;

    // ── COLOUR: pigment in the body, white only in the core ─────────────────
    // vColor.rgb is the trail's own ramp, so the element's identity arrives
    // here already resolved. A strand's CORE bleaches toward white; its
    // shoulders keep the ramp. Tinting the whole strand white is what makes an
    // effect invisible against bright scenery, which the rune circle paid for.
    // HEADROOM. Measured at the first working build: mean luminance 195/255
    // across the whole volume at every scale and thinness the knobs could
    // reach, i.e. saturated — and a saturated body has nothing left for a
    // strand to be brighter THAN, so no amount of frequency tuning produces
    // contrast. Same failure the energy orb had, from the same cause. The body
    // is dropped hard and the budget is spent on the strands instead.
    vec3 pigment = vColor.rgb;
    vec3 hot = mix(pigment, vec3(1.0), 0.72);
    float core = pow(strand, 1.8);
    vec3 bodyCol = mix(pigment * 0.18, pigment * 0.70,
                       clamp(haze + strand, 0.0, 1.0));
    vec3 emCol = mix(pigment, hot, clamp(core * 0.85 + rim * 0.25, 0.0, 1.0));

    float emit = (strand * 0.85
                + core * 0.75
                + haze * 0.06
                + rim * 0.22) * wall * vColor.a;

    // ONE pass, one contract. BLEND_ALPHA_PREMULTIPLY is (ONE,
    // ONE_MINUS_SRC_ALPHA), so this resolver hands over premultiplied colour
    // and the (1 - a) term is what still lets the volume bite into bright
    // scenery instead of saturating into it.
    finalColor = VFX_ResolvePremultiplied(bodyCol, 1.0, coverage,
                                          emCol, emit, 1.0);
}
