# Wuxing Skills — Modular Asset Generation Master Prompts

Complete collection of copy-paste production prompts for generating modular 3D wuxia character components using **Three.js WebGL (single-file `index.html`)** with one-click **"Export GLB"** buttons.

---

## Universal Standards (Enforced Across All Prompts)

| Property | Value | Notes |
|---|---|---|
| **Scale** | `1 unit = 1 meter` | Real-world metric scale. Total character height is `1.78m`. |
| **Axes** | `+Y = Up`, `+Z = Forward`, `+X = Left` | Standard glTF 2.0 convention. |
| **Skeleton Hierarchy** | 20 bones (`mixamorig:*`) | Exact Mixamo bone names and joint coordinates. |
| **Skinning Type** | SkinnedMesh vs Rigid Socket | Clothing is SkinnedMesh; Weapons, Hats, and Bracers are Rigid Sockets. |

### Mixamo Humanoid Joint Anchors
```
mixamorig:Hips           [ 0.00, 0.95, 0.00] (Root)
├── mixamorig:Spine      [ 0.00, 1.08, 0.00]
│   └── mixamorig:Spine1 [ 0.00, 1.22, 0.00]
│       └── mixamorig:Spine2 [ 0.00, 1.36, 0.00]
│           ├── mixamorig:Neck [ 0.00, 1.52, 0.00]
│           │   └── mixamorig:Head [ 0.00, 1.66, 0.00]
│           ├── mixamorig:LeftShoulder [ 0.08, 1.48, 0.00]
│           │   └── mixamorig:LeftArm [ 0.22, 1.48, 0.00]
│           │       └── mixamorig:LeftForeArm [ 0.48, 1.48, 0.00]
│           │           └── mixamorig:LeftHand [ 0.72, 1.48, 0.00]
│           └── mixamorig:RightShoulder [-0.08, 1.48, 0.00]
│               └── mixamorig:RightArm [-0.22, 1.48, 0.00]
│                   └── mixamorig:RightForeArm [-0.48, 1.48, 0.00]
│                       └── mixamorig:RightHand [-0.72, 1.48, 0.00]
├── mixamorig:LeftUpLeg  [ 0.10, 0.90, 0.00]
│   └── mixamorig:LeftLeg [ 0.10, 0.48, 0.00]
│       └── mixamorig:LeftFoot [ 0.10, 0.08, 0.00]
└── mixamorig:RightUpLeg [-0.10, 0.90, 0.00]
    └── mixamorig:RightLeg [-0.10, 0.48, 0.00]
        └── mixamorig:RightFoot [-0.10, 0.08, 0.00]
```

---

## Component Folder Mapping in `raw_asset/`

| Component | Target File Path | Type | Attachment Point / Slot |
|---|---|---|---|
| **1. Base Body** | `raw_asset/bodies/human_body.glb` | SkinnedMesh | `MODULAR_SLOT_BASE_BODY` |
| **2. Nón Lá** | `raw_asset/equipment/headgear/non_la.glb` | Rigid Socket | `MODULAR_SLOT_HEADGEAR` (`mixamorig:Head`) |
| **3. Áo (Robe)** | `raw_asset/equipment/torso/ao_giao_linh.glb` | SkinnedMesh | `MODULAR_SLOT_TORSO` |
| **4. Quần (Pants)** | `raw_asset/equipment/legs/quan_wuxia.glb` | SkinnedMesh | `MODULAR_SLOT_LEGS` |
| **5. Giày (Boots)** | `raw_asset/equipment/boots/giay_wuxia.glb` | SkinnedMesh | Footwear / Leg Bindings |
| **6. Hộ Uyển** | `raw_asset/equipment/bracers/ho_uyen.glb` | Rigid Socket | Forearm Sockets (`LeftForeArm` / `RightForeArm`) |
| **7. Kiếm** | `raw_asset/equipment/weapons/kiem_thuan_thien.glb` | Rigid Socket | `MODULAR_SLOT_WEAPON_R` (`mixamorig:RightHand`) |
| **8. Animations** | `raw_asset/animations/combat_moves.glb` | Pure Skeleton Bank | Shared Animation System |

---

## Prompt 1: Base Human Body (`human_body.glb`)

```markdown
   Bản vẽ thiết kế giải phẫu nhân vật nam kiếm hiệp (Male Base Character Sheet) gồm 2 góc nhìn: 
    mặt trước (front view) và mặt sau (back view) đặt cạnh nhau, tư thế T-pose chuẩn đứng thẳng  
    dang ngang 2 tay, góc nhìn phẳng không méo phối cảnh (orthographic view).                    
                                                                                                 
    Chi tiết nhân vật:                                                                           
    1. Vóc dáng: Thân hình hiệp khách võ thuật săn chắc, vai rộng ngực nở vừa phải, cơ bụng 6 múi
    thon gọn, tay chân gân guốc nhanh nhẹn (tỷ lệ chuẩn kiếm hiệp phương Đông, không cơ bắp hộ   
    pháp cồng kềnh).                                                                             
    2. Làn da: Nước da màu bánh mật sáng khỏe khoắn (da vàng ấm châu Á).                         
    3. Khuôn mặt & Kiểu tóc: Gương mặt cương nghị góc cạnh, búi tóc củ tỏi wuxia buộc cao trên   
    đỉnh đầu cắm trâm gỗ đơn giản, để lộ vầng trán sáng và gáy.                                  
    4. Trang phục tối giản: Chỉ mặc một chiếc quần lót võ hiệp ngắn màu xám đen ôm sát hông (để  
    lộ toàn bộ cơ bắp vai, ngực, bụng, lưng, cánh tay và chân phục vụ việc dựng 3D), chân trần   
    không giày.                                                                                  
                                                                                                 
    Phông nền xám tối đơn sắc sạch sẽ, ánh sáng studio đều và phẳng giúp thấy rõ từng khối cơ    
    bắp, phong cách 3D game AAA của Where Winds Meet.
```

