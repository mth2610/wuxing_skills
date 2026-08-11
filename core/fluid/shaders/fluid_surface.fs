#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;       // smoothed nearest fluid depth
uniform sampler2D u_thicknessTex; // additive optical path proxy
uniform sampler2D u_sceneTex;
uniform sampler2D u_sceneDepthTex;
uniform vec2 u_texel;      // fluid reconstruction texel
uniform vec2 u_sceneTexel; // full-resolution scene texel
uniform int u_hasSceneDepth;
uniform int u_qualityTier;
/* Diagnostic view selector, 0 = off. TEMPORARY instrumentation for the banding
 * hunt: four hypotheses were fixed without the bands moving, which means the
 * question "which buffer are they in" was never actually answered. Driven by
 * WUXING_FLUID_DEBUG; remove once the answer is known. */
uniform int u_debugView;

uniform mat4 u_projection;
uniform mat4 u_inverseProjection;
uniform mat4 u_viewToWorld;
uniform vec3 u_sunDirectionView;  // surface -> sun, view space
uniform vec3 u_sunColor;
uniform vec3 u_skyAmbient;
uniform vec3 u_groundAmbient;
uniform vec3 u_materialBody;
uniform vec3 u_materialGlow;
uniform vec3 u_materialSoft;
uniform float u_time;

#define FLUID_POINT_LIGHTS 4
uniform int u_pointLightCount;
uniform vec4 u_pointLightPosRadius[FLUID_POINT_LIGHTS];
uniform vec4 u_pointLightColor[FLUID_POINT_LIGHTS];

vec3 ReconstructViewPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = u_inverseProjection * clip;
    return view.xyz / max(abs(view.w), 1e-6) * sign(view.w);
}

float FresnelSchlick(float ndv) {
    const float waterF0 = 0.02037; // ((1.333 - 1)/(1.333 + 1))^2
    return waterF0 + (1.0 - waterF0) * pow(1.0 - ndv, 5.0);
}

// Multi-octave procedural wave generator for realistic dynamic liquid shimmer.
// Ripples roll in all three axes (a planar XZ sheet smears on a sphere
// silhouette), and a patchy high-frequency capillary term adds droplet
// detail that swell alone cannot reproduce.
vec3 WaterMultiOctaveWaves(vec3 worldPosition) {
    float t = u_time * 1.2;
    vec3 p = worldPosition;
    float s1 = sin(p.x * 6.0 + t * 0.8 + sin(p.y * 4.0 + t * 0.6));
    float s2 = sin(p.y * 7.0 - t * 0.7 + sin(p.z * 5.0 + t * 0.9));
    float s3 = sin(p.z * 5.5 + t * 1.1 + sin(p.x * 6.5 - t * 0.5));
    vec2 swell = vec2(s1 + s2, s2 + s3);
    float capFade = 0.5 + 0.5 * sin(dot(p, vec3(3.7, 5.1, 4.3)) + t * 0.7);
    vec2 capillary = vec2(
        sin(p.x * 47.0 + p.y * 29.0 + t * 2.4) + cos(p.z * 41.0 + t * 1.8),
        sin(p.z * 53.0 + p.y * 33.0 + t * 2.1) + cos(p.x * 37.0 + t * 1.6));
    /* The capillary term is the finest detail on the surface and the one that
     * aliases first: its wavelength is smaller than a splat, so at any real
     * viewing distance several periods land inside one pixel. Kept, because it
     * is what stops the body reading as polished plastic — but at a third of
     * its former amplitude, which is below the point where the specular lobes
     * above turn it into rings. */
    vec2 dh = swell * 0.016 + capillary * 0.004 * capFade;
    /* The SLOPE, with no Y component. This used to return a tangent-space NORMAL
     * (the same two slopes, but with a constant 1.0 in Y), which the caller then
     * added straight to a WORLD normal. That is only meaningful through a TBN basis, and there is none
     * here, so what actually happened was a constant +1.0 world-up bias added to
     * every surface normal, modulated by the sine field. On a curved body that
     * paints the iso-lines of the swell onto the surface as ridges: the parallel
     * bands. The pre-multi-octave version (2ce1a2e) got this right by projecting
     * its ripple onto the tangent plane; the rewrite dropped the projection and
     * kept the 1.0. */
    return vec3(-dh.x, 0.0, -dh.y);
}

