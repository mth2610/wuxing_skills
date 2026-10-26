#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_texel;
uniform vec2 u_direction;
uniform float u_kernelRadius;
uniform sampler2D u_materialIdTex;
uniform float u_materialKernels[6];
uniform int u_multipleMaterials;

/* Filled pixels borrow the nearest actual capture fragment's identity. */
int MaterialAt(vec2 uv) {
    vec4 sampleValue = texture(u_materialIdTex, uv);
    if (sampleValue.r >= 0.99999) {
        float nearest = 1.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                if (x == 0 && y == 0) continue;
                vec4 candidate = texture(u_materialIdTex, uv + vec2(x, y) * u_texel);
                if (candidate.r < nearest) {
                    nearest = candidate.r;
                    sampleValue = candidate;
                }
            }
        }
    }
    return clamp(int(sampleValue.b + 0.5), 0, 5);
}

/* 1 when texture0 is the RAW capture, i.e. it already carries the material id in
 * .b. Round 0 reads the capture and every later round reads a single-channel
 * smoothed target. */
uniform int u_sourceCarriesMaterial;
float DepthForMaterial(vec2 uv, int material) {
    if (u_multipleMaterials == 0) return texture(texture0, uv).r;
    if (u_sourceCarriesMaterial != 0) {
        vec4 raw = texture(texture0, uv);
        if (raw.r < 0.99999 && int(raw.b + 0.5) != material) return 1.0;
        return raw.r;
    } else {
        vec4 idSample = texture(u_materialIdTex, uv);
        if (idSample.r < 0.99999 && int(idSample.b + 0.5) != material) return 1.0;
        return texture(texture0, uv).r;
    }
}

uniform int u_filterRadius;
uniform int u_fillHoles;
uniform sampler2D u_occupancyTex;
uniform int u_useOccupancy;
/* 0 = the separable 1D pass (run twice by the host), 1 = the true 2D kernel. */
uniform int u_filter2D;
#define LIQUID_FILTER2D_MAX_RADIUS 10
uniform mat4 u_projection;
uniform mat4 u_inverseProjection;

/* Fast analytical Z/W evaluation: clip.xy is (0, 0), so only column 2 and 3
 * of u_inverseProjection / u_projection participate (2 MADs instead of 16). */
float ViewDistance(float deviceDepth) {
    float ndcZ = deviceDepth * 2.0 - 1.0;
    float viewZ = u_inverseProjection[2][2] * ndcZ + u_inverseProjection[3][2];
    float viewW = u_inverseProjection[2][3] * ndcZ + u_inverseProjection[3][3];
    return max(0.0001, -viewZ / max(abs(viewW), 1e-7) * sign(viewW));
}

float DeviceDepth(float viewDistance) {
    float clipZ = -u_projection[2][2] * viewDistance + u_projection[3][2];
    float clipW = -u_projection[2][3] * viewDistance + u_projection[3][3];
    return clipZ / max(abs(clipW), 1e-7) * sign(clipW) * 0.5 + 0.5;
}