```markdown
Bạn là một Lead Technical Artist & Kỹ sư Đồ họa WebGL / Three.js chuyên nghiệp. Hãy quan sát kỹ HÌNH ẢNH THIẾT KẾ THÂN THỂ NAM đính kèm và lập trình một ứng dụng Three.js hoàn chỉnh (chỉ trong 1 FILE HTML DUY NHẤT) để tạo mô hình 3D SkinnedMesh Thân Thể Nam Kiếm Hiệp (Wuxia Male Base Body) và XUẤT FILE `.glb` CHUẨN MỰC cho game engine C99 / Raylib 6.0 của tôi.

MÔ HÌNH NÀY LÀ NHÂN VẬT NỀN CHÍNH CỦA GAME, KHOÁC TRANG PHỤC VÀ CHẠY HOẠT ẢNH CHIẾN ĐẤU. ĐỂ KHÔNG BỊ RÁCH KHỚP, KHÔNG BỊ VẶN TAY NHƯ NỌNG PHÁO VÀ KHÔNG BỊ BIẾN DẠNG KHI CHUYỂN ĐỘNG, BẠN BẮT BUỘC PHẢI TUÂN THỦ TUYỆT ĐỐI CÁC QUY CHUẨN KỸ THUẬT SAU:

Tên mô hình: Võ Giả Phổ Thông (Wuxia Male Base Body)
ID file xuất: human_body.glb

---

### 1. BỘ XƯƠNG BẮT BUỘC: ĐÚNG CHÍNH XÁC 20 XƯƠNG MIXAMO VỚI QUATERNION CHUẨN
Tuyệt đối KHÔNG tạo xương ngón tay, ngón chân hay xương phụ (Raylib chỉ nhận đúng 20 xương body).
LƯU Ý CỰC KỲ QUAN TRỌNG VỀ QUATERNION: Khớp vai Mixamo (`LeftShoulder`, `RightShoulder`) và khớp đùi (`LeftUpLeg`, `RightUpLeg`) BẮT BUỘC phải có Quaternion xoay chuẩn để hệ trục tọa độ của cánh tay và chân trùng khớp 100% với file hoạt ảnh của game. Nếu để Quaternion đơn vị (0,0,0,1), cánh tay sẽ BỊ VẶN 90 ĐỘ RA PHÍA TRƯỚC!

Đoạn mã Three.js BẮT BUỘC phải tạo đúng 20 xương sau (tọa độ mét, Y-up):

```javascript
// Khởi tạo 20 xương chuẩn Mixamo khớp 100% với engine:
const bones = [];

// 0: Hips (Root)
const hips = new THREE.Bone(); hips.name = "mixamorig:Hips";
hips.position.set(0.00, 0.95, 0.00);
hips.quaternion.set(0.0, 0.0, 0.0, 1.0);
bones.push(hips);

// 1-3: Spine chain
const spine = new THREE.Bone(); spine.name = "mixamorig:Spine";
spine.position.set(0.00, 0.13, 0.00);
spine.quaternion.set(0.0, 0.0, 0.0, 1.0);
hips.add(spine); bones.push(spine);

const spine1 = new THREE.Bone(); spine1.name = "mixamorig:Spine1";
spine1.position.set(0.00, 0.14, 0.00);
spine1.quaternion.set(0.0, 0.0, 0.0, 1.0);
spine.add(spine1); bones.push(spine1);

const spine2 = new THREE.Bone(); spine2.name = "mixamorig:Spine2";
spine2.position.set(0.00, 0.14, 0.00);
spine2.quaternion.set(0.0, 0.0, 0.0, 1.0);
spine1.add(spine2); bones.push(spine2);

// 4-5: Neck & Head
const neck = new THREE.Bone(); neck.name = "mixamorig:Neck";
neck.position.set(0.00, 0.16, 0.00);
neck.quaternion.set(0.0, 0.0, 0.0, 1.0);
spine2.add(neck); bones.push(neck);

const head = new THREE.Bone(); head.name = "mixamorig:Head";
head.position.set(0.00, 0.14, 0.00);
head.quaternion.set(0.0, 0.0, 0.0, 1.0);
neck.add(head); bones.push(head);

