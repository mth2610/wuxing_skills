# Prompt 4: Quần Võ Hiệp Huyền Sắc (`quan_wuxia.glb`)

| Thuộc Tính | Giá Trị Quy Chuẩn | Ghi Chú |
|---|---|---|
| **File Xuất** | `raw_asset/equipment/legs/quan_wuxia.glb` -> `assets/characters/equipment/legs/quan_wuxia.glb` | File GLB trang phục hạ phục (Modular SkinnedMesh) |
| **Slot Trang Bị** | `MODULAR_SLOT_LEGS` | Hạ phục nhân vật võ hiệp |
| **Hệ Khung Xương** | Đúng chuẩn 20 xương Mixamo (`mixamorig:*`) | Tương thích 100% với `humanoid_anims.glb` |
| **Quy Chuẩn Kích Thước** | Cạp quần $Y = 0.94m$, Đũng $Y = 0.72m$, Ống xà cạp cổ chân $Y = 0.10m$ | Bán kính đùi $0.11m - 0.13m$, cổ chân thon $0.052m$ |
| **Chống Xuyên Thấu** | Lồng ngoài đùi deflated ($r = 0.06m$), nhét gọn trong cổ ủng ($r = 0.058m$) | Triệt tiêu 100% hiện tượng da thịt chọc thủng quần |

---

### 4.1. Prompt Tạo Ảnh Thiết Kế Concept 3D (Midjourney / Flux / DALL-E 3)
```markdown
    Bản vẽ thiết kế concept 3D một chiếc Quần Võ Hiệp Huyền Sắc (Wuxia Combat Pants / Hakama Trousers) 
    lơ lửng trong không trung theo hiệu ứng ma-nơ-canh tàng hình (Ghost Mannequin / Standalone Garment),
    gồm 3 góc nhìn: mặt trước (front), mặt bên (side) và góc nghiêng 45 độ đặt cạnh nhau trên cùng một khung hình.

    TUYỆT ĐỐI KHÔNG CÓ NGƯỜI, KHÔNG CÓ THÂN MÌNH, KHÔNG ÁO, KHÔNG GIÀY, KHÔNG ĐẾ ĐỨNG. 
    Bên trong cạp quần và ống quần hoàn toàn rỗng ruột.

    Chi tiết thiết kế chiếc quần:
    1. Kiểu dáng: Quần thụng võ hiệp dáng rộng thoải mái phía trên đùi, bo thon gọn dần từ đầu gối xuống cổ chân 
    theo kiểu quần xà cạp truyền thống phương Đông.
    2. Cạp quần & Đai lưng: Bo cạp màu đen than chắc chắn, đũng quần thụng có nếp gấp vải tự nhiên giúp xoạc chân linh hoạt.
    3. Thân quần: Vải thô dệt dày dặn màu đen huyền sắc (charcoal black #1E2028), có độ rủ mềm mại.
    4. Xà cạp bó chân (Gaiters / Shin Wraps): Từ bắp chân xuống đến gấu quần quấn nhiều vòng đai xà cạp màu xám tro (#3A3D48) 
    đan chéo chữ X ôm sát cẳng chân, viền chỉ bạc chìm tinh tế.

    Phông nền xám tối đơn sắc sạch sẽ, ánh sáng studio rim light sắc nét từng thớ vải và đường chỉ may, 
    chuẩn phong cách 3D game AAA của Where Winds Meet và Black Myth: Wukong.
```

---

