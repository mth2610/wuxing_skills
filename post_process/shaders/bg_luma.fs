#version 330

// BACKGROUND LUMINANCE — a wide 4-tap downsample of the scene, luma only.
//
// Captured AFTER the world is drawn and BEFORE any VFX, which is the whole
// point: an effect that asks "how bright is what I am standing in front of"
// must not be told about itself.

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 u_srcTexel;      // 1 / source size

out vec4 finalColor;

float SafeCompressedLuma(vec3 rgb) {
    // Chặn NaN/âm từ buffer HDR và nén mềm các điểm cháy sáng (specular fireflies)
    // trước khi trung bình cộng để 1 pixel chói không làm sai lệch cả ô 16x16.
    vec3 safe = clamp(rgb, vec3(0.0), vec3(65000.0));
    float l = dot(safe, vec3(0.2126, 0.7152, 0.0722));
    return l / (1.0 + l * 0.25);
}

void main() {
    // Dịch chuyển ±3.5 texel nguồn để 4 tap đại diện cho 4 góc phần tư của khối 16x16,
    // giảm hiện tượng nhấp nháy độ sáng khi camera di chuyển.
    vec4 ofs = u_srcTexel.xyxy * vec4(-3.5, -3.5, 3.5, 3.5);

    float l0 = SafeCompressedLuma(texture(texture0, fragTexCoord + ofs.xy).rgb);
    float l1 = SafeCompressedLuma(texture(texture0, fragTexCoord + ofs.zy).rgb);
    float l2 = SafeCompressedLuma(texture(texture0, fragTexCoord + ofs.xw).rgb);
    float l3 = SafeCompressedLuma(texture(texture0, fragTexCoord + ofs.zw).rgb);

    float avgCompressed = (l0 + l1 + l2 + l3) * 0.25;
    // Khôi phục lại dải luma tuyến tính tương ứng trong [0..1] trước khi ghi ra RGBA8
    float luma = avgCompressed / max(1.0 - avgCompressed * 0.25, 0.2);

    finalColor = vec4(vec3(clamp(luma, 0.0, 1.0)), 1.0);
}