# QUY ƯỚC, VẬT LÝ VÕ THUẬT & THUẬT TOÁN CÂN BẰNG VÕ HỌC (v3 — AI-Ready)

> **Vị trí trong repo:** `VO_HOC_BALANCE.md` (ngay thư mục gốc dự án).
> **Đối tượng sử dụng:** Nhà thiết kế game, lập trình viên và **AI sinh nội dung (Skill Generator)** dùng làm nguồn chân lý duy nhất (*Single Source of Truth*) để thiết kế Bộ Võ Học mới.
> **Mục tiêu cốt lõi:**
> 1. **Ngang sức tuyệt đối theo ngân sách:** Mọi bộ võ học đều bình đẳng về tổng sức mạnh; sự khác biệt nằm ở **bản sắc chiến thuật**, không có bộ "mạnh nhất" hay "rác nhất".
> 2. **Độc lập & Tự do tổ hợp có đánh đổi:** Một bộ có thể dùng trọn bộ, tháo lẻ từng chiêu sang bộ khác, hoặc trộn nhiều bộ lại với nhau — **mọi lối xây dựng đều có lợi thế và cái giá riêng**.
> 3. **Võ thuật Vật lý – Toán học (Kiếm hiệp / Tiên hiệp thực chiến):** Chiến đấu theo tinh thần *Avatar: The Last Airbender* — Ngũ Hành là phần nối dài của **thân pháp, kình lực và quy luật vật lý hư cấu**, không phải "vẫy đũa bắn phép màu thuần".
> 4. **Đẹp nhưng Tiết chế (Chống "nồi lẩu thập cẩm"):** Phân tầng hiệu ứng rõ ràng, tách lớp không gian và tự động tiết chế khi nhiều chiêu chồng chéo.
>
> *Ghi chú phạm vi:* Phiên bản này **bỏ qua trạng thái Thái Cực** để tập trung chuẩn hóa triệt để **Nguyên bộ – Trộn bộ – Vật lý võ thuật – Ngân sách thị giác**.
> Tài liệu mang tính **quy phạm, có máy kiểm tra** (autotest `vo_hoc_budget` sẽ fail build nếu có chiêu hoặc bộ nào vi phạm ràng buộc **C1–C12**).

---

## 1. Thuật ngữ & Đơn vị chuẩn

| Thuật ngữ | Ý nghĩa |
|---|---|
| **Bộ Võ Học** | 1 bộ gồm đúng **7 chiêu cùng hệ**: **5 chiêu Đả** (tấn công / khống chế / gây trạng thái) + **2 chiêu Trợ** (khiên / hồi phục / buff / giải khống chế). |
| **Loadout** | Tối đa **7 ô chiêu** mang vào trận (`AGENT_SKILL_SLOTS = 7`), chia cứng thành **5 ô Đả (ô 1–5)** và **2 ô Trợ/Buff (ô 6–7)**. Tự do mang nguyên bộ hoặc trộn nhiều bộ. |
| **Công Lực ($B$)** | *Ngân sách* của 1 chiêu, tính từ cái giá người chơi phải trả: Mana, hồi chiêu (CD), rủi ro tầm đánh, thời gian vận chiêu. |
| **Uy Lực ($V$)** | *Tổng giá trị* chiêu mang lại (sát thương, CC, trạng thái, phòng thủ, hỗ trợ), quy đổi về cùng đơn vị **UL**. |
| **Ấn Ngũ Hành** | Dấu cộng dồn đặc trưng của từng bộ (ví dụ **Triều Ấn** của *Thủy Triều Quyền Pháp*), làm cầu nối combo nội bộ và liên bộ theo vòng Tương Sinh. |
| **Tiểu Thành / Đại Thành** | Thưởng cơ chế kích hoạt khi mang $\ge 4$ chiêu (Tiểu Thành) hoặc đủ $7/7$ chiêu (Đại Thành) của cùng một bộ. |
| **Chỉ số $Q$** | $Q = V_{\text{hội-đồng}} / V_{\text{đơn-đấu}}$, đo mức độ thiên về đơn đấu hay hội đồng. |
| **Radar 4 trục** | **Công / Thủ / Khống / Trợ** (tổng $= 10.0$ điểm), định nghĩa bản sắc chiêu, bộ và loadout. |
| **Chu trình Khởi – Phát – Thu** | 3 pha động học võ thuật bắt buộc của mọi chiêu thức: Lấy đà/Báo trước (*Wind-up*) $\to$ Phát kình (*Active*) $\to$ Thu thế/Phản lực (*Recovery*). |
| **Bậc Thị Giác (VFX Tier)** | Phân cấp độ hoành tráng hiệu ứng (**Bậc 1 Gọn**, **Bậc 2 Trung**, **Bậc 3 Điểm nhấn**) nhằm giữ chiến trường rõ ràng, tránh loãng hình ảnh. |

---

## 2. Sáu Luật Vàng & Công Bằng Đối Kháng

1. **Ngang sức theo ngân sách:** Mọi chiêu phải thỏa $\frac{|V - B|}{B} \le 5\%$. Chiêu càng mạnh hoặc nhiều hiệu ứng càng phải đắt (Mana cao, CD lâu, vận lâu hoặc tầm gần rủi ro cao).
2. **Độc lập khi tháo lẻ (Modularity):** Mọi chiêu khi đứng một mình (0 thưởng bộ, 0 Ấn sẵn có từ chiêu khác) phải đạt $V_{\text{lẻ}} \ge 0.95 \cdot B$. Cấm thiết kế chiêu "phế hoàn toàn" nếu thiếu chiêu khác đi kèm.
3. **Mạnh ở đâu thì yếu ở đó:** Mỗi bộ bắt buộc có 2 trục chủ đạo rõ nét (tổng $\ge 6.0 / 10$) và tối thiểu 1 trục yếu ($\le 1.5 / 10$). Không tồn tại bộ toàn năng.
4. **Hiệu ứng trừ thẳng vào sát thương:** Mọi CC, DoT, dịch chuyển vị trí, phá giáp… đều có giá UL cố định. Chiêu nhiều khống chế bắt buộc có sát thương thấp hơn chiêu thuần công cùng ngân sách.
5. **Mọi lối tổ hợp đều có giá đánh đổi:** Nguyên bộ ($7/7$), Song hệ Tương Sinh ($4+3$), Song hệ Tự do, hay Đa hệ Tạp phái đều có ưu thế và nhược điểm chiến thuật rõ rệt (§7).
6. **Kỷ luật Võ thuật & Tiết chế Thị giác:** Mọi chiêu phải xuất phát từ động tác võ thuật tuân theo nguyên lý vật lý hư cấu (§3) và tuân thủ ngân sách VFX (§4) — không biến chiến trường thành màn pháo hoa rối mắt.

### 2.1 Luật Công Bằng Đối Kháng (Khả năng Né & Đỡ Đòn)

**Cấm tuyệt đối** thiết kế chiêu tự động trồi lên từ mặt đất hoặc rơi từ trên trời xuống ngay tại vị trí đối thủ ở xa mà không có thời gian phản ứng. Mọi chiêu bắt buộc thuộc **1 trong 3 hình thức ra đòn hợp lệ**:

