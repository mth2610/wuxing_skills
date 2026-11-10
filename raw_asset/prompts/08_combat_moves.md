# Prompt 8: Bộ Hoạt Ảnh Võ Thuật & Kiếm Pháp (`combat_moves.glb`)

| Thuộc Tính | Giá Trị Quy Chuẩn | Ghi Chú |
|---|---|---|
| **File Xuất** | `raw_asset/animations/combat_moves.glb` -> `assets/characters/animations/` | File GLB chứa thuần bộ xương & các AnimationClips |
| **Hệ Khung Xương** | Đúng chuẩn 20 xương Mixamo (`mixamorig:*`) | Tương thích 100% với `human_body.glb`, áo, quần, và vũ khí |
| **Chuẩn Hoạt Ảnh** | 60 FPS, Quaternion Keyframe Tracks | Nội suy tuyến tính / mượt (Linear / CubicSpline) |
| **Quy Chuẩn Tọa Độ** | `1 unit = 1 meter`, `+Y = Lên`, `+Z = Tới`, `+X = Trái` | Đặt gốc tọa độ mặt đất tại $Y = 0.0m$ |

---

## 1. Nguyên Lý Kỹ Thuật Skeletal Animation Trong Engine C99 / Raylib

### A. Định Dạng Keyframe Track Của Three.js & glTF 2.0
Mỗi cử động trong file `.glb` được định nghĩa bằng một `THREE.AnimationClip` chứa các `KeyframeTrack`:
1. **Root Motion / Hips Translation:**
   - Tên track: `"mixamorig:Hips.position"`
   - Kiểu: `THREE.VectorKeyframeTrack`
   - Chỉ duy nhất xương `Hips` được phép có track Translation (để nhân vật hạ thấp trọng tâm, nhún nhảy hoặc lao tới).
2. **Bone Rotations:**
   - Tên track: `"mixamorig:<BoneName>.quaternion"` (ví dụ: `"mixamorig:RightArm.quaternion"`)
   - Kiểu: `THREE.QuaternionKeyframeTrack`
   - Tuyệt đối không dùng Euler angles trong file GLB xuất ra; phải chuyển đổi sang Quaternion `(x, y, z, w)` chuẩn hóa trước khi ghi vào track.

### B. Cấu Trúc Khung Xương 20 Xương Chuẩn Mixamo
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

## 2. Danh Sách 6 Chiêu Thức Kiếm Hiệp Cần Sinh

| Tên Hoạt Ảnh (`clipName`) | Tên Võ Thuật | Thời Lượng | Đặc Tả Cử Động Kiếm Hiệp |
|---|---|---|---|
| **`sword_thrust`** | **Thuận Thiên Nhất Kiếm** (Kiếm Đâm Lao Tới) | 36 frames (~0.60s) | **Wind-up (0-10f):** Rút kiếm về sát sườn phải, chùng gối sau, tay trái đưa lên tạo thế kiếm quyết (kiếm chỉ).<br/>**Strike (10-16f):** Lao người về phía trước, chân phải dậm mạnh thành Cung Tiễn Bộ, tay phải phóng kiếm đâm thẳng xuyên tâm đối thủ.<br/>**Recovery (16-36f):** Giữ thế kình lực 4 frame rồi thu kiếm về thế thủ vững chãi. |
| **`sword_combo_3hit`** | **Tam Tuyệt Liên Hoàn Kiếm** | 72 frames (~1.20s) | **Chiêu 1 (0-22f):** Chém xéo từ vai trái xuống sườn phải (Slash Down-Right).<br/>**Chiêu 2 (22-44f):** Xoay cổ tay vung kiếm ngược từ dưới lên (Backhand Uppercut), thân mình xoay nửa vòng.<br/>**Chiêu 3 (44-72f):** Nhảy vọt lên không nửa mét, xoay 360 độ bổ kiếm từ trên đỉnh đầu xuống (Overhead Leap Cleave), tiếp đất vững vàng. |
| **`martial_dash`** | **Lăng Ba Vi Bộ** (Khinh Công Lướt) | 24 frames (~0.40s) | **Trượt đà:** Thân mình nghiêng chúi về phía trước $45^\circ$, trọng tâm hạ thấp sát mặt đất ($Y = 0.72m$).<br/>Hai tay duỗi ra sau như cánh hạc lướt gió, chân lướt nhanh trên mặt đất để lại tàn ảnh. |
| **`sword_parry_riposte`** | **Triệt Kiếm Phản Kích** | 30 frames (~0.50s) | **Đỡ đòn (0-10f):** Dựng đứng lưỡi kiếm trước ngực đỡ gạt đòn đánh của địch, thân người hơi chùng xuống đón lực.<br/>**Phản kích (10-30f):** Lợi dụng lực bật của đối thủ, xoay kiếm chém ngang cổ họng (Horizontal Riposte Slash). |
| **`hit_stagger`** | **Trúng Đòn Giật Lùi** | 24 frames (~0.40s) | Ngực và đầu giật mạnh ra sau do trúng chưởng lực, lùi bước chân sau để giữ thăng bằng, tay buông lơi kiếm rồi siết chặt lấy lại tư thế. |
| **`ultimate_taiji_cast`** | **Vạn Kiếm Quy Tông** (Khai Mở Kiếm Trận) | 90 frames (~1.50s) | **Khởi thế (0-30f):** Nhân vật bay lơ lửng lên không trung $0.8m$, hai tay vung theo hình vòng tròn Thái Cực âm dương, tà áo và tay áo bồng bềnh.<br/>**Tụ khí (30-60f):** Kiếm lơ lửng xoay tròn trước ngực, hai tay kết ấn Kiếm Quyết hướng lên trời.<br/>**Phát kình (60-90f):** Chỉ tay về phía trước phóng vạn đạo kiếm khí, tiếp đất uy dũng. |

