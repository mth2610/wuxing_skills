# Prompt 5: Giày / Ủng Võ Hiệp — Chiến Hài Vân Du (`giay_wuxia.glb`)

| Property | Value |
|---|---|
| **Target Path** | `raw_asset/equipment/boots/giay_wuxia.glb` -> `assets/characters/equipment/boots/giay_wuxia.glb` |
| **Modular Slot** | `MODULAR_SLOT_BOOTS` |
| **Rigging Type** | SkinnedMesh 20 xương Mixamo (Biến dạng theo LeftLeg/LeftFoot & RightLeg/RightFoot) |
| **Dimensions** | Cao 40cm, đế dày 2.4cm, dài 22cm (size 41-42), miệng mở rộng 5.8cm trùm ngoài quần |

---

## Prompt 5: Giày / Ủng Võ Hiệp — Chiến Hài Vân Du (`giay_wuxia.glb`)

### 5.1. Prompt Tạo Ảnh Thiết Kế Concept 3D (Midjourney / Flux / DALL-E 3)
```markdown
    Bản vẽ thiết kế concept 3D một đôi Chiến Hài Võ Hiệp (Wuxia Martial Combat Boots / Giày Vân Du) 
    lơ lửng trong không trung theo hiệu ứng ma-nơ-canh tàng hình (Ghost Mannequin / Standalone Boots),
    gồm 3 góc nhìn: mặt trước (front), mặt bên (side) và góc nghiêng 45 độ đặt cạnh nhau trên cùng một khung hình.

    TUYỆT ĐỐI KHÔNG CÓ NGƯỜI, KHÔNG CÓ CHÂN NGƯỜI, KHÔNG QUẦN ÁO, KHÔNG ĐẾ ĐỨNG. 
    Bên trong cổ ủng hoàn toàn rỗng ruột (nhìn thấy rõ lòng giày lót da tối màu).

    Chi tiết thiết kế đôi ủng:
    1. Kiểu dáng: Đôi ủng chiến đấu võ hiệp cổ cao đến bắp chân (chiều cao khoảng 42cm), 
    ôm sát cổ chân nhưng mở rộng nhẹ ở miệng ống để thuận tiện cho khinh công và thấu khí.
    2. Mũi hài & Thân giày: Mũi giày bọc da đanh dày dặn hơi cong vểnh nhẹ theo phong cách hài vân du phương Đông, 
    thân giày làm bằng da mềm màu nâu đen sẫm (dark oiled leather) có nếp nhăn tự nhiên ở mu bàn chân.
    3. Xà cạp & Dây buộc: Đoạn cổ chân và ống chân quấn nhiều lớp xà cạp vải thô dày dặn màu xám tro, 
    bên ngoài đan chéo các dây da đai nẹp thắt nút cài khóa đồng cổ (antique bronze buckles) giữ chặt bắp chân.
    4. Hoa văn & Điểm nhấn: Quanh cổ ủng và gót giày thêu họa tiết mây cuộn vàng kim (vân du mây vàng) 
    tinh tế, đậm chất kiếm hiệp cao thủ.
    5. Đế giày: Đế da dày 2 lớp ép chắc chắn viền chỉ khâu thủ công màu vàng nhạt, gầm đế có rãnh chống trượt màu đen than.

    Phông nền xám tối đơn sắc sạch sẽ, ánh sáng studio studio rim light sắc nét từng thớ da, đường chỉ may và sợi vải dệt thô, 
    chuẩn phong cách 3D game AAA của Where Winds Meet và Black Myth: Wukong.
```

---