/* The thickness pass sums one sphere chord per splat, scaled by 16. Splats are
 * reconstruction kernels that overlap, so the raw sum over-counts the real
 * traversal by roughly (summed kernel volume / body volume) — about 1.5 for the
 * authored orb populations. Divide that out, then place the saturation knee far
 * above the body's own range. The old 0.11 m knee saturated EVERY interior pixel
 * of a 2,000-splat orb at the 0.16 m cap, so the silhouette carried no thickness
 * gradient at all: one constant optical depth across a body is exactly what
 * reads as moulded plastic instead of liquid. The output range is unchanged, so
 * every downstream mask threshold still means the same thing.
 *
 * 0.42 was not far enough: debug view 2 showed the water ring's thickness buffer
 * still saturated to a flat white across the whole body, which is the "it lost
 * its mass" complaint measured rather than argued. At 1.20 the ring's densest
 * ray sits near half the cap, so there is room above it for a thicker body to
 * still read as thicker. */
#define FLUID_KERNEL_OVERLAP 1.5
float DecodeOpticalThickness(float encodedThickness) {
    float accumulatedPath = max(encodedThickness / 16.0, 0.0);
    float traversedPath = accumulatedPath / FLUID_KERNEL_OVERLAP;
    return 0.16 * (1.0 - exp(-traversedPath / 1.20));
}

float WaterSpecularBRDF(vec3 N, vec3 V, vec3 L, float roughness) {
    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 0.0);
    if (ndl <= 0.0 || ndv <= 0.0) return 0.0;
    vec3 H = normalize(V + L);
    float ndh = max(dot(N, H), 0.0);
    float vdh = max(dot(V, H), 0.0);
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / max(3.14159265 * denominator * denominator, 1e-5);
    float k = (roughness + 1.0);
    k = k * k * 0.125;
    float gv = ndv / (ndv * (1.0 - k) + k);
    float gl = ndl / (ndl * (1.0 - k) + k);
    float F = 0.02037 + (1.0 - 0.02037) * pow(1.0 - vdh, 5.0);
    return min(D * gv * gl * F / max(4.0 * ndl * ndv, 1e-4), 8.0);
}

bool SceneSampleMatchesBase(vec2 uv, float baseSceneDistance, float fluidDistance) {
    if (u_hasSceneDepth == 0) return true;
    float sceneDepth = texture(u_sceneDepthTex, uv).r;
    bool baseHasGeometry = baseSceneDistance < 1e19;
    bool sampleHasGeometry = sceneDepth < 0.99999;
    if (baseHasGeometry != sampleHasGeometry) return false;
    if (!sampleHasGeometry) return true;
    vec3 scenePosition = ReconstructViewPosition(uv, sceneDepth);
    float sampleDistance = -scenePosition.z;
    if (sampleDistance <= fluidDistance + 0.002) return false;
    float layerTolerance = max(0.060, baseSceneDistance * 0.018);
    return abs(sampleDistance - baseSceneDistance) <= layerTolerance;
}

// Lightweight Screen Space Reflection (SSR) raymarcher
vec4 TraceSSR(vec3 rayOrigin, vec3 rayDir, float fluidDistance) {
    if (u_hasSceneDepth == 0 || rayDir.z >= 0.05) return vec4(0.0);
    
    float stepSize = 0.035;
    vec3 currentRay = rayOrigin + rayDir * stepSize;
    
    for (int i = 0; i < 14; i++) {
        vec4 clip = u_projection * vec4(currentRay, 1.0);
        if (clip.w <= 0.0) break;
        vec2 sampleUV = clip.xy / clip.w * 0.5 + 0.5;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sceneDepth = texture(u_sceneDepthTex, sampleUV).r;
        if (sceneDepth < 0.99999) {
            vec3 scenePos = ReconstructViewPosition(sampleUV, sceneDepth);
            float sceneDist = -scenePos.z;
            float rayDist = -currentRay.z;
            
            if (rayDist >= sceneDist && rayDist - sceneDist < 0.25) {
                // Binary refinement step
                currentRay -= rayDir * (stepSize * 0.5);
                clip = u_projection * vec4(currentRay, 1.0);
                sampleUV = clamp(clip.xy / clip.w * 0.5 + 0.5, 0.001, 0.999);
                
                vec3 hitColor = texture(u_sceneTex, sampleUV).rgb;
                float edgeFade = smoothstep(0.0, 0.08, sampleUV.x) * smoothstep(1.0, 0.92, sampleUV.x) *
                                 smoothstep(0.0, 0.08, sampleUV.y) * smoothstep(1.0, 0.92, sampleUV.y);
                return vec4(hitColor, edgeFade * 0.85);
            }
        }
        currentRay += rayDir * stepSize;
        stepSize *= 1.25;
    }
    return vec4(0.0);
}

