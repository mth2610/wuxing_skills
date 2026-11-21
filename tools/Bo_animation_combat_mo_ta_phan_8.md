# BỘ MÔ TẢ ANIMATION COMBAT – THAY THẾ PHẦN 8

## Cách sử dụng

- Mỗi lần tạo file, chỉ sao chép **một mục 8.x** bên dưới vào Phần 8 của prompt gốc. Không ghép nhiều mục vào cùng một lần sinh, vì prompt gốc đang yêu cầu xuất một animation clip.
- Giữ nguyên các Phần 2–4 về skeleton, node index, rest pose và hệ trục. Không để AI tự dựng lại skeleton.
- Các động tác kiếm dưới đây giả định nhân vật cầm kiếm bằng tay phải; tay trái hỗ trợ giữ thăng bằng hoặc bảo vệ thân. Nếu nhân vật của bạn cầm kiếm tay trái, cần đổi bên chủ đạo ở mô tả động tác, không đổi tên node hoặc hệ trục.
- Tất cả clip dưới đây là **in-place**: không dịch chuyển Hips theo X/Z. Gameplay chịu trách nhiệm di chuyển nhân vật trong thế giới. Các bước chân trong clip chỉ tạo nhịp chân và tư thế, không tạo root motion.
- Tất cả thời lượng đều là bội số của 1/30 giây. Với thời lượng `T`, lấy mẫu từ `0` đến `T` với `frameCount = T * 30 + 1`; frame cuối phải đúng bằng `T`.
- Chuyển động cần có anticipation, action/contact, follow-through và recovery khi phù hợp. Không tự thêm nhảy, xoay vòng, múa tay, hiệu ứng phép thuật, vệt kiếm hoặc chuyển động ngoài mô tả.

---

## 8.1 – Kiếm Thủ Thế Cơ Bản (`sword_idle_guard`)

**Animation name:** `sword_idle_guard`  
**Slot:** `sword_idle`  
**Loop:** `true`  
**Tổng thời lượng:** `1.6 giây`  
**Hướng mặt:** `+Z`  
**Vũ khí:** Kiếm cầm tay phải.

### Tư thế và chuyển động

- Nhân vật đứng ở thế kiếm thủ trung tính, hai chân mở vừa bằng vai hoặc rộng hơn nhẹ; một chân đặt trước, chân còn lại làm chân trụ sau. Đầu gối chùng nhẹ, không khóa khớp.
- Trọng tâm thấp vừa phải, phân bổ chủ yếu trên chân sau nhưng không dồn cứng. Bàn chân bám đất, mũi chân hướng theo thế đứng tự nhiên.
- Tay phải giữ chuôi kiếm ở phía trước thân, khuỷu tay cong mềm; lưỡi kiếm giả định hướng chếch lên hoặc chếch ra trước, không xuyên qua đầu hay thân.
- Tay trái giữ gần ngực hoặc gần cẳng tay phải, tạo thế bảo vệ và cân bằng. Hai vai thả lỏng, không nhô cao.
- Thân người có dao động rất nhẹ theo nhịp thở: ngực nâng hạ nhỏ, vai và khuỷu tay phản ứng tinh tế, đầu giữ ổn định và mắt hướng về trước.
- Hông chỉ xoay vi lượng để tạo cảm giác sống, không lắc trái phải quá mức. Không bước chân, không nhấc gót rõ rệt.

### Nhịp thời gian

- `0.0–0.4s`: ổn định thế thủ, một nhịp hô hấp nhẹ.
- `0.4–0.8s`: trọng tâm nhún xuống rất nhẹ rồi trở lại, bàn tay điều chỉnh nhỏ quanh vị trí thủ kiếm.
- `0.8–1.2s`: chuyển một lượng trọng tâm rất nhỏ về chân trước, vai vẫn thả lỏng.
- `1.2–1.6s`: trở lại chính xác tư thế mở đầu.

### Yêu cầu loop

Frame đầu và frame cuối phải khớp về vị trí, hướng thân, góc tay, chân và Hips. Chuyển động hô hấp phải tuần hoàn êm, không tạo nhịp giật ở điểm nối.

**Yêu cầu chất lượng:** Đây là tư thế nền để blend sang đi bộ, chạy, tấn công, đỡ và né. Giữ silhouette rõ, tiết chế chuyển động, không diễn quá mạnh.

---

## 8.2 – Kiếm Bộ Tiến Trước (`combat_walk_forward`)

**Animation name:** `combat_walk_forward`  
**Slot:** `walk`  
**Loop:** `true`  
**Tổng thời lượng:** `1.0 giây`  
**Hướng di chuyển mô phỏng:** `+Z`  
**Vũ khí:** Kiếm cầm tay phải.

### Tư thế và chuyển động

- Duy trì thế kiếm thủ; thân và mặt luôn hướng về `+Z`, không quay toàn thân theo từng bước.
- Thực hiện chu kỳ bước tiến chậm, có kiểm soát: chân trái bước trước trước, sau đó chân phải bước vượt/thu lên để khôi phục khoảng cách hai chân. Nếu chân trái đang là chân trước trong tư thế đầu, hãy bắt đầu chu kỳ bằng nhấc nhẹ chân sau rồi tiến theo nhịp tự nhiên, nhưng kết thúc phải khớp chu kỳ mở đầu.
- Khi chân bước lên, gối gập vừa phải; bàn chân nhấc khỏi mặt đất vừa đủ, đặt xuống bằng gót hoặc lòng bàn chân tự nhiên rồi chuyển trọng lượng. Không trượt bàn chân trên mặt đất.
- Hông và ngực có phản chuyển động nhỏ để giữ thăng bằng. Vai không nhún quá mức.
- Tay phải giữ kiếm ổn định trước thân; tay trái cân bằng, không vung như chạy bộ thông thường.
- Hips chỉ nhún theo trục Y với biên độ nhỏ; X và Z luôn giữ nguyên.