---

## 3. PROMPT COPY-PASTE CHO AI LẬP TRÌNH THREE.JS (1 FILE HTML DUY NHẤT)

```markdown
Bạn là một Lead Technical Director & Chuyên gia Hoạt hình 3D WebGL / Three.js. Hãy lập trình một ứng dụng WebGL hoàn chỉnh (chỉ trong 1 FILE HTML DUY NHẤT) để sinh và xuất file hoạt ảnh võ thuật `combat_moves.glb` cho game engine C99 / Raylib 6.0 của tôi.

Ứng dụng phải bao gồm:
1. Bộ sinh Keyframe toán học bằng mã JavaScript tạo 6 AnimationClips chuẩn xác cho khung xương 20 xương Mixamo.
2. Mô hình người nộm 3D (Mannequin 3D + SkeletonHelper) hiển thị sống động các chiêu thức võ thuật kiếm hiệp.
3. Giao diện Dark Wuxia UI chuyên nghiệp với đầy đủ bộ điều khiển hoạt ảnh (Scrubber timeline, Play/Pause, Speed 0.25x-2.0x, Dropdown chọn chiêu thức).
4. Nút bấm "EXPORT COMBAT_MOVES.GLB" xuất file GLB chuẩn glTF 2.0.

---

### YÊU CẦU KỸ THUẬT BẮT BUỘC

#### 1. Khung xương chuẩn 20 xương Mixamo (Metric 1 unit = 1 meter):
Tạo đúng 20 xương với tên và vị trí chuẩn:
- `mixamorig:Hips` (Root, Y = 0.95m)
- `mixamorig:Spine`, `mixamorig:Spine1`, `mixamorig:Spine2`, `mixamorig:Neck`, `mixamorig:Head`
- `mixamorig:LeftShoulder`, `LeftArm`, `LeftForeArm`, `LeftHand`
- `mixamorig:RightShoulder`, `RightArm`, `RightForeArm`, `RightHand`
- `mixamorig:LeftUpLeg`, `LeftLeg`, `LeftFoot`
- `mixamorig:RightUpLeg`, `RightLeg`, `RightFoot`
Lưu ý về Quaternion gốc: Vai trái `(-0.5, -0.5, -0.5, 0.5)`, vai phải `(-0.5, 0.5, 0.5, 0.5)`, đùi trái & phải `(-1, 0, 0, 0)`.

#### 2. Thuật toán tạo Keyframe Tracks:
Sử dụng hàm chuyển đổi Euler (độ) sang Quaternion để dễ dàng dàn dựng các thế võ đẹp mắt:
```javascript
function makeRotTrack(boneName, times, eulerDegList) {
    const quatValues = [];
    const euler = new THREE.Euler();
    const quat = new THREE.Quaternion();
    for (let i = 0; i < eulerDegList.length; i++) {
        const [x, y, z] = eulerDegList[i];
        euler.set(x * Math.PI / 180, y * Math.PI / 180, z * Math.PI / 180, 'YXZ');
        quat.setFromEuler(euler);
        quatValues.push(quat.x, quat.y, quat.z, quat.w);
    }
    return new THREE.QuaternionKeyframeTrack(`mixamorig:${boneName}.quaternion`, times, quatValues);
}

