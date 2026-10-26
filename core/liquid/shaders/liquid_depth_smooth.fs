#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 u_texel;

void main() {
    float d = texture(texture0, fragTexCoord).r;
    /* Circular 5x5 disk min-filter (skips the 4 Euclidean-distant square corners):
     * merges overlapping droplets into a round continuous surface instead of
     * axis-aligned 5x5 squares while saving 16% of texture fetches. */
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if ((x == 0 && y == 0) || (abs(x) == 2 && abs(y) == 2)) continue;
            float sampleDepth = texture(texture0, fragTexCoord + vec2(x, y) * u_texel).r;
            d = min(d, sampleDepth);
        }
    }
    finalColor = vec4(d, 0.0, 0.0, 1.0);
}