### Nhịp thời gian

- `0.0–0.25s`: bắt đầu chuyển trọng lượng, chân bước nhấc lên.
- `0.25–0.50s`: chân bước đặt xuống và nhận lực; thân tiến động tác bằng chuyển động khớp, không root motion.
- `0.50–0.75s`: chân còn lại thu/bước theo, trọng tâm chuyển đều.
- `0.75–1.0s`: khôi phục chính xác pha chân của frame đầu để nối loop.

**Yêu cầu chất lượng:** Đây là combat walk, không phải đi bộ thư giãn. Giữ trọng tâm thấp, đầu hướng đối thủ, kiếm không rung lắc vô lý. Hai bàn chân phải luân phiên có pha tiếp đất rõ.

---

## 8.3 – Kiếm Bộ Chạy Tiến (`combat_run_forward`)

**Animation name:** `combat_run_forward`  
**Slot:** `run`  
**Loop:** `true`  
**Tổng thời lượng:** `0.8 giây`  
**Hướng di chuyển mô phỏng:** `+Z`  
**Vũ khí:** Kiếm cầm tay phải.

### Tư thế và chuyển động

- Nhân vật chạy áp sát trong tư thế chiến đấu, thân hơi nghiêng về trước từ cổ chân/hông, không gập lưng.
- Chân luân phiên bước nhanh, gối nâng vừa phải. Có pha hai chân cùng rời đất rất ngắn nếu phù hợp với nhịp chạy; không biến thành động tác nhảy cao.
- Chân tiếp đất phải hấp thụ lực qua cổ chân, gối và hông; tránh gối khóa cứng.
- Tay phải giữ kiếm chếch ra sau hoặc sát cạnh thân để tránh tự chém vào người; tay trái co tự nhiên hỗ trợ chạy và cân bằng.
- Vai, ngực và hông phối hợp theo nhịp chạy; đầu vẫn hướng về mục tiêu phía trước.
- Hips chỉ dao động Y nhỏ theo nhịp chạy; tuyệt đối không dịch chuyển X/Z.

### Nhịp thời gian

- `0.0–0.2s`: chân trái ở pha chống/đẩy, chân phải đang vung tới.
- `0.2–0.4s`: chân phải tiếp đất, cơ thể hấp thụ lực và chuyển pha.
- `0.4–0.6s`: chân phải đẩy, chân trái vung tới.
- `0.6–0.8s`: chân trái tiếp đất và trở lại đúng pha khởi đầu.

**Yêu cầu loop:** Frame cuối phải khớp pha chạy với frame đầu. Không tăng dần độ cao Hips, không trượt chân, không thay đổi hướng mặt.

---

## 8.4 – Kiếm Bộ Dạt Trái (`combat_strafe_left`)

**Animation name:** `combat_strafe_left`  
**Slot:** `walk`  
**Loop:** `true`  
**Tổng thời lượng:** `0.9 giây`  
**Hướng dạt mô phỏng:** Sang trái nhân vật `+X`  
**Hướng mặt:** Luôn giữ `+Z`.

### Tư thế và chuyển động

- Nhân vật giữ mặt và ngực hướng về đối thủ phía trước `+Z`; không xoay người quay sang trái.
- Thực hiện bước ngang sang trái: chân trái mở sang `+X`, chân phải thu theo để duy trì khoảng cách chiến đấu. Không bắt chéo hai chân.
- Trọng lượng chuyển sang chân trái khi đặt chân; chân phải giữ khả năng phản ứng, không bị kéo lê.
- Hông dịch chuyển tương đối nhờ phối hợp khớp chân và nghiêng thân nhẹ; không tạo translation X cho Hips.
- Kiếm giữ trước thân, tay trái mở nhẹ để cân bằng. Vai không xoay quá mức.

### Nhịp thời gian

- `0.0–0.3s`: hạ nhẹ trọng tâm, chân trái bắt đầu mở ngang.
- `0.3–0.6s`: chân trái đặt vững; chân phải thu theo, khoảng cách hai chân trở lại thế thủ.
- `0.6–0.9s`: ổn định trọng tâm và trở về đúng pha mở đầu để lặp.

**Yêu cầu loop:** Không trượt chân, không bắt chéo chân, không xoay mặt khỏi mục tiêu. Hips X/Z giữ nguyên theo quy tắc in-place.

---

## 8.5 – Kiếm Bộ Dạt Phải (`combat_strafe_right`)

**Animation name:** `combat_strafe_right`  
**Slot:** `walk`  
**Loop:** `true`  
**Tổng thời lượng:** `0.9 giây`  
**Hướng dạt mô phỏng:** Sang phải nhân vật `-X`  
**Hướng mặt:** Luôn giữ `+Z`.

### Tư thế và chuyển động

