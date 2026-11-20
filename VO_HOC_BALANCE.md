# QUY ƯỚC & THUẬT TOÁN CÂN BẰNG VÕ HỌC (v2)

> **Vị trí trong repo:** `VO_HOC_BALANCE.md` (ngay thư mục gốc dự án).
> **Mục tiêu:** **mọi bộ võ học ngang sức nhau** (không có bộ "xịn hơn" hay "rác hơn"). Sự khác biệt nằm ở **bản sắc chiến đấu** (đánh đơn hay hội đồng, công/thủ/khống/trợ, hiệu ứng CC/trạng thái) và **cách tổ hợp** (nguyên bộ hay trộn bộ).
> *Ghi chú phạm vi:* Phiên bản này **bỏ qua trạng thái Thái Cực** để tập trung cân bằng triệt để giữa **Nguyên bộ** và **Trộn bộ** cùng hệ thống hiệu ứng.
> Tài liệu mang tính **quy phạm, có máy kiểm tra** (autotest `vo_hoc_budget` sẽ fail build nếu có chiêu hoặc bộ nào vi phạm).

---

## 1. Thuật ngữ

| Thuật ngữ | Ý nghĩa |
|---|---|
| **Bộ Võ Học** | 1 bộ gồm **7 chiêu cùng hệ**: **5 chiêu Đả** (tấn công / khống chế / gây trạng thái) + **2 chiêu Trợ** (khiên / hồi phục / buff / giải khống chế). |
| **Loadout** | Tối đa **7 ô chiêu** mang vào trận (`AGENT_SKILL_SLOTS = 7`), chia cứng thành **5 ô Đả (ô 1–5)** và **2 ô Trợ/Buff (ô 6–7)**. Người chơi chọn tự do từ các chiêu đã học, mang trọn 1 bộ hoặc trộn nhiều bộ. |
| **Công Lực (B)** | *Ngân sách* của một chiêu, tính từ cái giá người chơi phải trả: mana, hồi chiêu, rủi ro tầm đánh, thời gian vận chiêu. |
| **Uy Lực (V)** | *Tổng giá trị* chiêu đó mang lại (sát thương, hiệu ứng khống chế, trạng thái bất lợi, phòng thủ, hỗ trợ), quy về cùng đơn vị **UL**. |
| **Ấn Ngũ Hành** | Dấu cộng dồn đặc trưng của từng bộ (ví dụ **Triều Ấn** của Thủy Triều Quyền Pháp), làm cầu nối combo nội bộ và liên bộ theo vòng Tương Sinh. |
| **Tiểu Thành / Đại Thành** | Thưởng kích hoạt khi mang **≥ 4 chiêu** (Tiểu Thành) hoặc **đủ 7/7 chiêu** (Đại Thành) của cùng một bộ. |
| **Chỉ số Q** | `V_hội-đồng / V_đơn-đấu`, đo mức độ thiên về đánh đơn hay đánh hội đồng. |
| **Radar 4 trục** | Công / Thủ / Khống / Trợ (tổng = 10 điểm), thể hiện bản sắc chiêu, bộ và loadout. |

---

## 2. Năm luật vàng

1. **Ngang sức theo ngân sách:** với mọi chiêu, `|V − B| / B ≤ 5%`. Chiêu nhiều hiệu ứng hoặc sát thương cao bắt buộc phải đắt (mana cao, hồi lâu, vận lâu hoặc khó trúng).
2. **Tự đứng vững khi dùng lẻ:** mỗi chiêu khi đứng một mình (không thưởng bộ, không dựa Ấn bộ khác) phải đạt `V_lẻ ≥ 0.95·B`. Chọn lẻ từng chiêu để lắp vào loadout trộn bộ vẫn phải phát huy tốt.
3. **Mạnh ở đâu thì yếu ở đó:** mỗi bộ có điểm mạnh rõ ràng (tổng 2 trục chủ đạo ≥ 6.0/10) và ít nhất 1 trục yếu (≤ 1.5/10). Không có bộ nào toàn diện mọi mặt.
4. **Hiệu ứng có giá đắt tương xứng:** mọi hiệu ứng (đẩy văng, giật lại gần, làm chậm, đóng băng, choáng, dính độc, bốc hỏa…) đều trừ trực tiếp vào ngân sách sát thương. Chiêu nhiều khống chế bắt buộc phải ít sát thương hơn chiêu thuần công.
5. **Nguyên bộ ≈ Trộn bộ:** không có lối tổ hợp nào luôn áp đảo lối còn lại (§6).

