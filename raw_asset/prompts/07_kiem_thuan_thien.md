# Prompt 7: Thuận Thiên Kiếm (`kiem_thuan_thien.glb`)

| Thuộc Tính | Giá Trị Quy Chuẩn | Ghi Chú |
|---|---|---|
| **File Xuất** | `raw_asset/equipment/weapons/kiem_thuan_thien.glb` -> `assets/characters/equipment/weapons/kiem_thuan_thien.glb` | File GLB vũ khí rời (Standalone Socket Weapon) |
| **Slot Trang Bị** | `MODULAR_SLOT_WEAPON_R` | Binh khí cầm tay phải |
| **Dạng Gắn Kết** | Rigid Socket (Không cần gắn xương skinning) | Socket: `mixamorig:RightHand` |
| **Tọa Độ Gốc (0, 0, 0)** | Tâm điểm cán kiếm nơi lòng bàn tay phải nắm chặt | Điểm chốt cân bằng trọng tâm vũ khí |
| **Kích Thước** | Tổng chiều dài $1.15m$ (Lưỡi $85cm$, Cán $20cm$, Đốc $10cm$) | Kiếm thẳng 2 lưỡi vát kim cương chuẩn kiếm hiệp |

---

### 7.1. Prompt Tạo Ảnh Thiết Kế Concept 3D (Midjourney / Flux / DALL-E 3)
```markdown
    Bản vẽ thiết kế concept 3D Thần Kiếm Thuận Thiên (Legendary Wuxia Double-Edged Sword / Thuận Thiên Kiếm) 
    lơ lửng nằm ngang hoặc chéo trong không trung (Standalone Weapon Showcase),
    gồm các góc nhìn: toàn thân kiếm, cận cảnh chuôi kiếm/hoa văn đốc kiếm và mặt cắt kim cương lưỡi kiếm.

    TUYỆT ĐỐI KHÔNG CÓ TAY CẦM, KHÔNG CÓ NGƯỜI, KHÔNG ĐẾ ĐỨNG.

    Chi tiết thiết kế thần kiếm:
    1. Lưỡi kiếm (Blade): Kiếm thẳng 2 lưỡi sắc bén dài 85cm, bản kiếm 4.2cm, sống kiếm vát kim cương có rãnh kiếm (fuller) 
    chạy dọc khắc 4 chữ Hán 'Thuận Thiên Kiếm Ý' tỏa ánh lam quang dịu nhẹ. Thép gập hoa văn damascus bóng loáng.
    2. Đốc kiếm & Tai kiếm (Crossguard): Tai kiếm bằng đồng cổ khảm vàng hình rồng uốn lượn, ở giữa nạm một viên ngọc bích lam ngọc lục giác.
    3. Chuôi kiếm (Hilt): Cán dài 20cm bọc da cá đuối sẫm màu đan dây chỉ vàng chống trơn trượt, cầm vừa cả 1 tay hoặc 2 tay.
    4. Chuôi kiếm & Tua kiếm (Pommel & Tassel): Núm đuôi kiếm khuyên vàng cổ, đính đôi dây lụa đỏ buộc ngọc bội và tua kiếm đỏ thắm rủ dài 25cm.

    Phông nền xám tối đơn sắc sạch sẽ, ánh sáng studio lấp lánh phản chiếu kim loại thép bóng và ngọc bích, 
    chuẩn phong cách 3D game AAA của Where Winds Meet và Black Myth: Wukong.
```

---