void main() {
    /* Fast early-out on empty 8x8 tiles for ALL passes when occupancy is enabled. */
    if (u_useOccupancy != 0 && texture(u_occupancyTex, fragTexCoord).r < 0.5) {
        finalColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    float centerDevice = texture(texture0, fragTexCoord).r;

    if (centerDevice >= 0.99999 && u_fillHoles != 0) {
        float nearest = 1.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                if (x == 0 && y == 0) continue;
                nearest = min(nearest, texture(texture0, fragTexCoord + vec2(x, y) * u_texel).r);
            }
        }
        centerDevice = nearest;
    }
    if (centerDevice >= 0.99999) {
        finalColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    int material = (u_multipleMaterials != 0) ? MaterialAt(fragTexCoord) : 0;
    float kernelRadius = u_materialKernels[material] > 0.0 ? u_materialKernels[material] : u_kernelRadius;
    float centerDistance = ViewDistance(centerDevice);
    float weightedDepth = centerDistance;
    float weightSum = 1.0;

    float projScale = abs(u_projection[1][1]) * (0.5 / max(u_texel.y, 1e-6));
    float kernelPixels = kernelRadius * projScale / max(centerDistance, 0.05);

    float reachPixels = max(kernelPixels * 1.25, 2.0);
    float sigmaS = max(reachPixels * 0.5, 1.0);
    int adaptiveRadius = int(min(ceil(sigmaS * 3.0), float(u_filterRadius)));

    float threshold = kernelRadius * 10.5;
    float lowerClamp = centerDistance - kernelRadius;
    float invTwoSigmaSq = -0.5 / (sigmaS * sigmaS);
    float invTwoRangeSq = -0.5 / max(threshold * threshold * 0.25, 1e-6);

    if (u_filter2D != 0) {
        vec4 upper = vec4(centerDistance + threshold);
        vec4 lower = vec4(centerDistance - threshold);
        int radius2D = min(adaptiveRadius, LIQUID_FILTER2D_MAX_RADIUS);
        float radiusSquared = float(radius2D * radius2D);
        for (int dy = 0; dy <= LIQUID_FILTER2D_MAX_RADIUS; dy++) {
            if (dy > radius2D) break;
            for (int m = 0; m <= LIQUID_FILTER2D_MAX_RADIUS; m++) {
                if (m > radius2D) break;
                if (dy == 0 && m == 0) continue;
                float r2 = float(m * m + dy * dy);
                if (r2 > radiusSquared) continue;
                float w = exp(r2 * invTwoSigmaSq);

                for (int pair = 0; pair < 2; pair++) {
                    if (pair == 1 && (m == 0 || dy == 0)) continue;
                    vec2 off = vec2((pair == 0 ? float(m) : -float(m)), float(dy)) * u_texel;
                    int iPos = pair * 2, iNeg = pair * 2 + 1;

                    float rawPos = DepthForMaterial(fragTexCoord + off, material);
                    float rawNeg = DepthForMaterial(fragTexCoord - off, material);
                    bool validPos = (rawPos < 0.99999);
                    bool validNeg = (rawNeg < 0.99999);
                    if (!validPos && !validNeg) continue;

                    float dPos = validPos ? ViewDistance(rawPos) : 1e6;
                    float dNeg = validNeg ? ViewDistance(rawNeg) : 1e6;
                    float wPos = validPos ? w : 0.0;
                    float wNeg = validNeg ? w : 0.0;

                    if (validPos) {
                        if (dPos > upper[iPos]) {
                            wPos = 0.0;
                        } else if (dPos < lower[iPos]) {
                            dPos = lowerClamp;
                        } else {
                            upper[iPos] = max(upper[iPos], dPos + threshold);
                            lower[iPos] = min(lower[iPos], dPos - threshold);
                        }
                    }
                    if (validNeg) {
                        if (dNeg > upper[iNeg]) {
                            wNeg = 0.0;
                        } else if (dNeg < lower[iNeg]) {
                            dNeg = lowerClamp;
                        } else {
                            upper[iNeg] = max(upper[iNeg], dNeg + threshold);
                            lower[iNeg] = min(lower[iNeg], dNeg - threshold);
                        }
                    }

                    /* When one side of a symmetric tap crosses a silhouette edge,
                     * weight the surviving interior tap by range similarity so
                     * silhouette edges still smooth without slope bias. */
                    if (wPos > 0.0 && wNeg == 0.0) {
                        float diff = dPos - centerDistance;
                        wPos *= 0.45 * exp(diff * diff * invTwoRangeSq);
                    } else if (wNeg > 0.0 && wPos == 0.0) {
                        float diff = dNeg - centerDistance;
                        wNeg *= 0.45 * exp(diff * diff * invTwoRangeSq);
                    }

                    weightedDepth += dPos * wPos + dNeg * wNeg;
                    weightSum += wPos + wNeg;
                }
            }
        }
        finalColor = vec4(clamp(DeviceDepth(weightedDepth / max(weightSum, 0.0001)), 0.0, 1.0),
                          0.0, 0.0, 1.0);
        return;
    }

    float upperPos = centerDistance + threshold, lowerPos = centerDistance - threshold;
    float upperNeg = upperPos, lowerNeg = lowerPos;
    vec2 stepUV = u_direction * u_texel;

    for (int i = 1; i <= 32; i++) {
        if (i > adaptiveRadius) break;
        float fi = float(i);
        float w = exp(fi * fi * invTwoSigmaSq);
        vec2 off = stepUV * fi;

        float rawPos = DepthForMaterial(fragTexCoord + off, material);
        float rawNeg = DepthForMaterial(fragTexCoord - off, material);
        bool validPos = (rawPos < 0.99999);
        bool validNeg = (rawNeg < 0.99999);
        if (!validPos && !validNeg) continue;

        float dPos = validPos ? ViewDistance(rawPos) : 1e6;
        float dNeg = validNeg ? ViewDistance(rawNeg) : 1e6;
        float wPos = validPos ? w : 0.0;
        float wNeg = validNeg ? w : 0.0;

        if (validPos) {
            if (dPos > upperPos) {
                wPos = 0.0;
            } else if (dPos < lowerPos) {
                dPos = lowerClamp;
            } else {
                upperPos = max(upperPos, dPos + threshold);
                lowerPos = min(lowerPos, dPos - threshold);
            }
        }

        if (validNeg) {
            if (dNeg > upperNeg) {
                wNeg = 0.0;
            } else if (dNeg < lowerNeg) {
                dNeg = lowerClamp;
            } else {
                upperNeg = max(upperNeg, dNeg + threshold);
                lowerNeg = min(lowerNeg, dNeg - threshold);
            }
        }

        if (wPos > 0.0 && wNeg == 0.0) {
            float diff = dPos - centerDistance;
            wPos *= 0.45 * exp(diff * diff * invTwoRangeSq);
        } else if (wNeg > 0.0 && wPos == 0.0) {
            float diff = dNeg - centerDistance;
            wNeg *= 0.45 * exp(diff * diff * invTwoRangeSq);
        }

        weightedDepth += dPos * wPos + dNeg * wNeg;
        weightSum += wPos + wNeg;
    }

    float filteredDistance = weightedDepth / max(weightSum, 0.0001);
    finalColor = vec4(clamp(DeviceDepth(filteredDistance), 0.0, 1.0), 0.0, 0.0, 1.0);
}
