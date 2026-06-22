# THIẾT KẾ TỔNG THỂ DỰ ÁN: NGŨ HÀNH TỶ VÕ
### Phiên bản cải tiến 3.5 — Lõi 3D Isometric, Đêm Trăng Giải Đố Ngũ Hành, Vô Hệ & Cảnh Giới Thái Cực (Tẩy bỏ tương tác địa hình động)

---

# PHẦN A — THIẾT KẾ CỐT LÕI

## I. TẦM NHÌN DỰ ÁN

**Ngũ Hành Tỷ Võ** là một game hành động đấu trường Lõi 3D khóa góc nhìn (Isometric / Fixed Perspective)[cite: 1], lấy cảm hứng từ văn hóa phương Đông và triết lý Ngũ Hành biện chứng. Toàn bộ các trận chiến **luôn luôn diễn ra vào ban đêm dưới ánh trăng sao ma mị**, tận dụng bóng đêm và sương mù che giấu sự thiếu chi tiết của mô hình Low-Poly[cite: 1], tối ưu hóa hiệu năng cho Raylib trên Android mà vẫn đạt hiệu ứng nghệ thuật cao[cite: 1].

Trận đấu là cuộc đối kháng Action RTS / MOBA mini **4vs4 dồn dập** trong các đấu trường đài đá lơ lửng[cite: 1]. Người chơi tự do phối hợp pháp thuật Kim - Mộc - Thủy - Hỏa - Thổ để chiến đấu, nơi kỹ năng tương tác trực tiếp tạo ra các phản ứng ngũ hành thời gian thực mà không cần bất kỳ UI hay văn bản hướng dẫn nào[cite: 1].

Mục tiêu thiết kế:
* Dễ tiếp cận nhưng có chiều sâu chiến thuật giải đố cực cao.
* Xả chiêu tốc độ cao (Fast Cooldown) kết hợp quản lý Linh Khí khắc nghiệt.
* Hiệu ứng biến hóa bằng Custom Shader (Fog, Lighting) kết hợp Hệ thống hạt tự do (Free-moving Particles)[cite: 1].
* Chạy mượt trên Android với tài nguyên Low-Poly tối giản và mảng tĩnh[cite: 1].
* Chi phí vận hành gần như bằng 0[cite: 1].

---

## II. TUYÊN NGÔN THIẾT KẾ

> Một đấu trường đêm 3D có chiều sâu, nơi người chơi sử dụng sự ngộ tính để triệt tiêu pháp thuật đối phương, nương tựa vào các Vùng Địa Hình Tĩnh bám sàn và điều khiển không gian thông qua các đại Trận Pháp để đột phá Cảnh Giới Thái Cực.

Khác với các game kỹ năng thông thường:
* **Hệ thống "Vô Hệ" (Dynamic Element):** Người chơi không chọn hệ cố định từ đầu. Hệ của nhân vật tự động đồng hóa và biến đổi linh hoạt dựa trên tổ hợp 4 chiêu thức họ trang bị hoặc bị đồng hóa theo vùng địa hình đứng dưới đêm.
* **Địa hình Tĩnh tinh giảm:** Để chống vỡ scope dự án solo, **bỏ hoàn toàn ý tưởng chiêu thức phá hủy hay làm biến đổi trạng thái địa hình** (Không có cháy rừng, không có rút cạn sông, không có cát nung thành thủy tinh). Địa hình hoàn toàn tĩnh, đóng vai trò là các **Vùng dữ liệu Nguyên tố ảo (Virtual Triggers)** bám sàn cố định[cite: 1] để cộng/trừ chỉ số (Modifiers) thuộc tính nhân vật khi đứng vào.
* **Quy luật tự khám phá (No Tutorial):** Mọi bí ẩn tương sinh tương khắc đều ẩn giấu dưới dạng quy luật tự nhiên, người chơi phải tự "Ngộ" (Aha! Moment).

### Mục tiêu cốt lõi

Người chơi phải luôn cảm thấy:
1. **Mình đang điều khiển một đại tông sư thông tuệ** — thông qua việc tự nhìn ra quy luật môi trường ban đêm, vận công thiền định phục hồi trong bóng tối.
2. **Mình đang làm chủ không gian và nhịp độ** — phối hợp xả chiêu liên miên bất tuyệt (Fast Cooldown) nhưng phải liên tục tính toán lượng Linh Khí (Mana) cạn kiệt và tận dụng trục $Z$ để đẩy lùi thực thể[cite: 1].
3. **Mình đang đấu pháp thuật chiến thuật** — điều phối quân lính tinh linh nhỏ[cite: 1], bảo vệ Boss phe mình, xả chiêu Đấu Pháp đối đòn[cite: 1] và dồn khống chế hất văng Boss địch xuống vực sâu[cite: 1].

