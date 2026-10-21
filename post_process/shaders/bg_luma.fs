#version 330

// BACKGROUND LUMINANCE — a 4-tap box downsample of the scene, luma only.
//
// Captured AFTER the world is drawn and BEFORE any VFX, which is the whole
// point: an effect that asks "how bright is what I am standing in front of"
// must not be told about itself. Sampling last frame's finished scene would
// include the effect's own light, and the effect would then dim itself, brighten
// because it dimmed, and oscillate at frame rate.
//
// Luma in all three channels so the consumer can sample any of them, and so a
// glance at the target in a debugger reads as greyscale rather than as colour.

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 u_srcTexel;      // 1 / source size

out vec4 finalColor;

void main() {
    vec3 c = texture(texture0, fragTexCoord + vec2(-0.5, -0.5) * u_srcTexel).rgb
           + texture(texture0, fragTexCoord + vec2( 0.5, -0.5) * u_srcTexel).rgb
           + texture(texture0, fragTexCoord + vec2(-0.5,  0.5) * u_srcTexel).rgb
           + texture(texture0, fragTexCoord + vec2( 0.5,  0.5) * u_srcTexel).rgb;
    c *= 0.25;
    // Rec.709, the same weights the composite's grade uses.
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    finalColor = vec4(vec3(luma), 1.0);
}