1. **Đạn đạo / Sóng di chuyển từ người ra chiêu:** Có đường bay hoặc mặt sóng tiến từ nhân vật tới mục tiêu (tích hợp `Combat_SubmitProjectile` nếu là đạn/xúc tu/vật thể bay) $\to$ đối thủ có thể **lướt né ngang** hoặc **Đấu Pháp phá/bẻ hướng**.
2. **Tỏa ra / Phát động ngay tại vị trí nhân vật:** Cận chiến, hộ thuẫn tự thân, vòng sóng $360^{\circ}$ tỏa từ chân người ra chiêu $\to$ đối thủ đọc được khoảng cách để giữ cự ly.
3. **Trồi lên / Rơi xuống ở xa CÓ DẤU HIỆU VẬT LÝ BÁO TRƯỚC (`Telegraph` $\ge 0.60\text{s}$):** Bắt buộc hiển thị hiện tượng vật lý rõ ràng trên mặt đất (nước sủi bọt, mặt đất nứt gãy, hơi nóng bốc đỏ, rễ cây rạn đất…) trong tối thiểu $0.60\text{s}$ trước khi gây sát thương/CC để đối thủ kịp **Dash né** hoặc **bật Khiên đỡ** (kiểm tra bởi **C9**).

---

## 3. Triết Lý Võ Thuật Vật Lý — Ngũ Hành Không Phải Phép Màu Thuần

Chiến đấu trong game mang hồn **kiếm hiệp / tiên hiệp thực chiến** kết hợp triết lý **Ngự Thuật (Bending)** kiểu *Avatar: The Last Airbender*. Nhân vật **không đứng yên niệm chú bắn tia sáng**, mà dùng **quyền, chưởng, cước, thân pháp, binh khí** để điều khiển và khuếch đại vật chất/năng lượng Ngũ Hành theo các quy luật vật lý – toán học nhất quán.

### 3.1 Chu trình Động học Võ thuật (Khởi – Phát – Thu)

Mọi chiêu thức khi thiết kế bắt buộc mô tả đủ 3 pha cơ học:
* **Pha Khởi thế (*Wind-up / Telegraph*):** Động tác chuẩn bị lực (hạ tấn, xoay eo, thu tay, dậm chân). Đây chính là tín hiệu trực quan giúp đối thủ đọc chiêu bằng mắt mà không cần lạm dụng vòng cảnh báo nhân tạo.
* **Pha Phát kình (*Active / Force Transfer*):** Vật chất Ngũ Hành chỉ được phóng thích theo đúng vector lực của cơ thể (đấm thẳng $\to$ sóng đẩy thẳng; quét ngang $\to$ hình quạt; xoay người $\to$ lực ly tâm/xoáy; dậm chân $\to$ sóng chấn động hướng tâm).
* **Pha Thu thế (*Follow-through / Recoil*):** Định luật bảo toàn động lượng — chiêu phát lực càng lớn (cú đấm dứt điểm, đại địa chấn, nổ hỏa) thì quán tính hoặc độ trễ thu tấn càng rõ, tạo khoảng hở tự nhiên nếu tung hụt.

### 3.2 Quy Luật Vật Lý & Toán Học Hư Cấu Theo Từng Hệ

Khi AI thiết kế chiêu cho một hệ, mọi cơ chế sát thương, di chuyển và hình ảnh **bắt buộc dẫn xuất từ nguyên lý vật lý của hệ đó**:

| Hệ | Nguyên lý Vật lý – Toán học cốt lõi | Phong cách Võ thuật đặc trưng | Cơ chế chiến đấu tiêu biểu |
|---|---|---|---|
| **Thủy** *(Water)* | **Cơ học chất lỏng & Chuyển pha:** Phương trình sóng nông (*SWE*), áp suất thủy tĩnh, độ nhớt, bảo toàn mô-men động lượng (xoáy), chuyển pha lỏng $\leftrightarrow$ rắn (băng). | *Thái Cực / Bát Quái:* Nhu khắc cương, vòng tròn liên hoàn, dẫn lực, kéo – đẩy – quấn – khóa. | Kéo giật, đẩy sóng, hút xoáy, làm chậm (lực cản nhớt), Đóng băng, bẻ/chặn đạn bằng màng nước. |
| **Hỏa** *(Fire)* | **Nhiệt động lực học & Lực đẩy phản lực:** Phản ứng cháy giãn nở thể tích tạo xung lực ($F = \dot{m} v_e$), truyền nhiệt/bức xạ, dòng đối lưu khí nóng. | *Bắc Thiếu Lâm / Đường Lang:* Tốc độ cao, tấn công trực diện, cước pháp xoay tạo lốc lửa, bước lướt bằng lực đẩy phản lực. | Bốc hỏa (DoT), xung nổ đẩy lùi, lướt áp sát nhanh, phá giáp bằng nhiệt cực cao; yếu về giữ chân/phòng thủ bền bỉ. |
| **Thổ** *(Earth)* | **Cơ học vật rắn, Sóng địa chấn & Trọng lực:** Động lượng khối lượng lớn ($p = m v$), truyền chấn động qua mặt đất, ứng suất nén, cấu trúc tinh thể/đá. | *Hồng Gia Quyền / Bát Cực Quyền:* Tấn pháp trầm vững, bám đất, ít bay nhảy; một đòn ít nhưng nặng, triệt tiêu xung lực địch. | Hất tung từ mặt đất (có vết nứt telegraph), tường/giáp đá hấp thụ lực, sóng địa chấn ngắt vận công, làm suy yếu; yếu về cơ động. |
| **Kim** *(Metal)* | **Cơ học đàn hồi, Ứng suất cắt & Cộng hưởng:** Tập trung lực trên tiết diện cực nhỏ ($P = F/A$), độ cứng cao, phản lực đàn hồi (*Parry/Reflect*), sóng âm/từ trường sắc bén. | *Kiếm pháp / Đao pháp / Thương pháp:* Đường nét thẳng, gọn, góc cạnh, ra chiêu dứt khoát, chính xác, cắt gọn quỹ đạo. | Xuyên tuyến tốc độ cao, Chảy máu (vết cắt), Phá giáp, phản đòn/đỡ đạn đàn hồi, Câm lặng (chấn âm); yếu về hồi phục duy trì. |
| **Mộc** *(Wood)* | **Sinh cơ học, Sức căng sợi & Khuếch tán:** Tăng trưởng có gốc bám (từ đất/vũ khí/thân), lực đàn hồi dây sợi, áp suất thẩm thấu (hút – truyền sinh lực), phát tán bào tử. | *Xà Quyền / Tùy Phong Trượng:* Linh hoạt, quấn trói, bẫy khu vực, lấy bền thắng tốc, dẫn dắt nhịp độ giao tranh. | Trói chân bằng dây/rễ, Dính độc (giảm hồi máu), hồi phục/hút sinh lực, vùng sinh cơ duy trì; yếu về dồn sát thương tức thì (Burst). |

### 3.3 Danh sách Cấm kỵ "Phép Màu Thuần" (*Anti-Pure-Magic Rules*)
* **Cấm đứng yên triệu hồi từ hư không:** Mọi tia, cột, vụ nổ đều phải có điểm xuất phát vật lý (từ bàn tay/chân/vũ khí truyền qua không khí hoặc truyền chấn động dọc mặt đất).
* **Cấm lạm dụng vòng tròn ma pháp phương Tây (*Arcane Magic Circles*):** Nếu cần hiển thị vùng ảnh hưởng hoặc telegraph, phải dùng **hiện tượng vật lý của hệ** (gợn sóng nước, vết rạn mặt đất, vòng tro nóng, đường khí cắt mảnh, mạng rễ cây).
* **Đấu Pháp (Clash) phải hợp lý vật lý:** Đạn đạo va chạm sẽ bị triệt tiêu, suy giảm động lượng hoặc lệch quỹ đạo theo tương khắc Ngũ Hành và Công Lực, không xuyên qua nhau vô cảm giác.

---

## 4. Kiến Trúc Thị Giác & Ngân Sách Hiệu Ứng (VFX Budget — Chống "Nồi Lẩu Thập Cẩm")