### 2.1 Luật Công Bằng Đối Kháng (Khả năng Né & Đỡ Đòn)

**Cấm tuyệt đối** thiết kế chiêu thức tự động trồi lên từ mặt đất hoặc rớt từ trên trời xuống tại vị trí mục tiêu ở xa mà đối thủ không có khả năng né hoặc đỡ đòn. Mọi chiêu thức bắt buộc thuộc một trong **3 hình thức ra đòn hợp lệ**:

1. **Đạn đạo / Sóng di chuyển từ người ra chiêu:** có đường bay hoặc mặt sóng tiến từ vị trí nhân vật tới mục tiêu (tích hợp `Combat_SubmitProjectile` nếu là đạn/xúc tu) → đối thủ có thể **lướt né ngang** hoặc **Đấu Pháp phá/đỡ đạn**.
2. **Tỏa ra / Trồi lên ngay tại vị trí nhân vật:** cận chiến, hộ thuẫn tự thân, vòng sóng 360° tỏa ra từ chân người ra chiêu → đối thủ đọc được khoảng cách để giữ cự ly.
3. **Trồi lên / Rớt xuống ở vị trí xa CÓ DẤU HIỆU BÁO TRƯỚC (`Telegraph ≥ 0.60s`):** bắt buộc hiển thị dấu hiệu rõ ràng trên mặt đất (vòng cảnh báo, mặt nước sủi bọt, vệt nứt…) trong tối thiểu **0.60 giây** trước khi gây sát thương hoặc khống chế để đối thủ kịp **Dash né** hoặc **bật Khiên đỡ** (kiểm tra tự động bởi ràng buộc **C9** trong `vo_hoc_budget`).

---

## 3. Thuật toán tính Công Lực (B) — cái giá phải trả

```
B = (Mana + λ · CD) · R_tầm · W_vận
```

| Hằng số | Giá trị | Lý do |
|---|---|---|
| `λ` (giá 1 giây hồi chiêu) | **3.0 UL/s** | Mana hồi thụ động 5/s và 34/s khi Thiền Định. 1 giây khóa chiêu tương đương ~3 mana hồi được trong giao tranh. |
| `R_tầm` | Cận chiến **1.20** · Tầm trung **1.00** · Tầm xa **0.90** · Tự thân **1.00** | Cận chiến chịu rủi ro áp sát nên được thưởng 20% ngân sách; tầm xa an toàn nên giảm 10%. |
| `W_vận` (cast time) | `< 0.30s`: **1.00** · `0.30–0.60s`: **1.05** · `> 0.60s`: **1.10** | Vận chiêu càng lâu càng dễ bị né hoặc bị ngắt. |

---

## 4. Thuật toán tính Uy Lực (V) — định giá Sát thương & Hiệu ứng

### 4.1 Công thức

```
V_kịch-bản = Σ_hiệu-ứng  UL_k · lượng_k · P_trúng · N_mục-tiêu · uptime
V          = 0.5 · V_đơn-đấu + 0.5 · V_hội-đồng
```

* Tính trên **2 kịch bản**: **Đơn đấu** (1 địch, 1 đồng minh là bản thân) và **Hội đồng** (2v2 / giao tranh quanh Boss).
* Hiệu ứng **lên địch** nhân `P_trúng · N_địch`. Hiệu ứng **lên đồng minh** nhân `N_đồng-minh`. Hiệu ứng **một lần** (giải khống chế, phá đạn) tính phẳng 1 lần.

