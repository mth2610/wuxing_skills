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

    // VISUAL UPGRADE: Giới hạn mềm chiều dài bóng để không bị kéo giãn quá mức khi nắng xế
    float rawLength = travelLenSq * invLen;
    float clampedLength = min(rawLength, 3.2); 
    vec2 safeTravel = direction * clampedLength;

    vec3 position = vertexPosition;
    position.xz += safeTravel * (plantHeight * along * u_projectionScale);
    
    // Mở rộng nhẹ bề ngang khi bóng vươn xa (hiệu ứng tán xạ vùng nửa tối - Penumbra)
    float widthProfile = mix(1.0, u_tipWidth, along) * u_widthScale;
    position.xz += perpendicular * (shadowWidth * across * widthProfile);

    // TỐI ƯU: Nhân sẵn hằng số cường độ bóng và độ suy giảm theo khoảng cách ngay tại đỉnh
    // Gốc cây (along = 0) đậm hơn, ngọn bóng (along = 1) nhạt dần tự nhiên
    float distanceAtten = mix(1.0, 0.65, along);
    float precomputedStrength = vertexColor.r * 0.58 * u_shadowStrength * distanceAtten;

    v_shadowData = vec3(along, across, precomputedStrength);
    gl_Position = mvp * vec4(position, 1.0);
}