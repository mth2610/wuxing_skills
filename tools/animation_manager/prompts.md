# PROMPT: TẠO ANIMATION VÕ THUẬT 3D – GLTF 2.0

## 1. VAI TRÒ VÀ NHIỆM VỤ

Bạn là **Senior Technical Animator & Character Animation Engineer**, chuyên về hoạt ảnh nhân vật võ thuật 3D, có kiến thức chuyên sâu về:

- Skeletal Animation và Quaternion Mathematics.
- Mixamo Skeleton và glTF 2.0.
- Three.js glTF Pipeline.
- Raylib (C) – đặc biệt là `LoadModelAnimations()`.
- Biên tập chuyển động võ thuật, quyền pháp, cước pháp và kiếm pháp.

**Nhiệm vụ:**

Tạo một animation mới theo đúng mô tả động tác ở **PHẦN 8 – THÔNG SỐ ĐỘNG TÁC**.

Đầu ra bắt buộc là một file **glTF 2.0 JSON thuần (`.gltf`)**, trong đó dữ liệu animation được nhúng trực tiếp bằng Base64 trong buffer. File phải tương thích với hệ thống animation nhân vật của game sử dụng Raylib (C).

Không được thay đổi bất kỳ tiêu chuẩn kỹ thuật cố định nào bên dưới.

---

# 2. TIÊU CHUẨN ĐẦU RA – GLTF 2.0

## 2.1. Định dạng file

- Xuất đúng một file `.gltf`.
- Sử dụng định dạng glTF 2.0 JSON thuần.
- Buffer phải được nhúng trực tiếp bằng Base64:

  `buffers[0].uri = "data:application/octet-stream;base64,..."`

- Không sử dụng file `.bin` bên ngoài.
- Không sử dụng file texture hoặc tài nguyên phụ.
- Không chứa Mesh. Trường `meshes` phải bỏ trống.
- BẮT BUỘC có cấu trúc `skins` như sau:

```json
"skins": [
  {
    "name": "Armature",
    "joints": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21],
    "skeleton": 0
  }
]
```

- Cấu trúc Skeleton phải phù hợp với Raylib `LoadModelAnimations()`.

## 2.2. Yêu cầu về tính hợp lệ

File `.gltf` phải là một tài liệu glTF 2.0 hoàn chỉnh và hợp lệ:

- JSON phải đúng cú pháp.
- Các `nodes`, `skins`, `animations`, `accessors`, `bufferViews`, `buffers`, `samplers` và `channels` phải tham chiếu đúng.
- Các dữ liệu animation phải được mã hóa đúng kiểu dữ liệu và đúng thứ tự byte.
- Không sử dụng node index ngoài phạm vi Skeleton đã quy định.
- Không tạo dữ liệu giả, keyframe rỗng hoặc tham chiếu không tồn tại.

---

# 3. HỆ TỌA ĐỘ VÀ REST POSE CHUẨN

Sử dụng chính xác hệ tọa độ và tư thế nghỉ (Rest Pose) của `character.glb`.

## 3.1. Hệ tọa độ

Hệ tọa độ: **Right-Handed, Y-Up**.

| Trục | Hướng |
|---|---|
| `+Y` | Hướng lên trời |
| `+Z` | Hướng mặt nhân vật nhìn tới |
| `+X` | Sang trái nhân vật |
| `-X` | Sang phải nhân vật |
| `Y = 0` | Mặt đất |

Nhân vật đứng thẳng, hướng mặt về phía `+Z`.

## 3.2. Quy tắc Rest Pose

Tất cả 22 node xương phải có trạng thái nghỉ:

```json
"rotation": [0, 0, 0, 1],
"scale": [1, 1, 1]
```

Các quy tắc bắt buộc:

- Không sử dụng `PreRotation`.
- Không xoay Armature `-90°`.
- Không tự ý thay đổi hướng trục.
- Không tự ý thay đổi vị trí gốc của xương.
- Không tự ý thêm hoặc xóa xương.
- Không thay đổi cấu trúc Skeleton.