---

## III. TRỤ CỘT GAMEPLAY (CẬP NHẬT)

### Trụ cột 1 — Khinh Công, Định Thân Vận Công & Hệ Vật Lý Trục $Z$ Real-time
* **Khinh công kết hợp Trục $Z$:** Người chơi có thể lướt, nhảy cao, lẩn trốn vào bóng đêm né tránh[cite: 1]. Mỗi hệ để lại dư ảnh nguyên tố phát sáng nhẹ (Tàn lửa tím, làn sương, hoa dạ quang)[cite: 1].
* **Cơ chế Rớt Vực (Ring Out):** Đấu trường là đài đá lơ lửng có bán kính cố định (`STAGE_RADIUS`)[cite: 1]. Khi bị trúng chiêu hất văng (Knockback Force) lọt ra ngoài rìa, thực thể sẽ bị trọng lực kéo tụt cao độ $Z$ xuống âm (`velocity.z -= GRAVITY * dt`) và chết hoàn toàn[cite: 1]. Mô hình/Billboard thu nhỏ dần đều trực quan từ góc nhìn camera trên xuống[cite: 1].
* **Thiền Định Vận Công (Meditation):** Xả chiêu nhanh (Fast Cooldown) làm cạn Mana rất nhanh. Khi hết Mana, người chơi phải tìm góc tối, bấm nút Vận Công (bất động 3 giây) để nạp lại linh khí.

### Trụ cột 2 — Đấu Pháp Tối Cao & Địa Hình Tĩnh (Map Elements)
* **Đấu Pháp Projectile:** Hệ thống va chạm đạn tầm xa hằng số thông qua ma trận dữ liệu **Clash Matrix (5x5)** thuần túy phía CPU[cite: 1]. 
* **Khai thác Địa hình Tĩnh:** Để tối ưu hiệu năng và giữ an toàn cho scope, địa hình hoàn toàn **bất biến**, chỉ sử dụng hàm check va chạm khối cầu bám sàn `CheckCollisionSpheres()` để áp các hệ số Multiplier ẩn lên thực thể đứng bên trong[cite: 1]:
    * **Khúc Sông (`NAT_RIVER`):** Nơi thuộc tính Thủy thịnh. Giảm 50% thời gian hồi chiêu hệ Thủy, nhưng nếu hệ Hỏa đứng vào sẽ bị giảm 50% sát thương.
    * **Rừng Cây (`NAT_FOREST`):** Vùng bóng tối giúp ẩn hình hoàn toàn (Silhouette), tăng 50% hiệu quả độc chất hệ Mộc. Hệ Kim đứng vào đây sẽ bị giảm tốc độ xuất chiêu do cành cây cản trở.
    * **Bãi Cát Sa Mạc (`NAT_DESERT_ZONE`):** Vùng Thổ thịnh, tăng lực Knockback cho chiêu hệ Thổ. Nước (Thủy) xả vào đây bị giảm 50% tầm đánh do cát thấm hút âm thầm.

### Trụ cột 3 — Trận Pháp Ngũ Hành & Cảnh Giới Thái Cực (Formations)
* **5 Trận Pháp Ngũ Hành:** Kỹ năng khống chế không gian (AoE rộng) duy trì lâu bám sàn đấu[cite: 1]. Ví dụ: *Cửu Thiên Lôi Động Trận* (Sét Thổ giật ngược gây Stun, tăng phạm vi khi đè lên Sông)[cite: 1]; *Hàn Băng Thủy Tuyệt Trận* (Làm sàn trơn trượt khiến kẻ địch dễ bị trượt chân rớt vực)[cite: 1].
* **Cảnh Giới Thái Cực (Đại Trận Lưỡng Nghi):** Kích hoạt khi build chiêu thức đạt cân bằng Âm Dương (2 Dương: Hỏa/Kim + 2 Âm: Thủy/Mộc) hoặc tung chiêu đúng vòng tương sinh. 
    * Toàn bộ thế giới chuyển sang Shader Đơn Sắc (Trắng - Đen). Người chơi được miễn nhiễm mọi khắc chế nguyên tố, có khả năng mượn lực phản lực (Tứ lạng bạt thiên cân) để bảo vệ Boss mình[cite: 1].
    * Bộ chiêu cũ bị ẩn, thay bằng 2 Tuyệt học: **PHONG** (Gió - Nhu: tạo cuồng phong hút gom toàn bộ kẻ địch/minions và đạn đạo vào tâm bão) và **LÔI** (Điện - Cương: đánh sét tím trắng giáng xuống tâm bão Phong gây sát thương chí mạng diện rộng).

