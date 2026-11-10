# Prompt 1: Thân Thể Nam Kiếm Hiệp (`human_body.glb`)

| Thuộc Tính | Giá Trị Quy Chuẩn | Ghi Chú |
|---|---|---|
| **Target Path** | `raw_asset/bodies/human_body.glb` -> `assets/characters/bodies/human_body.glb` | File GLB nhân vật nền (Base Body) |
| **Modular Slot** | `MODULAR_SLOT_BASE_BODY` | Slot gốc làm khung xương cho toàn bộ trang phục |
| **Rigging Type** | SkinnedMesh (Đúng chính xác 20 xương Mixamo) | Không tạo xương ngón tay, ngón chân, xương phụ |
| **Chiều Cao Tổng Thể** | **$1.78m$** (Đỉnh sọ $1.76m$, Búi tóc $1.81m$) | TUYỆT ĐỐI KHÔNG để vọt lên $1.93m$ |
| **Cổ (Neck)** | Cao đúng **$8cm$** ($Y \in [1.50m, 1.58m]$, $R = 0.055m$) | Khắc phục triệt để lỗi "cổ hươu cao cổ dài ngẳng" |
| **Lồng Ngực (Chest)** | $Y \in [1.25m, 1.40m]$, Max $Z \le +0.115m$, $Rx = 0.155m$ | Hở an toàn $> 3.5cm$ bên trong `ao_giao_linh.glb` ($Z = +0.150m$) |
| **Đùi & Cẳng Chân** | Đùi $R = 0.075m \to 0.060m$, Cổ chân $R = 0.044m$ tại $Y = 0.08m$ | Đút vừa khít vào lòng cổ ủng `chien_hai_van_du.glb` ($R = 0.058m$) |
| **Mesh Topology** | **1-Island Watertight SkinnedMesh** (Thể thống nhất) | TUYỆT ĐỐI KHÔNG ghép 11 khối primitive rời rạc |
| **Công Cụ Trực Tiếp** | `raw_asset/tools/generate_human_body.html` | Mở trực tiếp trong trình duyệt để xem và xuất file |

---

## 0. GIẢI MÃ NGUYÊN NHÂN 4 LỖI TRỰC QUAN KHI TẠO BODY (HÌNH ẢNH THỰC TẾ)

| Hiện Tượng Lỗi | Nguyên Nhân Kỹ Thuật Cốt Lõi | Giải Pháp Triệt Để Trong Prompt Này |
|---|---|---|
| **1. Man-nơ-canh vỡ khớp, tay gãy đôi** *(Hình 1 & 2)* | Prompt cũ không cấp mã nguồn đầy đủ, AI tự suy đoán bằng cách ghép **11 khối hộp và hình trụ rời rạc** (`BoxGeometry`, `CylinderGeometry`). Trên HTML đứng yên ở T-pose trông bình thường, nhưng khi engine áp hoạt ảnh chạy nhảy, các hình trụ quay quanh tâm cục bộ làm khớp khuỷu và đầu gối bị xé toạc, lộ cánh tay nhân đôi. | BẮT BUỘC dùng **thuật toán quét vành elip liên tục (Parametric Lofted Rings)** và ống tứ chi có nắp bán cầu lồng sâu ($3.5cm$) tạo thành **1 thể thống nhất duy nhất (1-Island Watertight Mesh)**. |
| **2. Cổ dài ngoẵng & Chiều cao vọt lên 1.933m** *(Hình 2 & 3)* | Chiều cao xương cổ bị nhân đôi, đoạn lưới cổ kéo dài từ $Y = 1.50m$ lên tới $1.70m$ ($20cm$), đẩy toàn bộ hộp sọ và búi tóc lên tới $Y = 1.933m$ (vượt chuẩn nhân vật võ hiệp $1.78m$ tới $15cm$). | Cổ người chuẩn võ hiệp chỉ cao đúng **$8cm$** ($Y \in [1.50m, 1.58m]$, bán kính $5.5cm$). Đỉnh sọ dừng tại $Y = 1.76m$. |
| **3. Búi tóc đâm thủng đỉnh Nón Lá** *(Hình 3)* | Nón Lá Hiệp Khách (`non_la_hiep_khach.glb`) được neo tại khớp đầu $Y = 1.72m$ với chiều cao nón $22cm$ (khoang trong từ $1.72m$ đến $1.94m$). Do đầu cũ bị đẩy lên $1.933m$, búi tóc trồi thẳng qua đỉnh nón. | Búi tóc củ tỏi wuxia thu gọn compact, đáy tại $Y = 1.765m$, đỉnh chóp búi tóc đạt tối đa **$Y = 1.810m$** (thấp hơn chóp nón $13cm$, nằm gọn 100% bên trong lòng nón). |
| **4. Ngực & Vai chọc thủng Áo Giao Lĩnh** *(Hình 3)* | Lồng ngực dạng hình hộp phẳng nhô ra cạnh sắc tại $Z = +0.143m$, vượt qua khoảng hở mặt trong của nẹp áo giao lĩnh ($Z \approx +0.135m$). | Tiết diện ngực nở wuxia hình elip vuốt thon mượt về phía trước với $Z_{\max} \le +0.115m$, kết hợp cơ chế Deflation tự động của engine, tạo khoảng hở an toàn $> 3.5cm$. |

