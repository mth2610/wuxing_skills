#version 330

// Stone Spear dissolve/glow fragment shader.
// Erodes the mesh tip-first -> tail-last as u_dissolve goes 0 -> 1, using
// the vertex UV.v that stone_spear_skill.c bakes along the shaft (v=0 tip,
// v=1 tail). A thin emissive rim is drawn right at the erosion edge so the
// stone visibly crumbles/glows rather than just clipping away.

in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_dissolve; // 0 = fully solid, 1 = fully eroded
uniform float u_time;

out vec4 finalColor;

// Cheap hash-based noise so the erosion edge isn't a perfectly straight line.
float Hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    // Base stone color: darker, cool grey-brown at the tail, slightly
    // brighter near the tip to read as a carved point.
    vec3 stoneDark  = vec3(0.34, 0.30, 0.27);
    vec3 stoneLight = vec3(0.62, 0.55, 0.47);
    vec3 baseColor = mix(stoneLight, stoneDark, fragTexCoord.y);

    // Simple fake-lighting from the vertex normal so the facets of the
    // hand-built bipyramid actually read as faceted rock, not a flat tint.
    vec3 lightDir = normalize(vec3(0.4, 0.8, 0.3));
    float ndotl = max(dot(normalize(fragNormal), lightDir), 0.0);
    vec3 litColor = baseColor * (0.45 + 0.55 * ndotl);

    // Jittered erosion edge: each fragment erodes at u_dissolve +/- small
    // per-fragment noise offset, so the boundary looks chipped, not laser-cut.
    float edgeNoise = (Hash21(fragTexCoord * 37.0) - 0.5) * 0.08;
    float erosionPoint = fragTexCoord.y - edgeNoise;

    if (erosionPoint < u_dissolve) {
        discard; // already eroded away at this point along the shaft
    }

    // Emissive glow band right at the crumbling edge.
    float edgeDist = erosionPoint - u_dissolve;
    float glowBand = 1.0 - smoothstep(0.0, 0.06, edgeDist);
    vec3 glowColor = vec3(1.0, 0.65, 0.25) * glowBand * (0.8 + 0.2 * sin(u_time * 18.0));

    vec3 finalRGB = litColor + glowColor;
    finalColor = vec4(finalRGB, 1.0);
}
