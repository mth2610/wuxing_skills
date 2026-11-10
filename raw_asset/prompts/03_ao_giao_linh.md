# Prompt 3: Áo Giao Lĩnh Lam Y (`ao_giao_linh.glb`)

| Property | Value |
|---|---|
| **Target Path** | `raw_asset/equipment/torso/ao_giao_linh.glb` -> `assets/characters/equipment/torso/ao_giao_linh.glb` |
| **Modular Slot** | `MODULAR_SLOT_TORSO` |
| **Rigging Type** | SkinnedMesh 28 xương (20 Mixamo + 8 Cloth Spring Bones) |
| **Physics Bones** | 4 tà áo (`cloth_skirt_*`), 2 tay áo thụng (`cloth_sleeve_*`), 2 dải lụa (`cloth_ribbon_*`) |

---

## Prompt 3: Áo Giao Lĩnh (`ao_giao_linh.glb`)
```markdown
    Vẽ duy nhất một chiếc Áo Giao Lĩnh kiếm hiệp lơ lửng trong không trung theo hiệu ứng         
    ma-nơ-canh tàng hình (Ghost Mannequin / Hollow Garment).                                     
                                                                                                 
    TUYỆT ĐỐI KHÔNG CÓ NGƯỜI, KHÔNG ĐẦU, KHÔNG MẶT, KHÔNG TAY, KHÔNG CHÂN, KHÔNG QUẦN, KHÔNG     
    GIÀY, KHÔNG ĐẾ ĐỨNG. Bên trong áo hoàn toàn rỗng ruột.                                       
                                                                                                 
    Chiếc áo ở tư thế T-pose nhìn thẳng trực diện (Front Orthographic View):                     
    1. Hai ống tay áo thụng rộng (bell sleeves) dang ngang sang hai bên, miệng tay áo viền hoa   
    văn mây vàng kim.                                                                            
    2. Cổ áo vắt chéo Giao Lĩnh truyền thống từ vai phải sang sườn trái, nẹp áo thêu hoa văn mây 
    vàng nổi bật trên nền vải lụa lam sẫm (navy blue).                                           
    3. Đai thắt lưng màu đỏ thắm có khóa ngọc bích lục giác màu lam ngọc ở chính giữa, bên dưới  
    đính 2 dải lụa đỏ rủ xuống.                                                                  
    4. Tà áo dưới dài ngang đùi, xẻ tà nhẹ, gấu chân áo viền chỉ vàng kim, nhìn thấy rõ bên trong
    lòng áo là khoảng trống rỗng (không có người mặc).                                           
                                                                                                 
    Phông nền xám tối đơn sắc sạch sẽ, ánh sáng studio rõ nét từng thớ vải và đường chỉ thêu,    
    phong cách 3D asset game AAA.
```

```markdown
Bạn là một Lead Technical Artist & Kỹ sư Đồ họa WebGL / Three.js chuyên nghiệp. Hãy quan sát kỹ HÌNH ẢNH THIẾT KẾ ÁO GIAO LĨNH đính kèm và lập trình một ứng dụng Three.js hoàn chỉnh (chỉ trong 1 FILE HTML DUY NHẤT) để tạo mô hình 3D SkinnedMesh chiếc Áo Giao Lĩnh này và XUẤT FILE `.glb` CHUẨN MỰC cho game engine C99 / Raylib 6.0 của tôi.

MÔ HÌNH NÀY TÍCH HỢP HỆ THỐNG VẬT LÝ VẢI LÒ XO (SPRING BONES) VÀ CHẠY HOẠT ẢNH CHIẾN ĐẤU. ĐỂ KHÔNG BỊ VẶN ỐNG TAY VÀ KHÔNG BỊ HỞ VAI, BẠN BẮT BUỘC PHẢI TUÂN THỦ TUYỆT ĐỐI CÁC QUY CHUẨN KỸ THUẬT SAU:

Tên trang phục: Áo Giao Lĩnh Lam Y (Tích hợp Cloth Spring Bones)
ID file xuất: ao_giao_linh.glb

---

### 1. BỘ XƯƠNG HOÀN CHỈNH: 20 XƯƠNG MIXAMO + 8 XƯƠNG PHỤ CLOTH SPRING BONES (TỔNG 28 XƯƠNG)
Engine C99 của tôi đã được nâng cấp để tự động điều khiển 20 xương Mixamo theo hoạt ảnh cơ thể và 8 xương `cloth_*` theo vật lý lò xo Verlet thời gian thực.
Đoạn mã Three.js BẮT BUỘC phải tạo đúng 28 xương sau (tọa độ mét, Y-up):

```javascript
// Khởi tạo 28 xương (20 Mixamo + 8 Cloth Spring Bones):
const bones = [];

