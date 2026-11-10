# Prompt 2: Nón Lá Hiệp Khách (`non_la.glb`)

| Thuộc Tính | Giá Trị Quy Chuẩn | Ghi Chú |
|---|---|---|
| **File Xuất** | `raw_asset/equipment/headgear/non_la.glb` -> `assets/characters/equipment/headgear/non_la_hiep_khach.glb` | File GLB vật phẩm rời (Standalone Socket Item) |
| **Slot Trang Bị** | `MODULAR_SLOT_HEADGEAR` | Gắn cố định vào xương đầu |
| **Dạng Gắn Kết** | Rigid Socket (Không cần gắn xương skinning) | Socket: `mixamorig:Head` |
| **Tọa Độ Gốc (0, 0, 0)** | Tâm điểm tiếp xúc của vành trong nón với đỉnh đầu | Đặt tại $Y = 0.0m$ trên mặt phẳng ngang |
| **Kích Thước** | Đường kính đáy $0.82m$, chiều cao chóp $0.22m$ | Độ cong tự nhiên rủ nhẹ xuống vành ngoài |

---

### 2.1. Prompt Tạo Ảnh Thiết Kế Concept 3D (Midjourney / Flux / DALL-E 3)
```markdown
    Bản vẽ thiết kế concept 3D một chiếc Nón Lá Hiệp Khách (Wuxia Conical Bamboo Hat / Nón Lãng Tử) 
    lơ lửng trong không trung theo hiệu ứng ma-nơ-canh tàng hình (Ghost Mannequin / Standalone Hat),
    gồm 3 góc nhìn: nhìn nghiêng 45 độ từ trên xuống, nhìn thẳng mặt trước và nhìn ngửa từ dưới lên thấy rõ vành nón.

    TUYỆT ĐỐI KHÔNG CÓ NGƯỜI, KHÔNG CÓ ĐẦU NGƯỜI, KHÔNG CÓ TÓC, KHÔNG ĐẾ ĐỨNG. 

    Chi tiết thiết kế chiếc nón:
    1. Hình dáng: Nón lá chóp nhọn truyền thống dáng thanh thoát, đường kính đáy khoảng 82cm, chiều cao chóp 22cm, 
    độ cong thoai thoải rủ nhẹ xuống mép vành nón che mát khuôn mặt.
    2. Chất liệu lá & nan tre: Đan từ lá cọ/lá buông phơi nắng màu vàng rơm tự nhiên (#d8be8a), 
    có 16 vòng nan tre uốn tròn đồng tâm khâu chỉ cước tỉ mỉ và các đường gân lá tỏa đều từ đỉnh chóp.
    3. Quai nón: Hai dải lụa mềm màu đỏ thẫm (#b22222) buộc vào vành trong buông thõng mềm mại xuống dưới (dài khoảng 45cm).
    4. Vành trong: Vành tre ôm đầu tròn trịa màu nâu mộc mạc gắn đệm êm ái.

    Phông nền xám tối đơn sắc sạch sẽ, ánh sáng studio rim light sắc nét từng đường nan tre và thớ lá, 
    chuẩn phong cách 3D game AAA của Where Winds Meet và Black Myth: Wukong.
```

---