### 4.2. Prompt Lập Trình WebGL / Three.js (Xuất File `quan_wuxia.glb`)
```markdown
Bạn là một Lead Technical Artist & Kỹ sư Đồ họa WebGL / Three.js chuyên nghiệp. Hãy quan sát kỹ HÌNH ẢNH THIẾT KẾ CHIẾC QUẦN VÕ HIỆP đính kèm và lập trình một ứng dụng Three.js hoàn chỉnh (chỉ trong 1 FILE HTML DUY NHẤT) để tạo mô hình 3D SkinnedMesh Quần Võ Hiệp Huyền Sắc này và XUẤT FILE `.glb` CHUẨN MỰC cho game engine C99 / Raylib 6.0 của tôi.

MÔ HÌNH NÀY LÀ TRANG PHỤC HẠ PHỤC CỦA NHÂN VẬT, CO GẬP THEO CÁC KHỚP HÔNG, ĐÙI, ĐẦU GỐI VÀ CỔ CHÂN TRONG TỪNG ĐÒN ĐÁNH CƯỚC PHÁP. ĐỂ KHÔNG BỊ RÁCH ĐŨNG KHI XOẠC CHÂN, KHÔNG BỊ TÀNG HÌNH LÒNG ỐNG QUẦN KHI BẬT BACKFACE CULLING VÀ KHÔNG BỊ ĐƠ T-POSE TRONG RAYLIB, BẠN BẮT BUỘC PHẢI TUÂN THỦ TUYỆT ĐỐI CÁC QUY CHUẨN KỸ THUẬT SAU:

Tên trang phục: Quần Võ Hiệp Huyền Sắc (Wuxia Combat Pants)
ID file xuất: quan_wuxia.glb
Slot trang bị: MODULAR_SLOT_LEGS

---

### 1. BỘ XƯƠNG BẮT BUỘC: ĐÚNG CHÍNH XÁC 20 XƯƠNG MIXAMO (KHÔNG TẠO XƯƠNG PHỤ)
Game engine C99 / Raylib của tôi kiểm tra nghiêm ngặt `model.skeleton.boneCount == 20`. Khác 20 xương sẽ BỊ ĐƠ T-POSE VÀ TỪ CHỐI HOẠT ẢNH.
Quần biến dạng chính theo 6 xương chân dưới:
- `mixamorig:Hips` (Root tại Y = 0.95m - điều khiển cạp quần và xương chậu)
- `mixamorig:LeftUpLeg` (Khớp háng trái, Y = 0.90m) & `mixamorig:RightUpLeg` (Khớp háng phải, Y = 0.90m)
- `mixamorig:LeftLeg` (Khớp gối trái, Y = 0.48m) & `mixamorig:RightLeg` (Khớp gối phải, Y = 0.48m)
- `mixamorig:LeftFoot` (Khớp cổ chân trái, Y = 0.08m) & `mixamorig:RightFoot` (Khớp cổ chân phải, Y = 0.08m)
Các xương còn lại trong 20 xương (Spine, Cổ, Đầu, Tay...) BẮT BUỘC vẫn phải có mặt trong Skeleton nhưng có trọng số gán đỉnh bằng 0.

Đoạn mã Three.js BẮT BUỘC tạo đúng cây xương sau (chuẩn Mixamo metric scale):
```javascript
const bones = [];

// 0: Hips (Root)
const hips = new THREE.Bone(); hips.name = "mixamorig:Hips"; hips.position.set(0.00, 0.95, 0.00); bones.push(hips);

// 1-5: Cột sống, Cổ, Đầu
const spine = new THREE.Bone(); spine.name = "mixamorig:Spine"; spine.position.set(0.00, 0.13, 0.00); hips.add(spine); bones.push(spine);
const spine1 = new THREE.Bone(); spine1.name = "mixamorig:Spine1"; spine1.position.set(0.00, 0.14, 0.00); spine.add(spine1); bones.push(spine1);
const spine2 = new THREE.Bone(); spine2.name = "mixamorig:Spine2"; spine2.position.set(0.00, 0.14, 0.00); spine1.add(spine2); bones.push(spine2);
const neck = new THREE.Bone(); neck.name = "mixamorig:Neck"; neck.position.set(0.00, 0.16, 0.00); spine2.add(neck); bones.push(neck);
const head = new THREE.Bone(); head.name = "mixamorig:Head"; head.position.set(0.00, 0.14, 0.00); neck.add(head); bones.push(head);

// 6-13: Tay trái & Tay phải
const lShoulder = new THREE.Bone(); lShoulder.name = "mixamorig:LeftShoulder"; lShoulder.position.set(0.08, 0.12, 0.00); lShoulder.quaternion.set(-0.5, -0.5, -0.5, 0.5); spine2.add(lShoulder); bones.push(lShoulder);
const lArm = new THREE.Bone(); lArm.name = "mixamorig:LeftArm"; lArm.position.set(0.00, 0.14, 0.00); lShoulder.add(lArm); bones.push(lArm);
const lForeArm = new THREE.Bone(); lForeArm.name = "mixamorig:LeftForeArm"; lForeArm.position.set(0.00, 0.26, 0.00); lArm.add(lForeArm); bones.push(lForeArm);
const lHand = new THREE.Bone(); lHand.name = "mixamorig:LeftHand"; lHand.position.set(0.00, 0.24, 0.00); lForeArm.add(lHand); bones.push(lHand);

const rShoulder = new THREE.Bone(); rShoulder.name = "mixamorig:RightShoulder"; rShoulder.position.set(-0.08, 0.12, 0.00); rShoulder.quaternion.set(-0.5, 0.5, 0.5, 0.5); spine2.add(rShoulder); bones.push(rShoulder);
const rArm = new THREE.Bone(); rArm.name = "mixamorig:RightArm"; rArm.position.set(0.00, 0.14, 0.00); rShoulder.add(rArm); bones.push(rArm);
const rForeArm = new THREE.Bone(); rForeArm.name = "mixamorig:RightForeArm"; rForeArm.position.set(0.00, 0.26, 0.00); rArm.add(rForeArm); bones.push(rForeArm);
const rHand = new THREE.Bone(); rHand.name = "mixamorig:RightHand"; rHand.position.set(0.00, 0.24, 0.00); rForeArm.add(rHand); bones.push(rHand);