- Giữ nguyên thế kiếm thủ và hướng mặt `+Z`.
- Chân phải mở sang `-X`, sau đó chân trái thu theo để khôi phục khoảng cách hai chân. Không bắt chéo chân.
- Trọng tâm chuyển sang chân phải một cách có kiểm soát; gối chùng tự nhiên.
- Thân trên giữ hướng về phía trước, chỉ nghiêng nhẹ để bù thăng bằng.
- Tay phải giữ kiếm trước thân; tay trái cân bằng, không vung rộng.
- Hips X/Z giữ nguyên; chuyển động dạt được biểu diễn bằng tư thế và khớp chân, còn gameplay thực hiện displacement.

### Nhịp thời gian

- `0.0–0.3s`: hạ nhẹ trọng tâm, chân phải mở ngang.
- `0.3–0.6s`: chân phải tiếp đất; chân trái thu theo.
- `0.6–0.9s`: ổn định và trở về đúng pha mở đầu.

**Yêu cầu loop:** Chuyển động là ảnh gương chức năng của dạt trái nhưng không được sao chép quaternion máy móc; phải phù hợp với trục local của từng chân.

---

## 8.6 – Kiếm Chém Ngang Phải Sang Trái (`sword_slash_01`)

**Animation name:** `sword_slash_01`  
**Slot:** `sword_slash`  
**Loop:** `false`  
**Tổng thời lượng:** `0.8 giây`  
**Tay phát lực:** Tay phải  
**Đường kiếm:** Từ phía phải nhân vật `-X` quét ngang sang trái `+X`, phía trước thân.

### Giai đoạn 1 – Lấy đà (`0.0–0.18s`)

- Bắt đầu ở thế kiếm thủ trung tính.
- Tay phải kéo kiếm về phía phải thân (`-X`), khuỷu tay cong; lưỡi kiếm nằm ở vị trí có khoảng trống để thực hiện đường chém.
- Hông và vai xoay nhẹ về phía phải để tích lực; trọng tâm hơi dồn về chân sau.
- Tay trái thu gần ngực hoặc mở nhẹ để giữ cân bằng. Đầu và mắt vẫn nhìn mục tiêu.

### Giai đoạn 2 – Chém và tiếp xúc (`0.18–0.43s`)

- Hông khởi động trước, tiếp đến thân trên, vai, cánh tay và cổ tay; truyền lực theo chuỗi từ chân trụ lên vũ khí.
- Tay phải quét kiếm ngang qua vùng trước thân từ `-X` sang `+X`. Khuỷu tay không khóa cứng; cổ tay giữ thẳng hàng tương đối với cẳng tay.
- Thân xoay theo đà nhưng không xoay quá mức khiến lưng quay khỏi đối thủ.
- Tay trái thu/điều chỉnh để bảo vệ thân, không vung ngược quá rộng.
- **Hit window đề xuất:** `0.30–0.40s`.

### Giai đoạn 3 – Theo đà và hồi thế (`0.43–0.8s`)

- Kiếm tiếp tục một đoạn ngắn theo quán tính rồi giảm tốc.
- Hông và vai giảm xoay dần; chân giữ trụ, đầu gối chùng.
- Tay phải thu kiếm về thế thủ trung tính, không giật ngược đột ngột.
- Kết thúc ở tư thế ổn định, có thể blend sang combat idle hoặc combo kế tiếp.

**Yêu cầu chất lượng:** Đường kiếm phải rõ, nhanh và có trọng lượng. Không vung tay đơn độc; không bước nhảy; không tạo root motion X/Z.

---

## 8.7 – Kiếm Chém Ngang Trái Sang Phải (`sword_slash_02`)

**Animation name:** `sword_slash_02`  
**Slot:** `sword_slash`  
**Loop:** `false`  
**Tổng thời lượng:** `0.8 giây`  
**Tay phát lực:** Tay phải  
**Đường kiếm:** Từ phía trái nhân vật `+X` quét ngang sang phải `-X`, phía trước thân.

### Giai đoạn 1 – Lấy đà (`0.0–0.18s`)

- Bắt đầu từ thế kiếm thủ trung tính.
- Kiếm được đưa về phía trái thân (`+X`) vừa đủ để tạo khoảng chạy kiếm; không kéo tay xuyên qua ngực.
- Hông và vai xoay nhẹ sang trái để tích lực; trọng tâm vẫn nằm trong vùng trụ của hai chân.
- Tay trái giữ gần ngực hoặc mở ra để cân bằng.

### Giai đoạn 2 – Chém và tiếp xúc (`0.18–0.43s`)

- Hông dẫn chuyển động, sau đó thân trên, vai, khuỷu và cổ tay nối tiếp.
- Tay phải quét kiếm ngang từ `+X` sang `-X`, luôn đi trong vùng trước thân và trước mục tiêu.
- Vai không nhô lên; khuỷu không khóa; cổ tay không bẻ gập.
- Chân trụ hấp thụ lực và giữ thăng bằng; không xoay bàn chân xuyên mặt đất.
- **Hit window đề xuất:** `0.30–0.40s`.

### Giai đoạn 3 – Theo đà và hồi thế (`0.43–0.8s`)

- Kiếm đi hết quỹ đạo, giảm tốc tự nhiên.
- Hông và vai dừng xoay có kiểm soát; tay phải thu về guard.
- Tay trái trở lại vị trí bảo vệ thân.
- Kết thúc trong thế thủ ổn định, không trở về T-Pose.

**Yêu cầu chất lượng:** Đây là đòn chém đảo chiều của `sword_slash_01`; nhịp điệu tương tự nhưng đường lực và xoay thân phải hợp lý theo hướng ngược lại.

---

