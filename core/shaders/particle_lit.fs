#version 330

// Đợt E / F1 — lit CPU particles. See core/docs/ELDEN_VFX_SPEC.md §0.1b:
// flat-shaded smoke can only ever look like a decal OF smoke. Volume reads
// almost entirely from lighting — a bright rim toward the light, a dark
// occluded core, and a glow when backlit.
//
// NORMAL SOURCE — an analytic hemisphere built from the quad-local UV. The
// first version derived it from dFdx/dFdy of the sprite alpha, which failed in
// the worst possible way: that gradient is ~0 across the flat CORE of a soft
// particle, so the tilt direction collapsed to zero and the normal snapped to
// (0,0,1) exactly where the sprite is brightest and largest. The result read as
// a uniformly slightly-brighter blob, plus quantisation spokes from a 64px
// texture magnified many times. The derivative path survives behind
// u_analyticUV = 0 for SpriteAnim atlases, where fragTexCoord is a sub-rect
// rather than quad-local UV.
//
// Everything is opt-in: at u_lightingStrength = 0 this returns exactly the old
// `texelColor * fragColor` result, so nothing already shipped changes look.

#ifdef GL_ES
precision highp float;
#endif

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;

out vec4 finalColor;

uniform sampler2D texture0;

// NOTE: deliberately does NOT use `colDiffuse`. rlgl only guarantees to push
// that uniform for its own shape/texture draw paths — through a raw
// rlBegin(RL_QUADS) run with a custom shader bound it may never be written, and
// an unwritten GLSL uniform is ZERO, which would multiply the whole batch to
// black. The per-vertex colour already carries the particle tint.

// Environment (set per frame by DrawParticles from environment_system)
uniform vec3 u_sunToLight;   // normalized, surface → sun
uniform vec3 u_sunColor;     // linear-ish 0..1
uniform vec3 u_ambient;      // flat ambient fill
uniform vec3 viewPos;

uniform float u_lightingStrength; // 0 = legacy unlit path (DEFAULT)
uniform float u_scatterStrength;  // forward-scatter / backlit glow
// Debug modes. 0 = off, 1 = screen-space normal, 2 = pure lighting term on an
// OPAQUE quad, 3 = the light vector L. Mode 2 is the one that settles "is this
// shading or just alpha stacking": it throws away both the texture colour AND
// the alpha falloff, so what is left can only be the lighting.
uniform float u_debugNormal;
uniform float u_normalBulge;      // 1 = true hemisphere; >1 exaggerates the dome
// Đợt E — EMISSIVE HDR BOOST. The reason particles had no blown-out core.
//
// Everything upstream is capped at 1.0: the vertex colour is rlColor4ub (8-bit),
// emissiveCurve is applied CPU-side and clamped at 255, and the texture is [0,1].
// So a single emissive sprite could never write more than 1.0 — while the scene
// buffer is R16F and happily holds 10.0. ACES then maps 1.0 to ~0.8, which is
// exactly the bloom threshold, so nothing ever blew out and nothing bloomed.
// The HDR pipeline existed and particles never used its headroom.
//
// Pushing the emissive population above 1.0 is what gives the white-hot core
// with a coloured rim: the tonemapper rolls the excess off to white and bloom
// picks up what is over threshold. Set per BATCH (>1 only for unlit/emissive
// particles); smoke must stay at 1.0 or it would emit light it should occlude.
uniform float u_emissiveBoost;
uniform float u_analyticUV;       // 1 = quad-local UV (default), 0 = derivative fallback
// Đợt E / E4 — atlas grid (cols, rows); (1,1) = not an atlas.
//
// With a SpriteAnim atlas, fragTexCoord is the ATLAS sub-rect (e.g. 0.25..0.375),
// not the quad's 0..1. The analytic hemisphere below reads it as if it were
// quad-local, so it shades from a small off-centre patch of the sphere — and
// that patch JUMPS to a different region every time the animation steps to the
// next cell, which reads as the sprite popping frame to frame. Handing the grid
// over lets the local UV be recovered exactly, so the analytic path (which
// exists because the derivative fallback has a dead core) keeps working.
uniform vec2 u_atlasGrid;
uniform float u_lightAzimuth;     // <0 = use the real sun; >=0 = debug override
uniform float u_sunGain;          // scales the directional term
uniform float u_ambientGain;      // scales the flat fill (LOWER = more contrast)