// 6-9: Tay trái (Vai xoay -90 deg quanh trục để trục con duỗi dọc theo cánh tay)
const lShoulder = new THREE.Bone(); lShoulder.name = "mixamorig:LeftShoulder";
lShoulder.position.set(0.08, 0.12, 0.00);
lShoulder.quaternion.set(-0.5, -0.5, -0.5, 0.5); // BẮT BUỘC
spine2.add(lShoulder); bones.push(lShoulder);

const lArm = new THREE.Bone(); lArm.name = "mixamorig:LeftArm";
lArm.position.set(0.00, 0.14, 0.00);
lArm.quaternion.set(0.0, 0.0, 0.0, 1.0);
lShoulder.add(lArm); bones.push(lArm);

const lForeArm = new THREE.Bone(); lForeArm.name = "mixamorig:LeftForeArm";
lForeArm.position.set(0.00, 0.26, 0.00);
lForeArm.quaternion.set(0.0, 0.0, 0.0, 1.0);
lArm.add(lForeArm); bones.push(lForeArm);

const lHand = new THREE.Bone(); lHand.name = "mixamorig:LeftHand";
lHand.position.set(0.00, 0.24, 0.00);
lHand.quaternion.set(0.0, 0.0, 0.0, 1.0);
lForeArm.add(lHand); bones.push(lHand);

// 10-13: Tay phải
const rShoulder = new THREE.Bone(); rShoulder.name = "mixamorig:RightShoulder";
rShoulder.position.set(-0.08, 0.12, 0.00);
rShoulder.quaternion.set(-0.5, 0.5, 0.5, 0.5); // BẮT BUỘC
spine2.add(rShoulder); bones.push(rShoulder);

const rArm = new THREE.Bone(); rArm.name = "mixamorig:RightArm";
rArm.position.set(0.00, 0.14, 0.00);
rArm.quaternion.set(0.0, 0.0, 0.0, 1.0);
rShoulder.add(rArm); bones.push(rArm);

const rForeArm = new THREE.Bone(); rForeArm.name = "mixamorig:RightForeArm";
rForeArm.position.set(0.00, 0.26, 0.00);
rForeArm.quaternion.set(0.0, 0.0, 0.0, 1.0);
rArm.add(rForeArm); bones.push(rForeArm);

const rHand = new THREE.Bone(); rHand.name = "mixamorig:RightHand";
rHand.position.set(0.00, 0.24, 0.00);
rHand.quaternion.set(0.0, 0.0, 0.0, 1.0);
rForeArm.add(rHand); bones.push(rHand);

// 14-16: Chân trái (Hông xoay lật 180 độ)
const lUpLeg = new THREE.Bone(); lUpLeg.name = "mixamorig:LeftUpLeg";
lUpLeg.position.set(0.10, -0.05, 0.00);
lUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0); // BẮT BUỘC
hips.add(lUpLeg); bones.push(lUpLeg);

const lLeg = new THREE.Bone(); lLeg.name = "mixamorig:LeftLeg";
lLeg.position.set(0.00, 0.42, 0.00);
lLeg.quaternion.set(0.0, 0.0, 0.0, 1.0);
lUpLeg.add(lLeg); bones.push(lLeg);

const lFoot = new THREE.Bone(); lFoot.name = "mixamorig:LeftFoot";
lFoot.position.set(0.00, 0.40, 0.00);
lFoot.quaternion.set(0.0, 0.0, 0.0, 1.0);
lLeg.add(lFoot); bones.push(lFoot);

// 17-19: Chân phải
const rUpLeg = new THREE.Bone(); rUpLeg.name = "mixamorig:RightUpLeg";
rUpLeg.position.set(-0.10, -0.05, 0.00);
rUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0); // BẮT BUỘC
hips.add(rUpLeg); bones.push(rUpLeg);

const rLeg = new THREE.Bone(); rLeg.name = "mixamorig:RightLeg";
rLeg.position.set(0.00, 0.42, 0.00);
rLeg.quaternion.set(0.0, 0.0, 0.0, 1.0);
rUpLeg.add(rLeg); bones.push(rLeg);

const rFoot = new THREE.Bone(); rFoot.name = "mixamorig:RightFoot";
rFoot.position.set(0.00, 0.40, 0.00);
rFoot.quaternion.set(0.0, 0.0, 0.0, 1.0);
rLeg.add(rFoot); bones.push(rFoot);
```

---

### 2. SỬA TRIỆT ĐỂ LỖI INVERSE BIND MATRIX (BẮT BUỘC PHẢI LÀM ĐÚNG THỨ TỰ)
Lý do "coi trên HTML thì bình thường, đưa vào engine bị nát hình" là vì: trên HTML chỉ hiển thị T-pose nên ma trận đơn vị chưa bộc lộ lỗi; khi engine áp animation, ma trận nghịch đảo sai làm đỉnh nhảy lung tung!
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
const skinnedMesh = new THREE.SkinnedMesh(bodyGeometry, bodyMaterial);
skinnedMesh.add(hips);
skinnedMesh.bind(skeleton);
```

---