### 4.2 Bảng định giá đầy đủ Hiệu ứng, Khống chế & Trạng thái (UL)

#### A. Sát thương trực tiếp & Trạng thái gây sát thương theo thời gian (DoT)

| Hiệu ứng | Đơn vị | Giá UL | Trục | Cơ chế & Ghi chú cân bằng |
|---|---|---|---|---|
| **Sát thương tức thì** | 1 HP | **1.00** | Công | Chuẩn gốc (1 UL = 1 HP sát thương tức thì). |
| **Bốc hỏa (Burn)** | 1 HP DoT | **0.85** | Công | Gây sát thương theo giây (tổng 3–4s), cộng dồn tối đa 3 tầng. Rẻ hơn sát thương tức thì 15% vì có thể bị hồi máu / giải kịp. |
| **Dính độc (Poison)** | 1 HP DoT + 1s giảm hồi máu | **0.85 / HP** + **4.0 / s** | Công + Khống | Vừa rút máu theo thời gian vừa **giảm 40% lượng máu hồi phục** (khắc chế bộ hồi máu). |
| **Chảy máu / Nội thương (Bleed)** | 1 HP DoT | **0.80** (0.95 nếu phạt di chuyển) | Công | Gây sát thương theo thời gian; có thể tăng sát thương khi mục tiêu di chuyển/lướt. |

#### B. Hiệu ứng Khống chế (CC — chia 3 bậc)

| Bậc | Hiệu ứng | Đơn vị | Giá UL | Trục | Cơ chế & Luật áp dụng |
|---|---|---|---|---|---|
| **Cứng (Hard CC)** | **Đóng băng (Freeze)** | 1 giây | **14.0** | Khống | Bất động hoàn toàn + không tung chiêu + mở điều kiện kích hoạt **Phá Băng**. **Chịu DR.** |
| **Cứng (Hard CC)** | **Hất tung (Launch / Knock-up)** | 1 giây trên không | **13.0** | Khống | Bay lên không trung, không thể di chuyển/tung chiêu, cắt vận công. **Chịu DR.** |
| **Cứng (Hard CC)** | **Choáng (Stun)** | 1 giây | **12.0** | Khống | Bất động + không tung chiêu, ngắt Thiền Định. **Chịu DR.** |
| **Vị trí (Displacement)** | **Giật lại gần / Hút gom (Pull)** | 1 mét kéo | **3.5** | Khống | Kéo mục tiêu về phía mình hoặc tâm xoáy. Rất mạnh để kéo địch vào tầm quyền hoặc tách địch khỏi Boss. |
| **Vị trí (Displacement)** | **Đẩy văng ra (Knockback)** | 1 m/s xung lực | **2.5** | Khống | Đẩy lùi mạnh; có giá trị đặc biệt ở bản đồ có mép vực (Ring-out) hoặc đẩy địch khỏi Trụ/Boss. |
| **Mềm (Soft CC)** | **Câm lặng (Silence)** | 1 giây | **9.0** | Khống | Vẫn đi được nhưng không thể tung chiêu (vẫn đánh thường được). |
| **Mềm (Soft CC)** | **Trói chân (Root)** | 1 giây | **8.0** | Khống | Không thể di chuyển/lướt, vẫn có thể xoay người tung chiêu. |
| **Mềm (Soft CC)** | **Làm chậm (Slow)** | `1s × %chậm` | **10.0** | Khống | Ví dụ: chậm 30% trong 1.5s = `0.30 × 1.5 × 10 = 4.5 UL`. |

#### C. Trạng thái Bất lợi (Debuff) & Ấn Ngũ Hành

