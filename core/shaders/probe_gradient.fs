#version 330

// GRADIENT PROBE — is a smooth colour ramp still smooth after this pipeline?
//
// WHY THIS EXISTS. The ShieldShell's silhouette and its ground-contact line are
// authored as one continuous white -> yellow -> orange ramp, and they arrive on
// screen as DISTINCT COLOUR PATCHES with visible edges between them. The shell is
// made of fresnel, a matcap, a Beer-Lambert wall and a depth-gap contact term, so
// "the ramp is banded" could plausibly be any of them.
//
// This shader removes every one of those. No geometry, no normals, no depth, no
// texture, no time: a rectangle whose colour is an ANALYTIC function of x. Whatever
// structure survives to the framebuffer was put there by the pipeline the rectangle
// was drawn through (bloom -> exposure -> tone map -> colour grade -> LUT -> vignette
// -> dither -> FXAA), not by any effect.
//
// It is deliberately drawn INTO the HDR scene target (core/post_fx.c mainRenderTex,
// R16G16B16A16), because that is where a VFX writes. A ramp drawn after PostFX_Draw
// skips the entire chain and is the control — see GradientProbe_DrawControl().
//
// FIVE BANDS, each isolating one suspect:
//   0  neutral grey                         achromatic. Bands here = the tone curve or
//                                           8-bit quantisation, nothing hue-related.
//   1  ORANGE at a rising level             ONE hue. This is literally what
//                                           glass_shell.fs's rim computes
//                                           (`rimColor * t * strength`). Bands here
//                                           cannot come from mixing colours, because
//                                           no colours are mixed.
//   2  orange->yellow->white, rising level  the reported look, both at once.
//   3  orange->yellow->white at a CONSTANT  hue sweep only. The tone map's restoration
//      level                                weight depends on PEAK, so at a constant
//                                           peak it is constant and this band must stay
//                                           smooth. Band 2 banded + band 3 smooth
//                                           pins the fault on the LEVEL dependence.
//   4  ONE flat colour, no ramp at all      THE POSITIONAL REFERENCE. Chromatic
//                                           aberration, vignette and FXAA vary with
//                                           screen x and would otherwise be read as
//                                           banding — the first run of this probe
//                                           scored the grey band worst for exactly
//                                           that reason, all of it vignette past the
//                                           clipping point. Whatever structure band 4
//                                           shows is the floor every other band also
//                                           carries; only structure ABOVE it is real.
//
// THE LEVEL RAMP IS LOGARITHMIC, 0.05 -> u_maxHDR. Linear was tried first and wastes
// the probe: the tone map's shoulder does all of its work below exposed peak ~9, and on
// a linear ramp to 12 that whole region is squeezed into the left eighth while seven
// eighths of the rectangle is clipped white. Log spreads the shoulder across the full
// width. It is still strictly monotone and still infinitely smooth, which is all the
// measurement requires.
//
// HOW TO READ IT (GradientProbe_Readback prints all of this)
//   all four smooth                  -> the pipeline is honest, the fault is in the
//                                       values glass_shell.fs computes
//   band 1 banded                    -> the fault is downstream of every VFX; no
//                                       amount of shell authoring can fix it
//   band 0 smooth, 1/2 banded        -> the fault is hue-dependent, i.e. the
//                                       hue-preserving tone map (post_process.fs
//                                       toneMapScene / postfx_hue_restore)
//   band 3 smooth, band 2 banded     -> confirms the level dependence specifically

in vec2 fragTexCoord;
out vec4 finalColor;

uniform float u_bands;     // how many horizontal bands
uniform float u_maxHDR;    // top of the level ramp, scene-linear
uniform float u_minHDR;    // bottom of the level ramp (log ramps cannot start at 0)
uniform float u_flatLevel; // the constant level used by bands 3 and 4

const vec3 ORANGE = vec3(1.00, 0.55, 0.18);
const vec3 YELLOW = vec3(1.00, 0.85, 0.35);
const vec3 WHITE  = vec3(1.00, 1.00, 1.00);

vec3 wyo(float t) {   // orange -> yellow -> white, hue only, unit scale
    return (t < 0.5) ? mix(ORANGE, YELLOW, t / 0.5)
                     : mix(YELLOW, WHITE,  (t - 0.5) / 0.5);
}

void main() {
    float t   = clamp(fragTexCoord.x, 0.0, 1.0);
    float row = clamp(fragTexCoord.y, 0.0, 0.99999);
    float bf  = row * u_bands;
    int   band = int(floor(bf));

    // A black gutter between bands. Not decoration: the readback finds the bands by
    // scanning for these, so the layout cannot drift out of sync with the analyser.
    float inBand = fract(bf);
    if (inBand < 0.10 || inBand > 0.90) { finalColor = vec4(0.0, 0.0, 0.0, 1.0); return; }

    float level = exp(mix(log(u_minHDR), log(u_maxHDR), t));

    vec3 c;
    if      (band == 0) c = vec3(level);
    else if (band == 1) c = ORANGE * level;
    else if (band == 2) c = wyo(t) * level;
    else if (band == 3) c = wyo(t) * u_flatLevel;
    else                c = ORANGE * u_flatLevel;

    finalColor = vec4(c, 1.0);
}
