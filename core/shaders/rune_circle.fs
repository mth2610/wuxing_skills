#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// ── VFX_ComposeRuneCircle — one quad, analytic coverage ──────────────────────
//
// WHY THIS IS NOT RIBBON GEOMETRY ANY MORE. The previous composition ran 28
// DrawRibbonStripEx calls (4 rings x 2 sub-layers x 2 passes, plus 12 focus
// polygons), each ending in a forced batch flush, to draw a path that cos/sin
// gives for free. That cost bought nothing, and it cost one thing that cannot
// be bought back: a ribbon is GEOMETRY, so a stroke narrower than a pixel is
// hit or missed by the rasteriser, never partially covered. Measured strokes
// were 1-3 px, which is why the ring read as a dotted wireframe that crawled
// when it turned. Here every edge is an analytic distance filtered against its
// own screen-space footprint, so a stroke thinner than a pixel resolves as a
// FAINT CONTINUOUS line instead of a bright broken one, at any camera distance,
// with no MSAA and no minimum-width hack in the vertex stage.
//
// WHAT MADE IT LOOK DEAD, AND WHAT REPLACES IT. Three separate failures, all
// visible in the 24/08 matrix capture:
//   1. cover% was 1.5% — the whole effect was stroke, no fill anywhere, so
//      there was no area for bloom to bleed from. A drawing, not a light.
//      Fixed by `veil` (a boiling interior), `halo` (a wide skirt outside the
//      rim) and `core`.
//   2. The only motion was `0.75 + 0.25*sin(...)`, a 25% brightness wobble
//      spread evenly round each ring. Evenly-spread modulation reads as a
//      slightly uneven ring, never as charge moving through one. Replaced by
//      tight comet heads (`Sweep`) that travel, and by radial pulses that
//      leave the centre and die at the rim.
//   3. Hue never left the material tint, so nothing ever looked HOT. Luminous
//      things ramp toward white at their peak; `u_hotColor` is that top of the
//      value ramp and `hot` is where it gets spent.
//
// COVERAGE AND EMISSION ARE SEPARATE ON PURPOSE (BRIGHT_BACKGROUND_VFX_SPEC 5).
// `coverage` is what OCCLUDES the scenery and carries the elemental pigment —
// it is the only reason this effect survived a white background when the two
// other disc-shaped composers vanished. `emit` is light added on top and is
// deliberately allowed to be large where coverage is zero (the halo), because a
// glow must not grey out the ground it is glowing over.

uniform vec4 u_bodyColor;   // elemental pigment, contrast-resolved on the CPU
uniform vec4 u_glowColor;   // elemental emission
uniform vec4 u_hotColor;    // top of the value ramp: where the effect reads hot
uniform vec4 u_params;      // x fade, y emission gain, z open, w premultiply
uniform vec4 u_style;       // x width, y energy, z coverage gain, w seed
uniform vec3 u_spin;        // outer / mid / inner rotation, PRE-WRAPPED to [0,2pi)
uniform vec3 u_sweep;       // three comet-head angles, PRE-WRAPPED
uniform vec2 u_pulse;       // two outward pulses, 0..1
uniform vec2 u_flow;        // interior advection, PRE-WRAPPED

const float TAU = 6.28318530718;

// Every phase above arrives ALREADY FOLDED into its period. Folding here
// instead would defeat the point: `fract()` cannot recover precision that the
// float lost before it (ENGINE_LANDMINES, "fract() for float precision: fold
// each product ONCE, never nest"), and u_time reaches four digits in a match.

