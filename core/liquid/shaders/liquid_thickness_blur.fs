#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_texel;
uniform vec2 u_direction;
uniform int u_radius;

/* Thickness gets a PLAIN separable Gaussian, not the bilateral narrow-range
 * filter the depth pass uses. That is Green's (NVIDIA, GDC 2010 "Screen Space
 * Fluid Rendering") prescription and the reason is worth keeping: thickness is
 * a low-frequency quantity with no silhouettes to preserve — an edge-stopping
 * filter here would only reproduce the splat-scale lumps it is meant to remove.
 * Bleeding a little thickness past the silhouette is harmless: the composite
 * masks itself on the DEPTH buffer, not on this one. */
void main() {
    float sum = 0.0;
    float weightSum = 0.0;
    float sigma = max(float(u_radius) * 0.5, 1.0);
    // Constant loop bound with an inner reject: GLES 3.x drivers still refuse
    // some non-constant bounds (ENGINE_LANDMINES.md, Android shader rules).
    for (int i = -24; i <= 24; i++) {
        if (i < -u_radius || i > u_radius) continue;
        float fi = float(i);
        float w = exp(-0.5 * fi * fi / (sigma * sigma));
        sum += texture(texture0, fragTexCoord + u_direction * u_texel * fi).r * w;
        weightSum += w;
    }
    finalColor = vec4(sum / max(weightSum, 1e-4), 0.0, 0.0, 1.0);
}
