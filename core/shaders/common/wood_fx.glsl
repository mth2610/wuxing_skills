// ============================================================
// WUXING — Wood Element Common VFX Shaders (wood_fx.glsl)
// Shared utilities for Wood VFX (Vines, Branches, Leaves, Spores).
// Compatible with both Desktop GLSL 330 and GLSL ES (mobile/web).
//
// Functions provided:
//   - Wood_GrowthCurve()           : Computes smooth growth factor [0..1]
//   - Wood_GrowthDisplace()        : Extrudes/contracts un-grown tips
//   - Wood_SwayOffset()            : Multi-frequency organic branch sway
//   - Wood_SapPulse()              : Dual-speed traveling biological energy wave
//   - Wood_WitherAlbedo()          : Green -> Autumn Gold -> Bark Brown transition
//   - Wood_SubsurfaceTransmission(): Two-sided thin-leaf transmission
// ============================================================

#ifndef WOOD_FX_GLSL
#define WOOD_FX_GLSL

// ------------------------------------------------------------------
// 1. GROWTH DYNAMICS
// localProgress: normalized progress for this specific segment [0..1]
// arc: position along tube or branch length [0..1]
// tipLen: taper transition width at the growing tip (typically 0.05..0.15)
// Returns: 1.0 = fully grown, 0.0 = not yet emerged
// ------------------------------------------------------------------
float Wood_GrowthFactor(float arc, float birth, float span, float growth, float tipLen) {
    float localProgress = clamp((growth - birth) / max(span, 0.001), 0.0, 1.0);
    return 1.0 - smoothstep(localProgress - tipLen, localProgress, arc);
}

// Computes vertex position under growth extrusion
// vertexPos: original vertex coordinate
// centerPos: spine center coordinate of this ring section
// growthFactor: factor from Wood_GrowthFactor()
vec3 Wood_GrowthDisplace(vec3 vertexPos, vec3 centerPos, float growthFactor) {
    // Un-grown part collapses smoothly toward spine center with quadratic tip profile
    float factorSq = growthFactor * growthFactor;
    return centerPos + (vertexPos - centerPos) * factorSq;
}

// ------------------------------------------------------------------
// 2. ORGANIC SWAY & MASS-LAG HIERARCHY
// Bends the vine/branch with wind and mass inertia.
// arc: 0 at root (anchored, 0 movement), 1 at tip (maximum sway).
// time: current time in seconds.
// swayAmp: overall sway amplitude in meters (typically 0.05..0.20m).
// Returns: 3D displacement vector to add to vertex position.
// ------------------------------------------------------------------
vec3 Wood_SwayOffset(vec3 pos, float arc, float time, float swayAmp) {
    // Arc weighting: quadratic curve so base stays rooted while tips whip naturally
    float arcWeight = arc * arc;
    
    // Low frequency primary wind sway
    float primaryPhase = time * 2.2 - arc * 3.5;
    float primarySwayX = sin(primaryPhase) * 0.7 + sin(primaryPhase * 0.5 + 1.2) * 0.3;
    float primarySwayZ = cos(primaryPhase * 0.8 + 0.5) * 0.6;
    
    // High frequency micro-tremble
    float microPhase = time * 7.5 + arc * 6.0;
    float microSway = sin(microPhase) * 0.15;
    
    vec3 sway = vec3(primarySwayX + microSway, 0.0, primarySwayZ + microSway);
    return sway * (arcWeight * swayAmp);
}

// ------------------------------------------------------------------
// 3. SAP PULSE (Travelling Biological Luminous Pulse)
// Generates dual-speed rhythmic pulses travelling along the vine/branch.
// arc: 0 at root, 1 at tip.
// time: u_time.
// Returns: scalar brightness [0..1] with sharp energy core and soft tail.
// ------------------------------------------------------------------
float Wood_SapPulse(float arc, float time, float freq, float speed) {
    // Pulse 1: Main fast traveling sap burst
    float phase1 = fract(arc * freq - time * speed);
    float pulse1 = pow(clamp(1.0 - abs(phase1 - 0.5) * 2.0, 0.0, 1.0), 4.0);
    
    // Pulse 2: Secondary counter-harmonic slow pulse
    float phase2 = fract(arc * (freq * 0.6) - time * (speed * 0.55) + 0.35);
    float pulse2 = pow(clamp(1.0 - abs(phase2 - 0.5) * 2.0, 0.0, 1.0), 3.0);
    
    return clamp(pulse1 * 1.2 + pulse2 * 0.5, 0.0, 1.5);
}

// ------------------------------------------------------------------
// 4. WITHER & DECAY ALBEDO TRANSITION
// Naturally shifts living green foliage/bark towards autumn gold,
// then dessicated wood brown, before crumbling.
// baseColor: original lush albedo (e.g. emerald green / moss bark).
// witherProgress: 0.0 = vibrant alive, 1.0 = completely withered/decayed.
// arc: decay propagates naturally from tips (arc=1) towards roots (arc=0).
// ------------------------------------------------------------------
vec3 Wood_WitherAlbedo(vec3 baseColor, float witherProgress, float arc) {
    // Autumn golden yellow
    const vec3 autumnGold = vec3(0.85, 0.65, 0.15);
    // Dessicated dried bark brown
    const vec3 witheredBrown = vec3(0.35, 0.22, 0.10);
    
    // Tip-first decay front: tip withers ahead of base
    float decayFront = clamp(witherProgress * 1.5 - (1.0 - arc) * 0.5, 0.0, 1.0);
    
    // Phase 1 (0 -> 0.5): Green -> Golden Yellow
    float phase1 = clamp(decayFront * 2.0, 0.0, 1.0);
    vec3 colorStage1 = mix(baseColor, autumnGold, phase1);
    
    // Phase 2 (0.5 -> 1.0): Golden Yellow -> Dry Brown
    float phase2 = clamp((decayFront - 0.5) * 2.0, 0.0, 1.0);
    return mix(colorStage1, witheredBrown, phase2);
}

// ------------------------------------------------------------------
// 5. SUBSURFACE TRANSMISSION (Two-sided thin leaf/petal scattering)
// Simulates sunlight shining through translucent plant tissue.
// normal: surface normal (will be flipped if back-facing).
// lightDir: normalized vector pointing TOWARD the light source.
// viewDir: normalized vector pointing TOWARD the camera.
// thinness: tissue thinness [0..1] (1 = paper-thin petal/leaf).
// tint: subsurface translucent color (e.g. bright lime jade / glowing amber).
// ------------------------------------------------------------------
vec3 Wood_SubsurfaceTransmission(vec3 normal, vec3 lightDir, vec3 viewDir, float thinness, vec3 tint) {
    // Light passing through thin membrane in the forward direction
    float forwardScatter = pow(clamp(dot(-lightDir, viewDir), 0.0, 1.0), 3.0);
    
    // Diffuse wrap around curvature
    float wrapScatter = clamp((dot(normal, lightDir) + 0.4) / 1.4, 0.0, 1.0);
    
    float transmission = (forwardScatter * 0.85 + wrapScatter * 0.15) * thinness;
    return tint * transmission;
}

#endif // WOOD_FX_GLSL