## 8.8 – Kiếm Chém Chéo Từ Trên Xuống (`sword_slash_03`)

**Animation name:** `sword_slash_03`  
**Slot:** `sword_slash`  
**Loop:** `false`  
**Tổng thời lượng:** `0.9 giây`  
**Tay phát lực:** Tay phải  
**Đường kiếm:** Từ cao bên phải nhân vật xuống thấp bên trái, phía trước thân.

### Giai đoạn 1 – Nâng kiếm và tích lực (`0.0–0.22s`)

- Bắt đầu ở guard. Tay phải nâng kiếm lên vùng cao bên phải đầu, không đưa lưỡi kiếm xuyên qua đầu hoặc vai.
- Khuỷu tay phải cong; vai giữ khoảng cách tự nhiên với cổ.
- Hông xoay nhẹ, trọng tâm dồn vừa phải về chân sau; gối chùng.
- Tay trái mở hoặc giữ gần ngực để cân bằng.

### Giai đoạn 2 – Chém chéo (`0.22–0.50s`)

- Chân trụ truyền lực lên hông; thân xoay vừa phải theo đường chém.
- Tay phải đưa kiếm theo đường chéo từ cao bên phải xuống thấp bên trái, qua phía trước cơ thể.
- Đường chém kết thúc ở vùng thấp nhưng không thấp đến mức lưỡi kiếm xuyên qua chân hoặc mặt đất.
- Đầu giữ hướng mục tiêu; không cúi gập cổ theo kiếm.
- **Hit window đề xuất:** `0.36–0.47s`.

### Giai đoạn 3 – Hãm đà và hồi thế (`0.50–0.9s`)

- Kiếm hoàn tất đường chém, giảm tốc nhờ vai, khuỷu và thân phối hợp.
- Tay phải thu về thế guard; hông trở lại trung tính.
- Hai chân vẫn vững, đầu gối không khóa.
- Kết thúc ở guard cân bằng.

**Yêu cầu chất lượng:** Đòn chém có trọng lượng, không phải cú vung cánh tay. Không nhảy lên, không xoay tròn toàn thân.

---

## 8.9 – Kiếm Đâm Thẳng (`sword_thrust_01`)

**Animation name:** `sword_thrust_01`  
**Slot:** `sword_thrust`  
**Loop:** `false`  
**Tổng thời lượng:** `0.8 giây`  
**Tay phát lực:** Tay phải  
**Hướng đâm:** Thẳng phía trước `+Z`.

### Giai đoạn 1 – Thu kiếm và nén lực (`0.0–0.18s`)

- Từ guard, tay phải kéo chuôi kiếm về gần hông phải, mũi kiếm hướng về mục tiêu.
- Khuỷu tay thu tự nhiên; không kéo vai ra sau quá giới hạn.
- Trọng tâm hạ nhẹ, đầu gối trước/sau chùng để chuẩn bị phát lực.
- Thân xoay nhẹ theo hướng tích lực; tay trái giữ gần ngực hoặc kéo nhẹ về sau để cân bằng.

### Giai đoạn 2 – Đâm thẳng (`0.18–0.42s`)

- Chân sau đẩy lực, hông và ngực hướng về trước.
- Tay phải duỗi ra theo hướng `+Z`, nhưng khuỷu không khóa cứng ở điểm cuối.
- Cổ tay thẳng với cẳng tay; bàn tay giữ chắc chuôi kiếm.
- Vai phải không nhô lên; đầu vẫn nhìn mục tiêu.
- Không dịch chuyển Hips theo Z; khoảng lao tới do gameplay xử lý.
- **Hit window đề xuất:** `0.30–0.40s`.

### Giai đoạn 3 – Rút kiếm và hồi thế (`0.42–0.8s`)

- Tay phải rút kiếm thẳng về guard theo quỹ đạo ngắn, có kiểm soát.
- Hông giảm xoay; trọng tâm trở về giữa hai chân.
- Tay trái trở lại vị trí cân bằng.
- Kết thúc ở thế thủ ổn định, sẵn sàng nối đòn.

**Yêu cầu chất lượng:** Đòn đâm tập trung, tuyến tính, không biến thành cú chém ngang. Mũi kiếm luôn hướng về `+Z` trong pha phát lực.

---

## 8.10 – Kiếm Trọng Kích Bổ Xuống (`sword_heavy_down`)

**Animation name:** `sword_heavy_down`  
**Slot:** `sword_slash`  
**Loop:** `false`  
**Tổng thời lượng:** `1.0 giây`  
**Tay phát lực:** Tay phải  
**Đường kiếm:** Bổ mạnh từ trên xuống trước mặt.

### Giai đoạn 1 – Tích lực (`0.0–0.30s`)

- Nhân vật hạ trọng tâm, hai gối chùng sâu hơn guard thông thường nhưng không vượt giới hạn giải phẫu.
- Tay phải nâng kiếm lên cao phía trên đầu hoặc hơi lệch phải; vai không ép sát tai.
- Hông và ngực hơi xoay để tích lực; lưng giữ ổn định, không ưỡn quá mức.
- Tay trái hỗ trợ gần cẳng tay/chuôi kiếm nếu phong cách sử dụng kiếm hai tay, hoặc giữ cân bằng nếu cầm một tay.

### Giai đoạn 2 – Bổ kiếm (`0.30–0.55s`)