Đẹp trong game võ thuật nằm ở **nhịp điệu, đường nét dứt khoát và cảm giác lực (hit-stop, bụi/nước văng, âm thanh va chạm)** — không nằm ở việc chiêu nào cũng phát sáng chói lòa kín màn hình. Khi 4 người chơi tung chiêu trộn nhiều hệ cùng lúc, chiến trường vẫn phải **sạch, đọc rõ vị trí nhân vật và đòn nguy hiểm**.

### 4.1 Phân tầng Bậc Thị Giác (VFX Tiering) — Bắt buộc trong mỗi Bộ 7 chiêu

| Bậc VFX | Điểm VFX | Số lượng bắt buộc / bộ | Vai trò chiêu phù hợp | Quy chuẩn hiển thị |
|---|---|---|---|---|
| **Bậc 1 — Gọn (Low)** | **1 đ** | **Tối thiểu 2 chiêu** *(thường 2–3 chiêu)* | Chiêu CD ngắn ($< 4\text{s}$), thăm dò, đòn cơ bản, khiên tự thân, vùng nền duy trì. | Hình khối mảnh, sắc nét bám sát động tác tay/chân; thời gian lưu hình $< 0.35\text{s}$; độ phủ màn hình thấp; **không dùng flash/bloom mạnh**. |
| **Bậc 2 — Trung (Medium)** | **2 đ** | **2 đến 3 chiêu** | Chiêu CD trung bình ($4\text{s}$–$9\text{s}$), kéo/đẩy, quét diện rộng vừa, mở giao tranh. | Hình khối đặc trưng rõ (sóng cuộn, dây kéo, luồng kiếm khí); rỗng hoặc bán trong suốt ở tâm để không che nhân vật. |
| **Bậc 3 — Điểm nhấn (Signature)** | **3 đ** | **Tối đa 2 chiêu** *(khuyến nghị 1–2 chiêu)* | Chiêu CD dài ($\ge 7\text{s}$), kết liễu cận chiến hoặc kiểm soát vùng lớn. | Được phép hoành tráng nhất bộ (cột xoáy, song long sóng, đại địa chấn), nhưng chỉ đạt đỉnh sáng (*Peak Frame*) tối đa $\le 0.40\text{s}$ rồi rút nhanh về lớp nền. |

> **Giới hạn Ngân sách VFX cả bộ (Ràng buộc C10):**
> $$\sum_{i=1}^{7} \text{VFX\_Tier}_i \le 13 \quad (\text{với } \ge 2 \text{ chiêu Bậc 1 và } \le 2 \text{ chiêu Bậc 3})$$

### 4.2 Bốn Quy tắc Đọc Chiến Trường (Combat Readability)

1. **Phân lớp độ cao (*Vertical Layering*):**
   * *Lớp Mặt đất ($y \le 0.3\text{m}$):* Toàn bộ chiêu vùng duy trì (Buff zone, DoT zone, vùng làm chậm) và dấu hiệu Telegraph phải nằm sát mặt đất dạng *ground-decal* viền mảnh, tâm mờ ($\alpha \le 0.35$).
   * *Lớp Thân người ($0.4\text{m} \le y \le 2.0\text{m}$):* Dành riêng cho nhân vật, đòn cận chiến, đạn đạo và sóng tấn công. **Cấm** chiêu duy trì dựng tường hạt dày che khuất hình dáng nhân vật (**C11**).
   * *Lớp Trên cao ($y > 2.0\text{m}$):* Chỉ dùng ngắn hạn ($\le 0.5\text{s}$) cho pha bùng nổ của chiêu Bậc 3.
2. **Hình khối quan trọng hơn Màu sắc (*Silhouette over Color*):** Người chơi phải phân biệt được chiêu dựa trên **chữ ký hình học** (mũi nhọn thẳng, vòng cung quét, xoáy tròn, cột trụ) ngay cả khi chiến trường trộn 3–4 hệ khác nhau.
3. **Kỷ luật Sắc độ (*Color & Luminance Budget*):** Mỗi hệ chỉ dùng **1 màu chủ đạo (độ bão hòa trung bình $55\%$–$75\%$) + 1 màu nhấn sáng cho lõi kình lực**. Hiệu ứng Buff/Đồng minh luôn tối dịu và trong suốt hơn hiệu ứng Đả/Nguy hiểm.
4. **Luật Tiết chế Chồng Chéo (*Stack Dampening*):** Khi có $\ge 3$ hiệu ứng cùng tồn tại trong bán kính $4.0\text{m}$:
   * Giảm độ đục (*opacity*) của các vùng duy trì cũ xuống $\le 30\%$.
   * Cắt $50\%$ hạt phụ (*secondary particles*), chỉ ưu tiên giữ rõ **đường viền hitbox** và **dấu hiệu telegraph nguy hiểm**.
   * Cấm cộng dồn phát sáng (*additive bloom cap*) để tránh cháy trắng màn hình.

---

## 5. Thuật Toán Cân Bằng Toán Học ($B$ & $V$)

### 5.1 Công thức Công Lực ($B$) — Cái giá phải trả

$$B = (\text{Mana} + \lambda \cdot \text{CD}) \cdot R_{\text{tầm}} \cdot W_{\text{vận}}$$

| Hằng số | Giá trị | Lý do |
|---|---|---|
| $\lambda$ (giá $1\text{s}$ hồi chiêu) | **$3.0\text{ UL/s}$** | Mana hồi thụ động $5/\text{s}$ và $34/\text{s}$ khi Thiền Định. $1\text{s}$ khóa chiêu tương đương $\approx 3$ Mana hồi được trong giao tranh. |
| $R_{\text{tầm}}$ | Cận chiến **$1.20$** · Tầm trung **$1.00$** · Tầm xa **$0.90$** · Tự thân **$1.00$** | Cận chiến chịu rủi ro áp sát nên thưởng $+20\%$ ngân sách; tầm xa an toàn nên giảm $-10\%$. |
| $W_{\text{vận}}$ (`castSecs`) | $< 0.30\text{s}$: **$1.00$** · $0.30\text{s}$–$0.60\text{s}$: **$1.05$** · $> 0.60\text{s}$: **$1.10$** | Vận chiêu càng lâu càng dễ bị né hoặc ngắt thế. |

### 5.2 Công thức Uy Lực ($V$) — Tổng giá trị mang lại

$$V_{\text{kịch-bản}} = \sum_{k} \text{UL}_k \cdot \text{lượng}_k \cdot P_{\text{trúng}} \cdot N_{\text{mục-tiêu}} \cdot \text{uptime}$$

$$V = 0.5 \cdot V_{\text{đơn-đấu}} + 0.5 \cdot V_{\text{hội-đồng}}$$

* Tính trên **2 kịch bản chuẩn**: **Đơn đấu** (1 địch, 1 đồng minh là bản thân) và **Hội đồng** (2v2 / giao tranh quanh Boss).
* Hiệu ứng **lên địch** nhân $P_{\text{trúng}} \cdot N_{\text{địch}}$. Hiệu ứng **lên đồng minh** nhân $N_{\text{đồng-minh}}$ (với $P_{\text{trúng}} = 1.0$). Hiệu ứng **tức thời một lần** (giải khống chế, phá đạn) tính phẳng 1 lần.

### 5.3 Bảng định giá đầy đủ Hiệu ứng, Khống chế & Trạng thái (UL)

#### A. Sát thương trực tiếp & Sát thương theo thời gian (DoT)