| Hiệu ứng | Đơn vị | Giá UL | Trục | Cơ chế |
|---|---|---|---|---|
| **Phá giáp / Dễ tổn thương (Vulnerable)** | `1s × %nhận thêm` | **12.0** | Trợ | Mục tiêu chịu thêm X% sát thương từ mọi nguồn (rất mạnh trong 2v2 / săn Boss). |
| **Suy yếu (Weaken)** | `1s × %giảm công` | **10.0** | Thủ | Giảm X% sát thương mục tiêu gây ra. |
| **Mù / Nhiễu (Blind)** | 1 giây | **7.0** | Thủ | Đòn đánh thường bị trượt / giảm mạnh độ chính xác tự ngắm. |
| **Ấn Ngũ Hành (Mark)** | 1 tầng | `Giá_kích_hoạt / Số_tầng` | Khống | Ví dụ **Triều Ấn**: đủ 3 tầng → Đóng băng 0.75s (`0.75 × 14 = 10.5 UL`) → mỗi tầng **3.5 UL**. |

#### D. Phòng thủ & Hỗ trợ (Buff / Support)

| Hiệu ứng | Đơn vị | Giá UL | Trục | Cơ chế |
|---|---|---|---|---|
| **Khiên hấp thụ (Shield)** | 1 HP khiên | **0.80** | Thủ | Trừ sát thương vào khiên trước HP; có thời hạn nên rẻ hơn hồi máu 20%. |
| **Hồi máu (Heal)** | 1 HP hồi | **1.00** | Thủ | Hồi HP trực tiếp (không vượt quá `maxHealth`, bị giảm bởi Dính độc). |
| **Phá / Chặn đạn địch (Deflect/Block)** | 1 viên đạn | **8.0** | Thủ | Tiêu hủy đạn địch qua `Combat_DeflectProjectilesInRadius`. |
| **Giải khống chế / Trạng thái (Cleanse)** | 1 lần giải | **10.0** | Thủ | Xóa ngay Choáng, Đóng băng, Trói, Hút, Làm chậm, Độc, Bốc hỏa, Chảy máu. |
| **Hồi Mana** | 1 Mana | **0.50** | Trợ | Bù năng lượng cho bản thân hoặc đồng đội. |
| **Tăng tốc chạy (Haste)** | `1s × %tăng` | **5.0** | Trợ | Ví dụ: +20% trong 3s = `0.20 × 3 × 5 = 3.0 UL`. |
| **Rút ngắn hồi chiêu (CDR)** | 1 giây giảm | **3.0 (= λ)** | Trợ | Rút ngắn thời gian chờ các chiêu khác đang hồi. |

### 4.3 Bảng xác suất trúng (`P_trúng`) & số mục tiêu kỳ vọng (`N`)

| Kiểu trúng | `P_trúng` | | Hình dạng | `N` Đơn đấu | `N` Hội đồng |
|---|---|---|---|---|---|
| Cận chiến | 0.85 | | Đơn mục tiêu | 1.0 | 1.0 |
| Đạn nhanh (≥ 14 m/s) | 0.75 | | Vùng nhỏ (r ≤ 1.6m) | 1.0 | 1.4 |
| Đạn chậm | 0.65 | | Đường thẳng xuyên | 1.0 | 1.8 |
| Sóng quét (SWE Wave) | 0.80 | | Hình nón | 1.0 | 2.0 |
| Mặt đất có trễ | 0.55 | | Vùng vừa (r ≤ 3.0m) | 1.0 | 2.0 |
| Vùng duy trì | `1.0 × uptime` | | Vùng lớn (r > 3.0m) | 1.0 | 2.4 |

### 4.4 Tám ràng buộc bắt buộc (Autotest `vo_hoc_budget` kiểm tra)