### 7.2. Prompt Lập Trình WebGL / Three.js (Xuất File `kiem_thuan_thien.glb`)
```markdown
Bạn là một Lead Technical Artist & Kỹ sư Đồ họa WebGL / Three.js chuyên nghiệp. Hãy quan sát kỹ HÌNH ẢNH THIẾT KẾ THẦN KIẾM THUẬN THIÊN đính kèm và lập trình một ứng dụng Three.js hoàn chỉnh (chỉ trong 1 FILE HTML DUY NHẤT) để tạo mô hình 3D thanh kiếm này và XUẤT FILE `.glb` CHUẨN MỰC cho game engine C99 / Raylib 6.0 của tôi.

MÔ HÌNH NÀY LÀ BINH KHÍ GẮN SOCKET TAY PHẢI (`mixamorig:RightHand`). ĐỂ KHÔNG BỊ CẦM NGƯỢC KIẾM, KHÔNG BỊ LỆCH TÂM NẮM TAY VÀ PHẢN QUANG ÁNH KIM ĐẸP MẮT TRONG ENGINE VULKAN, BẠN BẮT BUỘC PHẢI TUÂN THỦ TUYỆT ĐỐI CÁC QUY CHUẨN KỸ THUẬT SAU:

Tên vũ khí: Thuận Thiên Kiếm (Legendary Wuxia Sword)
ID file xuất: kiem_thuan_thien.glb
Slot trang bị: MODULAR_SLOT_WEAPON_R

---

### 1. QUY CHUẨN TỌA ĐỘ VÀ ĐIỂM GẮN SOCKET (BẮT BUỘC CHÍNH XÁC 100%)
- **Tỷ lệ thế giới thực:** `1 unit = 1 meter`.
- **Gốc tọa độ (0, 0, 0):** BẮT BUỘC đặt tại ĐÚNG TÂM CỦA CÁN KIẾM (nơi ngón tay trỏ và ngón giữa của bàn tay phải nắm quanh cán kiếm).
- **Quy chuẩn định hướng các trục:**
  - `+Y`: Hướng dọc theo thân kiếm về phía MŨI KIẾM (Mũi kiếm tại $Y = +0.95m$).
  - `-Y`: Hướng về phía ĐUÔI KIẾM & TUA KIẾM (Đuôi cán tại $Y = -0.10m$, chóp tua kiếm tại $Y = -0.35m$).
  - `+Z / -Z`: Hai mặt dẹp của lưỡi kiếm (hai mặt phẳng chém).
  - `+X / -X`: Hai bên cánh tai kiếm (tai kiếm sải rộng sang hai bên mép).
- Mô hình là vật thể cứng (Rigid Mesh), **TUYỆT ĐỐI KHÔNG GẮN XƯƠNG (Không tạo Armature/Bones)**.

---

### 2. THÔNG SỐ HÌNH HỌC VÀ CÁC THÀNH PHẦN CHI TIẾT
Tổng chiều dài vũ khí: $1.15m$ ($115cm$).

1. **Cán kiếm / Chuôi cầm (Grip Handle: Y ∈ [-0.10m, +0.10m]):**
   - Chiều dài cán: $0.20m$ ($20cm$), thiết diện elip ôm tay ($2.6cm \times 2.2cm$).
   - Bọc da cá đuối sẫm màu có vân nổi nhám, đan chéo các đường chỉ vàng kim chống trơn trượt.

2. **Tai kiếm / Chắn tay (Crossguard: Y ≈ +0.10m):**
   - Bản rộng sải cánh theo trục X: $0.14m$ ($14cm$), bề dày theo trục Y: $0.035m$, chiều sâu theo trục Z: $0.028m$.
   - Hai cánh tai kiếm uốn cong vểnh nhẹ về phía mũi kiếm theo hình đầu rồng uốn lượn mạ vàng đồng cổ.
   - Chính giữa tai kiếm nạm một viên ngọc lam ngọc lục giác (Hexagonal Jade Gem) nhẵn bóng.

3. **Lưỡi kiếm thẳng 2 lưỡi (Sword Blade: Y ∈ [+0.10m, +0.95m]):**
   - Chiều dài lưỡi kiếm: $0.85m$ ($85cm$), bản rộng: $0.042m$ ($4.2cm$), vuốt thon đều về $0.024m$ trước khi hội tụ tại mũi nhọn sắc bén $Y = +0.95m$.
   - **Mặt cắt vát kim cương (Diamond Cross-section):** Sống kiếm dày $0.009m$ ở giữa, vát nghiêng góc sắc bén về 2 bên mép lưỡi kiếm.
   - **Rãnh kiếm (Fuller / Blood Groove):** Rãnh lõm rộng $0.012m$ chạy dọc từ gốc kiếm lên đến $70\%$ chiều dài lưỡi kiếm, có khắc 4 chữ triện chìm 'Thuận Thiên Kiếm Ý'.

4. **Đuôi kiếm & Tua kiếm (Pommel & Crimson Tassel: Y ∈ [-0.10m, -0.35m]):**
   - Núm đuôi kiếm: Vòng khuyên đồng cổ tròn chạm rỗng $R = 0.022m$.
   - Tua kiếm lụa đỏ: Hai dải dây lụa đan bện gắn ngọc bội nhỏ, phía dưới bung xòe chùm tua kiếm đỏ thắm rủ dài $0.25m$ mềm mại.

---

### 3. VẼ PROCEDURAL PBR CANVAS TEXTURE (512x512)
Sử dụng mã JavaScript Canvas 2D vẽ texture màu, độ nhám và độ kim loại:
- $V \in [0.45, 1.00]$ (Lưỡi kiếm): Thép rèn gập nhiều lớp Damascus `#BAC2CF` (Metalness: 0.95, Roughness: 0.16) sáng loáng phản chiếu ánh gương sắc lạnh; rãnh kiếm có hoa văn phù điêu phát ánh lam ngọc nhẹ `#40E0D0`.
- $V \in [0.20, 0.45]$ (Đốc kiếm & Núm chuôi): Đồng cổ khảm hoàng kim `#D4A843` (Metalness: 0.88, Roughness: 0.30).
- $V \in [0.10, 0.20]$ (Cán kiếm): Da cá đuối nhám sẫm `#181A1F` đan dây chỉ vàng `#ECC860`.
- $V \in [0.03, 0.10]$ (Tua kiếm): Sợi lụa đỏ son `#9E1B1B` (Metalness: 0.05, Roughness: 0.65).
- $V \in [0.00, 0.03]$ (Ngọc bích tâm kiếm): Lam ngọc lục bảo `#26A69A` (Metalness: 0.1, Roughness: 0.12).

---

### 4. GIAO DIỆN WEBGL & NÚT XUẤT FILE .GLB
- Hiển thị Canvas 3D với OrbitControls, sàn bệ đá và hệ thống đèn studio 3 điểm (Key, Fill, Rim light).
- Thanh công cụ xem trước (Inspection Tools):
  - Nút xoay kiếm chậm $360^\circ$ quanh trục Y để ngắm trọn vẹn ánh sáng phản chiếu trên lưỡi thép damascus.
  - Checkbox bật/tắt hiển thị tâm điểm cầm nắm tay phải `(0, 0, 0)`.
- Nút bấm "EXPORT KIEM_THUAN_THIEN.GLB":
  - Sử dụng `THREE.GLTFExporter`.
  - Tùy chọn xuất: `{ binary: true, embedImages: true, onlyVisible: false }`.
  - Tên file tải về: `kiem_thuan_thien.glb`.
```