- Chân trụ đạp đất, hông và thân dẫn lực; tay phải đưa kiếm bổ xuống theo đường trước thân.
- Chuyển động nhanh và nặng, nhưng vai, khuỷu và cổ tay vẫn nằm trong giới hạn tự nhiên.
- Đầu không chúi xuống; mắt theo mục tiêu.
- **Hit window đề xuất:** `0.43–0.54s`.

### Giai đoạn 3 – Hãm lực và hồi phục (`0.55–1.0s`)

- Kiếm đi hết quỹ đạo và dừng ở vùng thấp phía trước, không cắm xuyên sàn.
- Hông và thân hạ theo quán tính rồi ổn định; gối hấp thụ lực.
- Tay phải thu kiếm từ từ về guard; tay trái hỗ trợ cân bằng.
- Kết thúc ở tư thế thấp vững chắc, có thể blend về idle.

**Yêu cầu chất lượng:** Đòn nặng cần anticipation dài hơn, pha phát lực ngắn và mạnh, recovery rõ. Không thêm nhảy hoặc xoay vòng.

---

## 8.11 – Kiếm Đỡ Cao (`sword_block_high`)

**Animation name:** `sword_block_high`  
**Slot:** `sword_block`  
**Loop:** `false`  
**Tổng thời lượng:** `0.6 giây`  
**Hướng đỡ:** Vùng đầu và thân trên.

### Giai đoạn 1 – Nhận biết và nâng kiếm (`0.0–0.18s`)

- Từ guard, nhân vật phản ứng nhanh bằng cách đưa kiếm lên trước vùng đầu/ngực trên.
- Tay phải nâng chuôi kiếm, khuỷu cong; tay trái hỗ trợ gần sống kiếm hoặc giữ tư thế bảo vệ.
- Cằm thu nhẹ, vai không nhún quá mức.

### Giai đoạn 2 – Chịu lực (`0.18–0.38s`)

- Kiếm tạo góc đỡ chéo trước đầu và ngực, không nằm sát mặt.
- Khuỷu tay hơi co để hấp thụ lực; cổ tay giữ thẳng, vai và thân hơi lùi theo lực va chạm giả định.
- Trọng tâm hạ nhẹ, hai chân giữ vững.

### Giai đoạn 3 – Hạ kiếm và trở về guard (`0.38–0.6s`)

- Giảm lực ở vai và khuỷu, hạ kiếm về thế thủ.
- Đầu ngẩng lại tự nhiên, trọng tâm ổn định.

**Yêu cầu chất lượng:** Đây là một hành động đỡ đòn đơn lẻ, không phải tư thế block loop. Không tạo chuyển động tấn công hoặc phản công trong clip này.

---

## 8.12 – Kiếm Phản Gạt (`sword_parry_01`)

**Animation name:** `sword_parry_01`  
**Slot:** `sword_parry`  
**Loop:** `false`  
**Tổng thời lượng:** `0.6 giây`  
**Tay điều khiển kiếm:** Tay phải  
**Mục tiêu:** Gạt lệch đòn tấn công đang đi vào vùng trước thân.

### Giai đoạn 1 – Chuẩn bị ngắn (`0.0–0.12s`)

- Bắt đầu từ guard, tay phải thu kiếm rất ngắn về gần trung tâm thân.
- Hông và vai xoay nhẹ để chuẩn bị đổi hướng lực.
- Chân giữ trụ, đầu gối chùng.

### Giai đoạn 2 – Gạt kiếm (`0.12–0.32s`)

- Cánh tay phải đưa kiếm theo một cung ngắn, dứt khoát, gạt hướng tấn công giả định lệch khỏi trục thân.
- Chuyển động xuất phát từ thân và vai, sau đó mới tới khuỷu và cổ tay.
- Không vung thành một cú chém tấn công lớn; mục tiêu là đổi hướng lực của đối thủ.
- Thân xoay một lượng nhỏ, đầu vẫn hướng đối thủ.
- **Parry contact event đề xuất:** `0.22–0.30s`.

### Giai đoạn 3 – Thu về guard (`0.32–0.6s`)

- Kiếm trở về vị trí phòng thủ trước thân.
- Hông và vai ổn định, hai chân tiếp tục chịu lực.
- Kết thúc trong tư thế có thể nối ngay sang counter attack, nhưng không tự phát sinh đòn phản công trong clip này.

**Yêu cầu chất lượng:** Nhanh, gọn và có điểm tiếp xúc rõ. Không xoay 180 độ, không vung kiếm rộng quá mức.

---

## 8.13 – Né Tiến (`dodge_forward`)

**Animation name:** `dodge_forward`  
**Slot:** `dodge`  
**Loop:** `false`  
**Tổng thời lượng:** `0.6 giây`  
**Hướng né mô phỏng:** Tiến về `+Z`  
**Kiểu né:** Lướt thấp, nhanh, giữ thăng bằng.

### Giai đoạn 1 – Nén người (`0.0–0.12s`)

- Hạ trọng tâm nhanh bằng cách gập gối và hông vừa phải.
- Thân nghiêng nhẹ về trước, đầu vẫn hướng mục tiêu.
- Tay giữ kiếm sát thân, tránh va vào chân hoặc thân.

### Giai đoạn 2 – Lướt né (`0.12–0.38s`)

- Chân sau đẩy, chân trước dẫn hướng; cơ thể tạo cảm giác lướt nhanh về phía trước.
- Có thể nhấc gót/chuyển bàn chân trong khoảnh khắc, nhưng không bật nhảy cao.
- Vai và đầu giữ ổn định tương đối; thân thấp để giảm diện tích trúng đòn.
- Không dịch chuyển Hips theo X/Z; gameplay áp dụng displacement theo hướng `+Z`.

