uniform int u_windImpactEnabled;
uniform vec2 u_windImpactCenter;
uniform vec2 u_windImpactDirection;
uniform int u_windImpactType;
uniform float u_windImpactRadius;
uniform float u_windImpactStrength;
uniform float u_windImpactAge;

vec2 NatureDominantWindImpact(vec3 worldPosition)
{
    if (u_windImpactEnabled == 0 || u_windImpactRadius <= 0.0001 || abs(u_windImpactStrength) <= 1e-5)
        return vec2(0.0);

    vec2 delta = worldPosition.xz - u_windImpactCenter;
    float distSq = dot(delta, delta);

    if (u_windImpactType == 1) {
        // Early exit if blast has fully dissipated
        if (u_windImpactAge >= 1.0)
            return vec2(0.0);

        float frontProgress = clamp(u_windImpactAge * 1.3333333, 0.0, 1.0); // 1.0 / 0.75
        float frontRadius = frontProgress * u_windImpactRadius;
        float bandWidth = max(0.45, u_windImpactRadius * 0.22);

        // Asymmetric wave bounds: tighter leading shockfront, wider trailing wake
        float outerLimit = frontRadius + bandWidth;
        if (distSq >= outerLimit * outerLimit)
            return vec2(0.0);

        float innerLimit = max(0.0, frontRadius - bandWidth * 1.35);
        if (distSq <= innerLimit * innerLimit)
            return vec2(0.0);

        float invDist = inversesqrt(max(distSq, 1e-8));
        float distanceToImpact = distSq * invDist;
        vec2 pushDirection = (distSq > 1e-8) ? (delta * invDist) : u_windImpactDirection;

        // Asymmetric Friedlander-style wave profile (sharp front, smooth trailing wake)
        float signedDist = distanceToImpact - frontRadius;
        float normDist = signedDist / (signedDist >= 0.0 ? bandWidth : (bandWidth * 1.35));
        float absNorm = abs(normDist);
        float wavefront = 1.0 - smoothstep(0.25, 1.0, absNorm);

        // Subtle secondary inward suction recoil at the trailing edge of the blast ring
        float trailingRecoil = (signedDist < -bandWidth * 0.55) ? -0.15 : 1.0;
        float tailFade = 1.0 - smoothstep(0.70, 1.0, u_windImpactAge);

        return pushDirection * (u_windImpactStrength * wavefront * tailFade * trailingRecoil);
    } else {
        // Early distance-squared cull: skips sqrt() for all vertices outside radius
        float radiusSq = u_windImpactRadius * u_windImpactRadius;
        if (distSq >= radiusSq)
            return vec2(0.0);

        float invDist = inversesqrt(max(distSq, 1e-8));
        float distanceToImpact = distSq * invDist;
        float t = 1.0 - distanceToImpact / u_windImpactRadius;
        float attenuation = t * t * (3.0 - 2.0 * t);

        // Blend primary slash direction with a slight radial parting push
        vec2 radialDir = (distSq > 1e-8) ? (delta * invDist) : vec2(0.0);
        vec2 pushDirection = normalize(u_windImpactDirection + radialDir * (0.22 * t));

        return pushDirection * (u_windImpactStrength * attenuation);
    }
}