### 5.2. Prompt Lập Trình WebGL / Three.js (Xuất File `giay_wuxia.glb`)
```markdown
Bạn là một Lead Technical Artist & Kỹ sư Đồ họa WebGL / Three.js chuyên nghiệp. Hãy quan sát kỹ HÌNH ẢNH THIẾT KẾ ĐÔI CHIẾN HÀI đính kèm và lập trình một ứng dụng Three.js hoàn chỉnh (chỉ trong 1 FILE HTML DUY NHẤT) để tạo mô hình 3D SkinnedMesh Đôi Chiến Hài Võ Hiệp (Wuxia Combat Boots) và XUẤT FILE `.glb` CHUẨN MỰC cho game engine C99 / Raylib 6.0 của tôi.

MÔ HÌNH NÀY LÀ TRANG PHỤC HẠ PHỤC CỦA NHÂN VẬT, CO GẬP THEO CỔ CHÂN VÀ CHẠY HOẠT ẢNH CHIẾN ĐẤU. ĐỂ KHÔNG BỊ RÁCH CỔ CHÂN, KHÔNG BỊ TÀNG HÌNH LÒNG GIÀY VÀ KHÔNG BỊ ĐƠ T-POSE TRONG RAYLIB, BẠN BẮT BUỘC PHẢI TUÂN THỦ TUYỆT ĐỐI CÁC QUY CHUẨN KỸ THUẬT SAU:

Tên trang phục: Chiến Hài Vân Du (Wuxia Combat Boots)
ID file xuất: giay_wuxia.glb
Slot trang bị: MODULAR_SLOT_BOOTS

---

### 1. BỘ XƯƠNG BẮT BUỘC: ĐÚNG CHÍNH XÁC 20 XƯƠNG MIXAMO
Game engine C99 / Raylib của tôi kiểm tra nghiêm ngặt `model.skeleton.boneCount == 20`. Khác 20 xương sẽ BỊ ĐƠ T-POSE VÀ TỪ CHỐI HOẠT ẢNH.
Đôi giày chỉ biến dạng theo 4 xương chính ở chân dưới:
- `mixamorig:LeftLeg` (Khớp gối trái, Y = 0.48m)
- `mixamorig:LeftFoot` (Khớp cổ chân trái, Y = 0.08m)
- `mixamorig:RightLeg` (Khớp gối phải, Y = 0.48m)
- `mixamorig:RightFoot` (Khớp cổ chân phải, Y = 0.08m)
Các xương còn lại trong 20 xương (Hips, Spine, Tay, Đầu...) BẮT BUỘC vẫn phải có mặt trong Skeleton nhưng có trọng số gán đỉnh bằng 0.

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

// 14-16: Chân trái (Xương chính điều khiển chiếc ủng trái)
const lUpLeg = new THREE.Bone(); lUpLeg.name = "mixamorig:LeftUpLeg"; lUpLeg.position.set(0.10, -0.05, 0.00); lUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0); hips.add(lUpLeg); bones.push(lUpLeg);
const lLeg = new THREE.Bone(); lLeg.name = "mixamorig:LeftLeg"; lLeg.position.set(0.00, 0.42, 0.00); lUpLeg.add(lLeg); bones.push(lLeg);
const lFoot = new THREE.Bone(); lFoot.name = "mixamorig:LeftFoot"; lFoot.position.set(0.00, 0.40, 0.00); lLeg.add(lFoot); bones.push(lFoot);

// 17-19: Chân phải (Xương chính điều khiển chiếc ủng phải)
const rUpLeg = new THREE.Bone(); rUpLeg.name = "mixamorig:RightUpLeg"; rUpLeg.position.set(-0.10, -0.05, 0.00); rUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0); hips.add(rUpLeg); bones.push(rUpLeg);
const rLeg = new THREE.Bone(); rLeg.name = "mixamorig:RightLeg"; rLeg.position.set(0.00, 0.42, 0.00); rUpLeg.add(rLeg); bones.push(rLeg);
const rFoot = new THREE.Bone(); rFoot.name = "mixamorig:RightFoot"; rFoot.position.set(0.00, 0.40, 0.00); rLeg.add(rFoot); bones.push(rFoot);
```

---

