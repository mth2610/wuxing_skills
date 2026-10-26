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
        float dirLenSq = dot(u_windImpactDirection, u_windImpactDirection);
        vec2 fallbackDir = (dirLenSq > 1e-8) ? (u_windImpactDirection * inversesqrt(dirLenSq)) : vec2(1.0, 0.0);
        vec2 pushDirection = (distSq > 1e-8) ? (delta * invDist) : fallbackDir;

        // Asymmetric Friedlander-style wave profile (sharp front, smooth trailing wake)
        float signedDist = distanceToImpact - frontRadius;
        float normDist = signedDist / (signedDist >= 0.0 ? bandWidth : (bandWidth * 1.35));
        float absNorm = abs(normDist);
        float wavefront = 1.0 - smoothstep(0.25, 1.0, absNorm);

        // [VISUAL FIX]: Chuyển mượt pha hút ngược (trailing suction recoil) bằng smoothstep
        // thay cho toán tử bậc thang (? -0.15 : 1.0) gây giật cục ngọn cỏ trong 1 frame
        float trailingRecoil = mix(-0.15, 1.0, smoothstep(-bandWidth * 0.75, -bandWidth * 0.35, signedDist));
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

        // [SPIR-V FIX]: Chuẩn hóa an toàn cho pushDirection tránh lỗi normalize(vec2(0.0)) -> NaN
        vec2 radialDir = (distSq > 1e-8) ? (delta * invDist) : vec2(0.0);
        vec2 rawPush = u_windImpactDirection + radialDir * (0.22 * t);
        float pushLenSq = dot(rawPush, rawPush);
        vec2 pushDirection = (pushLenSq > 1e-8) ? (rawPush * inversesqrt(pushLenSq)) : radialDir;

        return pushDirection * (u_windImpactStrength * attenuation);
    }
}