---

## 1. Prompt Tạo Ảnh Thiết Kế Concept 3D (Midjourney / Flux / DALL-E 3)

```markdown
Bản vẽ thiết kế giải phẫu nhân vật nam kiếm hiệp (Male Base Character Sheet) gồm 2 góc nhìn: 
mặt trước (front view) và mặt sau (back view) đặt cạnh nhau, tư thế T-pose chuẩn đứng thẳng  
dang ngang 2 tay, góc nhìn phẳng không méo phối cảnh (orthographic view).                    
                                                                                            
Chi tiết nhân vật:                                                                           
1. Vóc dáng: Thân hình hiệp khách võ thuật săn chắc, vai rộng ngực nở vừa phải, cơ bụng 6 múi
thon gọn, tay chân gân guốc nhanh nhẹn (tỷ lệ chuẩn kiếm hiệp phương Đông, không cơ bắp hộ   
pháp cồng kềnh). Chiều cao chuẩn 1.78m.                                                     
2. Làn da: Nước da màu bánh mật sáng khỏe khoắn (da vàng ấm châu Á #E5B289).                 
3. Khuôn mặt & Kiểu tóc: Gương mặt cương nghị góc cạnh, mắt phượng mày kiếm, búi tóc củ tỏi 
wuxia buộc cao gọn gàng trên đỉnh đầu cắm trâm gỗ mộc mạc, để lộ vầng trán sáng và gáy.     
4. Trang phục tối giản: Chỉ mặc một chiếc quần đùi võ hiệp ngắn màu xám đen ôm sát hông (để  
lộ toàn bộ cơ bắp vai, ngực, bụng, lưng, cánh tay và chân phục vụ việc dựng 3D), chân trần   
không giày.                                                                                  
                                                                                            
Phông nền xám tối đơn sắc sạch sẽ, ánh sáng studio đều và phẳng giúp thấy rõ từng khối cơ    
bắp, phong cách 3D game AAA của Where Winds Meet và Black Myth: Wukong.
```

---

## 2. Prompt Lập Trình WebGL / Three.js (Xuất File `human_body.glb`)

```markdown
Bạn là một Lead Technical Artist & Kỹ sư Đồ họa WebGL / Three.js chuyên nghiệp. Hãy quan sát kỹ HÌNH ẢNH THIẾT KẾ THÂN THỂ NAM đính kèm và lập trình một ứng dụng Three.js hoàn chỉnh (chỉ trong 1 FILE HTML DUY NHẤT) để tạo mô hình 3D SkinnedMesh Thân Thể Nam Kiếm Hiệp (Wuxia Male Base Body) và XUẤT FILE `.glb` CHUẨN MỰC cho game engine C99 / Raylib 6.0 của tôi.

MÔ HÌNH NÀY LÀ KHUNG XƯƠNG GỐC CỦA TOÀN BỘ HỆ THỐNG TRANG PHỤC VÀ HOẠT ẢNH CHIẾN ĐẤU. ĐỂ KHÔNG BỊ XÉ RÁCH KHỚP, KHÔNG BỊ CỔ CAO HƯƠU CAO CỔ VÀ KHÔNG CHỌC THỦNG ÁO/NÓN, BẠN BẮT BUỘC PHẢI TUÂN THỦ TUYỆT ĐỐI CÁC QUY CHUẨN KỸ THUẬT SAU:

Tên mô hình: Thân Thể Nam Kiếm Hiệp (Wuxia Male Base Body)
ID file xuất: human_body.glb
Slot trang bị: MODULAR_SLOT_BASE_BODY

---

### 1. BỘ XƯƠNG BẮT BUỘC: ĐÚNG CHÍNH XÁC 20 XƯƠNG MIXAMO VỚI TỌA ĐỘ VÀ QUATERNION CHUẨN
Tuyệt đối KHÔNG tạo xương ngón tay, ngón chân hay xương phụ (Raylib chỉ nhận đúng 20 xương body).
Khớp vai Mixamo (`LeftShoulder`, `RightShoulder`) và khớp đùi (`LeftUpLeg`, `RightUpLeg`) BẮT BUỘC phải có Quaternion xoay chuẩn để hệ trục tọa độ của cánh tay và chân trùng khớp 100% với file hoạt ảnh của game.

Đoạn mã Three.js BẮT BUỘC phải tạo đúng 20 xương sau (tọa độ mét, Y-up):

```javascript
// Khởi tạo 20 xương chuẩn Mixamo khớp 100% với engine:
const bones = [];

