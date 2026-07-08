#version 330

in vec2 fragTexCoord;

// Đã XÓA biến: in vec3 fragNormal;

uniform vec4 colDiffuse;
uniform sampler2D texture0; // Splatmap
uniform sampler2D texGrass; // Ảnh cỏ
uniform sampler2D texPath;  // Ảnh đường đi

uniform vec2 tiling;

uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;

out vec4 finalColor;

void main()
{
    // 1. TẠM THỜI gán mask = 1.0 để ép hiển thị 100% CỎ. 
    // Khi nào bạn thiết kế xong ảnh Splatmap chuẩn thì mở comment dòng dưới và xóa dòng mask = 1.0 đi.
    // float mask = texture(texture0, fragTexCoord).r;
    float mask = 1.0; 

    // Lấy màu Texture
    vec2 tiledUV = fragTexCoord * tiling;
    vec4 colorGrass = texture(texGrass, tiledUV);
    vec4 colorPath  = texture(texPath, tiledUV);
    vec4 mixedTex = mix(colorPath, colorGrass, mask);

    // 2. Tính toán Pháp tuyến (Normal) tĩnh cho mặt phẳng ngang
    vec3 normal = vec3(0.0, 1.0, 0.0); 

    // 3. Bảo vệ hướng sáng (Phòng trường hợp vector lightDir bị bằng 0 từ code C)
    vec3 light = vec3(0.0, 1.0, 0.0);
    if (length(lightDir) > 0.1) {
        light = normalize(-lightDir);
    }
    
    float NdotL = max(dot(normal, light), 0.0);
    
    // 4. Bảo vệ ánh sáng môi trường (Nếu C gửi sang màu Đen (Alpha=0), tự động dùng màu xám sáng)
    vec4 actualAmbient = ambientColor;
    if (actualAmbient.a == 0.0) {
        actualAmbient = vec4(0.4, 0.4, 0.4, 1.0); 
    }
    
    vec4 actualLight = lightColor;
    if (actualLight.a == 0.0) {
        actualLight = vec4(1.0, 1.0, 1.0, 1.0);
    }

    vec4 totalLight = actualAmbient + (actualLight * NdotL);

    // Xuất màu cuối
    finalColor = mixedTex * colDiffuse * totalLight;
}