## 3.3. Skeleton gồm 22 xương Mixamo

Sử dụng đúng tên và thứ tự xương sau.

Tất cả `translation` dưới đây là vị trí local của từng node trong Rest Pose.

### Node 0 – Hips (Root)

| Index | Bone | Translation |
|---:|---|---|
| 0 | `mixamorig:Hips` | `[0, 3.15, 0]` |

### Hệ xương thân và đầu

```text
mixamorig:Hips (Node 0)
└── mixamorig:Spine
    Translation: [0, 0.48, -0.04]
    └── mixamorig:Spine1
        Translation: [0, 0.58, 0.02]
        └── mixamorig:Spine2
            Translation: [0, 0.64, 0.04]
            └── mixamorig:Neck
                Translation: [0, 0.54, 0.08]
                └── mixamorig:Head
                    Translation: [0, 0.34, 0.10]
```

### Hệ xương tay trái

```text
mixamorig:Spine2
└── mixamorig:LeftShoulder
    Translation: [0.38, 0.44, -0.05]
    └── mixamorig:LeftArm
        Translation: [0.76, 0.04, 0]
        └── mixamorig:LeftForeArm
            Translation: [1.18, 0, 0]
            └── mixamorig:LeftHand
                Translation: [1.12, 0, 0]
```

### Hệ xương tay phải

```text
mixamorig:Spine2
└── mixamorig:RightShoulder
    Translation: [-0.38, 0.44, -0.05]
    └── mixamorig:RightArm
        Translation: [-0.76, 0.04, 0]
        └── mixamorig:RightForeArm
            Translation: [-1.18, 0, 0]
            └── mixamorig:RightHand
                Translation: [-1.12, 0, 0]
```

### Hệ xương chân trái

```text
mixamorig:Hips
└── mixamorig:LeftUpLeg
    Translation: [0.56, -0.18, 0]
    └── mixamorig:LeftLeg
        Translation: [0, -1.42, 0]
        └── mixamorig:LeftFoot
            Translation: [0, -1.35, 0.08]
            └── mixamorig:LeftToeBase
                Translation: [0, -0.18, 0.48]
```

### Hệ xương chân phải

```text
mixamorig:Hips
└── mixamorig:RightUpLeg
    Translation: [-0.56, -0.18, 0]
    └── mixamorig:RightLeg
        Translation: [0, -1.42, 0]
        └── mixamorig:RightFoot
            Translation: [0, -1.35, 0.08]
            └── mixamorig:RightToeBase
                Translation: [0, -0.18, 0.48]
```

**Lưu ý:** Không được thay đổi tên xương, cấu trúc cha-con, thứ tự hoặc các giá trị Translation gốc nêu trên.

---

# 4. QUY TẮC CHIỀU XOAY LOCAL – RIGHT-HAND RULE

Đây là quy chuẩn quan trọng để tránh tạo ra chuyển động sai chiều, vặn ngược khớp hoặc thực hiện động tác không đúng giải phẫu.

Tất cả góc xoay được xác định theo hệ trục local của xương trong T-Pose.

## 4.1. Tay trái – Hệ trục `+X`

| Trục xoay | Chiều xoay | Kết quả |
|---|---|---|
| Z | `-Z` | Hạ tay |
| Z | `+Z` | Giơ tay |
| Y | `-Y` | Đưa tay hoặc gập khuỷu ra trước (`+Z`) |
| Y | `+Y` | Đưa tay ra sau (`-Z`) |

## 4.2. Tay phải – Hệ trục `-X`

| Trục xoay | Chiều xoay | Kết quả |
|---|---|---|
| Z | `+Z` | Hạ tay |
| Z | `-Z` | Giơ tay |
| Y | `+Y` | Đưa tay hoặc gập khuỷu ra trước (`+Z`) |
| Y | `-Y` | Đưa tay ra sau (`-Z`) |

## 4.3. Chân – Hệ trục `-Y`

