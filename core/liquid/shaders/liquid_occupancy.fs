#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;

/* An 8x8 tile plus its one-pixel halo contains every possible 3x3 hole-fill
 * source. Integer fetches work even when R32F linear filtering is unsupported.
 * Early-exit as soon as the first valid fluid texel (< 0.99999) is found so
 * occupied tiles finish in 1 fetch instead of burning all 100 fetches. */
void main() {
    ivec2 size = textureSize(texture0, 0);
    ivec2 maxCoord = max(size - ivec2(1), ivec2(0));
    ivec2 tiles = (size + ivec2(7)) / 8;
    ivec2 base = clamp(ivec2(fragTexCoord * vec2(tiles)), ivec2(0), max(tiles - ivec2(1), ivec2(0))) * 8;

    /* Probe tile centre first for instant hit on interior fluid tiles */
    if (texelFetch(texture0, clamp(base + ivec2(4, 4), ivec2(0), maxCoord), 0).r < 0.99999) {
        finalColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    for (int y = -1; y <= 8; y++) {
        int cy = clamp(base.y + y, 0, maxCoord.y);
        for (int x = -1; x <= 8; x++) {
            int cx = clamp(base.x + x, 0, maxCoord.x);
            if (texelFetch(texture0, ivec2(cx, cy), 0).r < 0.99999) {
                finalColor = vec4(1.0, 0.0, 0.0, 1.0);
                return;
            }
        }
    }
    finalColor = vec4(0.0, 0.0, 0.0, 1.0);
}