#define MAX_VFX_LIGHTS 4
uniform int   u_vfxLightCount;
uniform vec3  u_vfxLightPos[MAX_VFX_LIGHTS];
uniform vec3  u_vfxLightColor[MAX_VFX_LIGHTS];
uniform float u_vfxLightRadius[MAX_VFX_LIGHTS];

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec4 base = texelColor * fragColor;

    if (base.a < 0.01) discard;   // fillrate: drop fully transparent edges

    if (u_lightingStrength <= 0.0)
    {
        // EMISSIVE particles take this branch — strength is set to 0 for them
        // per batch — so the HDR boost has to be applied HERE, not only at the
        // lit output below. Missing that is why the first attempt changed
        // nothing despite the uniform arriving: sparks and glints are exactly
        // the population that never reaches the lit path.
        // At boost 1.0 this is still byte-identical to the pre-F1 shader.
        finalColor = vec4(base.rgb * u_emissiveBoost, base.a);
        return;
    }

    // ── Hemisphere normal from the quad-local UV ─────────────────────────────
    // ANALYTIC, not from derivatives. The derivative route shipped first and is
    // kept as the fallback below, but it fails exactly where it matters: dFdx of
    // the sprite alpha is ~0 across the whole flat CORE of a soft particle, so
    // `dir` collapses to zero there and the normal snaps to (0,0,1) — the
    // brightest, largest part of every sprite ends up facing the camera with no
    // directional shading at all. On top of that the source texture is 64px
    // magnified many times over, so the derivative is quantised into visible
    // radial spokes.
    //
    // fragTexCoord is the quad-local UV for every particle that does not use a
    // SpriteAnim atlas, which after the purge is all of them; u_analyticUV lets
    // the caller fall back when an atlas is in play and the UV is a sub-rect.
    // Recover the quad-local UV. fract() of the cell-scaled coord is exactly the
    // position within the cell; guarded so a non-atlas particle (grid 1,1) is
    // untouched — fract(1.0) is 0.0 and would fold the quad's far edge.
    vec2 luv = (u_atlasGrid.x > 1.5 || u_atlasGrid.y > 1.5)
                 ? fract(fragTexCoord * u_atlasGrid)
                 : fragTexCoord;
    vec2  q = luv * 2.0 - 1.0;               // [-1,1] across the quad
    float rr = length(q);
    vec3  n;
    if (u_analyticUV > 0.5)
    {
        // Exact hemisphere: xy is the in-plane offset, z closes it to unit
        // length. Zero texture dependence, so no spokes and no dead core.
        float rc = min(rr, 1.0);
        vec2  xy = (rr > 1e-5) ? (q / rr) * rc * u_normalBulge : vec2(0.0);
        n = normalize(vec3(xy, sqrt(max(1.0 - rc * rc, 0.0))));
    }
    else
    {
        // Derivative fallback for atlas UVs. Reads alpha as a paraboloid height
        // a = 1 - r^2, so r = sqrt(1-a) and the normal is (r*dir, sqrt(a)).
        float a = texelColor.a;
        vec2  g = vec2(dFdx(a), dFdy(a));
        float glen = length(g);
        vec2  dir = glen > 1e-6 ? g / glen : vec2(0.0);
        float r = sqrt(clamp(1.0 - a, 0.0, 1.0));
        n = normalize(vec3(-dir * r * u_normalBulge, sqrt(max(a, 0.0))));
    }

    // dFdx/dFdy are in screen space, so n is too — rebuild a world-space basis
    // from the view vector. Billboards face the camera, so camera right/up are
    // the quad's right/up to a very good approximation.
    vec3 V = normalize(viewPos - fragPosition);
    vec3 upRef = abs(V.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 R = normalize(cross(upRef, V));
    vec3 U = cross(V, R);
    vec3 N = normalize(n.x * R + n.y * U + n.z * V);

    // Debug view: paint the SCREEN-SPACE normal, not the world one. Painting N
    // was useless — its view-direction component dominates, so every fragment
    // resolved to nearly the same colour whether the maths was right or wrong.
    // In screen space a correct dome is unmistakable: red rising left→right,
    // green rising bottom→top, pale blue in the middle. A flat single colour
    // means the normal collapsed and no light tuning will rescue it.
    if (u_debugNormal > 0.5 && u_debugNormal < 1.5)
    {
        finalColor = vec4(n * 0.5 + 0.5, base.a);
        return;
    }

    // ── Sun: half-Lambert wrap ───────────────────────────────────────────────
    // Same convention as surface_lit.fs so particles and characters agree about
    // where the light is. No hard terminator — smoke has none.
    // GAINS, and they are not optional. Raw `ambient + sunColor * wrap` is a
    // MULTIPLIER on the body colour, and in a night arena both terms sit around
    // 0.15-0.45 — so physically-correct maths dims the smoke to a third of its
    // authored brightness and stains it moonlight-blue, instead of shaping it.
    // Light is here to sculpt the puff, not to darken it: gain the sun term up
    // so the lit side lands near 1.0, and pull ambient DOWN to open up contrast
    // (ambient is a constant floor — it is what flattens the shading).
    // Debug: force a horizontal light at a chosen azimuth. Sweeping it MUST
    // sweep the bright side across the sprite. If the bright spot instead stays
    // pinned to the centre, the shading is radially symmetric — which is what
    // happens when the real sun points nearly along the view vector, since then
    // dot(N, L) collapses to n.z and n.z is radially symmetric by construction.
    // That failure looks identical to "lighting is broken" but is not.
    vec3 L = u_sunToLight;
    if (u_lightAzimuth >= 0.0)
    {
        float rad = radians(u_lightAzimuth);
        L = normalize(vec3(cos(rad), 0.25, sin(rad)));
    }

    // Mode 3 — is the uniform even arriving? Paint L. Sweeping the azimuth must
    // change this colour. If it does not, the tunable is not reaching the shader
    // and every other observation is meaningless.
    if (u_debugNormal > 2.5)
    {
        finalColor = vec4(L * 0.5 + 0.5, 1.0);
        return;
    }

    float ndl  = dot(N, L);
    float wrap = pow(ndl * 0.5 + 0.5, 1.5);

    // Mode 2 — pure lighting, opaque. No texture colour, no alpha falloff, so a
    // bright centre here CANNOT be alpha stacking. Each sprite shows as a hard
    // square: one side must be bright and the opposite side dark, and sweeping
    // the azimuth must rotate which side.
    if (u_debugNormal > 1.5)
    {
        finalColor = vec4(vec3(wrap), 1.0);
        return;
    }
    vec3  lit  = u_ambient * u_ambientGain + u_sunColor * u_sunGain * wrap;

    // ── Forward scatter — the backlit glow ───────────────────────────────────
    // The single most convincing volumetric cue: light coming from BEHIND the
    // puff bleeds through it. Peaks when the view vector aligns with the light.
    if (u_scatterStrength > 0.0)
    {
        float backlit = max(0.0, dot(-V, L));
        lit += u_sunColor * pow(backlit, 4.0) * u_scatterStrength;
    }

    // ── VFX point lights — a fireball lighting its own smoke ─────────────────
    for (int i = 0; i < MAX_VFX_LIGHTS; i++)
    {
        if (i >= u_vfxLightCount) break;
        vec3  toL  = u_vfxLightPos[i] - fragPosition;
        float dist = length(toL);
        float att  = clamp(1.0 - dist / max(u_vfxLightRadius[i], 0.001), 0.0, 1.0);
        att *= att;
        if (att <= 0.0) continue;
        float w = pow(dot(N, toL / max(dist, 0.001)) * 0.5 + 0.5, 1.5);
        lit += u_vfxLightColor[i] * w * att;
    }

    // Additive-free: modulate the body colour. Deliberately NOT clamped to 1 —
    // ACES in post_fx rolls the highlights off, and clamping here would flatten
    // exactly the bright rim this whole shader exists to produce.
    vec3 shaded = base.rgb * lit;
    // Boost is 1.0 for lit batches, so this is a no-op for smoke and dust.
    finalColor = vec4(mix(base.rgb, shaded, u_lightingStrength) * u_emissiveBoost, base.a);
}