| Hiệu ứng | Đơn vị | Giá UL | Trục | Cơ chế & Ghi chú cân bằng |
|---|---|---|---|---|
| **Sát thương tức thì** | $1\text{ HP}$ | **$1.00$** | Công | Chuẩn gốc ($1\text{ UL} = 1\text{ HP}$ sát thương tức thì). |
| **Bốc hỏa (Burn)** | $1\text{ HP}$ DoT | **$0.85$** | Công | Gây sát thương trong $3\text{s}$–$4\text{s}$, cộng dồn tối đa 3 tầng. Rẻ hơn $15\%$ vì có thể hồi máu / giải kịp. |
| **Dính độc (Poison)** | $1\text{ HP}$ DoT $+ 1\text{s}$ giảm hồi máu | **$0.85/\text{HP}$** $+$ **$4.0/\text{s}$** | Công + Khống | Rút máu theo thời gian và **giảm $40\%$ lượng hồi phục** (khắc chế bộ hồi máu). |
| **Chảy máu / Nội thương (Bleed)** | $1\text{ HP}$ DoT | **$0.80$** ($0.95$ nếu phạt di chuyển) | Công | Gây sát thương theo thời gian; biến thể phạt di chuyển tăng sát thương khi mục tiêu chạy/lướt. |

#### B. Hiệu ứng Khống chế (CC — chia 3 bậc)

| Bậc | Hiệu ứng | Đơn vị | Giá UL | Trục | Cơ chế & Luật áp dụng |
|---|---|---|---|---|---|
| **Cứng (Hard CC)** | **Đóng băng (Freeze)** | $1\text{s}$ | **$14.0$** | Khống | Bất động hoàn toàn + không tung chiêu + mở điều kiện **Phá Băng**. **Chịu DR.** |
| **Cứng (Hard CC)** | **Hất tung (Launch / Knock-up)** | $1\text{s}$ trên không | **$13.0$** | Khống | Bay lên không trung, cắt vận công, không tung chiêu. **Chịu DR.** |
| **Cứng (Hard CC)** | **Choáng (Stun)** | $1\text{s}$ | **$12.0$** | Khống | Bất động + không tung chiêu, ngắt Thiền Định. **Chịu DR.** |
| **Vị trí (Displacement)** | **Giật lại gần / Hút gom (Pull)** | $1\text{m}$ kéo | **$3.5$** | Khống | Kéo mục tiêu về phía mình hoặc tâm xoáy. |
| **Vị trí (Displacement)** | **Đẩy văng ra (Knockback)** | $1\text{ m/s}$ xung lực | **$2.5$** | Khống | Đẩy lùi theo vector động lượng; giá trị cao ở mép vực hoặc đẩy khỏi mục tiêu. |
| **Mềm (Soft CC)** | **Câm lặng (Silence)** | $1\text{s}$ | **$9.0$** | Khống | Đi chuyển bình thường nhưng khóa tung chiêu (vẫn đánh thường được). |
| **Mềm (Soft CC)** | **Trói chân (Root)** | $1\text{s}$ | **$8.0$** | Khống | Khóa di chuyển/lướt, vẫn có thể xoay người tung chiêu. |
| **Mềm (Soft CC)** | **Làm chậm (Slow)** | $1\text{s} \times \%\text{chậm}$ | **$10.0$** | Khống | VD: chậm $30\%$ trong $1.5\text{s} \implies 0.30 \times 1.5 \times 10 = 4.5\text{ UL}$. |

#### C. Trạng thái Bất lợi (Debuff) & Ấn Ngũ Hành

| Hiệu ứng | Đơn vị | Giá UL | Trục | Cơ chế |
|---|---|---|---|---|
| **Phá giáp / Dễ tổn thương (Vulnerable)** | $1\text{s} \times \%\text{nhận thêm}$ | **$12.0$** | Trợ | Mục tiêu nhận thêm $X\%$ sát thương từ mọi nguồn. |
| **Suy yếu (Weaken)** | $1\text{s} \times \%\text{giảm công}$ | **$10.0$** | Thủ | Giảm $X\%$ sát thương mục tiêu gây ra. |
| **Mù / Nhiễu (Blind)** | $1\text{s}$ | **$7.0$** | Thủ | Giảm mạnh độ chính xác đánh thường / tự ngắm. |
| **Ấn Ngũ Hành (Mark)** | $1$ tầng | $\frac{\text{Giá\_kích\_hoạt}}{\text{Số\_tầng}}$ | Khống | VD **Triều Ấn**: đủ 3 tầng $\to$ Đóng băng $0.75\text{s}$ ($0.75 \times 14 = 10.5\text{ UL}$) $\implies 3.5\text{ UL/tầng}$. |

#### D. Phòng thủ & Hỗ trợ (Buff / Support)

| Hiệu ứng | Đơn vị | Giá UL | Trục | Cơ chế |
|---|---|---|---|---|
| **Khiên hấp thụ (Shield)** | $1\text{ HP}$ khiên | **$0.80$** | Thủ | Chặn sát thương trước HP; có thời hạn nên rẻ hơn hồi máu $20\%$. |
| **Hồi máu (Heal)** | $1\text{ HP}$ hồi | **$1.00$** | Thủ | Hồi HP trực tiếp ($\le \text{maxHealth}$, bị giảm bởi Dính độc). |
| **Phá / Chặn đạn địch (Deflect/Block)** | $1$ viên đạn | **$8.0$** | Thủ | Tiêu hủy/bẻ đạn địch qua `Combat_DeflectProjectilesInRadius`. |
| **Giải khống chế / Trạng thái (Cleanse)** | $1$ lần giải | **$10.0$** | Thủ | Xóa ngay Choáng, Đóng băng, Trói, Hút, Làm chậm, Độc, Bốc hỏa, Chảy máu. |
| **Hồi Mana** | $1$ Mana | **$0.50$** | Trợ | Bù năng lượng cho bản thân hoặc đồng đội. |
| **Tăng tốc chạy (Haste)** | $1\text{s} \times \%\text{tăng}$ | **$5.0$** | Trợ | VD: $+20\%$ trong $3\text{s} \implies 0.20 \times 3 \times 5 = 3.0\text{ UL}$. |
| **Rút ngắn hồi chiêu (CDR)** | $1\text{s}$ giảm | **$3.0$ ($= \lambda$)** | Trợ | Rút ngắn thời gian chờ các chiêu khác đang hồi. |

### 5.4 Bảng xác suất trúng ($P_{\text{trúng}}$) & số mục tiêu kỳ vọng ($N$)

| Kiểu trúng | $P_{\text{trúng}}$ | | Hình dạng | $N$ Đơn đấu | $N$ Hội đồng |
|---|---|---|---|---|---|
| Cận chiến | $0.85$ | | Đơn mục tiêu | $1.0$ | $1.0$ |
| Đạn nhanh ($\ge 14\text{ m/s}$) | $0.75$ | | Vùng nhỏ ($r \le 1.6\text{m}$) | $1.0$ | $1.4$ |
| Đạn chậm ($< 14\text{ m/s}$) | $0.65$ | | Đường thẳng xuyên | $1.0$ | $1.8$ |
| Sóng quét (`SWE Wave`) | $0.80$ | | Hình nón | $1.0$ | $2.0$ |
| Mặt đất có trễ (`VH_HIT_GROUND`) | $0.55$ | | Vùng vừa ($r \le 3.0\text{m}$) | $1.0$ | $2.0$ |
| Vùng duy trì | $1.0 \times \text{uptime}$ | | Vùng lớn ($r > 3.0\text{m}$) | $1.0$ | $2.4$ |

### 5.5 Mười hai ràng buộc bắt buộc (Autotest `vo_hoc_budget` & Kiểm duyệt AI)

