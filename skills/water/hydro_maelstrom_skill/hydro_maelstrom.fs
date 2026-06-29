#version 330

/*
 * hydro_maelstrom.fs
 * ─────────────────────────────────────────────────────────────────
 * Water shader for the Hydro Maelstrom vortex funnel & geyser tube.
 *
 * Features:
 *   · Dual-layer caustic interference  (organic rippling light patterns)
 *   · Lambertian wrap-lighting + Fresnel rim glow
 *   · Depth-based darkening (funnel interior looks deep and ominous)
 *   · Sparse emissive crests (~20% surface area, rule §9.4)
 *   · Breathing animated multiplier to keep the mesh feeling alive
 *   · Noise-based edge dissolve with emissive "burn" halo
 *
 * All colors are derived from ELEMENT_COLOR_WATER = (41, 128, 185).
 * No hardcoded unrelated colors.
 */

// ─── Varyings ────────────────────────────────────────────────────
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragPosition;

// ─── Uniforms ────────────────────────────────────────────────────
uniform float     u_time;
uniform float     u_dissolve;
uniform vec3      u_viewPos;
uniform sampler2D texture0;

out vec4 finalColor;

// ════════════════════════════════════════════════════════════════
//  NOISE LIBRARY
//  Uses small multipliers on UV coords to avoid "TV-static" noise
//  artefacts (rule §9.2). FBM composed of 4 octaves.
// ════════════════════════════════════════════════════════════════

float hash2(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float sNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);           // Smoothstep interpolation
    return mix(
        mix(hash2(i),              hash2(i + vec2(1.0, 0.0)), f.x),
        mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x),
        f.y);
}

float fbm(vec2 p) {
    return 0.5000 * sNoise(p)
         + 0.2500 * sNoise(p * 2.07 + vec2(5.20, 1.30))
         + 0.1250 * sNoise(p * 4.11 + vec2(2.10, 8.90))
         + 0.0625 * sNoise(p * 8.19 + vec2(9.35, 4.72));
}

// ════════════════════════════════════════════════════════════════
//  MAIN
// ════════════════════════════════════════════════════════════════

void main() {

    // ── Element colors (derived from ELEMENT_COLOR_WATER) ─────────
    // Base:  rgb(41, 128, 185)  cyan-blue
    // Deep:  darker, desaturated near-black for funnel interior
    // Foam:  near-white with a cool cyan tint
    vec3 cWater = vec3(41.0/255.0, 128.0/255.0, 185.0/255.0);
    vec3 cDeep  = vec3(0.03, 0.07, 0.20);
    vec3 cFoam  = vec3(0.72, 0.92, 1.00);

    // ── Animated dual-layer caustic UVs ───────────────────────────
    // Layer A: spirals in one direction (slow U drift, fast V flow)
    vec2 uv = fragTexCoord;
    vec2 uvA = uv + vec2(sin(uv.y * 5.5 + u_time * 2.2) * 0.045,
                         u_time * -0.30);

    // Layer B: counter-rotates to create interference beating
    vec2 uvB = uv + vec2(-u_time * 0.18,
                         cos(uv.x * 4.8 - u_time * 1.7) * 0.038);

    // FBM noise on both layers, then multiply for caustic crests
    float nA     = fbm(uvA * 2.6);
    float nB     = fbm(uvB * 3.4 + vec2(6.12, 2.34));
    float caustic = nA * nB * 2.4;       // Range ≈ 0..1

    // ── 3D Lighting ───────────────────────────────────────────────
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(u_viewPos - fragPosition);

    // Single key-light from upper-front (matches night arena)
    vec3 L = normalize(vec3(0.30, 1.0, 0.45));

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // Wrap lighting: softens terminator, good for translucent water
    float diffuse = (NdotL * 0.55 + 0.45);

    // Fresnel rim: edges of the silhouette glow brighter
    float fresnel = pow(1.0 - NdotV, 2.6);

    // ── Base material color ───────────────────────────────────────
    // V coordinate encodes depth (0 = rim, 1 = funnel tip / tube base)
    // Use fract() so the animated UV offset doesn't saturate to 1
    float depthFrac = clamp(fract(uv.y) * 0.62, 0.0, 0.55);
    vec3  base      = mix(cWater, cDeep, depthFrac);
    base *= diffuse;

    // Foam highlights at caustic wave-peaks
    float foamMask = smoothstep(0.60, 0.82, caustic);
    base = mix(base, cFoam, foamMask * 0.52);

    // ── Emissive ──────────────────────────────────────────────────
    // Sparse emissive crests: only the top ~25% of caustic values
    // glow → keeps the mesh volumetric (rule §9.4 – sparse mask)
    vec3 emissive = cWater * smoothstep(0.68, 0.90, caustic) * 1.90;

    // Fresnel rim light: pale cyan glow at silhouette edges
    emissive += cFoam * fresnel * 0.58;

    // Breathing multiplier: keeps the mesh feeling alive even while static
    emissive *= (1.22 + 0.13 * sin(u_time * 4.5));

    // ── Final RGB ─────────────────────────────────────────────────
    vec3 rgb = base + emissive;

    // ── Noise-based dissolve mask ─────────────────────────────────
    // Low-frequency noise (small UV multiplier) produces large organic
    // patches rather than pixel-noise (rule §9.2)
    float dNoise = fbm(uv * 3.8 + vec2(u_time * 0.07, 1.52));

    // Smooth threshold: erodes from 0 (intact) → 1 (fully dissolved)
    float alpha = 1.0 - smoothstep(u_dissolve - 0.11,
                                    u_dissolve + 0.11,
                                    dNoise);

    // Fresnel boosts silhouette opacity (thicker perceived edges)
    alpha  = clamp(alpha + fresnel * 0.22, 0.0, 1.0);

    // Multiply by vertex alpha (respects tori color alpha from DrawCoreTorus)
    alpha *= 0.87 * fragColor.a;

    // ── Dissolve edge burn ────────────────────────────────────────
    // Narrow emissive halo right at the dissolving boundary for
    // an energetic "edge evaporation" look
    float edgeLo  = smoothstep(u_dissolve - 0.17, u_dissolve + 0.01, dNoise);
    float edgeHi  = smoothstep(u_dissolve + 0.01, u_dissolve + 0.17, dNoise);
    float edgeBand = edgeLo - edgeHi;
    rgb += cFoam * edgeBand * 2.9;

    finalColor = vec4(rgb, alpha);
}