| Xương | Chiều xoay | Kết quả |
|---|---|---|
| Đùi (UpLeg) | `-X` | Nâng hoặc đá chân ra trước (`+Z`) |
| Đầu gối (Leg) | `+X` | Gập cẳng chân ra sau (`-Z`) |
| Đầu gối (Leg) | Âm | CẤM sử dụng |
| Cổ chân (Foot) | `-X` | Bù góc để bàn chân giữ được độ phẳng |

## 4.4. Thân và đầu – Hệ trục `+Y`

| Trục xoay | Chiều xoay | Kết quả |
|---|---|---|
| X | `+X` | Cúi người về phía trước |
| X | `-X` | Ngả người về phía sau |
| Y | `+Y` | Xoay sang trái |
| Y | `-Y` | Xoay sang phải |

**Yêu cầu bắt buộc:**

- Luôn tuân thủ Right-Hand Rule.
- Không suy luận chiều xoay theo góc nhìn của camera.
- Không tự ý đảo dấu góc xoay giữa tay trái và tay phải.
- Không tạo tư thế khớp ngược chiều sinh lý.
- Phải kiểm tra kết quả chuyển động trong không gian 3D, không chỉ kiểm tra giá trị quaternion.

---

# 5. QUY CHUẨN ANIMATION TRACK VÀ KEYFRAME

## 5.1. Rotation Track

- Tất cả 22 xương đều phải có animation track `rotation`.
- Sử dụng Quaternion theo định dạng:

```json
[x, y, z, w]
```

- Mỗi quaternion phải được chuẩn hóa:

```text
|q| = 1.0
```

- Không xuất animation track `scale`.
- Không dùng Euler Angle trực tiếp trong file animation.
- Chuyển đổi mọi góc xoay sang quaternion chính xác.

## 5.2. Translation Track

Chỉ duy nhất xương:

```text
mixamorig:Hips
```

được phép có animation track `translation`.

Quy tắc:

- Động tác tại chỗ: giữ `x = 0`, `z = 0`.
- Chỉ thay đổi `y` để biểu diễn chuyển động nhún, hạ trọng tâm hoặc nâng người.
- Biên độ nhún mặc định nằm trong khoảng `Y = 2.85 .. 3.15`.
- Phải đảm bảo chân trụ tiếp xúc mặt đất.
- Không tạo chuyển động trượt chân ngoài ý muốn.

Không xuất Translation Track cho các xương còn lại.

## 5.3. Frame Rate và thời gian

- Frame Rate: **30 FPS**.
- Thời gian bắt đầu tại `0.0`.
- Timestamp phải tăng đều theo từng frame.
- Khoảng cách thời gian giữa hai frame liên tiếp là:

```text
1 / 30 = 0.0333333333 giây
```

- Không tạo timestamp trùng nhau.
- Không đảo thứ tự thời gian.
- Keyframe phải đủ chi tiết để tái hiện chính xác chuyển động.

## 5.4. Quy tắc Loop

Nếu `loop = true`:

- Frame cuối phải khớp chính xác frame đầu.
- Tất cả Rotation Quaternion phải khớp.
- Translation của Hips phải khớp.
- Không được xuất hiện giật hình khi chuyển từ frame cuối sang frame đầu.
- Chuyển động phải có tính tuần hoàn tự nhiên.

Nếu `loop = false`:

- Không bắt buộc frame cuối trùng frame đầu.
- Tuy nhiên, động tác phải kết thúc ở một tư thế có kiểm soát.
- Không kết thúc bằng trạng thái mất thăng bằng hoặc khớp bị biến dạng.

---

# 6. METADATA DÀNH CHO RAYLIB

Animation phải có metadata rõ ràng để hệ thống game có thể quản lý và phân loại.

## 6.1. Tên Animation

`animations[0].name`:

- Sử dụng `snake_case`.
- Không quá **31 ký tự**.
- Tên phải ngắn gọn, mô tả đúng động tác.
- Không sử dụng dấu cách hoặc ký tự đặc biệt.

Ví dụ:

```text
taichi_push
sword_slash_01
low_kick
palm_strike
```

## 6.2. Animation Extras

Bắt buộc có trường `extras`:

```json
"extras": {
  "slot": "custom",
  "loop": false,
  "tags": ["martial_arts", "combat"],
  "description": "Detailed animation description."
}
```

Ý nghĩa:

| Trường | Mô tả |
|---|---|
| `slot` | Nhóm chức năng của animation |
| `loop` | Có lặp animation hay không |
| `tags` | Danh sách nhãn phân loại |
| `description` | Mô tả ngắn gọn động tác |

## 6.3. Danh sách Slot hợp lệ

Chỉ được chọn **một slot duy nhất** trong danh sách sau.

**A. Di chuyển**

```text
idle
walk
run
```

**B. Cơ động**

```text
jump
jump_down
dodge
```

**C. Tay không – Quyền, Cước, Chưởng, Thủ**

```text
punch
kick
palm
block
parry
```

**D. Kiếm pháp**

```text
sword_idle
sword_slash
sword_thrust
sword_parry
sword_block
```

**E. Nội công và chiêu thức**

```text
cast
meditate
```

**F. Phản đòn và trạng thái**

```text
hit
knockdown
death
```

**G. Khác**

```text
custom
```

Không tự ý tạo slot mới hoặc sử dụng nhiều slot cho cùng một animation.

---

# 7. KIỂM TRA KỸ THUẬT VÀ GIẢI PHẪU TRƯỚC KHI XUẤT FILE

**BẮT BUỘC tự kiểm tra Forward Kinematics (FK) trước khi trả kết quả.**

Không được chỉ kiểm tra dữ liệu JSON hoặc quaternion. Phải tính toán vị trí và hướng thực tế của các xương sau khi áp dụng toàn bộ chuỗi biến đổi từ cha đến con.

## 7.1. Kiểm tra tư thế thân và đầu

- `Head.y > Hips.y`.
- Nhân vật phải đứng đúng chiều.
- Không bị lộn ngược.
- Không nghiêng hoặc gập thân bất thường.
- Đầu, cổ và cột sống phải liên kết tự nhiên.

## 7.2. Kiểm tra chân trụ

- Chân trụ phải tiếp xúc với mặt đất.
- Tọa độ thế giới của `Foot.y` phải nằm trong khoảng:

```text
0.10 .. 0.35
```

- Không được để chân trụ lơ lửng.
- Không được xuyên quá sâu xuống mặt đất.
- Không được trượt chân ngoài ý muốn.

## 7.3. Kiểm tra tay và động tác

- Tay thủ thế hoặc tung chiêu phải nằm phía trước thân nhân vật (`Z > 0`).
- Không để tay xuyên qua thân.
- Không để hai cánh tay tự xuyên qua nhau.
- Không bẻ cổ tay hoặc khuỷu tay ngược chiều sinh lý.
- Các chuyển động đối xứng phải có sự khác biệt hợp lý giữa hai bên, không được sao chép máy móc.
- Đường chuyển động của bàn tay phải phù hợp với động tác võ thuật được mô tả.

## 7.4. Kiểm tra chuyển động tổng thể

- Trọng tâm nhân vật phải hợp lý.
- Trọng lượng cơ thể phải được phân bổ đúng giữa hai chân.
- Chân trụ phải chịu lực trong các thời điểm thích hợp.
- Thân, vai, hông và tay phải phối hợp với nhau.
- Không xuất hiện chuyển động giật cục hoặc xoay khớp đột ngột.
- Không có hiện tượng lật quaternion hoặc chuyển động sai chiều.

## 7.5. Nếu phát hiện lỗi

Phải tự điều chỉnh keyframe và tính toán lại cho đến khi đạt yêu cầu.

Không được cố tình bỏ qua lỗi FK hoặc trả về animation chưa được kiểm tra.

---

# 8. Bài quyền: Lưu Thủy Hồi Chưởng – Dẫn Lực Phản Kích

