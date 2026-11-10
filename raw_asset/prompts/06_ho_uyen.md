# Prompt 6: Hộ Uyển Cổ Tay Hắc Thiết (`ho_uyen.glb`)

| Thuộc Tính | Giá Trị Quy Chuẩn | Ghi Chú |
|---|---|---|
| **File Xuất** | `raw_asset/equipment/bracers/ho_uyen_left.glb` & `ho_uyen_right.glb` | File GLB vật phẩm rời (Standalone Socket Item) |
| **Slot Trang Bị** | `MODULAR_SLOT_BRACERS` | Gắn cố định vào xương cẳng tay |
| **Dạng Gắn Kết** | Rigid Socket (Không cần gắn xương skinning) | Socket: `mixamorig:LeftForeArm` / `mixamorig:RightForeArm` |
| **Tọa Độ Gốc (0, 0, 0)** | Tâm khớp cổ tay nơi tiếp giáp mu bàn tay | Cẳng tay kéo dài dọc theo trục về phía cùi chỏ |
| **Kích Thước** | Chiều dài $0.22m$ ($22cm$), bán kính thon $0.062m \to 0.052m$ | Ôm khít theo giải phẫu cẳng tay võ giả |

---

### 6.1. Prompt Tạo Ảnh Thiết Kế Concept 3D (Midjourney / Flux / DALL-E 3)
```markdown
    Bản vẽ thiết kế concept 3D một đôi Hộ Uyển Cổ Tay Võ Hiệp (Wuxia Combat Vambraces / Forearm Bracers) 
    lơ lửng trong không trung theo hiệu ứng ma-nơ-canh tàng hình (Ghost Mannequin / Standalone Bracers),
    gồm 3 góc nhìn: mặt trên mu bàn tay (top dorsal), mặt dưới lòng cổ tay (inner straps) và góc nghiêng 45 độ.

    TUYỆT ĐỐI KHÔNG CÓ CÁNH TAY, KHÔNG CÓ BÀN TAY, KHÔNG NGƯỜI. Bên trong hộ uyển hoàn toàn rỗng ruột.

    Chi tiết thiết kế hộ uyển:
    1. Kiểu dáng: Đôi hộ uyển giáp tay thon gọn theo giải phẫu cẳng tay (dài 22cm, thon từ khuỷu tay 6.2cm xuống cổ tay 5.2cm).
    2. Mặt ngoài (Mu bàn tay & Cẳng tay): Phiến giáp thép đen tôi lạnh kết hợp nẹp đồng cổ chạm khắc nổi hình rồng cuộn/hổ phù uy dũng.
    3. Mặt trong (Lòng cánh tay): Đệm lót da mềm màu nâu thẫm, cố định bằng 3 quai da có khóa cài kim loại mini chắc chắn.

    Phông nền xám tối đơn sắc sạch sẽ, ánh sáng studio rim light phản chiếu ánh kim loại thép đen và đồng cổ bóng mờ, 
    chuẩn phong cách 3D game AAA của Where Winds Meet và Black Myth: Wukong.
```

---

