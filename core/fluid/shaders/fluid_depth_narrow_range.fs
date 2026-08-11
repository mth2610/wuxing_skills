#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_texel;
uniform vec2 u_direction;
uniform float u_depthRange;
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

void AccumulateSample(float sampleDeviceDepth, float spatialWeight,
                      inout float weightedDepth, inout float weightSum,
                      float centerDistance, float predictedDistance) {
    // Truong & Yuksel 2018: the range is NARROW and samples outside it are
    // CLAMPED into it rather than down-weighted — the one thing that method is
    // defined by. Clamped around the local tangent plane, not the centre depth,
    // or a sloped surface terraces into steps.
    float range = max(u_kernelRadius * 2.5, 0.006);
    float sampleDistance;
    if (sampleDeviceDepth >= 0.99999) {
        /* No surface on this side — the silhouette. DROPPING the sample is what
         * streaked the edges: the average then gathers only inward samples and
         * leans toward the body, and because that happens for every pixel along
         * the pass axis the lean smears into a streak ALONG that axis (measured:
         * the horizontal pass alone streaked horizontally, the vertical alone
         * vertically, the unfiltered capture not at all).
         *
         * Contribute the tangent-plane PREDICTION instead. The pair then stays
         * symmetric — prediction at +i and at -i average back to the centre — so
         * the filter cannot lean, and unlike simply stopping the run it keeps its
         * full width. Two earlier attempts (break on the first missing side, and
         * gating the hole fill on enclosure) both removed filtering instead of
         * removing bias, and both made the edge WORSE; see core/docs/PROGRESS.md. */
        sampleDistance = predictedDistance;
    } else {
        sampleDistance = ViewDistance(sampleDeviceDepth);
        float deltaZ = sampleDistance - centerDistance;
        // A genuinely separate sheet in FRONT is not part of this surface:
        // clamping it in would drag the whole neighbourhood towards the camera.
        // This is the one case that stays a rejection.
        if (deltaZ < -u_depthRange * 1.5) return;
    }

    /* Clamp around the local TANGENT PLANE, not around the centre sample. A
     * range centred on the centre depth terraces every sloped surface: past
     * range/slope texels every remaining sample pins to the same bound, so the
     * filter flattens the slope into steps and the body comes out corrugated —
     * the parallel ridges that appeared the moment the radius grew wide enough
     * for the slope to leave the range. Predicting along the surface makes the
     * residual (not the depth) the thing being bounded, so a smooth slope
     * passes through untouched while a splat's dome is still clipped away. */
    float clamped = clamp(sampleDistance, predictedDistance - range, predictedDistance + range);
    weightedDepth += clamped * spatialWeight;
    weightSum += spatialWeight;
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

    /* Local slope along THIS pass's axis, in metres per texel, so the clamp can
     * follow the surface instead of terracing it (see AccumulateSample). */
    float slope = 0.0;
    {
        /* Two texels of baseline, not one: a one-texel difference measures the
         * splat bumps this filter exists to remove, and feeding those back into
         * the prediction is what turned four iterated rounds into standing
         * ripples. */
        float nearPlus = texture(texture0, fragTexCoord + u_direction * u_texel * 2.0).r;
        float nearMinus = texture(texture0, fragTexCoord - u_direction * u_texel * 2.0).r;
        float gPlus = nearPlus < 0.99999 ? (ViewDistance(nearPlus) - centerDistance) * 0.5 : 0.0;
        float gMinus = nearMinus < 0.99999 ? (centerDistance - ViewDistance(nearMinus)) * 0.5 : 0.0;
        /* The central difference, faded out where the two sides DISAGREE. The
         * previous min-gradient pick was a hard switch between two estimates,
         * and a discontinuous kernel iterated four times bands: the output
         * alternates wherever the choice flips, in stripes perpendicular to the
         * pass axis — which is exactly how the ridges lined up with the screen
         * axes rather than with the water. Disagreement means a silhouette or a
         * crease, where there is no tangent plane worth predicting along, so the
         * slope fades smoothly to zero instead of jumping. */
        float rangeTrust = max(u_kernelRadius * 2.5, 0.006);
        float trust = 1.0 - smoothstep(rangeTrust * 0.25, rangeTrust * 0.75, abs(gPlus - gMinus));
        slope = 0.5 * (gPlus + gMinus) * trust;
        /* A prediction may not run away over a long radius, or a steep edge
         * would drag the far end of the kernel off the surface entirely. The
         * bound is on the TOTAL excursion across the run: six ranges covers
         * every slope a real body shows short of the silhouette (measured in
         * core/tests/fluid_depth_filter_test.c), and past that the samples
         * simply clamp again — degrading to the old behaviour rather than
         * inventing a surface. */
        float rangeLimit = max(u_kernelRadius * 2.5, 0.006);
        float maxSlope = rangeLimit * 6.0 / max(reachPixels, 1.0);   /* continuous, see above */
        slope = clamp(slope, -maxSlope, maxSlope);
    }

    // Loop up to adaptiveRadius (capped at 32 to satisfy GLSL constant loops).
    for (int i = 1; i <= 32; i++) {
        if (i > adaptiveRadius) break;
        float fi = float(i);
        /* The sigma has to scale with the radius too. It was fixed at sqrt(6)
         * ~= 2.4 texels, so a sample ten texels out weighed 0.0002 no matter how
         * far the loop reached — raising the radius alone would have changed
         * nothing and only cost taps. Half the adaptive radius puts the tail at
         * exp(-2) ~= 0.14 exactly where the loop stops. */
        float spatialWeight = exp(-0.5 * fi * fi / (sigmaS * sigmaS));
        float positive = texture(texture0, fragTexCoord + u_direction * u_texel * fi).r;
        float negative = texture(texture0, fragTexCoord - u_direction * u_texel * fi).r;

        AccumulateSample(positive, spatialWeight, weightedDepth, weightSum,
                         centerDistance, centerDistance + slope * fi);
        AccumulateSample(negative, spatialWeight, weightedDepth, weightSum,
                         centerDistance, centerDistance - slope * fi);
    }

    float filteredDistance = weightedDepth / max(weightSum, 0.0001);
    finalColor = vec4(clamp(DeviceDepth(filteredDistance), 0.0, 1.0), 0.0, 0.0, 1.0);
}
