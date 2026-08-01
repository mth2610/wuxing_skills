#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;       // smoothed fluid depth, raw depth [0,1]
uniform sampler2D u_thicknessTex;
uniform sampler2D u_sceneTex;
uniform sampler2D u_sceneDepthTex;
uniform vec2 u_texel;
uniform int u_hasSceneDepth;
void main() {
    float d = texture(texture0, fragTexCoord).r;
    if (d >= 0.99999) discard;
    if (u_hasSceneDepth != 0) {
        float sceneDepth = texture(u_sceneDepthTex, fragTexCoord).r;
        /* The fluid kernels sit only millimetres above their receiver.  Exact
         * comparison lets depth quantisation cut horizontal bands through the
         * surface as camera distance changes. */
        if (d > sceneDepth + 0.0015) discard;
    }
    float l = texture(texture0, fragTexCoord - vec2(u_texel.x,0)).r;
    float r = texture(texture0, fragTexCoord + vec2(u_texel.x,0)).r;
    float b = texture(texture0, fragTexCoord - vec2(0,u_texel.y)).r;
    float t = texture(texture0, fragTexCoord + vec2(0,u_texel.y)).r;
    // Background is encoded as depth 1.0.  Treating it as a fluid neighbour
    // produces an artificial near-horizontal normal and a white Fresnel ring
    // around every isolated splat.
    // Preserve a bounded silhouette slope instead of flattening it to zero:
    // this gives water a curved edge without the old white halo.
    l = l >= 0.99999 ? min(1.0, d + 0.018) : l;
    r = r >= 0.99999 ? min(1.0, d + 0.018) : r;
    b = b >= 0.99999 ? min(1.0, d + 0.018) : b;
    t = t >= 0.99999 ? min(1.0, d + 0.018) : t;
    vec3 n = normalize(vec3((l-r)*18.0, (b-t)*18.0, 1.0));
    float fresnel = pow(1.0 - clamp(n.z, 0.0, 1.0), 5.0) * 0.42;
    float thick = clamp(texture(u_thicknessTex, fragTexCoord).r, 0.0, 4.0);
    /* Keep refraction in the few-pixel range.  The old thickness multiplier
     * reached ~0.066 UV on a dense body, magnifying ground-grid lines into
     * zoom-dependent horizontal bands inside the water. */
    float refractStrength = 0.0022 + min(thick, 1.0)*0.0028;
    vec2 refractUV = clamp(fragTexCoord + n.xy*refractStrength, 0.0, 1.0);
    vec3 sharpRefracted = texture(u_sceneTex, refractUV).rgb;
    vec3 scatteredRefracted = vec3(0.0);
    for (int sy=-1; sy<=1; ++sy) for (int sx=-1; sx<=1; ++sx)
        scatteredRefracted += texture(u_sceneTex,
            clamp(refractUV + vec2(sx, sy)*u_texel*1.5, 0.0, 1.0)).rgb;
    scatteredRefracted *= 1.0/9.0;
    // Beer-Lambert: thin water remains transparent, accumulated volume gains
    // a saturated blue body instead of the uniform plastic tint of alpha fog.
    vec3 transmittance = exp(-thick * vec3(1.65, 0.48, 0.22));
    vec3 waterBody = vec3(0.02, 0.46, 0.82);
    float coreScatter = smoothstep(0.55, 1.65, thick);
    vec3 refracted = mix(sharpRefracted, scatteredRefracted, coreScatter);
    /* Dense water scatters before the receiver grid can remain legible.
     * Thin droplets keep the physical Beer-Lambert transparency. */
    vec3 effectiveTransmittance = transmittance * (1.0 - coreScatter*0.94);
    vec3 water = refracted * effectiveTransmittance
               + waterBody * (1.0 - effectiveTransmittance);
    /* Thin sheets and droplets remain transparent.  A dense body also
     * scatters light, so sharp ground-grid lines must not remain perfectly
     * legible through its centre. */
    water = mix(water, waterBody, coreScatter * 0.35);
    vec3 lightDir = normalize(vec3(-0.35, 0.48, 0.80));
    float specular = pow(max(dot(n, lightDir), 0.0), 56.0);
    // A stable screen-space micro breakup keeps highlights lively as the
    // surface moves, without another texture fetch or simulation pass.
    float grain = fract(sin(dot(floor(gl_FragCoord.xy * 0.75), vec2(12.9898, 78.233))) * 43758.5453);
    float sparkle = specular * mix(0.30, 1.0, step(0.82, grain));
    water = mix(water, vec3(0.76, 0.93, 1.0), fresnel);
    water += vec3(0.40, 0.62, 0.82) * sparkle;
    // Thin droplets remain translucent; accumulated body water is rich enough
    // to read against the dark scene.
    finalColor = vec4(water + fresnel*0.05, clamp(0.20 + (1.0 - transmittance.b)*0.72 + fresnel*0.08, 0.0, 0.88));
}
