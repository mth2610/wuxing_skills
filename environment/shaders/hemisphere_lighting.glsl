#ifndef WUXING_ENV_HEMISPHERE_LIGHTING_GLSL
#define WUXING_ENV_HEMISPHERE_LIGHTING_GLSL

// World-space unit normal and linear hemisphere colors from Environment.
// Keep ambient independent of cloud/directional shadow visibility.
vec3 Environment_HemisphereIrradiance(vec3 unitNormal, vec3 skyRadiance, vec3 groundRadiance) {
    float skyWeight = clamp(unitNormal.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(groundRadiance, skyRadiance, skyWeight);
}

// Same split as Environment_GetSkyAmbient / Environment_GetGroundAmbient.
// Preserve the configured day/night intensity instead of imposing a floor.
vec3 Environment_HemisphereIrradiance(vec3 unitNormal, vec3 ambientRadiance) {
    vec3 sky = clamp(ambientRadiance * vec3(1.25, 1.25, 1.35), 0.0, 1.0);
    vec3 ground = clamp(ambientRadiance * vec3(0.55, 0.45, 0.40), 0.0, 1.0);
    return Environment_HemisphereIrradiance(unitNormal, sky, ground);
}

#endif
