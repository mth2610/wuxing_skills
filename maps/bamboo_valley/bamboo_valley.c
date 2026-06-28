#include "bamboo_valley.h"
#include "raylib.h"
#include "rlgl.h"
#include "../../environment/environment_system.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static float s_time = 0.0f;
static Model s_bambooModel;
static bool s_bambooLoaded = false;

// Helper vẽ cây tre thực tế dùng Model 3D tải từ ngoài (.glb)
static void DrawBamboo(Vector3 pos, float heightScale, float rotAngle) {
    // 1. Bóng đổ cho toàn bộ cây tre
    Environment_DrawSmartShadow(pos, ENV_SHAPE_CYLINDER, 15.0f, 90.0f);
    
    // 2. Vẽ Model 3D tre
    if (s_bambooLoaded) {
        Vector3 rotationAxis = { 0.0f, 1.0f, 0.0f }; // Xoay quanh trục Y
        // Mặc định scale nhân thêm 3.5f vì mô hình 3D thô tải về thường nhỏ hơn đơn vị game
        float finalScale = heightScale * 3.5f; 
        Vector3 scaleVec = { finalScale, finalScale, finalScale };
        
        DrawModelEx(s_bambooModel, pos, rotationAxis, rotAngle, scaleVec, WHITE);
    }
}

// Helper vẽ ngôi nhà cổ trang kiểu Pagoda châu Á
static void DrawAsianHouse(Vector3 pos) {
    // 1. Bóng đổ hình hộp cho nền nhà
    Environment_DrawSmartShadow(pos, ENV_SHAPE_BOX, 70.0f, 40.0f);
    
    // 2. Móng nhà (Đá tảng)
    DrawCube((Vector3){ pos.x, pos.y + 4.0f, pos.z }, 80.0f, 8.0f, 80.0f, GetColor(0x424242ff));
    DrawCubeWires((Vector3){ pos.x, pos.y + 4.0f, pos.z }, 80.0f, 8.0f, 80.0f, GetColor(0x616161ff));
    
    // 3. Thân nhà (Gỗ đỏ nâu)
    DrawCube((Vector3){ pos.x, pos.y + 24.0f, pos.z }, 60.0f, 32.0f, 60.0f, GetColor(0x5c2518ff));
    DrawCubeWires((Vector3){ pos.x, pos.y + 24.0f, pos.z }, 60.0f, 32.0f, 60.0f, GetColor(0x3b160eff));
    
    // Cửa ra vào & cửa sổ
    DrawCube((Vector3){ pos.x, pos.y + 16.0f, pos.z - 30.2f }, 15.0f, 24.0f, 0.5f, GetColor(0x212121ff)); // Cửa đi
    DrawCube((Vector3){ pos.x - 20.0f, pos.y + 20.0f, pos.z - 30.2f }, 10.0f, 10.0f, 0.5f, GetColor(0xd7ccc8ff)); // Cửa sổ
    DrawCube((Vector3){ pos.x + 20.0f, pos.y + 20.0f, pos.z - 30.2f }, 10.0f, 10.0f, 0.5f, GetColor(0xd7ccc8ff));
    
    // 4. Mái nhà 2 tầng đặc trưng (Dùng DrawCylinder 4 cạnh để tạo Mái chóp tứ giác)
    // Tầng mái dưới (Rộng)
    Vector3 roofLowerPos = { pos.x, pos.y + 40.0f, pos.z };
    DrawCylinder(roofLowerPos, 0.0f, 50.0f, 12.0f, 4, GetColor(0x1a2421ff)); // Mái ngói đen xám
    DrawCylinderWires(roofLowerPos, 0.0f, 50.0f, 12.0f, 4, GetColor(0x2b3834ff));
    
    // Tầng mái trên (Chóp nhọn cao)
    Vector3 roofUpperPos = { pos.x, pos.y + 52.0f, pos.z };
    DrawCylinder(roofUpperPos, 0.0f, 35.0f, 18.0f, 4, GetColor(0x1a2421ff));
    DrawCylinderWires(roofUpperPos, 0.0f, 35.0f, 18.0f, 4, GetColor(0x2b3834ff));
    
    // Đỉnh mái (Hồ lô phát sáng ngọc)
    DrawSphere((Vector3){ pos.x, pos.y + 62.0f, pos.z }, 4.0f, GetColor(0xa7ffebff));
}

void InitBambooValleyMap(void) {
    // Nạp mô hình 3D cây tre từ tệp tin glb trong assets
    s_bambooModel = LoadModel("assets/models/bamboo.glb");
    s_bambooLoaded = true;

    // Không khí đêm xanh ngọc bích sương mù (Jade Night)
    Environment_SetAmbientColor((Color){ 8, 16, 12, 255 }); 
    Environment_SetSunColor((Color){ 75, 105, 90, 255 });    
    Environment_SetSunDirection((Vector3){ 0.6f, -0.7f, -0.4f }); 
    Environment_SetShadowColor((Color){ 2, 6, 4, 190 });

    EnvFogConfig fog = {0};
    fog.enabled = true;
    fog.color = (Color){ 8, 16, 12, 255 }; 
    fog.start = 450.0f;
    fog.end = 2200.0f;
    fog.density = 1.0f;
    Environment_SetFogConfig(fog);
}

