#version 330

// Sea-of-clouds plane for the "floating island ringed by mountains" motif
// every map uses (MAP_API.md). Uses discard for the cloud/gap boundary
// instead of alpha blending — maps/CLAUDE.md's hard rule is Alpha = 255
// ALWAYS in the main scene (partial alpha breaks particle rendering), so
// this shader is either fully opaque per-fragment or not drawn there at
// all, never in between.
//
// Density comes from a tileable noise TEXTURE (assets/textures/cloud_noise.png,
// scripts/generate_cloud_noise.py), not per-pixel sin()-based FBM math. This
// plane often covers most of the screen (fill-rate bound), and a texture
// fetch is far cheaper than the ~16 sin() calls/pixel the old 2-octave-FBM-x-
// 2-layers version cost — this was a measured, real FPS problem, not a
// theoretical one.

in vec2 fragTexCoord;

uniform sampler2D texture0; // tileable grayscale cloud noise, TEXTURE_WRAP_REPEAT
uniform vec2 tiling;
uniform float u_time;
uniform vec4 colDiffuse;

uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;

out vec4 finalColor;

void main()
{
    vec2 uv = fragTexCoord * tiling;

    // Two samples scrolling at different speed/direction/scale -> slow
    // roiling look, same idea as the old two-FBM-layer blend but each
    // layer is now one texture fetch instead of a noise loop.
    vec2 flow1 = uv + vec2(u_time * 0.02, u_time * 0.008);
    vec2 flow2 = uv * 1.7 - vec2(u_time * 0.015, -u_time * 0.01);
    float density = texture(texture0, flow1).r * 0.6 + texture(texture0, flow2).r * 0.4;

    // Opaque cutout: below threshold there is no cloud here at all (never a
    // partial-alpha fragment). cloud_noise.png is normalized full 0..1 range,
    // mean ~0.5 — threshold/edge band tuned around that.
    if (density < 0.45) discard;

    // Kept deliberately darker than a typical lit surface (unlike
    // grass/rock albedo ~0.3-0.5, a too-bright base clips to flat white once
    // ambient+sun lighting is added — see totalLight's clamp below) — the
    // density gradient itself is what should read as "cloud", not the
    // lighting.
    float edge = smoothstep(0.45, 0.6, density);
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
