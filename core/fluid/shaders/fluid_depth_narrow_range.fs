#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_texel;
uniform vec2 u_direction;
uniform float u_kernelRadius;
uniform int u_filterRadius;
uniform int u_fillHoles;
uniform mat4 u_projection;
uniform mat4 u_inverseProjection;

float ViewDistance(float deviceDepth) {
    vec4 clip=vec4(0.0,0.0,deviceDepth*2.0-1.0,1.0);
    vec4 view=u_inverseProjection*clip;
    return max(0.0001,-view.z/view.w);
}

float DeviceDepth(float viewDistance) {
    vec4 clip=u_projection*vec4(0.0,0.0,-viewDistance,1.0);
    return clip.z/clip.w*0.5+0.5;
}

void main() {
    float centerDevice = texture(texture0, fragTexCoord).r;

    /* Bridge gaps between densely packed small particles (e.g. a force-field orb
     * at high particle count) by taking the nearest depth around an empty pixel.
     *
     * 3x3, not 5x5. This fill is a DILATION: it also grows the body outward at
     * the silhouette, and every pixel it invents keeps its value as the centre
     * of its own filter run — the centre is the one sample the clamp cannot
     * correct, since the clamp only bounds neighbours. A 5x5 grew that invented
     * fringe two texels deep all the way round; 3x3 halves it while still
     * bridging the one-texel gaps this exists for. */
    if (centerDevice >= 0.99999 && u_fillHoles != 0) {
        float nearest = 1.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                nearest = min(nearest, texture(texture0, fragTexCoord + vec2(x, y) * u_texel).r);
            }
        }
        centerDevice = nearest;
    }
    if (centerDevice >= 0.99999) {
        finalColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    float centerDistance = ViewDistance(centerDevice);
    float weightedDepth = centerDistance;
    float weightSum = 1.0;

    /* The filter radius has to follow the splat's size ON SCREEN, which is what
     * this shader was missing: a fixed 10 texels cannot flatten a kernel that
     * projects to forty, so every splat kept its own dome and the body rendered
     * as a heap of beads whenever the camera came close. Derive the projected
     * kernel radius per pixel — proj[1][1] is 1/tan(fovy/2) and 1/u_texel.y is
     * the target height. Reach about ONE kernel across, not two: two erases the
     * splat dome and then keeps going, converging the interior toward a plane,
     * and a body whose middle is flat while its silhouette still carries splat
     * bumps reads as neither liquid nor solid. One kernel is the smallest reach
     * that can still remove a dome. u_filterRadius stays as the tier's CEILING,
     * so quality tiers still bound the cost. */
    float projScale = u_projection[1][1] * (0.5 / max(u_texel.y, 1e-6));
    float kernelPixels = u_kernelRadius * projScale / max(centerDistance, 0.05);

    /* The amount of smoothing must be CONTINUOUS in depth. Deriving the Gaussian
     * from an integer radius made it a step function of distance: every texel
     * the radius jumped, the surface got a different amount of smoothing, and
     * the boundary between the two amounts is an iso-depth curve — a contour
     * line. Eight of them across a two-metre body, which is exactly the
     * topographic-map pattern the sandbox showed. So sigma comes from the raw
     * projected size, and only the LOOP BOUND stays integer: cut at three sigma,
     * where the weight is ~1%, the quantized cutoff carries too little energy to
     * draw a line. */
    float reachPixels = max(kernelPixels * 1.25, 2.0);
    float sigmaS = max(reachPixels * 0.5, 1.0);
    int adaptiveRadius = int(min(ceil(sigmaS * 3.0), float(u_filterRadius)));

    /* Narrow-range filter, Truong & Yuksel 2018, implemented from the paper.
     *
     *   - a sample FURTHER than `upper` is background or a separate sheet: its
     *     weight goes to zero AND SO DOES ITS PARTNER'S on the opposite side.
     *     That pairing is the part everything else here got wrong. Dropping one
     *     side alone biases the average toward the side that still has surface;
     *     substituting a fabricated value instead adds weight carrying the
     *     CENTRE's own depth, which dilutes the smoothing and leaves the edge
     *     stiffer than the interior. Zeroing the pair removes weight rather than
     *     adding it, so the remaining rings still flatten at full strength after
     *     the sum/weight normalisation — unbiased and undiluted.
     *   - a sample NEARER than `lower` is clamped to one kernel radius in front
     *     of the centre, not rejected, so a splat's near face still pulls.
     *   - otherwise the range EXTENDS to follow it. This is what lets the filter
     *     track a sloped surface, and it replaces the tangent-plane prediction
     *     this file used to estimate from a finite difference.
     *
     * Each side carries its own bounds, exactly as the reference does. */
    float threshold = u_kernelRadius * 10.5;
    float lowerClamp = centerDistance - u_kernelRadius;
    float upperPos = centerDistance + threshold, lowerPos = centerDistance - threshold;
    float upperNeg = upperPos, lowerNeg = lowerPos;

    for (int i = 1; i <= 32; i++) {
        if (i > adaptiveRadius) break;
        float fi = float(i);
        float w = exp(-0.5 * fi * fi / (sigmaS * sigmaS));
        float wPos = w, wNeg = w;

        float dPos = ViewDistance(texture(texture0, fragTexCoord + u_direction * u_texel * fi).r);
        float dNeg = ViewDistance(texture(texture0, fragTexCoord - u_direction * u_texel * fi).r);

        if (dPos > upperPos) { wPos = 0.0; wNeg = 0.0; }
        else if (dPos < lowerPos) { dPos = lowerClamp; }
        else { upperPos = max(upperPos, dPos + threshold); lowerPos = min(lowerPos, dPos - threshold); }

        if (dNeg > upperNeg) { wNeg = 0.0; wPos = 0.0; }
        else if (dNeg < lowerNeg) { dNeg = lowerClamp; }
        else { upperNeg = max(upperNeg, dNeg + threshold); lowerNeg = min(lowerNeg, dNeg - threshold); }

        weightedDepth += dPos * wPos + dNeg * wNeg;
        weightSum += wPos + wNeg;
    }

    float filteredDistance = weightedDepth / max(weightSum, 0.0001);
    finalColor = vec4(clamp(DeviceDepth(filteredDistance), 0.0, 1.0), 0.0, 0.0, 1.0);
}
