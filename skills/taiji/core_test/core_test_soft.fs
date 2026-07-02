#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/soft_particle.glsl"

// CORE_ISSUES.md Item 3 test shape — verifies the buried-bottom of this
// sphere fades smoothly against the ground instead of hard-clipping.
uniform float u_fadeDistance;
// Debug view modes (toggle H = 1, J = 2), both at alpha=1 (always fully
// opaque) instead of using the real fade as alpha — lets us see the actual
// per-pixel value the GPU computes directly on the mesh, without the
// CPU-side sample-point mismatch that made the earlier 3-point world-space
// readback unreliable (see CORE_ISSUES.md Item 3 process note: prefer a
// raw/unclamped numeric view over guessing from blended color).
//   0 = normal shaded look
//   1 = SoftParticle_Factor() as grayscale (clamped [0,1])
//   2 = RAW unclamped diff = sceneLinear - fragLinear, sign-coded:
//       green = positive (unoccluded), red = negative (occluded),
//       brightness = magnitude/300, so we can see the true scale instead
//       of guessing from an already-clamped [0,1] factor.
uniform float u_debugShowFade;

void main() {
    vec3 normal   = normalize(fragNormal);
    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(u_lightDir);

    float factor = SoftParticle_Factor(u_fadeDistance);

    if (u_debugShowFade > 1.5) {
        vec2 screenUV = gl_FragCoord.xy / u_resolution;
        float sceneLinear = texture(u_cameraDepthTex, screenUV).r;
        float fragLinear  = SoftParticle_LinearDepth(gl_FragCoord.z);
        float diff = sceneLinear - fragLinear;
        vec3 color;
        if (abs(diff) < 10.0) {
            // Explicit flag color for "near zero" (the actual transition
            // band we're hunting for) — a wide /300 scale would clamp this
            // to the same saturated green/red as everything else, hiding a
            // thin band entirely. This makes it pop regardless of width.
            color = vec3(0.35, 0.35, 0.0);
        } else {
            // Max luma kept under main.c's bloomThreshold (0.5f).
            color = diff >= 0.0 ? vec3(0.0, clamp(diff / 300.0, 0.05, 0.4), 0.0)
                                 : vec3(clamp(-diff / 300.0, 0.05, 0.4), 0.0, 0.0);
        }
        finalColor = vec4(color, 1.0);
        return;
    }

    if (u_debugShowFade > 0.5) {
        // Scaled to max luma 0.35 — stays under main.c's bloomThreshold
        // (0.5f) so the post-process bloom pass can't add a glow halo that
        // would be mistaken for the real soft-particle gradient. Dim gray
        // = 1.0 (fully opaque / no occluder found), black = 0.0 (fully
        // faded / occluder right at this fragment's depth).
        finalColor = vec4(vec3(factor * 0.35), 1.0);
        return;
    }

    float diffuse = max(dot(normal, lightDir), 0.2);
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.0);

    vec3 baseColor = mix(vec3(0.45, 0.20, 0.65), vec3(0.85, 0.65, 1.0), fresnel) * diffuse;

    finalColor = vec4(baseColor, factor);
}