function makePosTrack(boneName, times, posList) {
    const flatPos = [];
    for (let p of posList) flatPos.push(p[0], p[1], p[2]);
    return new THREE.VectorKeyframeTrack(`mixamorig:${boneName}.position`, times, flatPos);
}
```

#### 3. Dàn dựng chi tiết 6 Animation Clips (60 FPS):
1. **`sword_thrust` (36 frames, 0.60s):**
   - Hips hạ thấp từ $0.95m \to 0.82m$, lao về trước $Z: 0.0 \to +0.45m$.
   - Cột sống nghiêng tới trước $25^\circ$.
   - Tay phải giương thẳng kiếm về trước $Z$, khuỷu tay duỗi tối đa.
   - Tay trái đưa ra sau tạo ngón tay kiếm chỉ (sword finger) giữ thăng bằng.
   - Chân trái đạp trụ, chân phải sải dài về trước thành thế Đinh Tự Bộ / Cung Bộ.

2. **`sword_combo_3hit` (72 frames, 1.20s):**
   - **Nhát 1 (0-22f):** Chém chéo từ trên xuống (Slash Down-Right), hông xoay $30^\circ$.
   - **Nhát 2 (22-44f):** Vung ngược kiếm từ dưới sườn lên (Reverse Cut), chân xoay $45^\circ$.
   - **Nhát 3 (44-72f):** Nhảy vọt lên $Y = 1.35m$, bổ thẳng kiếm xuống đất từ trên đầu, dậm chân chấn động mặt đất.

3. **`martial_dash` (24 frames, 0.40s):**
   - Hips hạ thấp $Y = 0.70m$, thân mình chúi về trước $40^\circ$.
   - Hai tay duỗi ra sau lưng tạo hình cánh hạc.
   - Hai chân sải bước nhanh sát mặt đất.

4. **`sword_parry_riposte` (30 frames, 0.50s):**
   - Frame 0-12: Dựng đứng kiếm trước mặt đỡ đòn (Parry), thân hơi lùi lại.
   - Frame 12-30: Chém ngang cổ họng đối phương (Riposte), bước chân phải tới một bước dứt khoát.

5. **`hit_stagger` (24 frames, 0.40s):**
   - Frame 0-6: Cột sống và đầu giật mạnh về sau $30^\circ$, Hips lùi $0.20m$.
   - Frame 6-24: Chân sau lùi 1 bước chống đỡ, từ từ gượng dậy lấy lại thế thủ.

6. **`ultimate_taiji_cast` (90 frames, 1.50s):**
   - Frame 0-35: Nhân vật từ từ bay lơ lửng lên $Y = 1.65m$, hai tay vẽ vòng cung Thái Cực trong không trung.
   - Frame 35-65: Hai tay chắp trước ngực rồi nâng kiếm chỉ thẳng lên trời triệu hồi kiếm khí.
   - Frame 65-90: Hai tay vung mạnh về phía trước phóng kiếm quang, thân người hạ xuống tiếp đất vững vàng.

---

### GIAO DIỆN WEBGL & BỘ CÔNG CỤ HOẠT HỌA

- **Viewport 3D:**
  - OrbitControls mượt mà, lưới tọa độ mặt đất bán kính 5m, trục tọa độ RGB.
  - Mô hình người nộm hiệp khách (Mannequin) bằng các hình capsule/cylinder kết nối với xương, tay phải cầm kiếm 3D thanh thoát.
  - Bật/Tắt hiển thị `THREE.SkeletonHelper` bằng checkbox.
- **Thanh điều khiển Hoạt ảnh (Animation Player UI):**
  - **Dropdown chọn chiêu thức:** Chuyển đổi mượt mà (Fade/CrossFade 0.2s) giữa 6 chiêu thức võ hiệp.
  - **Nút Play / Pause / Reset:** Dừng hoặc tua lại hoạt ảnh bất kỳ lúc nào.
  - **Scrubber Timeline:** Thanh kéo tua từng frame (từ frame 0 đến frame cuối) để soi rõ từng góc độ của chiêu thức.
  - **Tốc độ phát:** Nút chọn `0.25x (Slomotion)`, `0.5x`, `1.0x (Chuẩn)`, `1.5x`.
- **Nút "EXPORT COMBAT_MOVES.GLB":**
  - Xuất toàn bộ 6 animation clips vào file binary `.glb`.
  - Tự động đặt tên: `combat_moves.glb`.
  - Tương thích 100% khi thả vào thư mục `assets/characters/animations/` của game engine C99.
```