| # | Ràng buộc | Ngưỡng bắt buộc | Ý nghĩa |
|---|---|---|---|
| **C1** | Lệch ngân sách từng chiêu | $\frac{|V - B|}{B} \le 5\%$ | Không có chiêu quá mạnh hoặc quá yếu. |
| **C2** | Tự đứng vững khi tháo lẻ | $V_{\text{lẻ}} \ge 0.95 \cdot B$ | Chiêu lẻ luôn có giá trị đầy đủ khi trộn bộ. |
| **C3** | Giới hạn dồn sát thương (Burst) | Tổng sát thương kỳ vọng 5 chiêu Đả $\le 0.9 \cdot \text{HP}_{\text{PvP}} (= 90)$ | Không bộ nào hạ gục đối thủ đầy máu trong 1 vòng chiêu. |
| **C4** | Giới hạn khống chế cứng | Mỗi lần $\le 1.0\text{s}$; tổng cả bộ $\le 2.5\text{s}$ trong chu kỳ $10\text{s}$ | Chống khóa chết đối thủ không kịp phản kháng. |
| **C5** | Giảm dần hiệu lực CC (DR) | Cùng mục tiêu trong $4\text{s}$: lần 2 $\times 0.5$, lần 3 $\times 0.25$, lần 4 miễn nhiễm | Áp dụng toàn cục cho Choáng, Đóng băng, Hất tung. |
| **C6** | Chống lạm phát hiệu ứng | Mỗi chiêu tối đa $4$ loại hiệu ứng; tổng tỷ trọng CC cả bộ $\le 45\%$ | Chiêu gọn gàng, dễ đọc, bộ có điểm yếu rõ ràng. |
| **C7** | Cấu trúc bộ | Đúng **5 Đả + 2 Trợ**, cùng 1 hệ | Chuẩn hóa khung 7 ô cho mọi bộ võ học. |
| **C8** | Bản sắc bộ | Tổng 2 trục radar chủ đạo $\ge 6.0 / 10$ và $\ge 1$ trục yếu $\le 1.5 / 10$ | Đảm bảo không có bộ "toàn năng". |
| **C9** | Công bằng đối kháng (Telegraph) | Mọi chiêu `VH_HIT_GROUND` ở xa bắt buộc `castSecs` $\ge 0.60\text{s}$ + dấu hiệu vật lý mặt đất | Đối thủ luôn kịp Dash né hoặc bật Khiên đỡ (§2.1). |
| **C10** | Ngân sách VFX cả bộ | $\sum \text{VFX\_Tier} \le 13$; tối thiểu $2$ chiêu Bậc 1, tối đa $2$ chiêu Bậc 3 | Chống "nồi lẩu thập cẩm" thị giác (§4.1). |
| **C11** | Tách lớp vùng duy trì | Mọi chiêu duy trì $> 1.5\text{s}$ phải hiển thị lớp mặt đất ($y \le 0.3\text{m}$, $\alpha \le 0.35$) | Không che khuất thân người và đòn tấn công (§4.2). |
| **C12** | Vật lý Võ thuật bắt buộc | Khai báo đủ pha *Khởi–Phát–Thu* + *Nguyên lý vật lý hệ*; 0 chiêu "phép màu từ hư không" | Giữ vững bản sắc võ thuật thực chiến (§3). |

---

## 6. Phân loại định hướng chiến thuật ($Q$ & Radar)

| Chỉ số $Q = V_{\text{hội-đồng}} / V_{\text{đơn-đấu}}$ | Định hướng | Vai trò tiêu biểu |
|---|---|---|
| $Q < 1.25$ | **Đánh đơn (Duellist / Assassin)** | Truy kích 1v1, ám sát, ép góc mục tiêu lẻ |
| $1.25 \le Q \le 1.70$ | **Đa dụng (Skirmisher)** | Vừa tay đôi tốt vừa hỗ trợ giao tranh nhỏ |
| $Q > 1.70$ | **Đánh hội đồng (Teamfight / Control)** | Giao tranh 2v2, bảo vệ/công phá Boss, kiểm soát khu vực |

Radar của **loadout 7 chiêu (5 Đả + 2 Trợ)** được tính trực tiếp từ trung bình có trọng số Công Lực $B$ của 7 chiêu đang mang và hiển thị trực quan trong Tàng Kinh Các.

---

## 7. Kiến Trúc Tổ Hợp Đa Dạng — Lợi Thế & Bất Lợi Của Từng Lối Chơi

*(Bỏ qua trạng thái Thái Cực trong giai đoạn hiện tại.)*

Để **không có lối lắp chiêu nào mạnh nhất hay yếu nhất**, mọi chiêu thức phải tuân theo nguyên tắc thiết kế mô-đun:
* **Không khóa cứng nội bộ (*No Hard Dependency*):** Chiêu tiêu thụ Ấn vẫn đạt $\ge 95\% B$ khi mục tiêu có $0$ Ấn; chiêu Trợ/Buff (ô 6–7) phải hữu dụng cho mọi hệ, không viết điều kiện "chỉ buff cho chiêu cùng bộ".
* **Thưởng bộ là cơ chế vận hành, không cộng chỉ số phẳng:** Tiểu Thành ($\le +5\%$) và Đại Thành ($\le +10\%$) kích hoạt hoàn mana, giảm hồi chiêu, mở hiệu ứng phụ có điều kiện — không bao giờ là `+10% sát thương thụ động`.
* **Giao thức Ấn Tương Sinh:** Theo vòng sinh:
  $$\text{Thủy} \to \text{Mộc} \to \text{Hỏa} \to \text{Thổ} \to \text{Kim} \to \text{Thủy}$$
  Chiêu của hệ sau đánh trúng mục tiêu đang mang Ấn của hệ sinh ra mình sẽ nhận thưởng tương tác liên bộ (giới hạn giá trị $\le 10\% V$ của chiêu kích hoạt). Ví dụ: chiêu Thủy trúng mục tiêu có **Kim Ấn** $\to$ tặng thêm $+1$ **Triều Ấn**; chiêu Mộc trúng mục tiêu có **Triều Ấn** $\to$ kéo dài thời lượng Độc/Trói.

### Ma trận Đánh đổi 4 Lối Tổ Hợp Chính

| Lối tổ hợp | Cấu trúc tiêu biểu | Thưởng hệ thống | Lợi thế chiến thuật | Bất lợi / Cái giá phải trả |
|---|---|---|---|---|
| **1. Nguyên Bộ Chính Tông** | $7/7$ chiêu cùng 1 bộ | **Đại Thành** ($\le +10\% B$) | Combo nội bộ trơn tru nhất, tích và tiêu thụ Ấn nhanh nhất, tối ưu hóa tối đa 2 trục sở trường, dễ làm chủ nhịp đánh. | **Khóa cứng 1 hệ** $\to$ dễ bị đối thủ khắc hệ bắt bài trong Đấu Pháp $5 \times 5$; phải chịu trọn trục yếu ($\le 1.5/10$) và thiếu hiệu ứng đặc sản hệ khác. |
| **2. Song Hệ Tương Sinh** | $4+3$ hoặc $5+2$ (2 hệ liền kề vòng Tương Sinh) | **Tiểu Thành** ($\le +5\%$) $+$ **Ấn Tương Sinh** ($\le +10\% V$ chiêu tiêu thụ) | Tổng trần sức mạnh $\approx$ Đại Thành; vá được điểm yếu radar (VD: Thủy + Mộc vừa đóng băng vừa chống hồi máu); giảm mạnh rủi ro bị khắc hệ. | Mất cơ chế đỉnh cao của Đại Thành; đòi hỏi canh đúng thứ tự ra chiêu chéo hệ (timing chặt hơn); ít chuyên sâu cực đoan bằng nguyên bộ. |
| **3. Song Hệ Tự Do / Nghịch Hệ** | $4+3$ hoặc $5+2$ (2 hệ không liền kề, VD Thủy + Hỏa) | **Tiểu Thành** ($\le +5\%$) | Tạo combo cơ học/vật lý cực mạnh (VD: Thủy gom + đóng băng, Hỏa dồn nổ diện rộng) và **phản khắc hệ** (mang sẵn hệ khắc lại kẻ khắc hệ chính của mình). | **Không có Ấn Tương Sinh** $\to$ trần lý thuyết trong giao tranh kéo dài thấp hơn $\approx 3\%$–$5\%$, buộc người chơi phải thắng bằng tư duy vị trí & thời điểm. |
| **4. Đa Hệ / Tạp Phái Bách Gia** | $3+2+2$ hoặc $2+2+2+1$ ($\ge 3$ bộ) | **$0\%$ thưởng bộ** (có thể nối chuỗi 2 mắt xích Tương Sinh) | Tự do chọn đúng chiêu lẻ tối ưu nhất cho từng ô; **miễn nhiễm bị khắc 1 hệ**; tạo lối đánh dị biệt cực khó đoán (có thể nối chuỗi $\text{Kim} \to \text{Thủy} \to \text{Mộc}$). | Mất hoàn toàn Tiểu Thành & Đại Thành; khó tích đủ Ấn nội bộ của từng bộ; đòi hỏi kỹ năng cao để khớp nhịp hồi chiêu, Mana và thân pháp khác biệt giữa các phái. |