### 3. THUẬT TOÁN TẠO HÌNH HỌC LIỀN MẠCH (CHỐNG RÁCH KHỚP VÀ CHỐNG CỤC THỊT RỜI RẠC)
TUYỆT ĐỐI KHÔNG ghép các hình hộp chữ nhật hay hình trụ hở đầu rời rạc (`THREE.BoxGeometry` / `THREE.CylinderGeometry`) với nhau! Việc ghép các khối primitive rời rạc là nguyên nhân chính khiến mô hình biến thành ma-nơ-canh gỗ vỡ nát, các khớp khuỷu tay và đầu gối bị hở toang hoác khi chuyển động.

BẮT BUỘC áp dụng thuật toán lưới tham số liên tục (Continuous Lofted Rings & Overlapping Capsules):

#### A. Thân mình liền mạch (Lofted Torso & Pelvis Mesh):
Tạo thân mình bằng các vòng tiết diện tròn/elip (Lofted Cross-section Rings) chạy liên tục từ đáy chậu ($Y = 0.88m$) lên đến chân cổ ($Y = 1.52m$):
```javascript
// 32 tầng vòng elip, mỗi vòng 24 đỉnh (subdivision 24 radial x 32 vertical):
// Tại mỗi cao độ Y, bán kính rx (bề ngang) và rz (chiều sâu ngực/lưng) được nội suy mượt mà:
// - Y in [0.88, 1.02] (Hông/Đáy chậu): rx = 0.16m, rz = 0.12m
// - Y in [1.02, 1.18] (Eo thon wuxia): rx = 0.13m, rz = 0.10m
// - Y in [1.18, 1.34] (Khung sườn): rx = 0.16m, rz = 0.12m
// - Y in [1.34, 1.48] (Ngực nở & Lưng xô): rx = 0.18m, rz = 0.13m (có độ nhô ngực nhẹ ở +Z)
// - Y in [1.48, 1.54] (Chân cổ chuyển tiếp): rx = 0.08m -> 0.055m, rz = 0.08m -> 0.055m
```

#### B. Tứ chi dạng Capsule bán cầu lồng sâu (Overlapping Hemispherical Joints):
Với cánh tay và cẳng chân, các đầu khớp (Khuỷu tay, Đầu gối, Khớp vai) BẮT BUỘC phải dùng hình trụ có 2 đầu bán cầu khép kín (CapsuleGeometry hoặc Cylinder lồng nắp bán cầu Sphere):
- **Khớp khuỷu tay ($X = 0.48m$):** Nắp bán cầu của bắp tay (Upper Arm) và cẳng tay (Forearm) phải lồng sâu vào nhau tối thiểu $0.035m$ ($3.5cm$). Khi cẳng tay gập $90^\circ$, bề mặt bán cầu trượt mượt mà bên trong ổ khớp, tuyệt đối không lộ khoảng trống hay vành cắt phẳng.
- **Khớp đầu gối ($Y = 0.48m$):** Nắp bán cầu đùi (Thigh) và bắp chuối (Calf) lồng sâu $0.04m$ ($4cm$) đan xen nhau.
- **Hướng duỗi cánh tay trong T-pose:** Cánh tay duỗi thẳng dọc theo trục $X$ của thế giới (từ vai $X = \pm 0.08m$ ra đến mu bàn tay $X = \pm 0.72m$). Lòng bàn tay úp xuống ($-Y$), cùi chỏ hướng ra sau ($-Z$), giúp hoạt ảnh Mixamo khi gập khuỷu tay sẽ đưa cẳng tay hướng về phía trước ngực ($+Z$) một cách hoàn toàn tự nhiên!

---

### 4. PHÂN BỔ TRỌNG SỐ DA CHÍNH XÁC BẰNG HÀM SMOOTHSTEP (GRADIENT BLENDING)
Tuyệt đối KHÔNG gán cứng 100% một xương theo kiểu bậc thang thô sơ (`if (y > 1.2) bone = 3 else bone = 2`). Tất cả các khớp nối BẮT BUỘC phải dùng hàm chuyển tiếp nội suy mềm mại (Smoothstep Distance Blending):

```javascript
// Hàm tính trọng số chuyển tiếp giữa 2 xương liền kề:
function smoothstep(edge0, edge1, x) {
    const t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
}

// 1. Thân mình (Spine chain theo cao độ Y):
// Y in [0.88, 1.08]: Hips (bone 0) <---> Spine (bone 1)
// Y in [1.08, 1.22]: Spine (bone 1) <---> Spine1 (bone 2)
// Y in [1.22, 1.36]: Spine1 (bone 2) <---> Spine2 (bone 3)
// Y in [1.36, 1.52]: Spine2 (bone 3) <---> Neck (bone 4)

// 2. Khuỷu tay trái (theo hoành độ X):
// Tại X in [0.44m, 0.52m] quanh khớp khuỷu X = 0.48m:
const tForearm = smoothstep(0.44, 0.52, Math.abs(vertex.x));
// LeftArm weight = 1.0 - tForearm; LeftForeArm weight = tForearm;

// 3. Đầu gối (theo tung độ Y):
// Tại Y in [0.44m, 0.52m] quanh khớp gối Y = 0.48m:
const tCalf = smoothstep(0.52, 0.44, vertex.y);
// LeftUpLeg weight = 1.0 - tCalf; LeftLeg weight = tCalf;

// 4. Khớp vai & Nách:
// Đỉnh trên vai: 40% mixamorig:LeftShoulder + 60% mixamorig:LeftArm
// Đỉnh nách dưới: 50% mixamorig:Spine2 + 30% mixamorig:LeftShoulder + 20% mixamorig:LeftArm
```