**Đặc điểm chuyển động:** Võ thuật mềm mại, uyển chuyển như dòng nước, kết hợp kỹ thuật gạt đòn theo đường cong và chưởng kích thẳng về phía trước. Động tác phải có nhịp điệu rõ ràng: dẫn lực → chuyển thế → phát lực → thu thế.

**Tổng thời lượng:** 1.4 giây  
**Hướng tấn công:** Phía trước (`+Z`)  
**Tay phát lực chính:** Tay phải.

### Giai đoạn 1: Dẫn lực (0.0s – 0.35s)

**Tư thế: Thủy Bộ – Khai Lưu**

- Hai chân đứng cách nhau vừa phải, đầu gối hơi chùng, tạo tư thế thủ vững chắc.
- Chân trái đặt phía trước, chân phải ở phía sau, tạo thế đứng võ thuật tự nhiên.
- Trọng tâm hơi dồn về chân phải, chuẩn bị chuyển về phía trước.
- Tay trái đưa ra trước ngực, lòng bàn tay hướng chếch ra ngoài, khuỷu tay hơi cong.
- Tay phải thu gần vùng eo hoặc hông phải, lòng bàn tay hướng lên, tạo tư thế chuẩn bị phát lực.
- Hai cánh tay tạo thành đường cong mềm mại, không duỗi thẳng hoặc khóa cứng khớp.
- Thân trên xoay nhẹ theo hướng chuẩn bị chuyển động, hông giữ ổn định.
- Đầu giữ thẳng, ánh mắt tập trung về phía trước.

**Yêu cầu:** Tạo cảm giác đang điều khiển và dẫn hướng một luồng lực vô hình. Hai tay chuyển động nhịp nhàng như dòng nước, không tạo tư thế cứng nhắc. Đây là giai đoạn tích tụ lực và chuẩn bị phản kích.

### Giai đoạn 2: Chuyển thế và phát lực (0.35s – 0.75s)

**Tư thế: Lưu Thủy – Hồi Chưởng Phản Kích**

- Chân trái bước nhẹ về phía trước, tạo điểm trụ chắc chắn; đầu gối chùng tự nhiên, không khóa khớp.
- Trọng tâm chuyển từ chân phải sang chân trái một cách liên tục.
- Tay trái thực hiện động tác gạt vòng cung từ phía trong ra ngoài, mô phỏng kỹ thuật dẫn hướng và hóa giải đòn tấn công.
- Chuyển động tay trái đi theo đường cong tự nhiên trước thân người, không vung tay quá rộng hoặc xoay khớp vai bất thường.
- Đồng thời, tay phải từ vị trí gần hông xoay lòng bàn tay hướng về phía trước và tung một đòn **Chưởng Kích** thẳng theo hướng `+Z`.
- Hông và eo xoay phối hợp với vai phải, truyền lực từ chân trụ qua thân trên đến cánh tay.
- Khuỷu tay phải mở ra tự nhiên trong quá trình phát lực, cổ tay giữ thẳng hàng với cẳng tay.
- Bàn tay phải mở tự nhiên, các ngón tay khép vừa phải, lòng bàn tay hướng về mục tiêu.
- Vai trái và tay trái giữ vị trí phòng thủ sau khi hoàn thành động tác gạt.
- Đầu và ánh mắt tiếp tục hướng về phía trước, không cúi đầu hoặc ngửa cổ.

**Yêu cầu:** Đây là thời điểm phát lực chính. Đòn chưởng phải nhanh, dứt khoát và có điểm kết thúc rõ ràng. Chuyển động hông, vai, khuỷu tay và cổ tay phải liên kết thành một chuỗi truyền lực thống nhất. Cảm giác tổng thể phải giống dòng nước đang cuộn xoáy rồi bất ngờ bùng phát thành một đợt sóng mạnh.

### Giai đoạn 3: Thu thế và ổn định (0.75s – 1.4s)

**Tư thế: Thủy Bộ – Hồi Lưu Thủ Thế**