// 0: Hips (Root)
const hips = new THREE.Bone(); hips.name = "mixamorig:Hips"; hips.position.set(0.00, 0.95, 0.00); bones.push(hips);

// 1-3: Cột sống
const spine = new THREE.Bone(); spine.name = "mixamorig:Spine"; spine.position.set(0.00, 0.13, 0.00); hips.add(spine); bones.push(spine);
const spine1 = new THREE.Bone(); spine1.name = "mixamorig:Spine1"; spine1.position.set(0.00, 0.14, 0.00); spine.add(spine1); bones.push(spine1);
const spine2 = new THREE.Bone(); spine2.name = "mixamorig:Spine2"; spine2.position.set(0.00, 0.14, 0.00); spine1.add(spine2); bones.push(spine2);

// 4-5: Cổ & Đầu
const neck = new THREE.Bone(); neck.name = "mixamorig:Neck"; neck.position.set(0.00, 0.16, 0.00); spine2.add(neck); bones.push(neck);
const head = new THREE.Bone(); head.name = "mixamorig:Head"; head.position.set(0.00, 0.14, 0.00); neck.add(head); bones.push(head);

// 6-9: Tay trái (LƯU Ý: Khớp vai Mixamo quay 90 độ, các xương con duỗi dọc trục Y của xương cha)
const lShoulder = new THREE.Bone(); lShoulder.name = "mixamorig:LeftShoulder";
lShoulder.position.set(0.08, 0.12, 0.00);
lShoulder.quaternion.set(-0.5, -0.5, -0.5, 0.5); // Xoay chuẩn Mixamo
spine2.add(lShoulder); bones.push(lShoulder);

const lArm = new THREE.Bone(); lArm.name = "mixamorig:LeftArm"; lArm.position.set(0.00, 0.14, 0.00); lShoulder.add(lArm); bones.push(lArm);
const lForeArm = new THREE.Bone(); lForeArm.name = "mixamorig:LeftForeArm"; lForeArm.position.set(0.00, 0.26, 0.00); lArm.add(lForeArm); bones.push(lForeArm);
const lHand = new THREE.Bone(); lHand.name = "mixamorig:LeftHand"; lHand.position.set(0.00, 0.24, 0.00); lForeArm.add(lHand); bones.push(lHand);

// 10-13: Tay phải
const rShoulder = new THREE.Bone(); rShoulder.name = "mixamorig:RightShoulder";
rShoulder.position.set(-0.08, 0.12, 0.00);
rShoulder.quaternion.set(-0.5, 0.5, 0.5, 0.5); // Xoay chuẩn Mixamo
spine2.add(rShoulder); bones.push(rShoulder);

const rArm = new THREE.Bone(); rArm.name = "mixamorig:RightArm"; rArm.position.set(0.00, 0.14, 0.00); rShoulder.add(rArm); bones.push(rArm);
const rForeArm = new THREE.Bone(); rForeArm.name = "mixamorig:RightForeArm"; rForeArm.position.set(0.00, 0.26, 0.00); rArm.add(rForeArm); bones.push(rForeArm);
const rHand = new THREE.Bone(); rHand.name = "mixamorig:RightHand"; rHand.position.set(0.00, 0.24, 0.00); rForeArm.add(rHand); bones.push(rHand);

// 14-16: Chân trái
const lUpLeg = new THREE.Bone(); lUpLeg.name = "mixamorig:LeftUpLeg";
lUpLeg.position.set(0.10, -0.05, 0.00);
lUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0); // Lật 180 độ chuẩn Mixamo
hips.add(lUpLeg); bones.push(lUpLeg);