| # | Ràng buộc | Ngưỡng | Ý nghĩa |
|---|---|---|---|
| **C1** | Lệch ngân sách từng chiêu | `|V − B| / B ≤ 5%` | Không chiêu nào quá mạnh hoặc quá yếu. |
| **C2** | Tự đứng vững khi dùng lẻ | `V_lẻ ≥ 0.95 · B` | Chiêu lẻ luôn hữu dụng khi lắp trộn bộ. |
| **C3** | Giới hạn dồn sát thương (Burst) | Tổng sát thương kỳ vọng 5 chiêu Đả `≤ 0.9 · HP_PvP (= 90)` | Không bộ nào giết được đối thủ đầy máu trong 1 vòng chiêu. |
| **C4** | Giới hạn khống chế cứng | Mỗi lần `≤ 1.0s`; tổng cả bộ `≤ 2.5s` trong chu kỳ 10s | Tránh "khóa chết" đối thủ không kịp phản ứng. |
| **C5** | Giảm dần hiệu lực CC (DR) | Cùng mục tiêu trong cửa sổ **4s**: lần 2 `×0.5`, lần 3 `×0.25`, lần 4 **miễn nhiễm** | Áp dụng toàn cục cho Choáng, Đóng băng, Hất tung. |
| **C6** | Chống lạm phát hiệu ứng | Mỗi chiêu tối đa **4 loại hiệu ứng**; tổng tỷ trọng CC cả bộ `≤ 45%` | Giữ chiêu gọn gàng, dễ đọc, có điểm yếu rõ ràng. |
| **C7** | Cấu trúc bộ | Đúng **5 Đả + 2 Trợ**, cùng 1 hệ | Chuẩn hóa mọi bộ võ học. |
| **C8** | Bản sắc bộ | Tổng 2 trục radar chủ đạo `≥ 6.0/10` và ít nhất 1 trục yếu `≤ 1.5/10` | Mọi bộ đều có thế mạnh và điểm yếu riêng. |
| **C9** | Công bằng đối kháng (Telegraph) | Mọi chiêu `VH_HIT_GROUND` ở xa bắt buộc `castSecs ≥ 0.60s` + có dấu hiệu mặt đất | Đảm bảo đối thủ luôn có thời gian né hoặc đỡ đòn (§2.1). |

---

## 5. Phân loại định hướng chiến thuật (Q & Radar)

| Chỉ số Q (`V_hội-đồng / V_đơn-đấu`) | Định hướng | Vai trò tiêu biểu |
|---|---|---|
| `< 1.25` | **Đánh đơn (Duellist / Assassin)** | Truy kích 1v1, ám sát, ép góc mục tiêu lẻ |
| `1.25 – 1.70` | **Đa dụng (Skirmisher)** | Vừa tay đôi tốt vừa hỗ trợ giao tranh nhỏ |
| `> 1.70` | **Đánh hội đồng (Teamfight / Control)** | Giao tranh 2v2, bảo vệ/công phá Boss, dọn lính |

Radar của **loadout 7 chiêu (5 Đả + 2 Trợ)** được tính trực tiếp từ trung bình có trọng số của 7 chiêu đang chọn và hiển thị ngay trong Tàng Kinh Các.

---

## 6. Hai lối tổ hợp cân bằng nhau (Nguyên bộ vs Trộn bộ)

*(Bỏ qua trạng thái Thái Cực trong giai đoạn hiện tại.)*

### 6.1 Lối Nguyên bộ (Tiểu Thành 4 chiêu / Đại Thành đủ 7 chiêu)
* **Tiểu Thành (≥ 4 chiêu cùng bộ):** thưởng giá trị `≤ +5%` tổng Công Lực của loadout.
* **Đại Thành (đủ 7/7 chiêu cùng bộ):** thưởng giá trị `≤ +10%`.
* Thưởng bộ phải là **cơ chế kích hoạt có điều kiện** (hoàn mana khi đóng băng, giảm/reset hồi chiêu khi tiêu thụ đủ Ấn…), **không cộng chỉ số phẳng** kiểu `+x% sát thương`.
* **Cái giá đánh đổi:** mang đủ 7 chiêu cùng hệ khiến Vô Hệ bị khóa cứng vào 1 hệ → rất dễ bị đối thủ mang hệ khắc chế bắt bài trong Đấu Pháp 5×5; đồng thời thiếu các hiệu ứng đặc sản của hệ khác (ví dụ bộ Thủy không có Bốc hỏa hay Dính độc giảm hồi máu).

