#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;        // front depth capture (device depth in R, coverage in G)
uniform sampler2D u_backDepthTex;  // back depth capture  (device depth in R)
uniform mat4 u_inverseProjection;

/* Dual-depth thickness: T = z_back - z_front, a MEASURED path through the
 * splat cloud in metres. Analytical 2-MAD depth unprojection avoids full mat4*vec4. */
float ViewDistance(float deviceDepth) {
    float ndcZ = deviceDepth * 2.0 - 1.0;
    float viewZ = u_inverseProjection[2][2] * ndcZ + u_inverseProjection[3][2];
    float viewW = u_inverseProjection[2][3] * ndcZ + u_inverseProjection[3][3];
    return max(0.0001, -viewZ / max(abs(viewW), 1e-7) * sign(viewW));
}

void main() {
    vec4 frontSample = texture(texture0, fragTexCoord);
    float front = frontSample.r;
    if (front >= 0.99999) {
        finalColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float back = texture(u_backDepthTex, fragTexCoord).r;
    if (back <= 0.000001) {
        /* Preserve thin grazing splats that have valid front coverage */
        finalColor = vec4(max(frontSample.g * 0.008, 0.0), 0.0, 0.0, 1.0);
        return;
    }
    float thickness = max(ViewDistance(back) - ViewDistance(front), frontSample.g * 0.004);
    finalColor = vec4(thickness, 0.0, 0.0, 1.0);
}