void DrawBambooValleyMap(void) {
    Vector3 center = { 600.0f, 0.0f, 440.0f };
    float radius = 1805.0f;
    float lakeRadius = 320.0f;

    // --- BƯỚC 1: VẼ NỀN THẢO NGUYÊN (MÀU RÊU ĐÊM ĐỤC TUYỆT ĐỐI) ---
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    Color cMossCenter = GetColor(0x182c16ff); // Xanh rêu
    Color cMossEdge = GetColor(0x0a1409ff);   // Tối đen viền map
    int segments = 64;
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p1 = { center.x + cosf(a1) * radius, center.y - 0.2f, center.z + sinf(a1) * radius };
        Vector3 p2 = { center.x + cosf(a2) * radius, center.y - 0.2f, center.z + sinf(a2) * radius };
        
        rlColor4ub(cMossCenter.r, cMossCenter.g, cMossCenter.b, 255);
        rlVertex3f(center.x, center.y - 0.2f, center.z);
        rlColor4ub(cMossEdge.r, cMossEdge.g, cMossEdge.b, 255);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cMossEdge.r, cMossEdge.g, cMossEdge.b, 255);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();

    // --- BƯỚC 2: VẼ ĐỊA HÌNH GỒ GHỀ (PHE CẦU CHÌM TẠO MÔ ĐẤT LỒI LÕM) ---
    // Rải rác các mô đất xung quanh đài đấu để tăng tính 3D
    Color cHillColor = GetColor(0x132311ff);
    DrawSphere((Vector3){ center.x - 500.0f, -50.0f, center.z + 400.0f }, 150.0f, cHillColor);
    DrawSphere((Vector3){ center.x + 600.0f, -80.0f, center.z - 300.0f }, 220.0f, cHillColor);
    DrawSphere((Vector3){ center.x - 300.0f, -40.0f, center.z - 500.0f }, 120.0f, cHillColor);
    DrawSphere((Vector3){ center.x + 400.0f, -60.0f, center.z + 600.0f }, 180.0f, cHillColor);

    // --- BƯỚC 3: VẼ HỒ NƯỚC TĨNH LẶNG (ĐỤC HOÀN TOÀN, ALPHA = 255) ---
    rlBegin(RL_TRIANGLES);
    Color cLakeCenter = GetColor(0x132d30ff); // Xanh lục lục phản ánh trăng
    Color cLakeEdge = GetColor(0x081516ff);
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        
        // Sóng hồ gợn nhẹ real-time
        float w1 = sinf(a1 * 8.0f + s_time * 2.0f) * 6.0f;
        float w2 = sinf(a2 * 8.0f + s_time * 2.0f) * 6.0f;
        Vector3 p1 = { center.x + cosf(a1) * (lakeRadius + w1), center.y, center.z + sinf(a1) * (lakeRadius + w1) };
        Vector3 p2 = { center.x + cosf(a2) * (lakeRadius + w2), center.y, center.z + sinf(a2) * (lakeRadius + w2) };
        
        rlColor4ub(cLakeCenter.r, cLakeCenter.g, cLakeCenter.b, 255);
        rlVertex3f(center.x, center.y, center.z);
        rlColor4ub(cLakeEdge.r, cLakeEdge.g, cLakeEdge.b, 255);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cLakeEdge.r, cLakeEdge.g, cLakeEdge.b, 255);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();

    // --- BƯỚC 4: VẼ NGÔI NHÀ CỔ TRANG ---
    Vector3 housePos = { center.x + 400.0f, center.y, center.z - 280.0f };
    DrawAsianHouse(housePos);

    // --- BƯỚC 5: VẼ RỪNG TRE VÀ THẢM HOA CẠNH HỒ ---
    // Cụm tre 1 (Bên trái)
    DrawBamboo((Vector3){ center.x - 450.0f, center.y, center.z - 200.0f }, 1.1f, 10.0f);
    DrawBamboo((Vector3){ center.x - 490.0f, center.y, center.z - 240.0f }, 1.3f, -5.0f);
    DrawBamboo((Vector3){ center.x - 410.0f, center.y, center.z - 170.0f }, 0.9f, 15.0f);
    
    // Cụm tre 2 (Phía sau nhà)
    DrawBamboo((Vector3){ center.x + 350.0f, center.y, center.z - 400.0f }, 1.2f, -8.0f);
    DrawBamboo((Vector3){ center.x + 480.0f, center.y, center.z - 410.0f }, 1.4f, 5.0f);

    // Bụi hoa dại cạnh hồ (dùng DrawCylinder flat 6 cạnh làm bụi hoa)
    Vector3 flowerPos1 = { center.x - 200.0f, center.y, center.z + 250.0f };
    Vector3 flowerPos2 = { center.x + 220.0f, center.y, center.z + 180.0f };
    
    Environment_DrawSmartShadow(flowerPos1, ENV_SHAPE_CYLINDER, 10.0f, 15.0f);
    DrawCylinder(flowerPos1, 10.0f, 8.0f, 15.0f, 6, GetColor(0xe040fbff)); // Hoa màu hồng tím dại
    
    Environment_DrawSmartShadow(flowerPos2, ENV_SHAPE_CYLINDER, 12.0f, 18.0f);
    DrawCylinder(flowerPos2, 12.0f, 6.0f, 18.0f, 6, GetColor(0xffd740ff)); // Hoa màu vàng kim dại

    // --- BƯỚC 6: VẼ TRĂNG KHUYẾT PHƯƠNG XA (MẶT TRĂNG BẠC) ---
    // Nằm sâu về phía -Z để thấy chéo qua màn hình
    Vector3 moonPos = { center.x - 700.0f, 400.0f, center.z - 1400.0f };
    DrawSphere(moonPos, 160.0f, GetColor(0xe0f7faff)); // Trăng sáng ngọc lam
}

void UpdateBambooValleyMap(float dt) {
    s_time += dt;
}

void UnloadBambooValleyMap(void) {
    if (s_bambooLoaded) {
        UnloadModel(s_bambooModel);
        s_bambooLoaded = false;
    }
}