### 6.2 Lối Trộn bộ (Đa dạng hiệu ứng & Giao thức Ấn Tương Sinh)
* Người chơi tự do ghép 2–3 bộ vào khung **5 Đả + 2 Buff** (ví dụ: 4 chiêu Thủy lấy Tiểu Thành + 3 chiêu Mộc lấy Dính Độc chống hồi máu; hoặc 3 Đả Thủy + 2 Đả Hỏa + 2 Buff từ bộ khác).
* **Ấn Tương Sinh:** vòng sinh `Thủy → Mộc → Hỏa → Thổ → Kim → Thủy`. Chiêu của hệ sau trúng mục tiêu đang mang Ấn của hệ sinh ra mình sẽ nhận thưởng tương tác (ví dụ: chiêu Thủy trúng mục tiêu có Kim Ấn → tặng thêm `+1 Triều Ấn`; chiêu Mộc trúng mục tiêu có Triều Ấn → kéo dài Độc). Giá trị thưởng liên bộ bị chặn ở mức `≤ 10% V` của chiêu tiêu thụ.
* **Kết quả:** **Đại Thành 7/7 (+10%, 1 hệ)** ≈ **Tiểu Thành 4 chiêu (+5%) + Chuỗi Ấn Tương Sinh & đa dạng khắc chế/hiệu ứng**.

---

## 7. Kinh tế sở hữu (Tích lũy ở cả MOBA & Thế Giới Mở)

| Nguồn hoạt động | Ngân Lượng | Điểm Dã Tẩu | Linh Thạch hệ X | Tàn Quyển bộ X |
|---|---|---|---|---|
| Đánh quái / Boss Thế Giới Mở & Khám Phá | ✔ | – | ✔ theo hệ bản đồ | Boss bản đồ hệ X: 15% rơi |
| Nhiệm vụ Dã Tẩu | ✔ | ✔ | ✔ | Mốc chuỗi 10/20/30…: 1 quyển |
| **Đấu Trường PvP / MOBA (MỚI)** | Thua **120** / Thắng **180** | **20** / **30** | **2 viên** theo hệ Vô Hệ vừa dùng | – |
| Chống cày quá đà PvP | 10 trận đầu/ngày nhận 100%, từ trận 11 nhận 25% | | | |

**Giá học chiêu đồng giá** (mọi chiêu ngang sức):
* Học 1 chiêu bất kỳ: **300 Ngân Lượng + 6 Linh Thạch cùng hệ**, *hoặc* **1 Tàn Quyển của bộ đó**.
* Chiêu đầu tiên của mỗi bộ được **giảm 50% giá** để khuyến khích người chơi thử nhiều bộ.
* **PvE vs PvP:** PvP luôn khóa Lv.1 và tắt chỉ số đồ. PvE áp dụng `PlayerProfile_GetSkillScaling` chỉ lên **sát thương / khiên / hồi máu** — **thời lượng khống chế (CC) không bao giờ tăng theo cấp**.

---

## 8. Bộ Võ Học Mẫu (Hệ Thủy): **THỦY TRIỀU QUYỀN PHÁP** (7 chiêu mới 100%, nhánh `LiquidSWE` + `LiquidSSF`)

> **Phong cách:** Võ học cận–trung chiến kết hợp điều khiển sóng thủy triều (*Hydro-Martial Arts*), kết hợp cả nhánh **Shallow Water Equations (`core/liquid/liquid_swe.h`)** cho mào sóng/vòi rồng 3D và nhánh **Screen Space Fluid (`core/liquid/liquid_impact.h`)** cho vương miện nước văng/vũng nước ướt (không dùng Metaball, không tái sử dụng 2 chiêu cũ).
> **Bản sắc:** Mạnh về **Công + Khống chế vị trí & Đóng băng** (kéo địch vào, đẩy văng, hút xoáy đóng băng, đấm choáng kết liễu); **Đa dụng nghiêng Hội đồng (`Q = 1.66`)**; yếu ở **Trợ thuần (0.8/10)** và **tầm đánh xa**.

