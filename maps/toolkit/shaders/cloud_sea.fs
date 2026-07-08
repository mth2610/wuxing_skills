#version 330

// Sea-of-clouds plane for the "floating island ringed by mountains" motif
// every map uses (MAP_API.md). Pure procedural scrolling FBM noise, no
// texture needed. Uses discard for the cloud/gap boundary instead of alpha
// blending — maps/CLAUDE.md's hard rule is Alpha = 255 ALWAYS in the main
// scene (partial alpha breaks particle rendering), so this shader is either
// fully opaque per-fragment or not drawn there at all, never in between.

in vec2 fragTexCoord;

uniform vec2 tiling;
uniform float u_time;
uniform vec4 colDiffuse;

uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;

out vec4 finalColor;

float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

float noise(vec2 st) {
    vec2 i = floor(st); vec2 f = fract(st);
    float a = random(i); float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0)); float d = random(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

// 2 octaves, not 4 — this plane is huge and often covers a lot of the
// screen, and each noise() call is 4 sin()-based random() calls, so this is
// hot per-pixel cost. 2 octaves still reads fine combined with the two
// scrolling layers already blended below; measured to matter for FPS on
// weaker/integrated GPUs (see MAP_API.md's toolkit notes).
float fbm(vec2 st) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 2; i++) {
        v += amp * noise(st);
        st *= 2.0;
        amp *= 0.5;
    }
    return v;
}

void main()
{
    vec2 uv = fragTexCoord * tiling;

    // Two layers scrolling at different speed/direction -> slow roiling look.
    vec2 flow1 = uv + vec2(u_time * 0.02, u_time * 0.008);
    vec2 flow2 = uv * 1.7 - vec2(u_time * 0.015, -u_time * 0.01);
    float density = fbm(flow1) * 0.6 + fbm(flow2) * 0.4;

    // Opaque cutout: below threshold there is no cloud here at all (never a
    // partial-alpha fragment). Threshold/edge band scaled down from the
    // original 0.42/0.55 to match fbm's lower max amplitude now that it's
    // 2 octaves instead of 4 (0.75 vs 0.9375) — keeps roughly the same
    // cloud coverage instead of the plane going mostly-discarded/sparse.
    if (density < 0.33) discard;

    // Kept deliberately darker than a typical lit surface (unlike
    // grass/rock albedo ~0.3-0.5, these were originally 0.55-0.97 and any
    // lighting sum near/above 1.0 clipped the whole range to flat white,
    // losing all density contrast) — the density gradient itself is what
    // should read as "cloud", not the lighting.
    float edge = smoothstep(0.33, 0.44, density);
    vec3 cloudBase = mix(vec3(0.30, 0.33, 0.40), vec3(0.72, 0.75, 0.82), edge);

    vec3 normal = vec3(0.0, 1.0, 0.0);
    vec3 light = vec3(0.0, 1.0, 0.0);
    if (length(lightDir) > 0.1) light = normalize(-lightDir);
    float NdotL = max(dot(normal, light), 0.0);

    vec4 actualAmbient = ambientColor.a == 0.0 ? vec4(0.4, 0.4, 0.4, 1.0) : ambientColor;
    vec4 actualLight = lightColor.a == 0.0 ? vec4(1.0, 1.0, 1.0, 1.0) : lightColor;
    // Clamp so this can never blow out to flat white regardless of how
    // bright the environment's ambient+sun happen to be.
    vec3 totalLight = clamp(actualAmbient.rgb + actualLight.rgb * NdotL, 0.0, 1.05);

    finalColor = vec4(cloudBase * totalLight, 1.0) * colDiffuse;
}