---

## IV. KIẾN TRÚC TƯƠNG TÁC TINH GIẢM (CẬP NHẬT)

Hệ thống tương tác tập trung hoàn toàn vào va chạm khối cầu 3D bám sàn (`CheckCollisionSpheres`), tối ưu hiệu năng CPU tối đa[cite: 1]:
* **Tầng 1 — Skill $\leftrightarrow$ Skill:** Đấu Pháp Projectile hằng số dữ liệu[cite: 1].
* **Tầng 2 — Skill $\leftrightarrow$ Thực thể (Character/Boss/Minion):** Tính toán sát thương, áp lực đẩy lùi trục Z (Knockback Force)[cite: 1] hoặc trạng thái bất lợi: Độc ký sinh (Poison), Tự bỏng nhiệt (Overheat), Trọng lực đè nặng (Slow/Stun), Phản chấn lòng bàn tay (Disarm/Rơi vũ khí).
* **Tầng 3 — Địa hình tĩnh $\leftrightarrow$ Thực thể:** Nhận diện Sông/Rừng/Cát để cộng/trừ trực tiếp chỉ số thuộc tính ẩn (Modifiers), không làm thay đổi hay phá hủy hình ảnh địa hình.
* **Tầng 4 — Trận Pháp $\leftrightarrow$ Thực thể:** Quét mảng tĩnh liên tục chỉnh sửa chỉ số của toàn bộ nhân vật nằm trong bán kính trận pháp[cite: 1].

---

## V. HỆ THỐNG THỰC THỂ ĐẤU TRƯỜNG (4VS4 HAI PHE)

### 1. Phân cấp Cự ly Chiêu thức & Cơ chế Bùa Chú
Bùa Chú đóng vai trò như một dạng thức triển khai chiêu thức từ xa (Remote Pylons)[cite: 1]. Nhân vật ném lá bùa cắm phập xuống đất bám sàn để làm điểm cắm cọc phát tán hiệu ứng nguyên tố[cite: 1].
* **Chiêu Tầm xa (Projectile):** Công cụ chính kích hoạt Đấu Pháp[cite: 1]. Bao gồm Thủy Tiễn, Hỏa Cầu (Tử Viêm), Phi Kiếm, Đạn Đá, Dây Leo Bào Tử. Sát thương thấp nhưng an toàn[cite: 1].
* **Chiêu Tầm trung (AoE/Control):** Quản lý không gian 3D (Địa chấn trọng lực, Trận cuồng phong, Liệt diệm trận). Sát thương trung bình, khống chế đẩy lùi mạnh[cite: 1].
* **Chiêu Cận chiến (Melee):** Sát thương lớn nhất, ra chiêu nhanh, gây khựng/phá giáp[cite: 1]. Có thể chém đứt hoặc phản ngược Projectile địch nếu căn thời gian chuẩn.
* **Chiêu Bùa chú / Phụ trợ (Trap/Utility):** Phóng bùa cắm cọc kích hoạt trận địa từ xa[cite: 1] (Hỏa Phù, Hàn Băng Phù) hoặc Khinh công dư ảnh, Thiền định nạp linh khí[cite: 1].

### 2. Thiết kế 10 Đại Tinh Linh Boss Song Phe (Amorphous Spirits)
Trong trận chiến PvP, mỗi phe sở hữu 1 Đại Tinh Linh Boss đóng vai trò hạt nhân cốt lõi[cite: 1]. Người chơi phải bảo vệ Boss phe mình và tìm cách đẩy lùi Boss địch rớt vực[cite: 1]. Để CPU không gánh khung xương Rigging phức tạp, Boss được dựng hoàn toàn bằng hệ thống hạt (CPU Emitter) kết hợp Custom Shader (`spirit.fs`) tích hợp thuật toán Metaball và FBM để tạo hình dạng khói cuộn, lỏng, xé rách biến ảo[cite: 1].