const lLeg = new THREE.Bone(); lLeg.name = "mixamorig:LeftLeg"; lLeg.position.set(0.00, 0.42, 0.00); lUpLeg.add(lLeg); bones.push(lLeg);
const lFoot = new THREE.Bone(); lFoot.name = "mixamorig:LeftFoot"; lFoot.position.set(0.00, 0.40, 0.00); lLeg.add(lFoot); bones.push(lFoot);

// 17-19: Chân phải
const rUpLeg = new THREE.Bone(); rUpLeg.name = "mixamorig:RightUpLeg";
rUpLeg.position.set(-0.10, -0.05, 0.00);
rUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0);
hips.add(rUpLeg); bones.push(rUpLeg);

const rLeg = new THREE.Bone(); rLeg.name = "mixamorig:RightLeg"; rLeg.position.set(0.00, 0.42, 0.00); rUpLeg.add(rLeg); bones.push(rLeg);
const rFoot = new THREE.Bone(); rFoot.name = "mixamorig:RightFoot"; rFoot.position.set(0.00, 0.40, 0.00); rLeg.add(rFoot); bones.push(rFoot);

// 20-27: 8 XƯƠNG PHỤ VẢI (CLOTH SPRING BONES):
// Tà áo trước (Xương cha: Hips)
const skirtFL = new THREE.Bone(); skirtFL.name = "cloth_skirt_f_l"; skirtFL.position.set(0.12, -0.08, 0.10); hips.add(skirtFL); bones.push(skirtFL);
const skirtFR = new THREE.Bone(); skirtFR.name = "cloth_skirt_f_r"; skirtFR.position.set(-0.12, -0.08, 0.10); hips.add(skirtFR); bones.push(skirtFR);

// Tà áo sau (Xương cha: Hips)
const skirtBL = new THREE.Bone(); skirtBL.name = "cloth_skirt_b_l"; skirtBL.position.set(0.12, -0.08, -0.10); hips.add(skirtBL); bones.push(skirtBL);
const skirtBR = new THREE.Bone(); skirtBR.name = "cloth_skirt_b_r"; skirtBR.position.set(-0.12, -0.08, -0.10); hips.add(skirtBR); bones.push(skirtBR);

// Ống tay áo thụng rủ xuống (Xương cha: LeftForeArm / RightForeArm)
const sleeveL = new THREE.Bone(); sleeveL.name = "cloth_sleeve_l_01"; sleeveL.position.set(0.00, 0.15, -0.12); lForeArm.add(sleeveL); bones.push(sleeveL);
const sleeveR = new THREE.Bone(); sleeveR.name = "cloth_sleeve_r_01"; sleeveR.position.set(0.00, 0.15, -0.12); rForeArm.add(sleeveR); bones.push(sleeveR);

// Dải lụa thắt lưng rủ xuống trước bụng (Xương cha: Hips)
const ribbonL = new THREE.Bone(); ribbonL.name = "cloth_ribbon_l"; ribbonL.position.set(0.04, -0.05, 0.14); hips.add(ribbonL); bones.push(ribbonL);
const ribbonR = new THREE.Bone(); ribbonR.name = "cloth_ribbon_r"; ribbonR.position.set(-0.04, -0.05, 0.14); hips.add(ribbonR); bones.push(ribbonR);
```

---

### 2. SỬA TRIỆT ĐỂ LỖI INVERSE BIND MATRIX (BẮT BUỘC PHẢI LÀM ĐÚNG THỨ TỰ)
Lỗi vặn ống tay thành nòng pháo là do Three.js xuất ra toàn bộ ma trận nghịch đảo bằng Identity (0, 0, 0).
Để khắc phục, trong code Three.js bạn BẮT BUỘC PHẢI thực hiện đúng quy trình sau:
```javascript
// 1. Gắn Root bone vào Scene trước
scene.add(hips);

// 2. Ép Scene và toàn bộ khung xương cập nhật World Matrix chuẩn xác
scene.updateMatrixWorld(true);
hips.updateWorldMatrix(true, true);