// 14-16: Chân trái (Xương chính điều khiển ống quần trái)
const lUpLeg = new THREE.Bone(); lUpLeg.name = "mixamorig:LeftUpLeg"; lUpLeg.position.set(0.10, -0.05, 0.00); lUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0); hips.add(lUpLeg); bones.push(lUpLeg);
const lLeg = new THREE.Bone(); lLeg.name = "mixamorig:LeftLeg"; lLeg.position.set(0.00, 0.42, 0.00); lUpLeg.add(lLeg); bones.push(lLeg);
const lFoot = new THREE.Bone(); lFoot.name = "mixamorig:LeftFoot"; lFoot.position.set(0.00, 0.40, 0.00); lLeg.add(lFoot); bones.push(lFoot);

// 17-19: Chân phải (Xương chính điều khiển ống quần phải)
const rUpLeg = new THREE.Bone(); rUpLeg.name = "mixamorig:RightUpLeg"; rUpLeg.position.set(-0.10, -0.05, 0.00); rUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0); hips.add(rUpLeg); bones.push(rUpLeg);
const rLeg = new THREE.Bone(); rLeg.name = "mixamorig:RightLeg"; rLeg.position.set(0.00, 0.42, 0.00); rUpLeg.add(rLeg); bones.push(rLeg);
const rFoot = new THREE.Bone(); rFoot.name = "mixamorig:RightFoot"; rFoot.position.set(0.00, 0.40, 0.00); rLeg.add(rFoot); bones.push(rFoot);
```

---

### 2. SỬA TRIỆT ĐỂ LỖI INVERSE BIND MATRIX (BẮT BUỘC)
Lý do quần bị nát hình khi hoạt ảnh là vì ma trận nghịch đảo bị tính sai khi chưa cập nhật World Matrix.
Quy trình Three.js chuẩn xác BẮT BUỘC:
```javascript
// 1. Thêm Root bone vào scene
scene.add(hips);

// 2. Cập nhật World Matrix của toàn bộ cây xương
scene.updateMatrixWorld(true);
hips.updateWorldMatrix(true, true);

// 3. Khởi tạo Skeleton và tính Inverses từ World Matrix đã cập nhật:
const skeleton = new THREE.Skeleton(bones);
skeleton.calculateInverses();

// 4. Gán Skeleton vào SkinnedMesh và gọi bind:
const skinnedMesh = new THREE.SkinnedMesh(pantsGeometry, pantsMaterial);
skinnedMesh.add(hips);
skinnedMesh.bind(skeleton);
```

---

### 3. THÔNG SỐ HÌNH HỌC VÀ CHỐNG XUYÊN THẤU (ANTI-CLIPPING METRICS)
Mô hình quần võ hiệp được thiết kế chuẩn tỉ lệ mét và có khoảng hở an toàn với các trang phục lân cận:

1. **Cạp quần & Vùng xương chậu (Y ∈ [0.72m, 0.94m]):**
   - Mép cạp trên tại $Y = 0.94m$, chu vi thắt lưng elip: ngang $X = \pm 0.15m$, sâu $Z = \pm 0.11m$.
   - Đáy đũng quần hạ thấp tự nhiên tại $Y = 0.72m$, có mảng vải đũng rộng bo tròn (crotch gusset bridge) giúp khi xoạc chân hoặc tung cước đũng quần không bị kéo căng rách toạc.

2. **Ống đùi thụng võ hiệp (Baggy Thighs: Y ∈ [0.48m, 0.72m]):**
   - Bán kính ống đùi: $R = 0.11m - 0.13m$.
   - **Quy tắc an toàn chống xuyên:** Game engine đã có tính năng **Leg Deflation** tự động thu nhỏ đùi nhân vật vào trong còn bán kính $\sim 0.06m$. Với bán kính ống quần $0.11m$, khoảng hở không khí đạt $> 4cm$, đảm bảo **100% không bao giờ bị da thịt đùi chọc thủng quần khi chạy nhảy**.

3. **Vùng đầu gối (Knees: Y ∈ [0.42m, 0.52m]):**
   - Bán kính thu dần về $0.08m$, có 5 vòng nếp nhăn đa giác (edge loops) đan xen để khi gập gối $90^\circ$ mặt vải uốn cong mượt mà.

4. **Xà cạp bó cẳng chân (Shin Gaiters: Y ∈ [0.10m, 0.42m]):**
   - Bó thon dần từ $Y = 0.42m$ ($R = 0.075m$) xuống đến gấu quần $Y = 0.10m$ ($R = 0.052m$).
   - **Tương thích hoàn hảo với Ủng Vân Du (`giay_wuxia.glb`):** Miệng cổ ủng có bán kính mở $0.058m$. Do đó, gấu quần với bán kính $0.052m$ sẽ **được nhét gọn gàng vào bên trong cổ ủng một cách tự nhiên và sạch sẽ**, không có bất kỳ điểm giao cắt xuyên thấu nào!

5. **Độ dày vỏ 2 lớp thực tế (Solidified 2-Sided Shell):**
   - Ống quần và cạp quần BẮT BUỘC tạo 2 lớp đa giác (độ dày $2.5mm$) gồm mặt ngoài và mặt trong lót vải tối màu, normal hướng chuẩn ra hai phía. Khi nhìn từ cạp xuống hay từ gấu lên, lòng quần **KHÔNG BỊ TÀNG HÌNH DO BACKFACE CULLING**.

---

### 4. PHÂN BỔ TRỌNG SỐ DA CHUẨN XÁC BẰNG HÀM SMOOTHSTEP
Tổng trọng số của 4 xương trên mỗi đỉnh PHẢI BẰNG CHÍNH XÁC 1.0:

```javascript
function smoothstep(edge0, edge1, x) {
    const t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
}