---

## 8. Kinh tế sở hữu (Tích lũy ở cả MOBA & Thế Giới Mở)

| Nguồn hoạt động | Ngân Lượng | Điểm Dã Tẩu | Linh Thạch hệ $X$ | Tàn Quyển bộ $X$ |
|---|---|---|---|---|
| Đánh quái / Boss Thế Giới Mở & Khám Phá | Có | – | Có (theo hệ bản đồ) | Boss bản đồ hệ $X$: $15\%$ rơi |
| Nhiệm vụ Dã Tẩu | Có | Có | Có | Mốc chuỗi $10/20/30\dots$: 1 quyển |
| **Đấu Trường PvP / MOBA** | Thua **$120$** / Thắng **$180$** | **$20$** / **$30$** | **$2$ viên** theo hệ Vô Hệ vừa dùng | – |
| Chống cày quá đà PvP | $10$ trận đầu/ngày nhận $100\%$, từ trận $11$ nhận $25\%$ | | | |

**Giá học chiêu đồng giá** (vì mọi chiêu ngang sức):
* Học 1 chiêu bất kỳ: **$300$ Ngân Lượng $+$ $6$ Linh Thạch cùng hệ**, *hoặc* **$1$ Tàn Quyển của bộ đó**.
* Chiêu đầu tiên của mỗi bộ được **giảm $50\%$ giá** để khuyến khích người chơi thử nghiệm và trộn bộ.
* **PvE vs PvP:** PvP luôn khóa Lv.1 và tắt chỉ số trang bị. PvE áp dụng `PlayerProfile_GetSkillScaling` chỉ lên **sát thương / khiên / hồi máu** — **thời lượng khống chế (CC) không bao giờ tăng theo cấp**.

---

## 9. Bộ Võ Học Mẫu (Hệ Thủy): **THỦY TRIỀU QUYỀN PHÁP**

> **Phong cách Võ thuật & Vật lý:** *Hydro-Martial Arts* — kết hợp nhu quyền dẫn lực với **Shallow Water Equations (`core/liquid/liquid_swe.h`)** cho mào sóng/vòi rồng 3D và **Screen Space Fluid (`core/liquid/liquid_impact.h`)** cho giọt văng/vũng nước (không dùng Metaball). Mọi đòn đánh tuân theo bảo toàn động lượng chất lỏng (kéo – đẩy – xoáy – chuyển pha đóng băng).
> **Bản sắc:** Mạnh **Công + Khống chế vị trí & Đóng băng**; **Đa dụng nghiêng Hội đồng ($Q = 1.66$)**; yếu ở **Trợ thuần ($0.8/10$)** và **tầm đánh xa**.

**Cơ chế đặc trưng — Triều Ấn:** Tối đa 3 tầng, tồn tại $4\text{s}$. Đủ 3 tầng kích hoạt **Hàn Triều Đóng Băng** (`Freeze` $0.75\text{s}$, chịu DR), xóa hết tầng và cấp **Kháng Triều** $3\text{s}$.

| # | Tên chiêu (`RegisterSkill`) | Vai & Tầm | Bậc VFX & Động học (Khởi–Phát–Thu) | Visual `SWE + SSF` | Mana / CD | Hiệu ứng chi tiết | $B$ | $V$ | Lệch | $Q$ | Radar C/T/K/Tr |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | **Phá Lãng Quyền** (`TTQP_PHA_LANG_QUYEN`) | Đả – Thăm dò, trung | **Bậc 1 (1đ)** · Thu quyền ngang hông $\to$ đấm thẳng kéo vệt sóng mảnh $\to$ thu tay nhanh | `TAI_CHI_WHIP` + SSF splash nhỏ | $14$ / $1.4\text{s}$ | Đòn sóng bay $16\text{ m/s}$ (Đấu Pháp được): $18.5$ ST, **Làm chậm** $20\%$ ($1.0\text{s}$), $+1$ Triều Ấn | $18.2$ | $18.0$ | $-1.1\%$ | $1.00$ | 8/0/2/0 |
| 2 | **Nghịch Lưu Cầm Nã** (`TTQP_NGHICH_LUU_CAM_NA`) | Đả – Mở giao tranh, trung | **Bậc 2 (2đ)** · Vươn trảo dẫn dòng $\to$ phóng xúc tu nước bám mục tiêu $\to$ giật chỏ kéo người lùi | `OCTOPUS_FORM` + SSF giật ngược | $20$ / $5.5\text{s}$ | Xúc tu phóng $18\text{ m/s}$ (Đấu Pháp được): $18$ ST, **Giật lại gần** $4.0\text{m}$, **Trói chân** $0.8\text{s}$, $+1$ Triều Ấn | $38.3$ | $37.7$ | $-1.6\%$ | $1.40$ | 4/0/6/0 |
| 3 | **Băng Triều Đảo Hải** (`TTQP_BANG_TRIEU_DAO_HAI`) | Đả – Diện rộng, trung | **Bậc 2 (2đ)** · Hạ tấn xoay hông $\to$ quét song chưởng đẩy mặt sóng thấp $\to$ dừng thế vững | `CURLING_WAVE` + 2 đợt SSF | $24$ / $7.5\text{s}$ | Mặt sóng tiến $0 \to 6\text{m}$ (nón $70^{\circ}$): $16$ ST, **Đẩy văng** $5.5\text{ m/s}$, **Làm chậm** $30\%$ ($1.5\text{s}$), **Phá đạn địch**, $+1$ Triều Ấn | $48.8$ | $50.1$ | $+2.6\%$ | $1.86$ | 4/1/5/0 |
| 4 | **Hàn Uyên Quyền Toàn** (`TTQP_HAN_UYEN_QUYEN_TOAN`) | Đả – Kiểm soát vùng, trung | **Bậc 3 (3đ)** · Dậm chân chấn khí tạo nước sủi bọt $0.70\text{s}$ $\to$ xoay tay cuộn cột băng (peak $\le 0.4\text{s}$) $\to$ hạ tay | Vòng sủi bọt $0.70\text{s}$ + `WATER_SPOUT` (`cryo=1`) + cột SSF | $26$ / $8.0\text{s}$ | **Báo trước $0.70\text{s}$** (mặt nước sủi bọt), vùng $r = 3.5\text{m}$: $27.5$ ST, **Hút gom** $3.0\text{m}$, **Đóng băng** $0.85\text{s}$, $+2$ Triều Ấn | $55.0$ | $53.2$ | $-3.3\%$ | $2.40$ | 5/0/5/0 |
| 5 | **Nộ Hải Long Quyền** (`TTQP_NO_HAI_LONG_QUYEN`) | Đả – Cận chiến kết liễu | **Bậc 3 (3đ)** · Đạp nước lấy đà $\to$ lướt xoắn tung song quyền tạo rồng sóng va chạm $\to$ trượt quán tính | `TWIN_TIDAL` + SSF va chạm | $24$ / $7.0\text{s}$ | Lướt cận chiến, vùng $r = 1.6\text{m}$: $38$ ST, **Choáng** $0.6\text{s}$, **Đẩy văng** $3.5\text{ m/s}$. **Tiêu thụ Triều Ấn:** $+12\%$ ST/tầng | $56.7$ | $57.4$ | $+1.2\%$ | $1.40$ | 7/0/3/0 |
| 6 | **Thủy Kính Quyền Thuẫn** (`TTQP_THUY_KINH_QUYEN_THUAN`) | Trợ – Phòng thủ | **Bậc 1 (1đ)** · Vẽ vòng Thái Cực bằng cổ tay $\to$ lập màng nước mỏng trong suốt (chỉ lóe khi chặn đạn) | `ORBITAL_CHAKRAM` viền mảnh + SSF chặn đạn | $26$ / $12.0\text{s}$ | **Khiên** $34\text{ HP}$ trong $4\text{s}$ (đồng minh trong $5\text{m}$ nhận $50\%$), **Giải CC/trạng thái**, **Phá 2 đạn địch**, **Tăng tốc** $20\%$ ($3\text{s}$) | $62.0$ | $61.1$ | $-1.5\%$ | $1.47$ | 0/9/0/1 |
| 7 | **Triều Tịch Linh Tuyền** (`TTQP_TRIEU_TICH_LINH_TUYEN`) | Trợ – Duy trì vùng | **Bậc 1 (1đ — Lớp nền)** · Dậm chân mở gợn sóng sát đất ($y \le 0.25\text{m}$, $\alpha \le 0.30$) | `RADIAL_DELUGE` ground-decal mảnh + SSF gợn nhẹ | $26$ / $12.0\text{s}$ | Tỏa tại chân $r = 4.0\text{m}$ ($5\text{s}$). Đồng minh: **Hồi $6\text{ HP/s}$**, **$+8\text{ Mana/s}$**, giảm CD (tổng $2\text{s}$). Địch: **Làm chậm** $30\%$ $+$ $1$ Triều Ấn / $2.5\text{s}$ | $65.1$ | $65.4$ | $+0.4\%$ | $2.08$ | 0/4/2/4 |
| | **Tổng kết cả bộ** | **5 Đả + 2 Trợ** | **VFX = $13 / 13$ (3 Bậc 1, 2 Bậc 2, 2 Bậc 3)** | **SWE + SSF** | | **Tất cả C1–C12 ĐẠT** | **$344.1$** | **$342.9$** | **$-0.4\%$** | **$1.66$** | **3.4 / 2.6 / 3.2 / 0.8** |