// 3. Khởi tạo Skeleton và tính toán Inverse Matrices từ World Matrix đã cập nhật:
const skeleton = new THREE.Skeleton(bones);
skeleton.calculateInverses();

// 4. Gắn Skeleton vào SkinnedMesh và gọi bind:
const skinnedMesh = new THREE.SkinnedMesh(robeGeometry, robeMaterial);
skinnedMesh.add(hips);
skinnedMesh.bind(skeleton);
```

---

### 3. QUY TẮC PHÂN BỔ TRỌNG SỐ DA CHO 28 XƯƠNG
Mỗi đỉnh liên kết tối đa 4 xương (`skinIndex`, `skinWeight`), tổng trọng số bằng đúng 1.0:
1. **Cổ áo và ngực trên (|X| <= 0.16m):**
   - 80% `mixamorig:Spine2` + 20% `mixamorig:Neck`. **TUYỆT ĐỐI 0% VÀO ARM** (chống rách nẹp cổ và hở vai).
2. **Cầu vai (0.16m < |X| <= 0.24m):**
   - 60% `mixamorig:Spine2` + 40% `mixamorig:LeftShoulder` (hoặc `RightShoulder`). **0% VÀO ARM.**
3. **Mỏm vai (0.24m < |X| <= 0.28m):**
   - 50% `Shoulder` + 50% `Arm`.
4. **Bắp tay (|X| ∈ [0.28m, 0.44m]):**
   - 100% `mixamorig:LeftArm` (hoặc `RightArm`).
5. **Tay áo thụng buông lơi (Cuff & Droop Fabric):**
   - Vành ống trên quanh cổ tay: 70% `mixamorig:LeftForeArm` + 30% `mixamorig:LeftHand`.
   - Vạt vải thụng rủ xuống dưới cánh tay: 70% `cloth_sleeve_l_01` (hoặc `cloth_sleeve_r_01`) + 30% `mixamorig:LeftForeArm`.
6. **Thắt lưng (Y ∈ [0.88m, 0.98m]):**
   - 100% `mixamorig:Hips`.
7. **Tà áo dưới (Y ∈ [0.55m, 0.88m]):**
   - Tà trước bên trái: 70% `cloth_skirt_f_l` + 30% `mixamorig:Hips`.
   - Tà trước bên phải: 70% `cloth_skirt_f_r` + 30% `mixamorig:Hips`.
   - Tà sau bên trái: 70% `cloth_skirt_b_l` + 30% `mixamorig:Hips`.
   - Tà sau bên phải: 70% `cloth_skirt_b_r` + 30% `mixamorig:Hips`.
8. **Hai dải lụa đỏ thắt lưng rủ trước bụng:**
   - Dải trái: 100% `cloth_ribbon_l`.
   - Dải phải: 100% `cloth_ribbon_r`.

---

### 4. BẢO ĐẢM HÌNH HỌC & CULLING
- Lưới 2 lớp có độ dày vỏ (2mm - 3mm) cho ống tay áo thụng, dải lụa và tà áo xẻ.
- 100% vector `normal` hướng ra ngoài cơ thể (CCW).

---

### 5. PROCEDURAL CANVAS TEXTURE (512x512)
- Nền vải lụa lam sẫm navy wuxia `#1A243B`.
- Nẹp cổ vắt chéo Giao Lĩnh, viền tay áo và gấu áo: Hoa văn sóng mây vàng kim `#E6B84A`.
- Đai thắt lưng đỏ thẫm `#9E1B1B` có ngọc lam `#26A69A` và 2 dải lụa đỏ rủ xuống.

---

### 6. GIAO DIỆN WEBGL & NÚT XUẤT FILE .GLB
- Slider "Pose Test: T-Pose <---> Hạ tay": Xoay 2 cánh tay xuống để kiểm tra vai không bị hở, ống tay rủ tự nhiên.
- Nút "EXPORT AO_GIAO_LINH.GLB":
  - Tự động reset armature về T-pose trước khi export.
  - Sanity check: kiểm tra `skeleton.bones.length === 28` và `sum(skinWeight) === 1.0`.
  - Tải về file: `ao_giao_linh.glb`.
```
