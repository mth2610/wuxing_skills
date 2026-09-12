uniform int u_windImpactEnabled;
uniform vec2 u_windImpactCenter;
uniform vec2 u_windImpactDirection;
uniform int u_windImpactType;
uniform float u_windImpactRadius;
uniform float u_windImpactStrength;
uniform float u_windImpactAge;

vec2 NatureDominantWindImpact(vec3 worldPosition)
{
    if (u_windImpactEnabled == 0 || u_windImpactRadius <= 0.0001)
        return vec2(0.0);

    vec2 delta = worldPosition.xz - u_windImpactCenter;
    float distanceToImpact = length(delta);
    vec2 pushDirection = u_windImpactDirection;
    float attenuation;
    if (u_windImpactType == 1) {
        // VORTICLE_RADIAL_BLAST is a pressure front, not a filled noise field:
        // every plant points away from the source and only a soft annulus bends.
        if (distanceToImpact > 0.0001)
            pushDirection = delta / distanceToImpact;
        float frontProgress = clamp(u_windImpactAge / 0.75, 0.0, 1.0);
        float frontRadius = frontProgress * u_windImpactRadius;
        float bandWidth = max(0.45, u_windImpactRadius * 0.22);
        float distanceToFront = abs(distanceToImpact - frontRadius);
        float wavefront = 1.0 - smoothstep(bandWidth * 0.35,
                                           bandWidth, distanceToFront);
        float tailFade = 1.0 - smoothstep(0.72, 1.0, u_windImpactAge);
        attenuation = wavefront * tailFade;
    } else {
        attenuation = clamp(1.0 - distanceToImpact / u_windImpactRadius,
                            0.0, 1.0);
        attenuation *= attenuation * (3.0 - 2.0 * attenuation);
    }
    return pushDirection * u_windImpactStrength * attenuation;
}