**Cơ chế đặc trưng — Triều Ấn:** tối đa 3 tầng, tồn tại 4s. Đủ 3 tầng kích hoạt **Hàn Triều Đóng Băng** (`Freeze 0.75s`, chịu DR), xóa hết tầng và cấp **Kháng Triều** 3s.

| # | Tên chiêu (`RegisterSkill`) | Vai & Tầm | Visual `SWE + SSF` | Mana | CD | Hiệu ứng chi tiết | B | V | Lệch | Q | Radar C/T/K/Tr |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | **Phá Lãng Quyền** (`TTQP_PHA_LANG_QUYEN`) | Đả – Thăm dò, trung | `TAI_CHI_WHIP` + SSF splash | 14 | 1.4s | Đòn sóng bay 16 m/s (Đấu Pháp được): 18.5 sát thương, **Làm chậm** 20% (1.0s), +1 Triều Ấn | 18.2 | 18.0 | −1.1% | 1.00 | 8/0/2/0 |
| 2 | **Nghịch Lưu Cầm Nã** (`TTQP_NGHICH_LUU_CAM_NA`) | Đả – Mở giao tranh, trung | `OCTOPUS_FORM` + SSF giật ngược | 20 | 5.5s | Xúc tu phóng 18 m/s (Đấu Pháp được): 18 sát thương, **Giật lại gần** 4.0m, **Trói chân** 0.8s, +1 Triều Ấn | 38.3 | 37.7 | −1.6% | 1.40 | 4/0/6/0 |
| 3 | **Băng Triều Đảo Hải** (`TTQP_BANG_TRIEU_DAO_HAI`) | Đả – Diện rộng, trung | `CURLING_WAVE` + 2 đợt SSF | 24 | 7.5s | Mặt sóng tiến 0→6m (nón 70°): 16 sát thương, **Đẩy văng** 5.5 m/s, **Làm chậm** 30% (1.5s), **Phá đạn địch**, +1 Triều Ấn | 48.8 | 50.1 | +2.6% | 1.86 | 4/1/5/0 |
| 4 | **Hàn Uyên Quyền Toàn** (`TTQP_HAN_UYEN_QUYEN_TOAN`) | Đả – Kiểm soát vùng, trung | Vòng báo trước `0.70s` + `WATER_SPOUT` (`cryo=1`) + cột SSF | 26 | 8.0s | **Báo trước 0.70s** (vòng cảnh báo + nước sủi bọt), vùng r=3.5m: 27.5 sát thương, **Hút gom** 3.0m, **Đóng băng** 0.85s, +2 Triều Ấn | 55.0 | 53.2 | −3.3% | 2.40 | 5/0/5/0 |
| 5 | **Nộ Hải Long Quyền** (`TTQP_NO_HAI_LONG_QUYEN`) | Đả – Cận chiến kết liễu | `TWIN_TIDAL` + SSF va chạm | 24 | 7.0s | Lướt cận chiến, vùng r=1.6m: 38 sát thương, **Choáng** 0.6s, **Đẩy văng** 3.5 m/s. **Tiêu thụ Triều Ấn:** +12% sát thương/tầng | 56.7 | 57.4 | +1.2% | 1.40 | 7/0/3/0 |
| 6 | **Thủy Kính Quyền Thuẫn** (`TTQP_THUY_KINH_QUYEN_THUAN`) | Trợ – Phòng thủ | `ORBITAL_CHAKRAM` + Khiên 3D + SSF chặn đạn | 26 | 12.0s | **Khiên** 34 HP trong 4s (đồng minh trong 5m nhận 50%), **Giải khống chế/trạng thái**, **Phá 2 đạn địch**, **Tăng tốc** 20% (3s) | 62.0 | 61.1 | −1.5% | 1.47 | 0/9/0/1 |
| 7 | **Triều Tịch Linh Tuyền** (`TTQP_TRIEU_TICH_LINH_TUYEN`) | Trợ – Duy trì vùng | `RADIAL_DELUGE` + Vòng mặt đất + SSF | 26 | 12.0s | Tỏa tại chân nhân vật r=4.0m (5s). Đồng minh: **Hồi 6 HP/s**, **+8 Mana/s**, rút ngắn hồi chiêu (tổng 2s). Địch: **Làm chậm** 30% + 1 Triều Ấn / 2.5s | 65.1 | 65.4 | +0.4% | 2.08 | 0/4/2/4 |
| | **Tổng kết cả bộ** | **5 Đả + 2 Trợ** | **SWE + SSF** | | | **Tất cả C1–C9 ĐẠT** | **344.1** | **342.9** | **−0.4%** | **1.66** | **3.4 / 2.6 / 3.2 / 0.8** |

