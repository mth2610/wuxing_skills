#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform vec3 u_lightTravel;
uniform float u_projectionScale;
uniform float u_widthScale;
uniform float u_tipWidth;
uniform float u_shadowStrength;

// Gộp (along, across, precomputedStrength) vào 1 vec3 duy nhất
out vec3 v_shadowData;

void main()
{
    float along = vertexTexCoord.x;
    float across = vertexTexCoord.y * 2.0 - 1.0;
    float plantHeight = vertexNormal.x;
    float shadowWidth = vertexNormal.y;

    // Tính vector hướng bóng trên mặt phẳng XZ
    vec2 travel = u_lightTravel.xz / max(-u_lightTravel.y, 0.22);
    float travelLenSq = dot(travel, travel);
    float invLen = (travelLenSq > 1e-6) ? inversesqrt(travelLenSq) : 0.0;
    vec2 direction = (travelLenSq > 1e-6) ? (travel * invLen) : vec2(1.0, 0.0);
    vec2 perpendicular = vec2(-direction.y, direction.x);

    // Giới hạn mềm tiệm cận (Soft-knee asymptotic clamp) chiều dài bóng
    float rawLength = travelLenSq * invLen;
    const float MAX_LEN = 3.2;
    float clampedLength = (rawLength * MAX_LEN) * inversesqrt(rawLength * rawLength + MAX_LEN * MAX_LEN);
    vec2 safeTravel = direction * clampedLength;

    vec3 position = vertexPosition;
    position.xz += safeTravel * (plantHeight * along * u_projectionScale);

    // Mở rộng bề ngang ở phần ngọn để bao phủ trọn hình chiếu đài cánh hoa
    float widthProfile = mix(1.0, max(u_tipWidth, 1.08), along) * u_widthScale;
    position.xz += perpendicular * (shadowWidth * across * widthProfile);

    // [SLOPE & UNDERSTOREY GROUNDING FIX]:
    // Nâng nhẹ độ cao Y dọc theo chiều dài bóng để tấm bóng không bị chìm xuống dưới mặt đất
    // trên các sườn đồi dốc nhẹ và phủ êm lên lớp gốc cỏ thấp dưới chân khóm hoa.
    position.y += 0.012 + along * 0.022;

    float horizonFade = (dot(u_lightTravel, u_lightTravel) > 1e-6)
                      ? smoothstep(-0.05, 0.12, -u_lightTravel.y)
                      : 1.0;
    float distanceAtten = mix(1.0, 0.78, along);
    float precomputedStrength = vertexColor.r * 0.76 * u_shadowStrength * distanceAtten * horizonFade;

    v_shadowData = vec3(along, across, precomputedStrength);
    gl_Position = mvp * vec4(position, 1.0);
}