void main() {
    float fluidDepth = texture(texture0, fragTexCoord).r;
    if (fluidDepth >= 0.99999) discard;

    vec3 positionView = ReconstructViewPosition(fragTexCoord, fluidDepth);
    vec3 V = normalize(-positionView);
    float fluidDistance = -positionView.z;

    float sceneDepthAtSurface = 1.0;
    float sceneDistanceAtSurface = 1e20;
    float intersectionVisibility = 1.0;
    if (u_hasSceneDepth != 0) {
        sceneDepthAtSurface = texture(u_sceneDepthTex, fragTexCoord).r;
        if (sceneDepthAtSurface < 0.99999) {
            vec3 sceneAtSurface = ReconstructViewPosition(fragTexCoord, sceneDepthAtSurface);
            sceneDistanceAtSurface = -sceneAtSurface.z;
            float frontGap = sceneDistanceAtSurface - fluidDistance;
            if (frontGap <= -0.002) discard;
            intersectionVisibility = smoothstep(-0.002, 0.012, frontGap);
        }
    }

    // Minimum Gradient Normal Reconstruction: compare forward/backward pairs
    // to prevent edge bleeding and sharp 90-degree normal spikes at splat edges.
    float depthLeft = texture(texture0, fragTexCoord - vec2(u_texel.x, 0.0)).r;
    float depthRight = texture(texture0, fragTexCoord + vec2(u_texel.x, 0.0)).r;
    float depthDown = texture(texture0, fragTexCoord - vec2(0.0, u_texel.y)).r;
    float depthUp = texture(texture0, fragTexCoord + vec2(0.0, u_texel.y)).r;
    
    depthLeft = depthLeft >= 0.99999 ? fluidDepth : depthLeft;
    depthRight = depthRight >= 0.99999 ? fluidDepth : depthRight;
    depthDown = depthDown >= 0.99999 ? fluidDepth : depthDown;
    depthUp = depthUp >= 0.99999 ? fluidDepth : depthUp;

    vec3 posL = ReconstructViewPosition(fragTexCoord - vec2(u_texel.x, 0.0), depthLeft);
    vec3 posR = ReconstructViewPosition(fragTexCoord + vec2(u_texel.x, 0.0), depthRight);
    vec3 posD = ReconstructViewPosition(fragTexCoord - vec2(0.0, u_texel.y), depthDown);
    vec3 posU = ReconstructViewPosition(fragTexCoord + vec2(0.0, u_texel.y), depthUp);

    vec3 dx1 = positionView - posL;
    vec3 dx2 = posR - positionView;
    vec3 dy1 = positionView - posD;
    vec3 dy2 = posU - positionView;

    vec3 dx = abs(dx1.z) < abs(dx2.z) ? dx1 : dx2;
    vec3 dy = abs(dy1.z) < abs(dy2.z) ? dy1 : dy2;

    vec3 N = normalize(cross(dx, dy));
    if (dot(N, V) < 0.0) N = -N;

    // Apply multi-octave dynamic wave perturbation
    vec3 worldPosition = (u_viewToWorld * vec4(positionView, 1.0)).xyz;
    vec3 worldNormal = normalize(mat3(u_viewToWorld) * N);
    /* Project the wave slope onto the surface's tangent plane before adding it.
     * Without this the perturbation carries a component ALONG the normal, which
     * does not tilt the surface at all — it just rescales the vector before the
     * normalize, so the visible effect is whatever is left over, banded. A
     * perturbation is only a perturbation in the tangent plane. */
    vec3 waveSlope = WaterMultiOctaveWaves(worldPosition);
    waveSlope -= worldNormal * dot(waveSlope, worldNormal);
    worldNormal = normalize(worldNormal + waveSlope * 0.045);
    N = normalize(transpose(mat3(u_viewToWorld)) * worldNormal);
    if (dot(N, V) < 0.0) N = -N;
    float ndv = clamp(dot(N, V), 0.0, 1.0);

    float thicknessProxy = max(texture(u_thicknessTex, fragTexCoord).r, 0.0);
    float kernelThickness = DecodeOpticalThickness(thicknessProxy);
    /* The silhouette must FADE, not end. Full opacity at 1 cm of water made a
     * one-splat-deep rim as solid as the body's middle, so every kernel at the
     * boundary drew its own hard dome against the background — the edge that
     * does not match the smooth interior. Fading across a thicker band lets
     * those lumps go translucent and stop competing with the body's shape. */
    float surfaceCoverage = smoothstep(0.0004, 0.032, kernelThickness) * intersectionVisibility;
    
    float sceneGap = 1.0;
    float waterColumnDepth = kernelThickness;
    if (sceneDepthAtSurface < 0.99999) {
        float depthGap = max(sceneDistanceAtSurface - fluidDistance, 0.0);
        sceneGap = depthGap;
        /* The receiver BOUNDS the water column; it never creates one. The old
         * max() against the gap pinned any airborne body at a constant 0.40 m
         * across its whole silhouette — 2.5x the measured path, with the
         * thickness variation deleted. Absorption follows the thickness pass
         * alone (van der Laan et al. 2009 / Green 2010); the depth gap only
         * shortens it where liquid rests on a receiver, with a 2.2 cm floor so
         * a thin resting sheet does not become invisible. */
        waterColumnDepth = min(kernelThickness, max(0.022, depthGap * 1.25));
    }
    
    float opticalPath = min(waterColumnDepth / max(ndv, 0.22), 0.50);
    float airborneWeight = smoothstep(0.070, 0.220, sceneGap);

    // Snell Refraction
    vec3 incident = -V;
    vec3 refractedDirection = refract(incident, N, 1.0 / 1.333);
    vec2 incidentSlope = incident.xy / max(abs(incident.z), 0.25);
    vec2 refractedSlope = refractedDirection.xy / max(abs(refractedDirection.z), 0.25);
    float travelPixels = clamp(opticalPath * 100.0, 0.35, 13.0);
    vec2 refractOffset = (refractedSlope - incidentSlope) * travelPixels * u_sceneTexel;
    vec2 refractUV = clamp(fragTexCoord + refractOffset, u_sceneTexel, vec2(1.0) - u_sceneTexel);

    /* Kept in a variable so the diagnostic view can report the test's verdict at
     * the offset it was actually asked about, not at the UV after the snap-back. */
    bool refractValid = SceneSampleMatchesBase(refractUV, sceneDistanceAtSurface, fluidDistance);
    if (!refractValid) {
        refractUV = mix(refractUV, fragTexCoord, 0.70);
    }

    vec3 refractedScene;
    if (u_qualityTier >= 2) {
        vec2 dispersion = refractOffset * mix(0.018, 0.045, 1.0 - ndv);
        vec2 refractUVRed = clamp(refractUV + dispersion, u_sceneTexel, vec2(1.0) - u_sceneTexel);
        vec2 refractUVBlue = clamp(refractUV - dispersion, u_sceneTexel, vec2(1.0) - u_sceneTexel);
        refractedScene.r = texture(u_sceneTex, refractUVRed).r;
        refractedScene.g = texture(u_sceneTex, refractUV).g;
        refractedScene.b = texture(u_sceneTex, refractUVBlue).b;
    } else {
        refractedScene = texture(u_sceneTex, refractUV).rgb;
    }

    /* The caustic projection that used to live here is GONE, and this note is
     * why it must not come back in this form.
     *
     * It added `sin(x + sin(y)) * sin(y + sin(x))` raised to the third power
     * straight into refractedScene. A sin*sin lattice cubed IS a set of thin
     * bright lines — and because its frequency was scaled by the water's height
     * above the receiver, those lines bent and re-spaced across the body. That
     * is the topographic-contour banding that survived four wrong diagnoses
     * (filter continuity, iterated feedback, the wave perturbation's missing
     * tangent projection, the integer filter radius). Isolating it took a
     * debug view: views 1 and 3 proved the surface and its shading were clean,
     * 9 proved the scene copy was pixel-exact, 7 proved the validity test never
     * fired — and 11 showed the pattern alone, unmistakably.
     *
     * It is also in the wrong place physically. A caustic is light focused ONTO
     * the receiver; it belongs on the ground under the water, where it stays put
     * as the water moves. Added into the water's own refracted colour it becomes
     * a decal stuck to the liquid, sliding with the surface — which is exactly
     * why it read as contour lines rather than as a pool of light. If caustics
     * are wanted, they go on the receiver (the decal system already draws on
     * ground), not into this term. */

    // Beer-Lambert Volumetric Absorption
    float materialPeak = max(max(u_materialBody.r, u_materialBody.g), max(u_materialBody.b, 0.001));
    vec3 materialTransmission = clamp(u_materialBody / materialPeak, vec3(0.035), vec3(1.0));
    /* Volume is carried by ABSORPTION, not by geometry: what tells the eye a
     * body has mass is its middle being deeper in colour than its rim. With the
     * surface now correctly smooth, a weak absorption scale leaves the whole
     * body one flat wash — the "lost its mass" complaint. The thickness range
     * this multiplies is unchanged, so every downstream mask still means what
     * it did; only the depth-of-colour across that range grows. */
    vec3 absorption = -log(materialTransmission) * 2.60 + vec3(0.06);
    vec3 transmittance = exp(-absorption * opticalPath);

    float hemi = clamp(worldNormal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(u_groundAmbient, u_skyAmbient, hemi);
    vec3 L = normalize(u_sunDirectionView);
    float ndl = max(dot(N, L), 0.0);

    vec3 waterScatterColor = u_materialBody;
    vec3 illumination = ambient + u_sunColor * (0.12 + 0.30 * ndl);
    vec3 transmitted = refractedScene * transmittance;
    float illuminationEnergy = dot(illumination, vec3(0.2126, 0.7152, 0.0722));
    float backgroundScatter = 1.0 - dot(transmittance, vec3(0.2126, 0.7152, 0.0722));
    float scatterAmount = clamp(0.30 + 0.70 * backgroundScatter, 0.0, 1.0);
    // Grazing modulation: edge-on water scatters more (long apparent optical
    // path) while the centre stays clearer so the background reads through.
    // Uniform body fill is what makes a water sphere read as a plastic ball.
    float grazingWeight = mix(0.32, 1.0, pow(1.0 - ndv, 1.5));
    vec3 inScatter = waterScatterColor * scatterAmount * (0.34 + illuminationEnergy * 0.42) * grazingWeight;

    float fresnel = FresnelSchlick(ndv);
    float volumeWeight = smoothstep(0.016, 0.095, kernelThickness);
    float visibleFresnel = fresnel * mix(0.045, 1.0, volumeWeight);
    // Fresnel rim: keeps the silhouette legible on any background (bright
    // grass included) without painting a cartoon outline - it fades to zero
    // on thin edges and is lit by the sun.
    float rimStrength = pow(1.0 - ndv, 2.0) * smoothstep(0.012, 0.10, kernelThickness);
    vec3 rimLight = u_materialGlow * rimStrength * (0.12 + 0.55 * illuminationEnergy) * (0.30 + 0.70 * ndl);
    vec3 viewWorld = normalize(mat3(u_viewToWorld) * V);
    vec3 reflectedWorld = reflect(-viewWorld, worldNormal);
    float skyReflection = smoothstep(-0.18, 0.62, reflectedWorld.y);
    // Night arenas have a dark sky ambient: without a glow floor the grazing
    // Fresnel edge reflects near-black and the ball gets a dark glossy rim -
    // the "plastic toy" silhouette. Boost the sky side with the material glow
    // only when the actual sky is dark; bright-sky maps stay unchanged.
    float skyLuma = dot(u_skyAmbient, vec3(0.2126, 0.7152, 0.0722));
    vec3 skyReflectionColor = u_skyAmbient * 1.38 + u_materialGlow * 0.65 * clamp(1.0 - skyLuma, 0.0, 1.0);
    vec3 reflection = mix(u_groundAmbient * 0.68, skyReflectionColor, skyReflection);
    float horizonReflection = pow(1.0 - abs(reflectedWorld.y), 3.0);
    reflection += u_materialSoft * (0.045 + horizonReflection * 0.10);

    // Screen Space Reflection (SSR) for High/Ultra tier
    if (u_qualityTier >= 2) {
        vec3 rayDirView = reflect(-V, N);
        vec4 ssrHit = TraceSSR(positionView, rayDirView, fluidDistance);
        if (ssrHit.a > 0.0) {
            reflection = mix(reflection, ssrHit.rgb, ssrHit.a);
        }
    }

    vec3 localReflectionFill = mix(refractedScene, u_skyAmbient, 0.30) * 0.42;
    float lowerHemisphere = 1.0 - skyReflection;
    reflection = mix(reflection, max(reflection, localReflectionFill), airborneWeight * lowerHemisphere);
    vec3 dielectricBase = mix(transmitted + inScatter, reflection, visibleFresnel);

    // Specular Highlights
    //
    // The lobes below used to be razor-thin (roughness 0.035, exponents 190 and
    // 256) while the normal above carries the capillary ripple — whose shortest
    // wavelength (2*PI/53 ~= 12 cm) is smaller than one splat. A highlight that
    // narrow, swept across a rippled dome, crosses in and out of the lobe
    // several times per splat and paints CONCENTRIC RINGS on every bulge: the
    // onion pattern that showed up on the grass and bright-arena backgrounds and
    // vanished on the dark one, where there was no specular contrast to alias.
    // Widening the lobe is the fix that costs nothing: a real water surface at
    // this scale is not a mirror, and the ripple then reads as shimmer instead
    // of as contour lines.
    float surfaceNoise = sin(dot(worldPosition, vec3(12.3, 7.1, -9.5)) + u_time * 1.1) * 0.5 + 0.5;
    float roughness = mix(0.090, 0.150, surfaceNoise);
    vec3 sunHalf = normalize(V + L);
    float broadSunLobe = pow(max(dot(N, sunHalf), 0.0), 48.0) * 0.020;
    float sharpGlint = pow(max(dot(N, sunHalf), 0.0), 96.0) * smoothstep(0.55, 0.86, surfaceNoise) * 0.22;
    vec3 specular = u_sunColor * (WaterSpecularBRDF(N, V, L, roughness) * 1.0 + broadSunLobe + sharpGlint) * ndl;
    specular *= mix(vec3(1.0), u_materialGlow, 0.08);
    // Cell-hash sparkle: sub-centimetre glints that break the smooth
    // highlight into the micro-facet flicker of real water.
    vec3 sparkleCell = floor(worldPosition * 90.0 + vec3(u_time * 2.5, u_time * 1.7, u_time * 3.1));
    float sparkleHash = fract(sin(dot(sparkleCell, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
    specular += u_sunColor * smoothstep(0.985, 1.0, sparkleHash) * 0.12 * ndl;

    for (int i = 0; i < FLUID_POINT_LIGHTS; i++) {
        if (i >= u_pointLightCount) break;
        vec3 toLight = u_pointLightPosRadius[i].xyz - positionView;
        float distanceToLight = length(toLight);
        float attenuation = clamp(1.0 - distanceToLight / max(u_pointLightPosRadius[i].w, 0.001), 0.0, 1.0);
        attenuation *= attenuation;
        if (attenuation <= 0.0) continue;
        vec3 pointL = toLight / max(distanceToLight, 0.001);
        float pointNdl = max(dot(N, pointL), 0.0);
        vec3 pointHalf = normalize(V + pointL);
        float broadPointLobe = pow(max(dot(N, pointHalf), 0.0), 44.0) * 0.070;
        specular += u_pointLightColor[i].rgb * (WaterSpecularBRDF(N, V, pointL, roughness) * 1.12 + broadPointLobe) * pointNdl * attenuation * 1.55;
    }

    // Shoreline Wetness & Edge Softening Foam
    float thicknessL = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord - vec2(u_texel.x, 0.0)).r);
    float thicknessR = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord + vec2(u_texel.x, 0.0)).r);
    float thicknessD = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord - vec2(0.0, u_texel.y)).r);
    float thicknessU = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord + vec2(0.0, u_texel.y)).r);
    float thicknessGradient = length(vec2(thicknessR - thicknessL, thicknessU - thicknessD)) * 0.5;
    
    float surfaceLaplacian = abs(-posL.z + -posR.z + -posD.z + -posU.z - 4.0 * (-positionView.z));
    float curvature = smoothstep(0.0012, 0.010, surfaceLaplacian);
    float receiverProximity = 1.0 - smoothstep(0.010, 0.070, sceneGap);
    float shoreline = smoothstep(0.004, 0.030, thicknessGradient) * receiverProximity * smoothstep(0.018, 0.080, kernelThickness);
    float crest = curvature * smoothstep(0.025, 0.085, kernelThickness) * (1.0 - smoothstep(0.16, 0.30, kernelThickness)) * 0.55 * mix(1.0, 0.16, airborneWeight);
    float foamPattern = smoothstep(0.48, 0.76, surfaceNoise);
    float foamMask = clamp(max(shoreline, crest) * foamPattern, 0.0, 0.72);
    vec3 foamColor = mix(u_materialSoft, vec3(1.0), 0.20);
    vec3 foam = foamColor * foamMask * (0.38 + 0.62 * dot(ambient, vec3(0.333333)));

    /* Each view isolates ONE stage, so the bands can be attributed instead of
     * guessed at. Read them as a decision tree:
     *   1 NORMAL     bands here => the reconstructed SURFACE is corrugated
     *                (depth capture or the filter), not the shading
     *   2 THICKNESS  bands here => the additive thickness pass
     *   3 SPECULAR   bands here but NOT in 1 => shading aliasing on a smooth
     *                surface (lobe width, wave perturbation)
     *   4 WAVE       the perturbation alone, x20; bands here => the sine field
     *   5 REFRACTION the scene tap alone; bands here => the refraction path,
     *                which views 6-9 then split:
     *   6 OFFSET     the refraction UV offset, x400 (red = x, green = y)
     *   7 VALIDITY   the SceneSampleMatchesBase test as a mask; it is a BINARY
     *                switch on a continuous field, so its boundary is an
     *                iso-depth curve — a contour line by construction
     *   8 PATH       the optical path that scales the offset
     *  10 |OFFSET|    the offset's magnitude, wrapped for contrast
     *   9 COPY       the scene copy sampled with NO offset: bands here mean the
     *                copy itself carries them and the offset maths is innocent */
    if (u_debugView != 0) {
        vec3 dbg = vec3(0.0);
        if (u_debugView == 1) dbg = N * 0.5 + 0.5;
        else if (u_debugView == 2) dbg = vec3(kernelThickness / 0.16);
        else if (u_debugView == 3) dbg = specular;
        else if (u_debugView == 4) dbg = abs(waveSlope) * 20.0;
        else if (u_debugView == 5) dbg = refractedScene;
        /* Sub-views that split the refraction path itself. 9 is the decisive
         * one: it samples the scene copy with NO offset at all, so if the bands
         * are there the COPY is contaminated and no amount of offset maths will
         * explain them; if it is clean, they come from where we sample. */
        /* Offsets are ~13 texels, i.e. ~0.01 in UV: x20 puts that mid-grey and
         * leaves room to see the sign. x400 saturated and showed nothing. */
        else if (u_debugView == 6) dbg = vec3(refractOffset * 20.0 + 0.5, 0.5);
        /* The offset's LENGTH alone, at high contrast — bands in the magnitude
         * are what a banded sample pattern on a smooth background requires. */
        else if (u_debugView == 10) dbg = vec3(fract(length(refractOffset) * 200.0));
        else if (u_debugView == 7) dbg = vec3(refractValid ? 1.0 : 0.0);
        else if (u_debugView == 8) dbg = vec3(opticalPath / 0.5);
        else if (u_debugView == 9) dbg = texture(u_sceneTex, fragTexCoord).rgb;

        finalColor = vec4(dbg, surfaceCoverage);
        return;
    }

    vec3 water = dielectricBase + specular + foam + rimLight;
    finalColor = vec4(water, surfaceCoverage);
}