// 0: Hips (Root)
const hips = new THREE.Bone(); hips.name = "mixamorig:Hips";
hips.position.set(0.00, 0.95, 0.00);
hips.quaternion.set(0.0, 0.0, 0.0, 1.0);
bones.push(hips);

// 1-3: Spine Chain
const spine = new THREE.Bone(); spine.name = "mixamorig:Spine";
spine.position.set(0.00, 0.13, 0.00); hips.add(spine); bones.push(spine);

const spine1 = new THREE.Bone(); spine1.name = "mixamorig:Spine1";
spine1.position.set(0.00, 0.14, 0.00); spine.add(spine1); bones.push(spine1);

const spine2 = new THREE.Bone(); spine2.name = "mixamorig:Spine2";
spine2.position.set(0.00, 0.14, 0.00); spine1.add(spine2); bones.push(spine2);

// 4-5: Neck & Head (Cổ cao đúng 8cm, Đỉnh đầu 1.76m)
const neck = new THREE.Bone(); neck.name = "mixamorig:Neck";
neck.position.set(0.00, 0.16, 0.00); // World Y = 1.52m
spine2.add(neck); bones.push(neck);

const head = new THREE.Bone(); head.name = "mixamorig:Head";
head.position.set(0.00, 0.14, 0.00); // World Y = 1.66m
neck.add(head); bones.push(head);

// 6-9: Tay trái (Vai xoay -90 deg để trục con duỗi dọc theo cánh tay)
const lShoulder = new THREE.Bone(); lShoulder.name = "mixamorig:LeftShoulder";
lShoulder.position.set(0.08, 0.12, 0.00);
lShoulder.quaternion.set(-0.5, -0.5, -0.5, 0.5); // BẮT BUỘC
spine2.add(lShoulder); bones.push(lShoulder);

const lArm = new THREE.Bone(); lArm.name = "mixamorig:LeftArm";
lArm.position.set(0.00, 0.14, 0.00); // World X = 0.22m
lShoulder.add(lArm); bones.push(lArm);

const lForeArm = new THREE.Bone(); lForeArm.name = "mixamorig:LeftForeArm";
lForeArm.position.set(0.00, 0.26, 0.00); // World X = 0.48m
lArm.add(lForeArm); bones.push(lForeArm);

const lHand = new THREE.Bone(); lHand.name = "mixamorig:LeftHand";
lHand.position.set(0.00, 0.24, 0.00); // World X = 0.72m
lForeArm.add(lHand); bones.push(lHand);

// 10-13: Tay phải (Đối xứng)
const rShoulder = new THREE.Bone(); rShoulder.name = "mixamorig:RightShoulder";
rShoulder.position.set(-0.08, 0.12, 0.00);
rShoulder.quaternion.set(-0.5, 0.5, 0.5, 0.5); // BẮT BUỘC
spine2.add(rShoulder); bones.push(rShoulder);

const rArm = new THREE.Bone(); rArm.name = "mixamorig:RightArm";
rArm.position.set(0.00, 0.14, 0.00); // World X = -0.22m
rShoulder.add(rArm); bones.push(rArm);

const rForeArm = new THREE.Bone(); rForeArm.name = "mixamorig:RightForeArm";
rForeArm.position.set(0.00, 0.26, 0.00); // World X = -0.48m
rArm.add(rForeArm); bones.push(rForeArm);