* **Kiểm tra ràng buộc:**
  * C1 (lệch từng chiêu): từ `-4.2%` đến `+2.6%` (`≤ 5%`) ✔
  * C3 (Burst 5 chiêu Đả): sát thương kỳ vọng `88.7 ≤ 90.0` ✔
  * C4 (Hard CC): Choáng 0.6s + Đóng băng 0.85s + Băng Triều Ấn 0.75s = `2.20s ≤ 2.50s` ✔
  * C6 (Tỷ trọng CC): `32.4% ≤ 45%` ✔
  * C8 (Bản sắc): Công + Khống = `6.6 ≥ 6.0`, Trợ = `0.8 ≤ 1.5` ✔

### Thưởng bộ Thủy Triều Quyền Pháp
* **Tiểu Thành (≥ 4 chiêu) — *Hàn Triều Dư Ba*:** Khi kích hoạt Đóng Băng từ đủ 3 Triều Ấn, hoàn **10 Mana** cho người ra chiêu và kéo dài thời gian tồn tại Triều Ấn từ **4s → 6s** (ước tính `+4.6% ≤ 5%`).
* **Đại Thành (đủ 7/7 chiêu) — *Nộ Triều Quy Hải*:** Khi **Nộ Hải Long Quyền** tiêu thụ đủ **3 Triều Ấn**, hồi chiêu của nó giảm ngay **50%** (ICD 10s) và kích hoạt **Phá Băng** gây thêm **10 sát thương** (ước tính `+9.2% ≤ 10%`).

### Một số lối lắp chiêu (Loadout 5 Đả + 2 Buff) tiêu biểu
1. **Thủy Triều Chính Tông (Nguyên bộ 7/7 — Đại Thành):** Đủ cả 5 chiêu Đả + 2 chiêu Buff *Thủy Triều Quyền Pháp*. Mạnh toàn diện về kéo–đẩy–đóng băng–hộ thể, hồi chiêu *Nộ Hải Long Quyền* nhanh trong giao tranh; điểm yếu là thuần hệ Thủy (bị hệ Thổ khắc) và thiếu DoT chống hồi máu.
2. **Thủy Mộc Tương Sinh (Trộn bộ 4 Thủy + 3 Mộc — Tiểu Thành):** 3 Đả + 1 Buff Thủy (*Nghịch Lưu*, *Hàn Uyên*, *Nộ Hải*, *Thủy Kính*) + 2 Đả + 1 Buff Mộc (gây Dính Độc giảm hồi máu). Kéo địch vào vùng Độc rồi đóng băng giữ chân, vừa có Tiểu Thành vừa tránh bị khắc 1 hệ.
3. **Thủy Hỏa Song Hành (Trộn bộ 4 Thủy + 3 Hỏa — Tiểu Thành):** Dùng *Hàn Uyên* hút gom + đóng băng nhiều mục tiêu, sau đó dồn 2 chiêu Bốc Hỏa diện rộng; dùng 2 Buff tùy chọn (Thủy Kính + Buff công/tốc của Hỏa).