---

### 5. CƠ CHẾ TỰ ĐỘNG CHỐNG XUYÊN ÁO (ENGINE-LEVEL AUTO TORSO & LEG DEFLATION)
Lưu ý về quy chuẩn kích thước hình học:
- Game engine đã tích hợp sẵn tính năng **Selective Body Deflation** trong module `character_model.c`. Khi nhân vật khoác Áo (`MODULAR_SLOT_TORSO`) hoặc Mặc Quần (`MODULAR_SLOT_LEGS`), engine sẽ tự động thu nhỏ các đỉnh thân mình (ngực, lưng, sườn) vào trong $16\%$ và các đỉnh đùi vào trong $20\%$, triệt tiêu 100% hiện tượng da thịt chọc thủng áo/quần khi chạy nhảy chiến đấu.
- Do đó, khi tạo thân thể nam trong Three.js:
  - Bán kính lồng ngực tối đa: $Z \le 0.14m$ (không làm ngực quá gồ ghề vượt qua $0.15m$).
  - Bề rộng vai: $X \in [-0.22m, +0.22m]$.
  - Chiều cao mỏm vai: $Y \le 1.50m$.
  - Đùi: bán kính $0.075m$ tại $Y = 0.70m$.

---

### 6. PROCEDURAL CANVAS TEXTURE (512x512)
- $V \in [0.40, 1.00]$: Da màu vàng ấm châu Á khỏe khoắn `#E5B289`, có đổ bóng chuyển sắc nhẹ làm nổi bật rãnh ngực, cơ bụng 6 múi và cơ lưng.
- $V \in [0.15, 0.40]$: Quần đùi ngắn màu đen than `#1E2028` viền xám tro che kín vùng xương chậu.
- $V \in [0.00, 0.15]$: Mái tóc và búi tóc đen tuyền `#16171B` cùng trâm gỗ nâu sẫm `#5C3A21`.
- Vùng UV khuôn mặt: Vẽ mắt kiếm hiệp sắc nét, lông mày lưỡi mác, môi tự nhiên.

---

### 7. GIAO DIỆN WEBGL & NÚT XUẤT FILE .GLB
- Slider `Pose Test: T-Pose <---> Hạ tay / Idle`: Xoay cánh tay xuống để kiểm tra khớp vai không bị rách toạc.
- Slider `Pose Test: Gập gối / Squat`: Kiểm tra đầu gối không bị đứt đoạn.
- Nút `EXPORT HUMAN_BODY.GLB`:
  - Đưa Armature về T-pose chuẩn trước khi export.
  - Sanity check: kiểm tra `skeleton.bones.length === 20` và `sum(skinWeight) === 1.0`.
  - Tải về file: `human_body.glb`.
```

## Prompt 2: Nón Lá Hiệp Khách (`non_la.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models, textures, and exports ONLY a standalone Wuxia Conical Bamboo Hat (Nón Lá Hiệp Khách) to GLB.

Do NOT include any body, face, or character mesh. ONLY the hat as an independent socket item.

### 1. Attachment Socket Standard
- Metric Scale: 1 unit = 1 meter.
- Origin (0, 0, 0): Located at the center of the inner headband contact point (where it touches the crown of the head).
- Dimensions: Outer base diameter 0.82m (radius 0.41m), cone height 0.22m, apex angle ~62 degrees.

### 2. Geometry Details
- Conical shell with 48 radial segments, slight downward natural curvature towards the perimeter brim.
- Inner bamboo headband rim (radius 0.09m, thickness 0.012m) located at Y=0.0m.
- 16 concentric bamboo ribbing rings modeled along the cone slope.
- Two red silk tie ribbons hanging down from the inner ring (length 0.45m) with gentle wind curve.