### 2. SỬA TRIỆT ĐỂ LỖI INVERSE BIND MATRIX (BẮT BUỘC)
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
const skinnedMesh = new THREE.SkinnedMesh(bootsGeometry, bootsMaterial);
skinnedMesh.add(hips);
skinnedMesh.bind(skeleton);
```

---

### 3. THÔNG SỐ HÌNH HỌC & KÍCH THƯỚC CHUẨN (METRIC 1 UNIT = 1 METER)
Mô hình bao gồm cả 2 chiếc ủng (Trái và Phải) đặt đối xứng qua trục Z:
- **Tọa độ tâm chân:** Chiếc trái tại $X = +0.10m$, chiếc phải tại $X = -0.10m$.
- **Chiều cao ủng:** Từ mặt đất $Y = 0.00m$ lên đến dưới đầu gối $Y = 0.40m$ (chiều cao cổ ủng $40cm$).
- **Đế giày (Sole):**
  - Mặt đế phẳng đặt tại $Y = 0.00m$, độ dày đế $0.024m$ ($2.4cm$).
  - Chiều dài bàn chân: từ gót $Z = -0.06m$ đến đầu mũi $Z = +0.16m$ (tổng dài $22cm$, tỷ lệ size 41-42 wuxia).
  - Chiều rộng bàn chân: $0.088m$ ($8.8cm$).
  - Mũi giày: Hơi cong vểnh lên tại $Z \in [0.12m, 0.16m]$, chóp mũi $Y = 0.035m$.
- **Cổ ủng & Ống chân (Boot Shaft & Gaiter Wraps):**
  - Cổ chân ($Y = 0.08m$): bán kính $0.046m$ ôm vừa vặn mắt cá chân.
  - Ống bắp chân ($Y = 0.25m$): bán kính nở nhẹ ra $0.054m$.
  - Miệng trên cổ ủng ($Y = 0.40m$): bán kính mở $0.058m$ để trùm ra ngoài ống quần `quan_wuxia` mà KHÔNG BỊ XUYÊN THỦNG.
- **Dây da buộc chéo & Khóa đồng:** 3 đai da quấn quanh ống chân có khóa đồng nhỏ ở má ngoài.
- **Vỏ lưới 2 lớp có độ dày (Solidified Shell):** Miệng cổ ủng BẮT BUỘC gập mép vào trong $0.03m$ ($3cm$) tạo thành lòng giày khép kín, vector normal hướng chuẩn, đảm bảo khi nhìn từ trên xuống không bị tàng hình do Backface Culling.

---

### 4. PHÂN BỔ TRỌNG SỐ DA CHUẨN XÁC BẰNG HÀM SMOOTHSTEP
Khớp cổ chân xoay tại $Y = 0.08m$. Trọng số gán mượt mà bằng hàm `smoothstep` giữa Foot và Leg:

```javascript
function smoothstep(edge0, edge1, x) {
    const t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
}

// Cho mỗi đỉnh của ủng trái:
// 1. Vùng bàn chân & đế giày (Y <= 0.06m):
// 100% mixamorig:LeftFoot (bone 16 trong cây xương).

// 2. Vùng cổ chân & mắt cá (Y in [0.06m, 0.14m]):
const tAnkle = smoothstep(0.06, 0.14, vertex.y);
// LeftFoot weight = 1.0 - tAnkle;
// LeftLeg weight = tAnkle;

// 3. Vùng ống ủng & xà cạp (Y > 0.14m):
// 100% mixamorig:LeftLeg (bone 15 trong cây xương).

// Đối với ủng phải: áp dụng công thức tương tự với RightFoot (bone 19) và RightLeg (bone 18).
// Toàn bộ các đỉnh có tổng 4 skinWeights = 1.0.
```

---

### 5. PROCEDURAL CANVAS PBR TEXTURE (512x512)
Vẽ Canvas 2D làm PBR Color Atlas cho đôi ủng:
- $V \in [0.45, 1.00]$: Da mềm nâu đen sẫm `#26201B` có vân da thuộc nhẹ cho phần mu bàn chân và thân ủng.
- $V \in [0.18, 0.45]$: Xà cạp vải thô xám tro `#48443E` với đường đan chéo chỉ viền cho ống chân.
- $V \in [0.08, 0.18]$: Đường viền thêu hoa văn mây vàng kim `#D4A843` quanh miệng ủng và gót giày.
- $V \in [0.00, 0.08]$: Đế cao su/da đanh đen than `#161618` có rãnh chống trượt và chi tiết khóa đồng cổ `#9E7844`.

---

### 6. GIAO DIỆN WEBGL & NÚT XUẤT FILE .GLB
- Hiển thị Canvas 3D với OrbitControls, sàn bệ đá và hệ thống đèn studio 3 điểm (Key, Fill, Rim light).
- **Thanh trượt thử nghiệm biến dạng khớp (Pose Test Sliders):**
  - Slider `Gập cổ chân (Ankle Flex: -20° đến +35°)`: Xoay `LeftFoot` và `RightFoot` quanh trục X để kiểm tra nếp gấp cổ chân co giãn tự nhiên, không rách lưới.
  - Slider `Nhón gót / Tiêu bộ (Heel Raise)`: Kiểm tra độ cong của mũi hài khi bước đi.
- **Nút xuất file "EXPORT GIAY_WUXIA.GLB":**
  - Sử dụng `THREE.GLTFExporter`.
  - Tự động reset khung xương về T-pose chuẩn ($Y = 0.0m$ trên mặt đất).
  - Tự động chạy hàm Sanity Check kiểm tra `skeleton.bones.length === 20` và `sum(weights) === 1.0`.
  - Tên file tải về: `giay_wuxia.glb`.
```

---