vec2 Rot(vec2 p, float a)
{
    float c = cos(a);
    float s = sin(a);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// ── Seam-free polar noise ───────────────────────────────────────────────────
//
// fbm2(vec2(angle, radius)) LOOKS like the obvious way to put noise on a disc
// and prints a hard radial line down one side of it: atan() jumps by 2*pi at
// its branch cut, so the field is discontinuous there. Walking the angle around
// a CIRCLE in the noise domain instead closes the loop by construction, and the
// radius goes on the third axis. Same construction ground_aura.fs already uses
// ("dùng góc cos/sin cho trục XY và bán kính r cho trục Z"); reused here rather
// than reinvented.
float PolarFbm(float a, float rr, float radialScale, float scale)
{
    return fbm3(vec3(cos(a), sin(a), rr * radialScale) * scale);
}

// Value noise around the ring only, on a lattice whose index WRAPS. Two hashes
// where PolarFbm costs twenty-four, and radial rays want exactly this: high
// angular frequency, no radial structure at all.
float RingNoise(float a, float count, float seed)
{
    float x = a / TAU * count;
    float i = floor(x);
    float f = fract(x);
    float u = f * f * (3.0 - 2.0 * f);
    return mix(hash2(vec2(mod(i, count) + seed, 3.7)),
               hash2(vec2(mod(i + 1.0, count) + seed, 3.7)), u);
}

// ── Coverage AND core, from one distance ────────────────────────────────────
//
// .x is the antialiased coverage of a band of half-width `hw`. The clamp is the
// trick that replaced ribbon geometry: below half a pixel the band widens to
// the pixel and its amplitude is scaled by exactly the ratio it widened by, so
// the light it puts on screen does not depend on how thin it got. A rasteriser
// cannot do that for a sub-pixel triangle — it hits or misses — which is why
// the ribbon version broke into dashes.
//
// .y is the CORE: how close to the centreline, weighted by that same coverage.
// .z is the AURA: a wide, soft, uncounted skirt hugging the same stroke.
//
// THE THREE TOGETHER ARE THE "BRIGHT CORE, COLOURED RIM" PROFILE, and it needs
// all three to read. A stroke drawn as coverage alone is a coloured line no
// matter how bright it is, because every fragment of it is the same colour and
// the same intensity — which is what made the first pass of this shader look
// like clean vector art. A filament of light instead has a centre that has run
// out of colour, shoulders that still have it, and a wash of hue around it that
// the centre is throwing off. Deriving the core from a second, narrower Band()
// was the other option and is worse: a narrower band is itself sub-pixel on
// every thin stroke, so the core would vanish exactly where it is most wanted.
// Taken off the same distance, all three survive down to one pixel.
vec3 Band3(float x, float c, float hw, float px)
{
    float fw = max(px * 0.5, 1e-6);
    float w  = max(hw, fw);
    float k  = hw / w;
    float d  = abs(x - c);
    float cov = k * (1.0 - smoothstep(w - fw, w + fw, d));
    float prof = 1.0 - clamp(d / w, 0.0, 1.0);
    float aura = k * (1.0 - smoothstep(w * 0.4, w * 3.6, d));
    return vec3(cov, cov * prof * prof * prof, aura);
}

float SegDist(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

vec3 Stroke3(vec2 p, vec2 a, vec2 b, float hw, float px)
{
    return Band3(SegDist(p, a, b), 0.0, hw, px);
}

// Signed distance to a square OUTLINE, so the focus star costs two Band2 calls
// instead of eight ribbon segments — and has no corners to mitre. A ribbon
// through polygon vertices pinches to cos(theta/2) of its width at every
// corner; an SDF has uniform thickness all the way round by construction.
float BoxSDF(vec2 p, float s)
{
    vec2 d = abs(p) - vec2(s);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// The pixel footprint of one angular cell, in cell units. Derived, never taken
// from fwidth(): a cell coordinate is built on fract() and its derivative is
// garbage at every cell boundary, which would print a bright seam per cell.
float CellPx(float radius, float count, float px)
{
    return px * count / (TAU * max(radius, 1e-4));
}

// A tight head of light running around a ring. `tight` sets how compact it is.
float Sweep(float ang, float head, float tight)
{
    return pow(0.5 + 0.5 * cos(ang - head), tight);
}

// ── One rune ────────────────────────────────────────────────────────────────
//
// Drawn, not sampled. The authored sheets this replaced were 128x4096 with ink
// in only columns 25..81, mapped so 4096 texels of glyph landed on ~1100 px of
// circumference with no mip chain: every glyph minified into the same grey
// smear, which is why the reference capture showed a ring of identical boxes.
// Strokes generated here are resolution-independent and cost no texture unit.
//
// The vocabulary is deliberately narrow — a spine, up to three crossbars, one
// hook — because that is what makes a set of marks read as ONE writing system
// rather than as noise. Randomising stroke count and angle freely produces
// scribbles that look procedural, which is the failure this shape avoids. The
// variation that keeps it from looking STAMPED goes somewhere else: per-rune
// weight, tilt and offset, applied by the caller before the strokes are laid.
vec3 GlyphCell(vec2 g, float id, float px)
{
    float hw = 0.082 + 0.030 * hash2(vec2(id, 41.7));
    vec3 ink = Stroke3(g, vec2(0.0, -0.64), vec2(0.0, 0.64), hw, px);

    for (int k = 0; k < 3; k++)
    {
        float fk = float(k);
        float ha = hash2(vec2(id * 3.7 + fk, 5.21));
        float hb = hash2(vec2(id * 1.9 - fk, 9.77));
        float hc = hash2(vec2(id + fk * 7.3, 2.13));

        float present = step(0.24, ha);
        float y = -0.44 + fk * 0.44;
        float ext = 0.26 + 0.34 * hc;
        float tilt = (hc - 0.5) * 0.26;

        float xa = (hb < 0.30) ? -ext : ((hb < 0.62) ? 0.0 : -ext);
        float xb = (hb < 0.30) ? 0.0 : ext;

        ink = max(ink, present * Stroke3(g, vec2(xa, y - tilt),
                                            vec2(xb, y + tilt), hw, px));
    }

    float hh = hash2(vec2(id * 5.5, 17.1));
    float hs = hash2(vec2(id * 2.3, 23.9));
    float hy = (hh < 0.5) ? 0.64 : -0.64;
    float hx = (hs < 0.5) ? -0.36 : 0.36;
    ink = max(ink, step(0.30, hs) *
                   Stroke3(g, vec2(0.0, hy), vec2(hx, hy - sign(hy) * 0.24),
                           hw, px));

    return clamp(ink, 0.0, 1.0);
}

// A band of runes around one ring.
//
// EVERY RUNE IS SET BY HAND, not stamped. A ring of identically weighted,
// identically placed, identically bright marks at a perfectly even pitch is the
// single loudest "this was generated" signal the effect can send, and no amount
// of erosion on top hides it. So each cell gets its own tilt, its own radial
// and tangential offset, and its own resting brightness before a single stroke
// is drawn.
vec3 GlyphRing(float r, float ang, float px,
               float bandCentre, float bandHalf, float count, float spin,
               float seed)
{
    float ca = (ang + spin) / TAU * count;
    float cell = mod(floor(ca), count);
    float id = cell + seed;

    float jx = (hash2(vec2(id, 51.3)) - 0.5) * 0.26;
    float jy = (hash2(vec2(id, 67.9)) - 0.5) * 0.22;
    float rot = (hash2(vec2(id, 83.1)) - 0.5) * 0.30;
    float weight = 0.62 + 0.38 * hash2(vec2(id, 95.7));

    float gy = (r - bandCentre) / bandHalf;
    float gx = (fract(ca) - 0.5) * (TAU * bandCentre / count) / bandHalf;
    float gpx = px / bandHalf;

    vec2 g = Rot(vec2(gx - jx, gy - jy), rot);

    // Hard-clip to the band on the UNJITTERED coordinate, so a nudged rune is
    // trimmed by the ring rather than allowed to wander out of it.
    float inside = step(abs(gy), 1.0);
    return GlyphCell(g, id, gpx) * inside * weight;
}

void main()
{
    // fragTexCoord arrives in rune radii, signed, so the quad's own centre is
    // the origin and no world/view-space confusion is possible.
    vec2 p0 = fragTexCoord;
    float px0 = max(max(fwidth(p0.x), fwidth(p0.y)), 1e-6);

    // The open curve scales the whole construction. The footprint scales with
    // it, or the antialiasing stops matching the pattern it is filtering and
    // the rings shimmer exactly when they move.
    float inv = 1.0 / max(u_params.z, 0.05);
    vec2 p = p0 * inv;
    float px = px0 * inv;

    float ang = atan(p.y, p.x);
    float w = u_style.x;
    float energy = u_style.y;
    float seed = u_style.w;

    // ── the fields ──────────────────────────────────────────────────────────
    // `ero` is ANCHORED TO THE OUTER RING's frame. That is not a detail: a bite
    // mark has to travel with the mark it is eating. Sampled in world or screen
    // space instead, the runes swim through a stationary field and the whole
    // assembly reads as a picture sliding behind a dirty pane.
    float aRing = ang + u_spin.x;
    float rRaw = length(p);
    // The drift is a SINE of the wrapped phase, not the phase itself. Every
    // rotating value here arrives folded into [0, 2pi) so it never loses float
    // precision — but a folded value used as an additive offset into a noise
    // DOMAIN jumps by a full period each time it wraps, and the whole field
    // pops. sin() of it is bounded and continuous, and for erosion a field that
    // breathes back and forth reads better than one that crawls one way anyway.
    float ero = PolarFbm(aRing, rRaw + sin(u_flow.y) * 0.055, 2.30, 3.40);

    // THE CIRCLE IS NOT A PERFECT CIRCLE. One low-frequency radial wobble
    // applied to r BEFORE any layer reads it, so every ring, band and dash
    // wobbles together and the construction stays internally consistent —
    // wobbling each layer separately is what makes concentric rings look like
    // they were drawn by different hands.
    float r = rRaw + (ero - 0.44) * 0.020 * energy;

    // ── erosion ─────────────────────────────────────────────────────────────
    // Two different thresholds on one field. The keylines are eaten GENTLY: a
    // structural line bitten in half reads as broken, not as energetic. The
    // runes are eaten hard, because a half-consumed glyph is exactly the thing
    // that stops a ring of marks looking printed.
    // THREE SCALES, because one is a texture and three is a history. The fbm
    // bites the edges, the grain speckles them, and the sector term is what
    // actually reads in a still frame: whole arcs of the ring sitting dimmer
    // than their neighbours, which is the difference between "a line with noise
    // on it" and "a line that is unevenly charged".
    float grain  = RingNoise(aRing * 3.0, 97.0, 11.0);
    float sector = RingNoise(aRing + sin(u_flow.x) * 0.25, 7.0, 71.0);
    float field = ero * 0.52 + grain * 0.18 + sector * 0.30;
    float biteLine = mix(1.0, smoothstep(0.12, 0.62, field), 0.46 * energy);
    float biteMark = mix(1.0, smoothstep(0.26, 0.68, field), 0.62 * energy);

    vec3 lineLC = vec3(0.0);
    vec3 markLC = vec3(0.0);

    // ── keylines ────────────────────────────────────────────────────────────
    lineLC = max(lineLC, Band3(r, 1.000, 0.0090 * w, px));
    lineLC = max(lineLC, Band3(r, 0.966, 0.0038 * w, px) * 0.75);
    lineLC = max(lineLC, Band3(r, 0.836, 0.0062 * w, px));
    lineLC = max(lineLC, Band3(r, 0.470, 0.0055 * w, px));
    lineLC = max(lineLC, Band3(r, 0.196, 0.0078 * w, px));

    // ── rune bands, turning against each other ──────────────────────────────
    markLC = max(markLC, GlyphRing(r, ang, px, 0.901, 0.052 * w, 28.0,
                                   u_spin.x, seed));
    markLC = max(markLC, GlyphRing(r, ang, px, 0.572, 0.038 * w, 17.0,
                                   u_spin.z, seed + 63.0) * 0.92);

    // ── dashed ring ─────────────────────────────────────────────────────────
    // Duty varies per dash. An even pitch of identical dashes is a dotted line
    // in a diagram; an uneven one is something that was inscribed.
    {
        float count = 64.0;
        float x = (ang + u_spin.y) / TAU * count;
        float duty = 0.20 + 0.20 * hash2(vec2(mod(floor(x), count), 13.9));
        float cpx = CellPx(0.706, count, px);
        float dash = Band3(fract(x), 0.5, duty, cpx).x;
        lineLC = max(lineLC, dash * Band3(r, 0.706, 0.0105 * w, px));
    }

    // ── radial ticks ────────────────────────────────────────────────────────
    {
        float count = 56.0;
        float x = (ang - u_spin.y) / TAU * count;
        float len = 0.62 + 0.55 * hash2(vec2(mod(floor(x), count), 29.1));
        float cpx = CellPx(0.782, count, px);
        float tick = Band3(fract(x), 0.5, 0.13, cpx).x;
        lineLC = max(lineLC, tick * Band3(r, 0.782, 0.022 * w * len, px) * 0.85);
    }

    // ── focus star: two squares 45 degrees apart ────────────────────────────
    float bw = 0.0072 * w;
    vec3 star = Band3(BoxSDF(Rot(p, u_spin.z), 0.318), 0.0, bw, px);
    star = max(star, Band3(BoxSDF(Rot(p, u_spin.z + 0.78539816), 0.318),
                           0.0, bw, px));
    lineLC = max(lineLC, star);

    lineLC *= biteLine;
    markLC *= biteMark;

    // A SECOND, FINER BITE ON THE CORES ONLY. A stroke that is being consumed
    // loses its hot centre before it loses its body — the same way a filament
    // browns out in patches before it breaks — so eroding coverage alone leaves
    // an evenly-lit line with ragged edges, which still reads as machine-made.
    // Taken from the cheap ring lattice, at a frequency well above the field
    // above, so the two do not beat against each other.
    float flick = 0.34 + 0.66 * RingNoise(aRing * 7.0, 211.0, 37.0);
    lineLC.y *= mix(1.0, flick, 0.70 * energy);
    markLC.y *= mix(1.0, flick, 0.62 * energy);

    float line = lineLC.x;
    float mark = markLC.x;

    // ── the interior ────────────────────────────────────────────────────────
    // Held INSIDE the rings. Letting it reach the rune bands put a warm wash
    // over the marks, and on a bright plate a washed mark stops being darker
    // than the scenery, which is the only thing making it visible there.
    float aFlow = ang + u_flow.x;
    float boil = PolarFbm(aFlow, r + sin(u_flow.y * 0.7) * 0.14, 1.55, 2.10);

    // SQUARED, and with almost no constant term. An earlier draft used
    // (0.26 + 0.62 * boil), which is a fill with a slight texture on it: it
    // raised cover% to 15% and drove `detail` from 0.66 down to 0.056, i.e. it
    // bought silhouette by washing out every ring inside it. Squaring turns the
    // same field into filaments — dark nearly everywhere, bright in a few
    // places — so the interior reads as depth the rings sit in front of.
    float inner = 1.0 - smoothstep(0.10, 0.80, r);
    float veil = inner * (0.05 + 0.86 * boil * boil) * energy;

    // Radial rays leaving the core, on the cheap ring lattice: they want high
    // angular frequency and no radial structure, which is all RingNoise is.
    float rayA = RingNoise(ang - u_flow.y * 0.35, 34.0, 5.0);
    float rayB = RingNoise(ang - u_flow.y * 0.35, 13.0, 19.0);
    float rays = pow(clamp(rayA * 0.66 + rayB * 0.60, 0.0, 1.0), 2.6)
               * (1.0 - smoothstep(0.09, 0.86, r)) * energy;

    // ── outward pulses ──────────────────────────────────────────────────────
    float d0 = (r - u_pulse.x * 1.02) / 0.115;
    float d1 = (r - u_pulse.y * 1.02) / 0.115;
    float pulse = exp(-d0 * d0) * (1.0 - u_pulse.x)
                + exp(-d1 * d1) * (1.0 - u_pulse.y);
    pulse *= (0.35 + 0.85 * ero) * energy;

    // ── comet heads ─────────────────────────────────────────────────────────
    float sw = Sweep(ang, u_sweep.x, 22.0) * 1.00
             + Sweep(ang, u_sweep.y, 38.0) * 0.85
             + Sweep(ang, u_sweep.z, 13.0) * 0.55;
    sw *= energy;

    // A head passing over a rune re-ignites the part of it the erosion took.
    float glyphFlare = markLC.x * pow(0.5 + 0.5 * cos(ang - u_sweep.x), 8.0);

    // ── core ────────────────────────────────────────────────────────────────
    float core = exp(-pow(r / 0.105, 2.0));

    // ── halo ────────────────────────────────────────────────────────────────
    // Kept NARROW and weak on purpose. The wide painted version of this read as
    // brown haze rather than as light, for a reason worth writing down: nothing
    // in the effect was crossing the bloom threshold, so every bit of "glow" on
    // screen was this hand-drawn skirt, and a dim orange skirt over black IS
    // brown. The fix is not a bigger skirt — it is to make the keylines and the
    // core bright enough for the post chain's bloom to spread them, and to
    // leave this as a seat under that. Torn by the same field as the ink, so
    // the seat is wispy rather than a clean ellipse.
    //
    // TWO BELLS, both two-sided, and that detail is load-bearing. An earlier
    // draft built its skirts on a gaussian over a CLAMPED distance, which is
    // exactly 1.0 for every r on the clamped side — so a term meant as an
    // inward skirt painted a flat orange field over the entire quad, corners
    // included. A one-sided gaussian written that way has no falloff where it
    // is clamped; it has no shape at all there.
    float hr = (r - 0.985) / 0.115;
    float hb = r / 0.68;
    float halo = (exp(-hr * hr) * 0.34 + exp(-hb * hb) * 0.09)
               * (0.30 + 1.15 * ero) * energy;

    // NOTHING MAY SURVIVE TO THE QUAD EDGE. A polygon boundary drawn across a
    // glow is the one artefact a single-quad effect can still produce, and the
    // bells above are only small out there, not zero.
    float rim = 1.0 - smoothstep(1.10, 1.29, r);

    // THE STROKES ARE PIGMENT FIRST AND LIGHT SECOND. Driving the whole ring
    // bright is what saturated the white plate: emission can only add, so an
    // effect that is uniformly hot has no way to be darker than bright scenery
    // and measures as invisible on it (BRIGHT_BACKGROUND_VFX_SPEC.md 5.7).
    // Keeping the resting term low and spending the budget on the travelling
    // heads means most of the ring stays elemental pigment that bites into a
    // white background, while the part the charge is passing through goes hot.
    float lineLit = line * (1.05 + 1.35 * sw + 0.48 * pulse);
    float markLit = mark * (0.34 + 0.72 * sw) + glyphFlare * 0.95;

    // ── output ──────────────────────────────────────────────────────────────
    // COVERAGE excludes the halo. That is light, and light that occludes turns
    // a glow into a grey smudge on bright ground.
    // THE PLATE: a soft disc of elemental pigment under the inscription, in
    // COVERAGE only. On the night arena it is dark-on-dark and costs nothing;
    // on stone or snow it is the thing that gives the whole circle a ground to
    // sit on instead of leaving the runes floating as isolated marks. Torn by
    // the same boil as the interior so its edge is not a clean disc.
    // 0.34 is measured, not chosen: against the white plate it gives darken
    // 56.6% / absvar 18.3, where 0.25 gives 50.4% / 15.9 and dropping it
    // entirely gives 2.4% / 12.0. The dark-plate figures do not move.
    float plate = smoothstep(1.00, 0.60, r) * 0.34 * (0.62 + 0.58 * boil);

    float coverage = clamp(mark * 0.99 + line * 0.88 + veil * 0.18
                           + rays * 0.16 + core * 0.55 + plate, 0.0, 1.0)
                   * rim * u_params.x * u_style.z;

    // The core term is deliberately the largest coefficient in this sum. Past
    // the tone map an intensely bright saturated colour desaturates on its own,
    // so a stroke whose centreline is driven far harder than its shoulders
    // resolves white in the middle WITHOUT anything mixing white into it, and
    // the shoulders keep full chroma. The `emCol` ramp below then only has to
    // finish the job rather than do it.
    float emit = (lineLit * 0.92
                + lineLC.y * 2.30
                + lineLC.z * 0.62
                + markLit * 0.62
                + markLC.y * 1.15
                + markLC.z * 0.50
                + halo * (0.34 + 0.62 * sw)
                + veil * 0.42
                + rays * 0.46
                + pulse * 0.62
                + core * 1.20) * rim * u_params.x;

    // BRIGHT CORE, COLOURED RIM. `coreness` is the fraction of this fragment's
    // coverage that sits on a stroke's centreline, so a stroke resolves white
    // down its middle and keeps the element's hue on its shoulders — the thing
    // that makes a drawn line read as a filament of light rather than as a
    // coloured line. The core of a hot spot (the centre, a comet head) is
    // folded in on top of it.
    float coreness = clamp((lineLC.y * 1.55 + markLC.y * 1.05)
                           / max(line * 0.9 + mark * 0.7, 1e-3), 0.0, 1.0);
    float hot = clamp(coreness * (0.78 + 0.55 * sw)
                    + core * 0.95 + pulse * 0.40, 0.0, 1.0);
    vec3 emCol = mix(u_glowColor.rgb, u_hotColor.rgb, hot * hot);

    // The runes keep the material's own pigment; only a stroke's core is
    // allowed to bleach toward the emission colour.
    vec3 bodyCol = mix(u_bodyColor.rgb, u_glowColor.rgb,
                       clamp(coreness * 0.55 + lineLit * 0.18, 0.0, 1.0));

    // ONE formula per blend state, never one for both (ENGINE_LANDMINES,
    // "BLEND_ALPHA is NOT premultiplied"). The consumer sets the blend that
    // matches the branch it asked for through u_params.w.
    if (u_params.w > 0.5)
        // The emission pass carries NO pigment: the body pass already laid it
        // down, and adding it twice is how a two-pass effect ends up looking
        // washed out. What it does carry is coverage, because premultiplied
        // blending is (ONE, ONE_MINUS_SRC_ALPHA) and that (1 - a) term is the
        // only reason a glow is still visible against a white background
        // instead of saturating into it.
        finalColor = VFX_ResolvePremultiplied(vec3(0.0), 0.0, coverage,
                                              emCol, emit, u_params.y);
    else
        finalColor = VFX_ResolveBody(bodyCol, u_params.y, coverage);
}