### 3. Procedural Canvas Texture (512x512)
- Generate procedural canvas pattern:
  - Base: Warm sun-bleached natural bamboo straw (#d8be8a).
  - Concentric straw weave rings (#b89a64) and radial leaf seams.
  - Scarlet red (#b22222) silk ribbon cords.

### 4. UI & Export
- Three.js OrbitControls, metalness/roughness lighting, and an "Export Nón Lá GLB" button downloading `non_la.glb`.
```

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

## Prompt 4: Quần Võ Hiệp (`quan_wuxia.glb`)

```markdown
   Bạn là một Technical Artist & Kỹ sư Đồ họa WebGL / Three.js. Hãy quan sát HÌNH ẢNH THIẾT KẾ  
    QUẦN đính kèm và lập trình một ứng dụng Three.js hoàn chỉnh (chỉ trong 1 file HTML duy nhất) 
    để tạo mô hình 3D SkinnedMesh chiếc Quần Võ Hiệp này và XUẤT FILE `.glb` CHUẨN MỰC cho game  
    engine C99 / Raylib 6.0 của tôi.                                                             
                                                                                                 
    LƯU Ý CỐT LÕI VỀ XƯƠNG:                                                                      
    - Quần PHẢI ĐƯỢC GẮN XƯƠNG (Rigged SkinnedMesh) ăn theo các khớp xương chân chuẩn Mixamo để  
    co giãn mượt mà khi nhân vật chạy nhảy và tung cước.                                         
    - Quần KHÔNG CẦN XƯƠNG PHỤ VẢI (Không cần các xương cloth spring bones).                     
                                                                                                 
    Tên trang phục: Quần Võ Hiệp Huyền Sắc                                                       
    ID file xuất: quan_wuxia.glb                                                                 
                                                                                                 
    ---                                                                                          
                                                                                                 
    ### 1. HỆ THỐNG KHUNG XƯƠNG BẮT BUỘC (MIXAMO LEG RIG)                                        
    Armature của quần phải chứa đầy đủ cây xương chuẩn Mixamo (để tương thích 100% với animation 
    clip của Raylib mà không bị lỗi lệch khớp):                                                  
    ĐÚNG CHÍNH XÁC 20 XƯƠNG MIXAMO (KHÔNG ĐƯỢC TẠO mixamorig:LeftToeBase, RightToeBase hay xương phụ):
    Engine Raylib kiểm tra nghiêm ngặt `model.skeleton.boneCount == 20`. Khác 20 xương sẽ BỊ ĐƠ T-POSE.
    Armature bắt buộc phải có đầy đủ 20 xương chuẩn:
    - mixamorig:Hips (Root tại Y = 0.95m)
    - Spine -> Spine1 -> Spine2 -> Neck -> Head
    - LeftShoulder -> LeftArm -> LeftForeArm -> LeftHand
    - RightShoulder -> RightArm -> RightForeArm -> RightHand
    - LeftUpLeg -> LeftLeg -> LeftFoot
    - RightUpLeg -> RightLeg -> RightFoot
    (Xương bàn chân dừng ở mixamorig:LeftFoot và mixamorig:RightFoot, KHÔNG CÓ ToeBase).                            
                                                                                                 
    ---                                                                                          
                                                                                                 
    ### 2. PHÂN BỔ TRỌNG SỐ DA (SKIN WEIGHT BLENDING CHO QUẦN)                                   
    Mỗi đỉnh chỉ liên kết tối đa 4 xương (`skinIndex` và `skinWeight` kiểu `vec4`), tổng 4 trọng 
    số của mỗi đỉnh PHẢI BẰNG CHÍNH XÁC 1.0:                                                     
    1. Cạp quần (Waistband, Y ≈ 0.94m):                                                          
       - 80% gán vào `mixamorig:Hips`, 20% gán vào `mixamorig:Spine`.                            
    2. Đũng quần (Crotch, Y ≈ 0.72m):                                                            
       - Chuyển tiếp mượt: 50% `mixamorig:Hips`, 25% `mixamorig:LeftUpLeg`, 25%                  
       `mixamorig:RightUpLeg` (chống rách đũng khi xoạc chân).                                   
    3. Đùi (Thighs, Y: 0.52m -> 0.88m):                                                          
       - Đùi trái: 85% `mixamorig:LeftUpLeg` + 15% `mixamorig:Hips`.                             
       - Đùi phải: 85% `mixamorig:RightUpLeg` + 15% `mixamorig:Hips`.                            
    4. Đầu gối (Knees, Y ≈ 0.48m):                                                               
       - Chuyển tiếp mượt: 50% `UpLeg` (đùi) + 50% `Leg` (bắp chân) để khi gập gối vải không bị  
       méo dị dạng.                                                                              
    5. Bắp chân & Xà cạp bó chân (Calf & Gaiters, Y: 0.12m -> 0.45m):                            
       - Chân trái: 95% `mixamorig:LeftLeg` + 5% `mixamorig:LeftFoot`.                           
       - Chân phải: 95% `mixamorig:RightLeg` + 5% `mixamorig:RightFoot`.                         
                                                                                                 
    ---                                                                                          
                                                                                                 
    ### 3. QUY CHUẨN HÌNH HỌC & CHỐNG LỖI CULLING (RAYLIB COMPATIBILITY)                         
    Engine game C99/Vulkan của tôi mặc định BẬT Backface Culling:                                
    1. Normal hướng ra ngoài (Outward Normals):                                                  
       - Mọi mặt đa giác mặt ngoài PHẢI có thứ tự đỉnh ngược chiều kim đồng hồ (CCW) và vector   
       pháp tuyến (`normal`) hướng thẳng ra ngoài không gian.                                    
    2. Tạo độ dày vỏ thực tế (Solidified Shell):                                                 
       - Ống quần BẮT BUỘC phải tạo 2 lớp đa giác (độ dày vải 2.5mm) gồm cả mặt trong và mặt     
       ngoài với pháp tuyến đối xứng nhau.                                                       
       - Điều này đảm bảo khi nhìn từ cạp quần xuống hoặc từ gấu quần lên, lòng quần KHÔNG BỊ    
       TÀNG HÌNH.                                                                                
                                                                                                 
    ---                                                                                          
                                                                                                 
    ### 4. VẼ PROCEDURAL TEXTURE CANVAS 512x512                                                  
    Dùng HTML Canvas 2D vẽ texture PBR atlas cho quần theo đúng ảnh thiết kế:                    
    - V ∈ [0.45, 1.00]: Nền vải đen than (#1E2028) có vân dệt thô nhẹ cho phần thân đùi và đũng  
    quần.                                                                                        
    - V ∈ [0.15, 0.45]: Dải xà cạp quấn chữ X màu xám tro (#3A3D48) viền chỉ bạc chìm bó chặt ống
    chân.                                                                                        
    - V ∈ [0.00, 0.15]: Bo cạp quần màu đen tuyền (#14151B).                                     
                                                                                                 
    ---                                                                                          
                                                                                                 
    ### 5. GIAO DIỆN WEBGL & NÚT XUẤT FILE .GLB                                                  
    - Hiển thị Canvas 3D với OrbitControls, sàn bệ đá và hệ thống đèn 3 điểm.                    
    - Thanh trượt thử nghiệm (Pose Test Sliders):                                                
      - Slider thử nghiệm co/gập đầu gối 90 độ để kiểm tra vùng đầu gối không bị vỡ mesh.        
      - Slider thử nghiệm xoạc chân (Squat / Split) để kiểm tra vùng đũng quần không bị rách.    
    - Nút xuất file "EXPORT QUAN_WUXIA.GLB":                                                     
      - Sử dụng `THREE.GLTFExporter`.                                                            
      - Tùy chọn: `{ binary: true, embedImages: true, animations: [], onlyVisible: false }`.     
      - Tự động chạy hàm Sanity Check kiểm tra chuẩn hóa `skinWeight` trước khi ghi file.        
      - Tên file tải về: `quan_wuxia.glb`.     
```

---

## Prompt 5: Giày / Ủng Võ Hiệp (`giay_wuxia.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models, skins, and exports ONLY a pair of standalone Wuxia Martial Boots (Ủng / Giày Hiệp Khách) to GLB.

Do NOT include torso, head, arms, or upper pants. ONLY the boots and shin bindings.

### 1. Skeleton & Skinning
- Standard Mixamo bones: LeftLeg / RightLeg (Y=0.48m) and LeftFoot / RightFoot (Y=0.08m, ground at Y=0.0m).
- Smooth vertex weights bound to LeftLeg/LeftFoot and RightLeg/RightFoot.

### 2. Geometry Details (Y = 0.0m to 0.42m)
- Tall knee-high wuxia boots (height 0.42m).
- Reinforced curved toe caps, thick dark leather soles, and layered cloth shin wraps with leather criss-cross straps and miniature bronze buckles.

### 3. Material
- Dark oiled leather (#1c1917, roughness 0.4, metalness 0.1) with off-white cloth shin wraps (#e2e8f0).

### 4. UI & Export
- "Export Boots GLB" button downloading `giay_wuxia.glb`.
```

---

## Prompt 6: Hộ Uyển Cổ Tay (`ho_uyen.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models and exports a pair of Rigid Wuxia Forearm Bracers (Hộ Uyển Cổ Tay) to GLB.

Do NOT include hands or arms. Only the bracers as rigid socket attachments.

### 1. Attachment Socket Standard
- Rigid socket (no bones).
- Origin (0, 0, 0): Centered at the forearm/wrist axis.
- Length: 0.22m, inner radius 0.052m, outer radius 0.062m (anatomical forearm taper).

### 2. Geometry & Engravings
- Segmented layered brass/steel armor plates over dark leather backing.
- 3 leather straps with miniature metal buckles wrapping around the inner forearm.
- Embossed bronze tiger/dragon engraving on the outer dorsal plate.

### 3. UI & Export
- Two download buttons:
  - "Export Left Bracer GLB" (`ho_uyen_left.glb`)
  - "Export Right Bracer GLB" (`ho_uyen_right.glb`)
```

---

## Prompt 7: Thuận Thiên Kiếm (`kiem_thuan_thien.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models, textures, and exports a legendary Vietnamese/Wuxia Double-Edged Sword (Thuận Thiên Kiếm) to GLB.

Do NOT include character hands or body. Only the sword as a rigid socket weapon.

### 1. Socket Mount Standard
- Metric Scale: 1 unit = 1 meter.
- Origin (0, 0, 0): Placed at the EXACT center of the grip handle where the right hand holds the hilt.
- Orientation: Blade extends along +Y, flat edges face +/-Z, guard extends along +/-X.

### 2. Geometry Components
- Blade (Length 0.85m, Width 0.042m, Thickness 0.009m):
  - Double-edged diamond cross-section with central ridge fuller (rãnh kiếm).
  - Tapers smoothly to an acute wuxia piercing tip.
- Crossguard (Y = +0.10m):
  - Ornate golden dragon/lotus guard (width 0.14m, height 0.035m) with forward-swept quillons.
  - Inlaid cyan jade jewel in the guard center.
- Grip Handle (Y = -0.10m to +0.10m, Length 0.20m, Diameter 0.026m):
  - Ergonomic oval grip wrapped with dark ray-skin and criss-cross gold wire.
- Pommel & Tassel (Y = -0.10m):
  - Carved golden ring pommel with dual red silk cords and a dangling crimson tassel (tua kiếm length 0.25m).

### 3. PBR Materials & Textures
- Blade: Polished damascus steel (metalness: 0.95, roughness: 0.18) with cyan runic fuller glow.
- Guard & Pommel: Ancient gilded bronze/gold (metalness: 0.88, roughness: 0.32).
- Tassel: Crimson silk (#9e1b1b, roughness 0.7).

### 4. UI & Export
- Include OrbitControls, environment map reflections, and an "Export Kiếm GLB" button downloading `kiem_thuan_thien.glb`.
```

---

## Prompt 8: New Martial Combat Animations (`combat_moves.glb`)

```markdown
| Property | Value | Notes |
|---|---|---|
| **Scale** | `1 unit = 1 meter` | Real-world metric scale. Total character height is `1.78m`. |
| **Axes** | `+Y = Up`, `+Z = Forward`, `+X = Left` | Standard glTF 2.0 convention. |
| **Skeleton Hierarchy** | 20 bones (`mixamorig:*`) | Exact Mixamo bone names and joint coordinates. |
| **Skinning Type** | SkinnedMesh vs Rigid Socket | Clothing is SkinnedMesh; Weapons, Hats, and Bracers are Rigid Sockets. |

### Mixamo Humanoid Joint Anchors
```
mixamorig:Hips           [ 0.00, 0.95, 0.00] (Root)
├── mixamorig:Spine      [ 0.00, 1.08, 0.00]
│   └── mixamorig:Spine1 [ 0.00, 1.22, 0.00]
│       └── mixamorig:Spine2 [ 0.00, 1.36, 0.00]
│           ├── mixamorig:Neck [ 0.00, 1.52, 0.00]
│           │   └── mixamorig:Head [ 0.00, 1.66, 0.00]
│           ├── mixamorig:LeftShoulder [ 0.08, 1.48, 0.00]
│           │   └── mixamorig:LeftArm [ 0.22, 1.48, 0.00]
│           │       └── mixamorig:LeftForeArm [ 0.48, 1.48, 0.00]
│           │           └── mixamorig:LeftHand [ 0.72, 1.48, 0.00]
│           └── mixamorig:RightShoulder [-0.08, 1.48, 0.00]
│               └── mixamorig:RightArm [-0.22, 1.48, 0.00]
│                   └── mixamorig:RightForeArm [-0.48, 1.48, 0.00]
│                       └── mixamorig:RightHand [-0.72, 1.48, 0.00]
├── mixamorig:LeftUpLeg  [ 0.10, 0.90, 0.00]
│   └── mixamorig:LeftLeg [ 0.10, 0.48, 0.00]
│       └── mixamorig:LeftFoot [ 0.10, 0.08, 0.00]
└── mixamorig:RightUpLeg [-0.10, 0.90, 0.00]
    └── mixamorig:RightLeg [-0.10, 0.48, 0.00]
        └── mixamorig:RightFoot [-0.10, 0.08, 0.00]
```

---

Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that generates 5 NEW martial arts combat animation clips on the standard 20-bone Mixamo humanoid skeleton:

Bones:
mixamorig:Hips, Spine, Spine1, Spine2, Neck, Head,
LeftShoulder, LeftArm, LeftForeArm, LeftHand,
RightShoulder, RightArm, RightForeArm, RightHand,
LeftUpLeg, LeftLeg, LeftFoot, RightUpLeg, RightLeg, RightFoot.

Animations to generate (Euler/Quaternion keyframes):
1. `sword_thrust` (Kiếm Đâm - Fast lunging piercing thrust forward with RightArm extending, body leaning forward, left hand forming sword finger gesture, 35 frames).
2. `sword_combo_3hit` (Tam Liên Trảm - Triple sword strike: diagonal cut, backhand reverse cut, overhead leap chop, 75 frames).
3. `martial_dash` (Khinh Công Lướt - Low evasive burst forward, torso pitched forward at 45 deg, arms trailing behind, 25 frames).
4. `stagger_hit` (Trúng Đòn - Torso recoils backward, head jerks back, legs stagger 1 step to recover balance, 30 frames).
5. `ultimate_cast` (Vạn Kiếm Quy Tông - Floating in mid-air, both hands sweeping in a circular Taiji arc, raising sword fingers to the sky, 90 frames).

Export:
- Exports a GLB containing ONLY the Skeleton and all 5 AnimationClips (NO mesh needed).
- Button: "Export Combat Moves GLB" downloading `combat_moves.glb`.
```

---

## Automated Ingestion Pipeline

Once you generate any of the files above, drop them into their respective folder in `raw_asset/`:
```bash
raw_asset/
├── bodies/human_body.glb
├── equipment/
│   ├── headgear/non_la.glb
│   ├── torso/ao_giao_linh.glb
│   ├── legs/quan_wuxia.glb
│   ├── boots/giay_wuxia.glb
│   ├── bracers/ho_uyen_left.glb
│   └── weapons/kiem_thuan_thien.glb
└── animations/combat_moves.glb
```

Then notify me, and I will execute the engine registration and validation automatically.