// 1. Cạp quần (Y in [0.88m, 0.94m]):
// 80% mixamorig:Hips + 20% mixamorig:Spine (để cạp quần uốn nhịp nhàng theo thắt lưng).

// 2. Vùng đũng quần (Y in [0.68m, 0.78m], quanh trục giữa |X| < 0.06m):
// Chuyển tiếp mượt 3 chiều: 50% Hips, 25% LeftUpLeg, 25% RightUpLeg.

// 3. Ống đùi trái (Y in [0.52m, 0.88m]):
// 85% mixamorig:LeftUpLeg + 15% mixamorig:Hips.

// 4. Khớp đầu gối trái (Y in [0.42m, 0.54m] quanh khớp gối Y = 0.48m):
const tKnee = smoothstep(0.54, 0.42, vertex.y);
// LeftUpLeg weight = 1.0 - tKnee;
// LeftLeg weight = tKnee;

// 5. Bắp chân & Xà cạp trái (Y in [0.10m, 0.42m]):
// 95% mixamorig:LeftLeg + 5% mixamorig:LeftFoot.

// (Ống chân phải áp dụng công thức đối xứng với RightUpLeg, RightLeg, RightFoot).
```

---

### 5. PROCEDURAL CANVAS PBR TEXTURE (512x512)
Sử dụng mã JavaScript Canvas 2D vẽ texture màu, độ nhám và hoa văn cho quần:
- $V \in [0.45, 1.00]$ (Thân đùi & Đũng quần): Vải dệt thô màu đen huyền sắc `#1E2028` (Metalness: 0.0, Roughness: 0.82) có vân thớ vải dệt đan chéo vi mô.
- $V \in [0.15, 0.45]$ (Xà cạp bó chân): Nền vải xám tro `#3A3D48` có họa tiết dải đai da quấn chữ X đan bện viền chỉ bạc chìm `#7C808E`.
- $V \in [0.00, 0.15]$ (Bo cạp quần & Viền gấu): Vải viền đen tuyền `#14151B`.

---

### 6. GIAO DIỆN WEBGL & NÚT XUẤT FILE .GLB
- Hiển thị Canvas 3D với OrbitControls, sàn bệ đá và hệ thống đèn studio 3 điểm (Key, Fill, Rim light).
- **Thanh trượt thử nghiệm biến dạng khớp (Pose Test Sliders):**
  - Slider `Gập gối (Knee Bend: 0° đến 90°)`: Kiểm tra độ uốn lượn của nếp vải đầu gối, không bị vỡ mesh.
  - Slider `Xoạc chân / Trung bình tấn (Horse Stance / Squat)`: Kiểm tra đũng quần co giãn tự nhiên, không rách đáy.
- **Nút xuất file "EXPORT QUAN_WUXIA.GLB":**
  - Sử dụng `THREE.GLTFExporter`.
  - Tự động reset khung xương về T-pose chuẩn trước khi xuất file.
  - Tự động chạy hàm Sanity Check kiểm tra `skeleton.bones.length === 20` và `sum(weights) === 1.0`.
  - Tên file tải về: `quan_wuxia.glb`.
```