const rHand = new THREE.Bone(); rHand.name = "mixamorig:RightHand";
rHand.position.set(0.00, 0.24, 0.00); // World X = -0.72m
rForeArm.add(rHand); bones.push(rHand);

// 14-16: Chân trái (Hông xoay lật 180 độ)
const lUpLeg = new THREE.Bone(); lUpLeg.name = "mixamorig:LeftUpLeg";
lUpLeg.position.set(0.10, -0.05, 0.00); // World Y = 0.90m
lUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0); // BẮT BUỘC
hips.add(lUpLeg); bones.push(lUpLeg);

const lLeg = new THREE.Bone(); lLeg.name = "mixamorig:LeftLeg";
lLeg.position.set(0.00, 0.42, 0.00); // World Y = 0.48m
lUpLeg.add(lLeg); bones.push(lLeg);

const lFoot = new THREE.Bone(); lFoot.name = "mixamorig:LeftFoot";
lFoot.position.set(0.00, 0.40, 0.00); // World Y = 0.08m
lLeg.add(lFoot); bones.push(lFoot);

// 17-19: Chân phải (Đối xứng)
const rUpLeg = new THREE.Bone(); rUpLeg.name = "mixamorig:RightUpLeg";
rUpLeg.position.set(-0.10, -0.05, 0.00);
rUpLeg.quaternion.set(-1.0, 0.0, 0.0, 0.0); // BẮT BUỘC
hips.add(rUpLeg); bones.push(rUpLeg);

const rLeg = new THREE.Bone(); rLeg.name = "mixamorig:RightLeg";
rLeg.position.set(0.00, 0.42, 0.00);
rUpLeg.add(rLeg); bones.push(rLeg);

const rFoot = new THREE.Bone(); rFoot.name = "mixamorig:RightFoot";
rFoot.position.set(0.00, 0.40, 0.00);
rLeg.add(rFoot); bones.push(rFoot);
```

---

### 2. QUY CHUẨN KÍCH THƯỚC HÌNH HỌC (METRIC SCALE INVARIANTS)
1. **Chiều cao tổng thể:** Đúng **$1.78m$**.
   - Đỉnh sọ: $Y = 1.76m$.
   - Búi tóc củ tỏi: Đáy $Y = 1.765m$, đỉnh chóp $Y = 1.810m$, bán kính $0.025m$. Nằm gọn gàng bên dưới nón lá, tuyệt đối không xuyên thủng chóp nón lá!
2. **Cổ (Neck):**
   - $Y \in [1.50m, 1.58m]$ (Cao đúng $8cm$, bán kính $0.055m$). Tỷ lệ cổ tự nhiên, không làm cổ dài quá $1.60m$.
3. **Lồng ngực & Thắt lưng:**
   - Ngực: $Y \in [1.25m, 1.40m]$, bán kính ngang $Rx = 0.155m$, độ dày trước sau $Rz = 0.115m$, điểm nhô ngực $Z_{\max} \le +0.115m$.
   - Thắt lưng: $Y \in [1.02m, 1.18m]$, $Rx = 0.125m$, $Rz = 0.092m$.
   - Hông: $Y \in [0.88m, 1.02m]$, $Rx = 0.160m$, $Rz = 0.105m$.
4. **Tứ chi (Arms & Legs):**
   - Bắp tay: $X \in [\pm 0.16m, \pm 0.48m]$, bán kính $0.052m \to 0.044m$.
   - Cẳng tay: $X \in [\pm 0.48m, \pm 0.72m]$, bán kính $0.044m \to 0.034m$.
   - Bàn tay: $X \in [\pm 0.72m, \pm 0.84m]$, mu bàn tay phẳng úp xuống $-Y$.
   - Đùi: $Y \in [0.90m, 0.48m]$, bán kính $0.075m \to 0.058m$.
   - Cẳng chân: $Y \in [0.48m, 0.08m]$, bán kính $0.058m \to 0.044m$ (thon gọn để lọt hoàn toàn vào trong ủng chiến hài).
   - Bàn chân: $Y \in [0.00m, 0.08m]$, $Z \in [-0.04m, +0.15m]$, rộng $0.075m$.

---

### 3. THUẬT TOÁN TẠO LƯỚI LIỀN MẠCH (1-ISLAND WATERTIGHT MESH)
TUYỆT ĐỐI KHÔNG ghép các hình hộp chữ nhật hay hình trụ hở đầu rời rạc (`THREE.BoxGeometry` / `THREE.CylinderGeometry`) với nhau! Việc ghép các khối primitive rời rạc là nguyên nhân chính khiến mô hình vỡ thành 11 mảnh man-nơ-canh gỗ.

Áp dụng thuật toán:
- Thân mình, cổ, đầu, búi tóc: Quét vành elip liên tục (`Lofted Rings`) gồm 21 vành từ $Y = 0.88m$ đến $Y = 1.81m$, mỗi vành 24 phân đoạn xuyên suốt.
- Tứ chi: Ống tham số liên tục (`Limb Tubes`) nối mượt mà giữa các khớp xương, các đầu khớp lồng sâu vào nhau $3.5cm$.

---

### 4. PHÂN BỔ TRỌNG SỐ DA NỘI SUY MỀM MẠI (SMOOTHSTEP DISTANCE BLENDING)
Tuyệt đối KHÔNG gán cứng 100% một xương. Tất cả các khớp nối BẮT BUỘC dùng hàm chuyển tiếp nội suy mềm:
```javascript
function smoothstep(edge0, edge1, x) {
    const t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
}
```
- Khớp khuỷu tay: Nội suy giữa `LeftArm` và `LeftForeArm` trong khoảng $X \in [0.44m, 0.52m]$.
- Khớp đầu gối: Nội suy giữa `LeftUpLeg` và `LeftLeg` trong khoảng $Y \in [0.44m, 0.52m]$.
- Khớp vai: Đỉnh trên vai $40\%$ Shoulder + $60\%$ Arm. Nách dưới $50\%$ Spine2 + $30\%$ Shoulder + $20\%$ Arm.

---

### 5. PROCEDURAL CANVAS TEXTURE (1024x1024)
- Vùng da: Da vàng ấm châu Á `#E5B289` có đường đổ bóng nổi cơ ngực, cơ bụng 6 múi và rãnh lưng.
- Vùng quần đùi: Quần boxer đen than `#1B1C22` có viền thun bạc `#787C8A` bao phủ vùng hông $Y \in [0.88m, 1.02m]$.
- Vùng tóc & Búi tóc: Tóc đen tuyền `#141518` và trâm gỗ nâu `#5A3825`.