* **Kiểm toán ràng buộc C1–C12:**
  * **C1 (lệch từng chiêu):** từ $-3.3\%$ đến $+2.6\%$ ($\le 5\%$) — **ĐẠT**
  * **C2 (độc lập khi tháo lẻ):** mọi chiêu đều đạt $V_{\text{lẻ}} \ge 0.95 B$ (ngay cả *Nộ Hải Long Quyền* ở $0$ Triều Ấn đã đạt $57.4 \ge 0.95 \times 56.7$) — **ĐẠT**
  * **C3 (Burst 5 chiêu Đả):** sát thương kỳ vọng $88.7 \le 90.0$ — **ĐẠT**
  * **C4 (Hard CC):** Choáng $0.6\text{s} +$ Đóng băng $0.85\text{s} +$ Băng Triều Ấn $0.75\text{s} = 2.20\text{s} \le 2.50\text{s}$ — **ĐẠT**
  * **C6 (Tỷ trọng CC):** $32.4\% \le 45\%$ — **ĐẠT**
  * **C8 (Bản sắc):** Công $+$ Khống $= 6.6 \ge 6.0$, Trợ $= 0.8 \le 1.5$ — **ĐẠT**
  * **C9 (Telegraph):** *Hàn Uyên Quyền Toàn* báo trước $0.70\text{s} \ge 0.60\text{s}$ bằng mặt nước sủi bọt — **ĐẠT**
  * **C10–C12 (VFX & Võ thuật):** Tổng điểm VFX $= 13 \le 13$ ($3$ Bậc 1, $2$ Bậc 2, $2$ Bậc 3); *Triều Tịch Linh Tuyền* nằm sát mặt đất; 100% chiêu có động tác võ học & vật lý chất lỏng — **ĐẠT**

### Thưởng bộ Thủy Triều Quyền Pháp
* **Tiểu Thành ($\ge 4$ chiêu) — *Hàn Triều Dư Ba*:** Khi kích hoạt Đóng Băng từ đủ 3 Triều Ấn, hoàn **$10$ Mana** cho người ra chiêu và kéo dài thời gian tồn tại Triều Ấn từ $4\text{s} \to 6\text{s}$ (ước tính $+4.6\% \le 5\%$).
* **Đại Thành (đủ $7/7$ chiêu) — *Nộ Triều Quy Hải*:** Khi **Nộ Hải Long Quyền** tiêu thụ đủ **3 Triều Ấn**, hồi chiêu của nó giảm ngay **$50\%$** (ICD $10\text{s}$) và kích hoạt **Phá Băng** gây thêm **$10$ sát thương** (ước tính $+9.2\% \le 10\%$).

### Bốn lối lắp chiêu (Loadout 5 Đả + 2 Buff) tiêu biểu
1. **Nguyên bộ 7/7 — Thủy Triều Chính Tông (Đại Thành):** Đủ 7 chiêu bộ Thủy. Mạnh về kéo–đẩy–đóng băng liên hoàn, hồi *Nộ Hải Long Quyền* nhanh; yếu khi gặp hệ Thổ khắc chế và thiếu DoT chống hồi máu.
2. **Song hệ Tương Sinh 4 Thủy + 3 Mộc (Tiểu Thành + Ấn Tương Sinh):** 3 Đả + 1 Buff Thủy (*Nghịch Lưu*, *Hàn Uyên*, *Nộ Hải*, *Thủy Kính*) + 2 Đả + 1 Buff Mộc (gây Dính Độc). Kéo địch vào vùng Độc rồi đóng băng giữ chân, kích hoạt Triều Ấn $\to$ kéo dài Độc.
3. **Song hệ Nghịch Hệ 4 Thủy + 3 Hỏa (Tiểu Thành + Combo vật lý):** Dùng *Hàn Uyên* hút gom + đóng băng nhiều mục tiêu, sau đó dồn 2 chiêu nổ/Bốc Hỏa diện rộng; chọn *Thủy Kính* + 1 Buff tăng tốc/công của Hỏa.
4. **Đa hệ Tạp Phái 3 Kim + 2 Thủy + 2 Mộc (Bách Gia):** Nhặt *Nghịch Lưu Cầm Nã* (kéo) + *Thủy Kính* (giải CC/phá đạn) của Thủy, kết hợp chiêu đánh dấu Kim Ấn + 2 chiêu Đả Mộc. Không có thưởng bộ nhưng sở hữu chuỗi liên hoàn $\text{Kim} \to \text{Thủy} \to \text{Mộc}$ và không sợ bị bắt bài 1 hệ.

---

## 10. Giao Thức Sinh Bộ Võ Học Cho AI (AI Generation Protocol)

Khi được yêu cầu tạo một Bộ Võ Học mới, AI **bắt buộc thực hiện tuần tự 5 bước** dưới đây và xuất kết quả theo **Khuôn mẫu Chuẩn (§10.2)** để đảm bảo không sai số toán học, không vi phạm vật lý võ thuật và không gây rối mắt.