### 2.2. Prompt Lập Trình WebGL / Three.js (Xuất File `non_la.glb`)
```markdown
Bạn là một Lead Technical Artist & Kỹ sư Đồ họa WebGL / Three.js chuyên nghiệp. Hãy quan sát kỹ HÌNH ẢNH THIẾT KẾ NÓN LÁ HIỆP KHÁCH đính kèm và lập trình một ứng dụng Three.js hoàn chỉnh (chỉ trong 1 FILE HTML DUY NHẤT) để tạo mô hình 3D chiếc Nón Lá này và XUẤT FILE `.glb` CHUẨN MỰC cho game engine C99 / Raylib 6.0 của tôi.

MÔ HÌNH NÀY LÀ VẬT PHẨM TRANG BỊ GẮN RỜI (RIGID SOCKET ATTACHMENT), GẮN VÀO KHỚP ĐẦU `mixamorig:Head`. ĐỂ KHÔNG BỊ TÀNG HÌNH LÒNG NÓN KHI BẬT BACKFACE CULLING VÀ KHÔNG BỊ LỆCH TÂM ĐỈNH ĐẦU, BẠN BẮT BUỘC PHẢI TUÂN THỦ TUYỆT ĐỐI CÁC QUY CHUẨN KỸ THUẬT SAU:

Tên vật phẩm: Nón Lá Lãng Khách (Wuxia Conical Bamboo Hat)
ID file xuất: non_la.glb
Slot trang bị: MODULAR_SLOT_HEADGEAR

---

### 1. QUY CHUẨN TỌA ĐỘ VÀ ĐIỂM GẮN SOCKET (METRIC SCALE)
- **Tỷ lệ thế giới thực:** `1 unit = 1 meter`.
- **Gốc tọa độ (0, 0, 0):** Đặt tại ĐÚNG TÂM của vành đai tròn bên trong nón (nơi tiếp xúc với đỉnh chóp đầu nhân vật).
- **Trục tọa độ:**
  - `+Y`: Hướng thẳng lên đỉnh chóp nhọn của nón lá ($Y = +0.22m$).
  - `+Z`: Hướng về phía trước mặt nhân vật.
  - `+X`: Hướng sang bên trái.
- Mô hình là vật thể cứng (Rigid Mesh), **TUYỆT ĐỐI KHÔNG GẮN XƯƠNG (Không tạo Armature/Bones)** để tối ưu hiệu năng.

---

### 2. THÔNG SỐ HÌNH HỌC & CHỐNG LỖI CULLING (SOLIDIFIED 2-SIDED SHELL)
Engine C99/Vulkan mặc định BẬT Backface Culling. Nếu chỉ dùng nón hình chóp 1 lớp, khi nhìn từ dưới lên lòng nón sẽ BỊ TÀNG HÌNH HOÀN TOÀN.
BẮT BUỘC tạo hình học 2 lớp đa giác khép kín có độ dày:

1. **Vỏ nón chính (Conical Shell):**
   - Đáy nón: Bán kính ngoài $R = 0.41m$ (Đường kính $0.82m$).
   - Đỉnh chóp: Tọa độ $(0, 0.22m, 0)$.
   - Profile mặt cắt: Có độ võng tự nhiên (parabolic droop curve), mép vành nón uốn cong nhẹ xuống dưới tại bán kính ngoài.
   - Phân giải: 48 radial segments quanh vòng tròn đáy, 16 height segments dọc theo sườn nón.
   - **Tạo độ dày vỏ 2.5mm:** Vỏ nón gồm mặt ngoài (màu vàng rơm phơi nắng) và mặt trong (màu tre sẫm), hai mặt úp lưng vào nhau, normal hướng chuẩn ra hai phía.

2. **Vành ôm đầu bên trong (Inner Headband Rim):**
   - Đặt tại $Y = 0.00m$, bán kính trong $0.09m$, bán kính ngoài $0.102m$, chiều cao đai $0.018m$.
   - Mô phỏng vành nan tre đan tròn ôm lấy đỉnh đầu, ngăn cách khoảng trống giữa đầu và chóp nón.

3. **Hai dải quai lụa đỏ (Scarlet Tie Ribbons):**
   - Gắn từ 2 bên vành nón trong ($X = \pm 0.095m, Y = 0.0m$).
   - Mỗi dải lụa rộng $0.024m$ ($2.4cm$), dài $0.45m$ buông lơi tự nhiên xuống dưới ($Y: 0.0m \to -0.45m$), có độ uốn lượn nhẹ theo gió.

---

### 3. VẼ PROCEDURAL PBR CANVAS TEXTURE (512x512)
Sử dụng mã JavaScript Canvas 2D vẽ texture màu và độ nhám cho nón lá:
- $V \in [0.45, 1.00]$ (Mặt ngoài nón): Nền lá cọ/lá buông phơi nắng màu vàng rơm tự nhiên `#D8BE8A`, có các đường gân lá nan tre đồng tâm `#B89A64` khâu chỉ cước.
- $V \in [0.18, 0.45]$ (Mặt trong nón): Màu tre sẫm mộc mạc `#9E7D48` với họa tiết đan lát chéo nan tre.
- $V \in [0.08, 0.18]$ (Quai lụa): Màu lụa đỏ thắm `#B22222` viền chỉ đỏ sẫm `#801414`.
- $V \in [0.00, 0.08]$ (Vành đai ôm đầu): Màu gỗ tre nâu đậm `#5A3E22`.

---

### 4. GIAO DIỆN WEBGL & NÚT XUẤT FILE .GLB
- Hiển thị Canvas 3D với OrbitControls mượt mà, sàn bệ đá và hệ thống đèn studio 3 điểm (Key, Fill, Rim light).
- Thước đo tỉ lệ (Scale Visualizer): Có hiển thị khung bbox hoặc khối đầu tham chiếu mờ $18cm$ để kiểm tra nón đội vừa vặn.
- Nút bấm "EXPORT NON_LA.GLB":
  - Sử dụng `THREE.GLTFExporter`.
  - Tùy chọn xuất: `{ binary: true, embedImages: true, onlyVisible: false }`.
  - Tên file tải về: `non_la.glb`.
```