---

### 6. GIAO DIỆN WEBGL & NÚT XUẤT FILE .GLB
- Thanh trượt thử hoạt ảnh:
  - `Hạ tay / Idle`: Xoay cánh tay xuống $0^\circ \to 65^\circ$.
  - `Gập gối / Squat`: Gập đùi và đầu gối $0^\circ \to 45^\circ$.
  - `Quay đầu / Look`: Xoay đầu quanh trục Y $\pm 45^\circ$.
- Nút `XUẤT HUMAN_BODY.GLB`:
  - Đưa xương về đúng T-pose chuẩn trước khi xuất.
  - Sanity check: `skeleton.bones.length === 20`, trọng số da chuẩn hóa về $1.0$.
  - Tải về file: `human_body.glb`.
```

---

## 3. FILE ỨNG DỤNG HOÀN CHỈNH: `raw_asset/tools/generate_human_body.html`

File mã nguồn HTML hoàn chỉnh đã được lập trình sẵn và lưu tại đường dẫn:
👉 **[`raw_asset/tools/generate_human_body.html`](file:///Users/mth2610/Desktop/c_games/wuxing_skills/raw_asset/tools/generate_human_body.html)**

### Hướng dẫn sử dụng:
1. Nhấp đúp chuột để mở trực tiếp file [`generate_human_body.html`](file:///Users/mth2610/Desktop/c_games/wuxing_skills/raw_asset/tools/generate_human_body.html) trên trình duyệt (Chrome, Safari, Edge, Firefox).
2. Kéo các thanh trượt `Hạ tay / Idle`, `Gập gối / Squat`, `Quay đầu / Look` để kiểm tra độ co giãn mượt mà của các khớp, đảm bảo không có bất kỳ vết nứt, khe hở hay gãy góc nào.
3. Bấm nút **[ XUẤT HUMAN_BODY.GLB ]** để tải ngay file mô hình 3D chuẩn xác về máy.
4. Chép đè file tải về vào thư mục:
   - `assets/characters/bodies/human_body.glb`
   - `raw_asset/bodies/human_body.glb`