### Giai đoạn 3 – Hãm và ổn định (`0.38–0.6s`)

- Chân trước nhận lực, gối chùng để hấp thụ đà.
- Chân sau thu về thế thủ; kiếm trở lại vị trí bảo vệ.
- Kết thúc ở tư thế thấp và sẵn sàng hành động.

**Yêu cầu chất lượng:** Đây là né lướt sát đất, không phải chạy hoặc nhảy. Không trượt bàn chân trong pha tiếp đất.

---

## 8.14 – Né Lùi (`dodge_backward`)

**Animation name:** `dodge_backward`  
**Slot:** `dodge`  
**Loop:** `false`  
**Tổng thời lượng:** `0.6 giây`  
**Hướng né mô phỏng:** Lùi về `-Z`.

### Giai đoạn 1 – Nén thế (`0.0–0.12s`)

- Từ guard, hạ trọng tâm nhanh; đầu gối chùng, ngực giữ hướng đối thủ.
- Hai tay giữ kiếm trước thân, không hạ vũ khí xuống.

### Giai đoạn 2 – Rút người khỏi tầm đánh (`0.12–0.38s`)

- Chân sau dẫn động tác lùi; chân trước rút theo vừa đủ để giữ thế thủ.
- Hông lùi tương đối nhờ khớp chân, thân nghiêng rất nhẹ về sau nhưng không ngả mất thăng bằng.
- Đầu và mắt vẫn nhìn về `+Z`; không quay lưng đối thủ.
- Không dịch chuyển Hips theo Z; gameplay áp dụng displacement về `-Z`.

### Giai đoạn 3 – Đặt chân và hồi thế (`0.38–0.6s`)

- Chân sau tiếp đất chắc, chân trước điều chỉnh về khoảng cách chiến đấu.
- Trọng tâm ổn định; kiếm vẫn che vùng trước thân.
- Kết thúc trong guard thấp, không ngã ra sau.

**Yêu cầu chất lượng:** Né lùi phải gọn và có kiểm soát, không biến thành cú nhảy lùi hoặc cú ngã.

---

## 8.15 – Né Sang Trái (`dodge_left`)

**Animation name:** `dodge_left`  
**Slot:** `dodge`  
**Loop:** `false`  
**Tổng thời lượng:** `0.6 giây`  
**Hướng né mô phỏng:** Sang trái nhân vật `+X`  
**Hướng mặt:** `+Z`.

### Giai đoạn 1 – Nén trọng tâm (`0.0–0.12s`)

- Hạ thấp trọng tâm, gối chùng, thân hơi nghiêng về hướng chuẩn bị né.
- Kiếm giữ trước thân; đầu vẫn nhìn đối thủ.

### Giai đoạn 2 – Bước né ngang (`0.12–0.38s`)

- Chân trái mở nhanh sang `+X`, chân phải đẩy và theo sau.
- Không bắt chéo hai chân, không xoay ngực sang trái.
- Hông và vai nghiêng vừa đủ để đưa trọng tâm qua vùng chân trụ mới.
- Không dịch chuyển Hips X; gameplay áp dụng displacement sang trái.

### Giai đoạn 3 – Hãm và hồi thế (`0.38–0.6s`)

- Chân trái nhận lực; gối chùng để hấp thụ chuyển động.
- Chân phải ổn định lại khoảng cách; kiếm trở về vị trí guard.
- Kết thúc trong thế thủ hướng `+Z`.

**Yêu cầu chất lượng:** Động tác né ngang nhanh, ngắn, không xoay lưng và không trượt chân.

---

## 8.16 – Né Sang Phải (`dodge_right`)

**Animation name:** `dodge_right`  
**Slot:** `dodge`  
**Loop:** `false`  
**Tổng thời lượng:** `0.6 giây`  
**Hướng né mô phỏng:** Sang phải nhân vật `-X`  
**Hướng mặt:** `+Z`.

### Giai đoạn 1 – Nén trọng tâm (`0.0–0.12s`)

- Hạ thấp trọng tâm bằng cách chùng gối; thân giữ hướng trước.
- Kiếm giữ sát thân, không vung rộng.

### Giai đoạn 2 – Bước né ngang (`0.12–0.38s`)

- Chân phải mở sang `-X`, chân trái đẩy và theo sau.
- Không bắt chéo hai chân; thân chỉ nghiêng nhẹ để giữ trọng tâm trên vùng chân trụ.
- Đầu và ngực tiếp tục hướng `+Z`.
- Không dịch chuyển Hips X; gameplay áp dụng displacement sang phải.

### Giai đoạn 3 – Hãm và hồi thế (`0.38–0.6s`)

- Chân phải nhận lực, gối chùng.
- Chân trái ổn định lại thế đứng; kiếm trở về guard.
- Kết thúc ở tư thế sẵn sàng phản công.

**Yêu cầu chất lượng:** Đây là hướng ngược chức năng với `dodge_left`, nhưng phải được giải theo hệ trục local của chân phải và chân trái, không đảo dấu quaternion một cách máy móc.

---

## 8.17 – Trúng Đòn Từ Phía Trước (`hit_front`)

**Animation name:** `hit_front`  
**Slot:** `hit`  
**Loop:** `false`  
**Tổng thời lượng:** `0.5 giây`  
**Nguồn lực tác động:** Từ phía trước `+Z` vào ngực/thân trên.