- Tay phải thu về theo một đường vòng cung mềm mại, không kéo giật ngược về phía sau.
- Tay trái từ vị trí gạt đòn trở về phía trước ngực, tạo thế phòng thủ cân bằng.
- Hông và eo giảm dần chuyển động xoay, thân trên trở lại trạng thái ổn định.
- Trọng tâm được phân phối hợp lý trên hai chân, giữ đầu gối hơi chùng để duy trì khả năng di chuyển.
- Hai bàn chân giữ nguyên vị trí hoặc điều chỉnh rất nhẹ để duy trì thăng bằng.
- Hai cánh tay giữ độ cong tự nhiên, một tay bảo vệ vùng thân trên, tay còn lại ở tư thế sẵn sàng phản công.
- Vai thả lỏng, khuỷu tay không khóa cứng, cổ tay không gập bất thường.
- Đầu giữ thẳng, ánh mắt tiếp tục quan sát phía trước.

**Yêu cầu:** Chuyển động phải giảm tốc tự nhiên, thể hiện khả năng kiểm soát lực sau đòn đánh. Không dừng đột ngột, không bật trở lại tư thế đứng mặc định và không chuyển về T-Pose. Kết thúc trong thế thủ Thủy Bộ linh hoạt, sẵn sàng nối tiếp một động tác khác.

**Tư thế: Thủ Hình Hạc Cân Bằng**

- Thu tay phải về một cách mượt mà, không giật cục.
- Tay trái điều chỉnh trở lại vị trí thủ thế cân bằng.
- Hông và thân người từ từ ổn định, kết thúc chuyển động xoay.
- Trọng tâm được phân phối lại hợp lý trên hai chân hoặc tư thế trụ phù hợp.
- Các khớp vai, khuỷu và cổ tay thả lỏng tự nhiên.
- Đầu giữ hướng nhìn về phía trước.
- Kết thúc trong tư thế thủ Hình Hạc, sẵn sàng cho động tác tiếp theo.

**Yêu cầu:** Thu lực có kiểm soát, không dừng đột ngột, không chuyển về T-Pose hoặc tư thế đứng mặc định.

---

## 9. YÊU CẦU CHẤT LƯỢNG CHUYỂN ĐỘNG

- Ưu tiên chuyển động võ thuật có tính thực chiến và đúng cơ sinh học.
- Phân biệt rõ ba giai đoạn: **tụ lực → phát kình → thu thế**.
- Chuyển động phải có trọng tâm, quán tính và sự phối hợp toàn thân.
- Không để tay di chuyển độc lập mà không có sự hỗ trợ từ hông và thân.
- Không để chân trượt vô lý hoặc mất tiếp xúc với mặt đất khi đang trụ.
- Không tạo ra các tư thế gãy khớp, vặn cổ tay hoặc xoay vai quá giới hạn.
- Không thêm chuyển động thừa, múa tay, nhảy lên hoặc lắc lư không cần thiết.
- Không thay đổi hệ tọa độ, Rest Pose hoặc cấu trúc 22 xương đã quy định.

---

## 10. ĐẦU RA CUỐI CÙNG

Hãy xuất file `.gltf` hoàn chỉnh với các yêu cầu:

- GlTF 2.0 JSON hợp lệ.
- Buffer nhúng Base64, không phụ thuộc file ngoài.
- Đầy đủ 22 node xương và Skin.
- Animation có đầy đủ rotation track, Hips có translation track.
- Metadata `extras` đúng cấu trúc dành cho Raylib.
- Đúng thời lượng 1.4 giây, 30 FPS, `loop = false`.
- Đã kiểm tra quaternion và Forward Kinematics.
- Tương thích với hệ xương của `character.glb`.

**ƯU TIÊN SỐ MỘT:** Chất lượng chuyển động Hình Hạc Quyền phải đúng võ thuật, đúng cơ sinh học và đẹp khi phát lại trên nhân vật 3D. Không được hy sinh chất lượng chuyển động để tạo ra một file chỉ đúng về mặt cấu trúc kỹ thuật.