* **Hình dạng Thể xác Đêm Đen (Hắc Diện Tôn Giả):** Bản chất các Boss là khối đa giác Low-Poly đen tuyền ẩn hiện dạng bóng (Silhouette) dưới đêm[cite: 1]. Trên người chạy dọc các đường rãnh hoa văn phong thủy phát sáng theo Hệ thuộc tính hiện tại để người chơi tự nhận biết giải đố.
* **Cơ chế Hỗn Độn hai phe:** Khi Boss một trong hai phe tụt xuống dưới 30% máu, hoa văn chuyển sang màu Trắng - Đen đơn sắc $\rightarrow$ **Boss đó đột phá vào trạng thái Thái Cực**, liên tục dùng Phong để gom đội hình đối phương và giáng Lôi điện, tạo nên cao trào trận đấu.

10 Đại Tinh Linh Boss chia luân phiên cho các trận đấu bao gồm[cite: 1]:
1. **Hắc Linh Nhuyễn Thể (Vatu):** Khối chất lỏng tím đen phập phồng co giãn theo hàm `sinf()`[cite: 1].
2. **Cuồng Phong Linh (Zephyrus):** Cơn lốc xoáy năng lượng xanh neon, hạt Emitter xoắn ốc thăng thiên trục $Z$[cite: 1].
3. **Oán Linh Sương Mù (Umbra):** Làn sương đen loang loãng mờ ảo bập bùng, ẩn hiện bóng quỷ[cite: 1].
4. **Độc Khí Linh (Malakor):** Dải ruy-băng khói độc xanh thẫm uốn lượn zích-zắc (Thuật toán Moving Ribbon)[cite: 1].
5. **Diệt Linh Kích (Blitz):** Lõi sấm sét Plasma trắng phát xạ, hạt con giật nảy ngẫu nhiên[cite: 1].
6. **Thạch Nham Linh (Obsidian):** Khối đá magma chảy dòng bán lỏng bán rắn, lõi đen nứt đỏ rực rỡ[cite: 1].
7. **Hải Hồn Linh (Tiamat):** Thần nước hình dạng sứa nước khổng lồ uốn lượn, Shader khúc xạ trong suốt[cite: 1].
8. **Thực Linh Ma (Guzzler):** Hố đen năng lượng, logic vật lý đảo ngược hút toàn bộ hạt con và đạn lạc về tâm[cite: 1].
9. **Thần Mộc Linh (Yggdrasil's Shadow):** Cụm rễ cây rêu phong bò trườn quản lý bằng các dải ruy-băng xoáy[cite: 1].
10. **Tai Ương Tinh Linh (Kora):** Lõi mặt nạ 2D bao quanh bởi 4 luồng hạt nguyên tố xoay quanh gầm thét[cite: 1].

### 3. Quy tắc Trận chiến Hai Phe (PvP Đăng Đối)
Khi hai bên giáp mặt trực tiếp trong đêm, gameplay chuyển dịch thành cuộc đấu tranh giành không gian:
* **Chiến thuật Tranh Sáng / Tranh Tối:** Khi đứng im không ra chiêu, nhân vật ở trạng thái ẩn mình dưới bóng đêm (Silhouette). Hệ Hỏa (Tử Viêm) hoặc hệ Kim (Kiếm khí chói sáng) tuy sát thương cao nhưng sẽ tự biến mình thành "ngọn hải đăng" để phe đối phương đứng từ góc tối phản kích.
* **Hủy diệt và Bảo vệ Boss:** Minions (tinh linh nhỏ) liên tục được sinh ra từ Boss phe mình, lầm lũi tiến về phía Boss phe địch để tự nổ[cite: 1]. Người chơi phải dùng kỹ năng cự ly tầm trung hoặc Trận Pháp sàn để dọn dẹp Minions địch, tạo khoảng trống đẩy lùi Boss địch[cite: 1].
* **Âm Dương Đối Trận:** Khi cả hai phe cùng đột phá Thái Cực, hai đại trận đồ Trắng - Đen giao nhau sẽ tự triệt tiêu khả năng phản lực của nhau, đưa trận đấu về thế đối đầu kỹ năng Phong (Nhu - Khống chế/Gom đạn) và Lôi (Cương - Hủy diệt) thuần túy.

---

## VI. CHẾ ĐỘ CAMPAIGN & QUY HOẠCH QUÂN LÝ MẢNG TĨNH

**Giai đoạn 0:** Offline 100%. Vào thẳng Arena 3D đài đá lơ lửng, đánh nhau với 1 Boss Đại Tinh Linh Hắc Diện Tôn Giả biến đổi hệ theo Phase để thử nghiệm ma trận cự ly, Đấu Pháp ẩn và vật lý rớt vực trục Z, thắng hoặc thua[cite: 1].

**Quản lý bộ nhớ hiệu năng cao (Cấm Dynamic Allocations):** Toàn bộ thực thể được nạp vào các mảng tĩnh (Array Pool) cố định dưới CPU để tối ưu hóa CPU cache và triệt tiêu hoàn toàn bộ thu gom rác (Garbage Collector) gây giật lag trên Android[cite: 1]:
* `agentPool[8]` (4 Anh hùng Ta + 4 Anh hùng Địch AI)[cite: 1]
* `minionPool[40]` (Bầy tinh linh nhỏ do Boss sinh ra, tự động bò về phía đối phương để tự nổ)[cite: 1]
* `formationPool[4]` (Quản lý các Đại Trận Pháp đang hoạt động trên sân)[cite: 1]

---

## VIII. ĐỊNH HƯỚNG ĐỒ HỌA & KỸ THUẬT RENDER (CẬP NHẬT)

* **Giải pháp Đồ họa Nhân vật / Cảnh vật:** Kết hợp Model Low-Poly tối giản ($1500-3000$ polygons) gắn xương bằng Mixamo xuất file nén `.iqm` siêu nhẹ[cite: 1], hoặc dùng kỹ thuật **Billboard 2D** dựng đứng trong không gian 3D bám sàn[cite: 1]. Bóng tối màn đêm tự nhiên che đi các góc cạnh thô của mô hình.
* **Kỷ luật Canvas tổng duy nhất (Bắt buộc):** Toàn bộ Game Loop 3D kết xuất vào duy nhất một `Global RenderTexture` kích thước bằng màn hình[cite: 1]. Hiệu ứng Lửa Tím, Kiếm Khí Rung Động, Tia Nước Siêu Áp, Hệ thống hạt tự do[cite: 1], và trạng thái đơn sắc của Thái Cực đều được xử lý thông qua hệ thống **Custom Shaders** nạp vào Canvas tổng này đúng 1 lần duy nhất trước khi hiển thị[cite: 1], triệt tiêu hoàn toàn hiện tượng sụt giảm FPS do hoán đổi Render Target trên Android[cite: 1]. Cấm đổ bóng động thời gian thực (Real-time Shadows).

---

# PHẦN B — LỘ TRÌNH PHÁT TRIỂN & KỸ THUẬT

## IX. CHIẾN LƯỢC PHÁT TRIỂN TỔNG THỂ

| Giai đoạn | Mục tiêu chính | Có networking? |
|---|---|---|
| 0 — Test nội bộ | Kiểm chứng combat feel 3D Isometric, Đấu Pháp ẩn, Vật lý rớt vực trục Z và Boss Hắc Diện Tôn Giả | Không |
| 1 — MVP | Hoàn thiện vòng lặp chơi đơn (4vs4 với Minions) + thêm PvP 1v1 đối kháng hai phe + Auto-Targeting | Có (peer-hosted)[cite: 1] |
| 2 — Mở rộng | Tích hợp Trận Pháp Ngũ Hành, 3 Map địa hình tĩnh (Trigger sàn), PvP 2v2 song phe | Có |
| 3 — Hoàn thiện | Ranking xác thực Server, trang phục, sự kiện mùa | Có[cite: 1] |

---

## X. GIAI ĐOẠN 0 — BẢN TEST NỘI BỘ (CẬP NHẬT OFFLINE)

### Tiêu chí hoàn thành (Definition of Done)
* Người chơi điều khiển nhân vật di chuyển, khinh công lướt đêm, tung chiêu phân cấp cự ly mượt mà[cite: 1].
* Đánh bại hoặc thua trước Boss Hắc Diện Tôn Giả (được render lung linh bằng Shader Metaball + FBM đổi màu rãnh hoa văn chỉ thị hệ).
* Cơ chế va chạm khối cầu bám sàn hoạt động chuẩn xác; thực thể bị hất văng ra rìa đài đá sẽ rơi tự do theo trục $Z$ thực và chết[cite: 1].
* Đột phá trạng thái Thái Cực (Trắng - Đen) hoạt động đúng logic chuyển đổi sang 2 chiêu Phong - Lôi.
* Duy trì ổn định $\ge 60\text{FPS}$ trên thiết bị Android thật nhờ kỷ luật Gộp Canvas tổng duy nhất.

---

## XI. GIAI ĐOẠN 1 — MVP (PvP 1v1 PEER-HOSTED & AUTO-TARGETING)

* **ENet Đa luồng:** Thiết lập luồng phụ ENet gánh netcode chạy song song với luồng chính đồ họa Raylib đơn luồng để triệt tiêu hiện tượng lag giật mạng. Host phân giải toàn bộ va chạm Đấu Pháp hằng số[cite: 1].
* **Cân bằng hệ Thổ (Võ Lâm Style):** Để kìm hãm sự bá đạo của hệ Thổ (sở hữu cả hiệu ứng đẩy lùi và khống chế), áp dụng hình phạt vật lý: Tốc độ đạn bay chậm bằng một nửa hệ Hỏa, thời gian niệm chú lâu (Cast time 1.5s đứng yên gồng), và bị giảm 50% sát thương khi bay vào khu vực Rừng Cây (`NAT_FOREST`).
* **Mobile Auto-Targeting (Nâng cấp):** Thuật toán tự động khóa hồng tâm thông minh giải quyết bài toán UX cảm ứng di động: Ưu tiên bắt dính hướng đạn Projectile đối phương đang bay đến để tạo góc đối đòn kích hoạt Đấu Pháp chuẩn xác, hoặc bắt dính lõi tâm Boss phe địch trong đêm.

---

## XVII. RỦI RO CẦN THEO DÕI (TỔNG HỢP & CẬP NHẬT)

| Rủi ro | Giai đoạn ảnh hưởng | Hướng xử lý |
|---|---|---|
| Hệ Thổ quá bá đạo do ôm đồm nhiều hiệu ứng khống chế | 0 $\rightarrow$ 1 | Áp dụng hình phạt: Cast time lâu, tốc độ đạn lờ đờ, bị giảm 50% sát thương khi gặp Rừng (Mộc). |
| Người chơi không tìm ra quy luật giải đố vì không có UI | 0 $\rightarrow$ 1 | Thiết kế tín hiệu thị giác (Visual Cues) rõ ràng trong đêm: các rãnh bùa chú hoặc hạt năng lượng đổi màu chỉ thị rõ ràng khi tương tác đúng hệ khắc chế. |
| Cơ chế Thái Cực (Phong-Lôi) quá mạnh phá vỡ cân bằng hai phe | 1 | Siết chặt nhược điểm "Vô Sát": không có sát thương chủ động nếu đối thủ không đánh trước, tụt mana nhanh khi đánh sét. |
| Sụt giảm FPS trên Android do đổi Render Target | 0 trở đi | Tuyệt đối áp dụng kỷ luật **Gộp Canvas tổng duy nhất** ngay từ đầu, cấm tạo RenderTexture riêng cho từng skill/trận pháp[cite: 1]. |
| Thao tác UX điều khiển cảm ứng 3D quá khó khăn | 1 | Thiết kế và kiểm thử module **Auto-targeting** thông minh hỗ trợ bắt dính hướng đạn để đối đòn hoặc bắt dính tâm Boss. |
| Host migration trong PvP nhiều người (2v2, 3v3) | 2 | Thiết kế phương án "trận tự kết thúc khi host rời" thay vì chuyển host giữa chừng[cite: 1]. |

---

## XVIII. MỤC TIÊU HOÀN THÀNH PHIÊN BẢN 1.0 (sau Giai đoạn 1)

* 1 Đấu trường Sa mạc đêm trăng (Heightmap tĩnh bám sàn) tích hợp cơ chế rơi tự do vật lý trục $Z$[cite: 1].
* 1 Chế độ Campaign vượt sóng quái tinh linh nhỏ (Minions) kết hợp thử thách Boss Hắc Diện Cảnh giới Thái Cực[cite: 1].
* 1 PvP 1v1 Đối kháng hai phe Peer-hosted (ENet + Firebase) tích hợp hệ thống Auto-Targeting thông minh bắt đạn đối đòn.
* 20 Kỹ năng thuộc 5 Hệ phân cấp cự ly rõ ràng, cơ chế **Vô Hệ** tự động biến đổi thuộc tính theo bảng chiêu.
* Ít nhất 3 Đại Tinh Linh Boss hệ vô định hình hoạt động biến ảo mượt mà không dùng xương cứng qua Shader Metaball.
* Hệ thống nạp hệ số nhân Địa hình ảo tĩnh (Sông, Rừng, Bãi Cát) tự động bám sàn qua Virtual Triggers[cite: 1].
* Shader Ngũ Hành + Trạng thái Thái Cực đơn sắc + Hiệu ứng hạt tự do hiển thị qua một Canvas tổng duy nhất[cite: 1].
* Android + PC[cite: 1].