### Giai đoạn 1 – Tiếp xúc và giật phản ứng (`0.0–0.12s`)

- Ngực và vai giật nhẹ về sau do lực tác động từ phía trước.
- Đầu lùi theo quán tính một lượng nhỏ; cổ không gập hoặc ngửa quá mức.
- Tay phải và tay trái mở/phản ứng ngắn theo bản năng nhưng vẫn giữ kiểm soát khớp.

### Giai đoạn 2 – Mất thăng bằng ngắn (`0.12–0.30s`)

- Thân nghiêng về sau vừa phải; hông và đầu gối hấp thụ lực.
- Chân lùi/điều chỉnh bằng xoay khớp để giữ thăng bằng; không trượt chân vô lý.
- Vũ khí không xuyên qua thân; tay giữ kiếm vẫn có kiểm soát.

### Giai đoạn 3 – Ổn định (`0.30–0.5s`)

- Thân trở về gần trục thẳng; tay thu lại vị trí phòng thủ.
- Kết thúc ở tư thế hơi mất thế nhưng còn đứng, sẵn sàng blend về guard hoặc stagger.

**Yêu cầu chất lượng:** Phản ứng có lực nhưng không bị thổi bay. Không thực hiện động tác ngã hoặc xoay 180 độ.

---

## 8.18 – Trúng Đòn Từ Phía Sau (`hit_back`)

**Animation name:** `hit_back`  
**Slot:** `hit`  
**Loop:** `false`  
**Tổng thời lượng:** `0.5 giây`  
**Nguồn lực tác động:** Từ phía sau `-Z` vào lưng/thân sau.

### Giai đoạn 1 – Va chạm (`0.0–0.12s`)

- Vai và lưng giật về trước do lực đẩy từ phía sau.
- Đầu chúi nhẹ theo quán tính nhưng mắt không bị xoay ngược bất thường.
- Tay phản ứng hướng về trước để tìm lại thăng bằng.

### Giai đoạn 2 – Chao người (`0.12–0.30s`)

- Thân trên nghiêng nhẹ về trước; hông giữ ổn định, đầu gối chùng để hấp thụ lực.
- Một chân có thể bước/điều chỉnh về trước bằng chuyển động khớp để tránh ngã.
- Không xoay toàn thân khỏi hướng `+Z`.

### Giai đoạn 3 – Hồi ổn định (`0.30–0.5s`)

- Cột sống và đầu trở lại tư thế tự nhiên.
- Hai tay thu về vùng bảo vệ; chân ổn định.
- Kết thúc ở trạng thái stagger nhẹ, không tự động quay mặt về phía sau.

**Yêu cầu chất lượng:** Thể hiện lực đánh từ sau lưng rõ ràng, nhưng vẫn giữ khả năng điều khiển nhân vật.

---

## 8.19 – Trúng Đòn Từ Bên Trái (`hit_left`)

**Animation name:** `hit_left`  
**Slot:** `hit`  
**Loop:** `false`  
**Tổng thời lượng:** `0.5 giây`  
**Nguồn lực tác động:** Từ bên trái nhân vật `+X`.

### Giai đoạn 1 – Va chạm (`0.0–0.12s`)

- Vai trái và phần thân trái giật về phía phải nhân vật (`-X`) do lực đến từ trái.
- Thân nghiêng ngang rất ngắn, đầu phản ứng theo thân nhưng không gập cổ quá mức.
- Tay trái co lại bảo vệ vùng thân, tay phải giữ kiếm không để vũ khí xuyên qua cơ thể.

### Giai đoạn 2 – Chao lệch (`0.12–0.30s`)

- Hông và lồng ngực chống lại lực, trọng tâm dịch chuyển tương đối bằng tư thế chân và nghiêng thân; không translation Hips X.
- Chân phải tăng chịu lực, chân trái điều chỉnh để tránh mất thăng bằng.
- Giữ mặt hướng gần `+Z`.

### Giai đoạn 3 – Ổn định (`0.30–0.5s`)

- Thân trở về trục giữa, vai hạ xuống.
- Tay trái trở lại vùng bảo vệ; chân ổn định.
- Kết thúc ở tư thế stagger nhẹ.

**Yêu cầu chất lượng:** Phản ứng phải đúng phía bị đánh; không chỉ xoay thân theo một hướng ngẫu nhiên.

---

## 8.20 – Trúng Đòn Từ Bên Phải (`hit_right`)

**Animation name:** `hit_right`  
**Slot:** `hit`  
**Loop:** `false`  
**Tổng thời lượng:** `0.5 giây`  
**Nguồn lực tác động:** Từ bên phải nhân vật `-X`.

### Giai đoạn 1 – Va chạm (`0.0–0.12s`)

- Vai phải và phần thân phải giật về phía trái nhân vật (`+X`) do lực đến từ phải.
- Đầu phản ứng theo thân một lượng nhỏ; cổ giữ giải phẫu tự nhiên.
- Tay phải co bảo vệ theo phản xạ nhưng không làm kiếm xuyên qua người; tay trái mở nhẹ để cân bằng.

### Giai đoạn 2 – Chao lệch (`0.12–0.30s`)

- Trọng tâm chống lại lực bằng cách tăng chịu lực ở chân trái và điều chỉnh chân phải.
- Thân nghiêng ngang ngắn rồi bắt đầu trở về giữa.
- Không translation Hips X; giữ mặt gần hướng `+Z`.