### 6.2. Prompt Lập Trình WebGL / Three.js (Xuất File `ho_uyen.glb`)
```markdown
Bạn là một Lead Technical Artist & Kỹ sư Đồ họa WebGL / Three.js chuyên nghiệp. Hãy quan sát kỹ HÌNH ẢNH THIẾT KẾ ĐÔI HỘ UYỂN CỔ TAY đính kèm và lập trình một ứng dụng Three.js hoàn chỉnh (chỉ trong 1 FILE HTML DUY NHẤT) để tạo mô hình 3D đôi Hộ Uyển Giáp Tay này và XUẤT FILE `.glb` CHUẨN MỰC cho game engine C99 / Raylib 6.0 của tôi.

MÔ HÌNH NÀY LÀ VẬT PHẨM TRANG BỊ GẮN RỜI (RIGID SOCKET ATTACHMENT), GẮN VÀO KHỚP CẲNG TAY `mixamorig:LeftForeArm` VÀ `mixamorig:RightForeArm`. ĐỂ KHÔNG BỊ TÀNG HÌNH LÒNG HỘ UYỂN KHI BẬT BACKFACE CULLING VÀ KHÔNG BỊ LỆCH TÂM CỔ TAY, BẠN BẮT BUỘC PHẢI TUÂN THỦ TUYỆT ĐỐI CÁC QUY CHUẨN KỸ THUẬT SAU:

Tên vật phẩm: Hộ Uyển Hắc Thiết (Wuxia Combat Vambraces)
ID file xuất: ho_uyen_left.glb và ho_uyen_right.glb
Slot trang bị: MODULAR_SLOT_BRACERS

---

### 1. QUY CHUẨN TỌA ĐỘ VÀ ĐIỂM GẮN SOCKET (METRIC SCALE)
- **Tỷ lệ thế giới thực:** `1 unit = 1 meter`.
- **Gốc tọa độ (0, 0, 0):** Đặt tại ĐÚNG TÂM vòng tròn cổ tay (nơi tiếp xúc giữa cẳng tay và cổ tay).
- **Trục tọa độ:**
  - `+Y`: Hướng dọc theo cẳng tay từ cổ tay ($Y = 0.0m$) lên đến cùi chỏ ($Y = +0.22m$).
  - `+Z`: Hướng lên mu cẳng tay (mặt trên có phiến giáp thép chính).
  - `-Z`: Hướng vào lòng cẳng tay (mặt dưới có 3 dây đai da buộc khóa).
  - `+X / -X`: Sang hai bên mép cẳng tay.
- Mô hình là vật thể cứng (Rigid Mesh), **TUYỆT ĐỐI KHÔNG GẮN XƯƠNG (Không tạo Armature/Bones)**.

---

### 2. THÔNG SỐ HÌNH HỌC & CHỐNG LỖI CULLING (SOLIDIFIED 2-SIDED SHELL)
Engine C99/Vulkan mặc định BẬT Backface Culling. BẮT BUỘC tạo hình học 2 lớp đa giác khép kín có độ dày $2mm - 3mm$ (Solidified Shell) để nhìn từ miệng ống vào không bị tàng hình:

1. **Ống cẳng tay vuốt thon giải phẫu (Anatomical Tapered Cuff):**
   - Chiều dài: $0.22m$ ($22cm$).
   - Miệng cổ tay ($Y = 0.00m$): Bán kính trong $0.052m$, bán kính ngoài $0.055m$.
   - Miệng cùi chỏ ($Y = 0.22m$): Bán kính trong $0.062m$, bán kính ngoài $0.065m$.
   - Cắt xéo nhẹ ở khớp cổ tay: Phía mu tay vươn dài thêm $0.025m$ ($2.5cm$) để bảo vệ mu bàn tay; phía lòng bàn tay khoét lõm nhẹ để không cản trở cử động gập cổ tay.

2. **Phiến giáp thép đen mặt trên (Dorsal Steel Plate):**
   - Phiến giáp hình mai rùa cong theo mu cẳng tay, viền vát cạnh (chamfered bevel).
   - Trên sống lưng phiến giáp có nẹp đồng cổ chạm nổi hoa văn rồng cuộn / hổ phù uy nghiêm.

3. **Ba đai da và khóa cài mặt dưới (Underarm Straps & Buckles):**
   - 3 vòng đai da bản rộng $0.015m$ đặt tại $Y = 0.04m, 0.11m, 0.18m$.
   - Mỗi quai da có một khóa kim loại vuông mạ đồng cổ đính đinh tán mini ở má ngoài.

---

### 3. VẼ PROCEDURAL PBR CANVAS TEXTURE (512x512)
Sử dụng mã JavaScript Canvas 2D vẽ texture màu, độ nhám và độ kim loại:
- $V \in [0.45, 1.00]$ (Phiến giáp chính): Thép đen tôi hàn tuyền `#1C1E24` (Metalness: 0.85, Roughness: 0.28) với ánh phản quang kim loại lạnh.
- $V \in [0.20, 0.45]$ (Đai da & Đệm lót trong): Da thuộc dầu nâu sẫm `#2B2018` (Metalness: 0.0, Roughness: 0.65) có nếp nhăn viền chỉ may.
- $V \in [0.08, 0.20]$ (Hoa văn phù điêu & Nẹp giáp): Đồng cổ chạm nổi khảm vàng mờ `#D4A843` (Metalness: 0.80, Roughness: 0.35).
- $V \in [0.00, 0.08]$ (Khóa nẹp & Đinh tán): Khóa đồng thau cổ `#A6783A`.

---

### 4. GIAO DIỆN WEBGL & HAI NÚT XUẤT FILE .GLB
- Hiển thị Canvas 3D với OrbitControls mượt mà, sàn bệ đá và hệ thống đèn studio 3 điểm (Key, Fill, Rim light).
- Hộp kiểm (Checkbox): Chuyển đổi hiển thị giữa Hộ Uyển Tay Trái và Hộ Uyển Tay Phải (chiếc bên phải được lật đối xứng qua trục Z: $X \to -X$).
- **Hai nút xuất file độc lập:**
  1. Nút **"EXPORT LEFT BRACER GLB"** -> Tải về file `ho_uyen_left.glb`.
  2. Nút **"EXPORT RIGHT BRACER GLB"** -> Tải về file `ho_uyen_right.glb`.
  - Tùy chọn GLTFExporter: `{ binary: true, embedImages: true, onlyVisible: false }`.
```