### 10.1 Quy trình 5 bước bắt buộc

1. **Bước 1 — Khóa Bản sắc & Nguyên lý Vật lý:**
   * Chọn **Hệ** (Kim / Mộc / Thủy / Hỏa / Thổ), **Vũ khí hoặc Quyền cước**, **Nguyên lý vật lý cốt lõi** (§3.2).
   * Chọn **2 trục Radar chủ đạo** (tổng $\ge 6.0$) và **1 trục yếu** ($\le 1.5$), xác định định hướng $Q$ mục tiêu (Đơn đấu / Đa dụng / Hội đồng).
   * Thiết kế **Ấn Ngũ Hành** (tính trước giá UL/tầng $=$ Giá kích hoạt $/$ Số tầng) và **1 tương tác Tương Sinh** nhận từ hệ trước + **1 tương tác** cấp cho hệ sau.
2. **Bước 2 — Quy hoạch Khung 7 Chiêu & Ngân sách VFX:**
   * Chia đúng **5 Đả + 2 Trợ** (**C7**).
   * Gán sẵn **Bậc VFX** cho cả 7 chiêu sao cho $\sum \text{VFX\_Tier} \le 13$, có $\ge 2$ chiêu Bậc 1 và $\le 2$ chiêu Bậc 3 (**C10**). Mọi chiêu duy trì $> 1.5\text{s}$ phải gán lớp mặt đất (**C11**).
3. **Bước 3 — Thiết kế Động học Võ thuật & Công bằng Đối kháng:**
   * Viết rõ 3 pha **Khởi – Phát – Thu** cho từng chiêu (**C12**).
   * Gán hình thức ra đòn hợp lệ (§2.1): Đạn/Sóng từ người, Tỏa từ chân, hoặc `VH_HIT_GROUND` có `castSecs` $\ge 0.60\text{s}$ kèm hiện tượng vật lý báo trước (**C9**).
4. **Bước 4 — Tính ngược từ Công Lực ($B$) sang Uy Lực ($V$):**
   * Với mỗi chiêu: chọn `Mana`, `CD`, `R_tầm`, `W_vận` $\to$ tính chính xác $B$.
   * Phân bổ sát thương, CC, DoT, Shield/Heal dựa đúng bảng giá §5.3 và hệ số $P_{\text{trúng}}, N$ ở §5.4 sao cho:
     * $\frac{|V - B|}{B} \le 5\%$ (**C1**),
     * $V_{\text{lẻ}} \ge 0.95 B$ khi không có Ấn hay thưởng bộ (**C2**),
     * Mỗi chiêu $\le 4$ loại hiệu ứng (**C6**).
   * Kiểm tra cộng dồn cả bộ: Burst 5 chiêu Đả $\le 90$ (**C3**), tổng Hard CC $\le 2.5\text{s}$ và mỗi lần $\le 1.0\text{s}$ (**C4**), tỷ trọng CC cả bộ $\le 45\%$ (**C6**).
5. **Bước 5 — Thiết kế Thưởng Bộ & Tự Kiểm Toán:**
   * Thiết kế **Tiểu Thành** ($\le +5\% B$) và **Đại Thành** ($\le +10\% B$) dưới dạng **cơ chế kích hoạt có điều kiện** (không cộng chỉ số phẳng).
   * Xuất bảng tự kiểm toán **C1–C12** kèm tối thiểu 3 ví dụ Loadout (1 Nguyên bộ, 1 Song hệ, 1 Tạp phái).

### 10.2 Khuôn mẫu Xuất Chuẩn cho AI (Output Template)

```markdown
# BỘ VÕ HỌC: [TÊN BỘ] (Hệ: [Kim/Mộc/Thủy/Hỏa/Thổ])

- **Thân pháp / Binh khí:** [VD: Bát Cực Quyền / Trọng Kiếm / Trượng Mộc...]
- **Nguyên lý Vật lý cốt lõi:** [Trích dẫn từ §3.2]
- **Định hướng chiến thuật:** Q mục tiêu = [x.xx] ([Đơn đấu/Đa dụng/Hội đồng]) | Radar: Công [x] / Thủ [x] / Khống [x] / Trợ [x]
- **Ấn Ngũ Hành ([Tên Ấn]):** Tối đa [k] tầng, tồn tại [t]s. Đủ [k] tầng -> [Hiệu ứng kích hoạt = X UL] -> Giá mỗi tầng = [X/k] UL.
- **Giao thức Tương Sinh:**
  - Nhận từ hệ [Hệ trước]: [Điều kiện -> thưởng <= 10% V chiêu kích hoạt].
  - Cấp cho hệ [Hệ sau]: [Khi chiêu hệ sau trúng mục tiêu có Tên Ấn -> hiệu ứng].

## Bảng 7 Chiêu Thức (5 Đả + 2 Trợ)
[Bảng đầy đủ các cột: # | Tên & ID | Vai & Tầm | Hình thức ra đòn (§2.1) | Bậc VFX & Lớp độ cao | Động học Khởi-Phát-Thu | Mana / CD / Cast | Hiệu ứng chi tiết | B | V_đơn | V_hội | V | Lệch% | Q | Radar]

## Chi tiết Tính Toán Kiểm Chứng (Bắt buộc ghi rõ để tránh sai số)
- Chiêu 1: B = (Mana + 3*CD) * R_tầm * W_vận = ... | V_đơn = ... | V_hội = ... | V = 0.5*(V_đơn + V_hội) = ... | V_lẻ = ...
- (Lặp lại đủ 7 chiêu)

## Thưởng Bộ (Cơ chế kích hoạt — Không cộng chỉ số phẳng)
- **Tiểu Thành (>= 4 chiêu) — [Tên]:** [Mô tả cơ chế + Ước tính giá trị <= 5% tổng B]
- **Đại Thành (7/7 chiêu) — [Tên]:** [Mô tả cơ chế + Ước tính giá trị <= 10% tổng B]

## Bảng Tự Kiểm Toán C1–C12
- C1 (|V-B|/B <= 5%): [Min% .. Max%] -> ĐẠT
- C2 (V_lẻ >= 0.95 B): [Min tỷ lệ] -> ĐẠT
- C3 (Burst 5 chiêu Đả <= 90): [Giá trị] -> ĐẠT
- C4 (Hard CC <= 1.0s/lần, tổng <= 2.5s): [Chi tiết] -> ĐẠT
- C5 (DR áp dụng toàn cục): ĐẠT
- C6 (<= 4 hiệu ứng/chiêu, tỷ trọng CC <= 45%): [Tỷ trọng%] -> ĐẠT
- C7 (5 Đả + 2 Trợ, 1 hệ): ĐẠT
- C8 (2 trục mạnh >= 6.0, >= 1 trục yếu <= 1.5): [Giá trị] -> ĐẠT
- C9 (Telegraph >= 0.60s cho VH_HIT_GROUND): [Chi tiết] -> ĐẠT
- C10 (Tổng VFX <= 13, >= 2 Bậc 1, <= 2 Bậc 3): [Tổng/phân bổ] -> ĐẠT
- C11 (Vùng duy trì > 1.5s ở lớp mặt đất): [Chi tiết] -> ĐẠT
- C12 (Đủ Khởi-Phát-Thu & Vật lý hệ): ĐẠT

## Ví dụ Phối Loadout (5 Đả + 2 Trợ)
1. **Nguyên bộ 7/7:** [Ưu / Nhược]
2. **Song hệ (4+3):** [Ưu / Nhược]
3. **Đa hệ Tạp phái (>= 3 bộ):** [Ưu / Nhược]
```