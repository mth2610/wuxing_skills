#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;        // front depth capture (device depth in R)
uniform sampler2D u_backDepthTex;  // back depth capture  (device depth in R)
uniform mat4 u_inverseProjection;

/* Dual-depth thickness: T = z_back - z_front, a MEASURED path through the
 * splat cloud in metres.
 *
 * This replaces an additive accumulation of per-splat sphere chords. The method
 * of accumulating is standard (FleX, Obi, VTK), but its output is a sum over
 * overlapping reconstruction kernels, not a length: it has to be divided by an
 * unmeasurable overlap factor and pushed through a saturating knee before it
 * can be called metres, and both of those constants were invented. Subtracting
 * two depths needs neither.
 *
 * What it measures is the ENVELOPE of the cloud. For a dense body that is the
 * water; for an emitter that spawns only on a surface it is the whole hollow
 * volume. */
float ViewDistance(float deviceDepth) {
    vec4 clip = vec4(0.0, 0.0, deviceDepth * 2.0 - 1.0, 1.0);
    vec4 view = u_inverseProjection * clip;
    return max(0.0001, -view.z / view.w);
}

void main() {
    float front = texture(texture0, fragTexCoord).r;
    float back = texture(u_backDepthTex, fragTexCoord).r;
    // Front target clears to 1 (nothing in front), back target clears to 0
    // (nothing behind) — the two passes reduce in opposite directions.
    if (front >= 0.99999 || back <= 0.000001) {
        finalColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float thickness = ViewDistance(back) - ViewDistance(front);
    finalColor = vec4(max(thickness, 0.0), 0.0, 0.0, 1.0);
}