### Giai đoạn 3 – Ổn định (`0.30–0.5s`)

- Vai và thân trở về gần trục trung tính.
- Hai tay thu về tư thế phòng thủ; chân ổn định.
- Kết thúc ở stagger nhẹ, không ngã.

**Yêu cầu chất lượng:** Là phản ứng ngược hướng chức năng với `hit_left`, nhưng phải giải riêng theo local axis từng xương; không sao chép và chỉ đổi dấu toàn bộ quaternion.

---

# GHI CHÚ KỸ THUẬT CẦN BỔ SUNG VÀO PROMPT GỐC

Chèn các quy tắc sau vào Phần 5 và Phần 7 của prompt gốc để tránh AI diễn giải sai:

## A. Quy tắc in-place và Root Motion

- Tất cả animation trong bộ này là in-place.
- `mixamorig:Hips.translation.x` luôn bằng `0` và `mixamorig:Hips.translation.z` luôn bằng `0` ở mọi frame.
- Chỉ được thay đổi `mixamorig:Hips.translation.y` trong giới hạn đã được xác nhận bằng FK của `character.glb`.
- Không dùng translation của xương khác.
- Các động tác tiến, lùi, né trái/phải chỉ mô phỏng bằng tư thế và chu kỳ chân. Game code chịu trách nhiệm dịch chuyển nhân vật trong world space.

## B. Quy tắc lấy mẫu thời gian

- Dùng `LINEAR` interpolation cho mọi sampler để ưu tiên tương thích với bộ nạp glTF của Raylib.
- Với clip dài `T` giây ở `30 FPS`, `T` phải là bội số của `1/30` giây.
- Số frame là `T * 30 + 1`, gồm cả frame tại `t = 0` và frame kết thúc tại `t = T`.
- Timestamp frame thứ `i` là `i / 30`.
- Không dùng `CUBICSPLINE` trong file dành cho `LoadModelAnimations()` nếu chưa tự xác minh hỗ trợ ở đúng phiên bản Raylib đang build.

## C. Quy tắc sự kiện gameplay trong extras

Giữ các trường `slot`, `loop`, `tags`, `description` hiện có. Với animation tấn công/đỡ đòn, bổ sung metadata tùy chỉnh, ví dụ:

```json
"extras": {
  "slot": "sword_slash",
  "loop": false,
  "tags": ["combat", "sword", "attack"],
  "description": "Right-to-left horizontal sword slash.",
  "events": {
    "hit_window": [0.30, 0.40],
    "combo_window": [0.48, 0.66],
    "cancel_window": [0.58, 0.72]
  }
}
```

- Chỉ khai báo `hit_window` cho đòn thực sự gây sát thương.
- `combo_window` là khoảng thời gian gameplay được phép nhận lệnh nối combo.
- `cancel_window` là khoảng thời gian gameplay cho phép hủy/né nếu thiết kế combat cho phép.
- Đây là metadata tùy chỉnh; game cần tự đọc và xử lý. Không giả định glTF hoặc Raylib tự động thực thi loop, hit event hay combo event.

## D. Quy tắc track xương

- Giữ đầy đủ 22 node xương trong skeleton và giữ nguyên index/hierarchy.
- Nếu pipeline nội bộ của bạn vẫn yêu cầu rotation track cho đủ 22 xương, các xương không chuyển động phải có quaternion identity ổn định ở mọi frame: `[0, 0, 0, 1]`.
- Không tạo keyframe rỗng. Mọi sampler phải có input/output hợp lệ và mỗi channel chỉ nhắm một cặp node/path duy nhất.

## E. Cảnh báo cần xác minh trước khi dùng cho nhân vật thật

1. Chưa đưa `knockdown` nằm sàn và `get_up` từ tư thế nằm vào bộ đầu tiên. Phần 5.2 hiện giới hạn Hips Y trong `2.85..3.15`; giới hạn này có thể không đủ để đưa pelvis xuống tư thế nằm/ngồi thực tế. Trước khi tạo knockdown/get-up, cần xác minh kích thước `character.glb`, vị trí Hips và khoảng translation Y cho phép bằng FK; không tự đoán một khoảng mới.
2. Cần xác minh lại các Translation ở Phần 3 có thực sự là giá trị local của rig `character.glb` hay không. Nếu coi đơn vị là mét, Hips ở `Y = 3.15`; cộng chuỗi Spine/Spine1/Spine2/Neck/Head sẽ đưa node Head lên xấp xỉ `Y = 5.73`. Chuỗi chân từ Hips xuống ToeBase lại kết thúc xấp xỉ `Y = 0.02`. Như vậy chiều cao xấp xỉ từ ToeBase đến Head là khoảng `5.71` đơn vị. Đây là dấu hiệu rất đáng ngờ nếu nhân vật dự kiến là người có chiều cao khoảng 1.7–1.8 m. Không tự sửa các con số trong prompt nếu chưa mở `character.glb` để đo; nhưng phải xác minh trước khi yêu cầu AI tạo chuyển động giải phẫu, vì sai scale sẽ làm toàn bộ biên độ khớp và kiểm tra chân trụ sai.

Nếu các con số trên chỉ là rig quy ước đã được engine scale ở ngoài, hãy ghi rõ hệ số scale đó trong prompt và trong bước FK; nếu không, hãy sửa skeleton/rest pose đồng bộ với `character.glb` trước khi sản xuất hàng loạt animation.
