# THIẾT KẾ TỔNG THỂ DỰ ÁN: NGŨ HÀNH TỶ VÕ
### Phiên bản cải tiến 2.2 — tối ưu cho lộ trình 1 người + AI (Cập nhật Phân cấp cự ly & Tinh giảm ô môi trường)

---

# PHẦN A — THIẾT KẾ CỐT LÕI

## I. TẦM NHÌN DỰ ÁN

Ngũ Hành Tỷ Võ là một game hành động đấu trường 2.5D góc nhìn nghiêng, lấy cảm hứng từ văn hóa phương Đông và triết lý Ngũ Hành.

Người chơi điều khiển các nhân vật sử dụng pháp thuật Kim - Mộc - Thủy - Hỏa - Thổ để chiến đấu trong các đấu trường nhỏ, nơi kỹ năng không chỉ gây sát thương mà còn tương tác trực tiếp và tạo ra các phản ứng ngũ hành thời gian thực[cite: 1].

Mục tiêu thiết kế:

* Dễ tiếp cận
* Chiến đấu có chiều sâu
* Hiệu ứng đẹp bằng Shader
* Chạy mượt trên Android
* Chi phí vận hành gần như bằng 0[cite: 1]

**Bối cảnh phát triển:** Dự án do một người làm, có hỗ trợ của AI, dùng raylib làm engine và đã có module xây dựng skill riêng[cite: 1].

---

## II. TUYÊN NGÔN THIẾT KẾ

> Một đấu trường sống, nơi người chơi sử dụng Ngũ Hành để triệt tiêu pháp thuật đối phương và tạo ra các phản ứng nguyên tố theo thời gian thực. Bản thân thời tiết đầu trận sẽ đóng vai trò như "người chơi thứ 3" thúc đẩy sự kích thích và ép người chơi phải thích nghi với nghịch cảnh.

Khác với các game kỹ năng thông thường:
* **Tạm thời bỏ qua việc chiêu thức tác động lên môi trường** để tinh giảm độ phức tạp của hệ lưới Cell Grid, chống vỡ scope dự án.
* Skill tác động trực tiếp lên skill khác (Đấu Pháp).
* Skill tác động trực tiếp lên nhân vật thông qua hệ thống trạng thái bất lợi.

Người chơi không chỉ đánh nhau. Người chơi đang liên tục tìm cách thích nghi và khắc chế bộ chiêu thức của đối thủ.

### Mục tiêu cốt lõi

Người chơi phải luôn cảm thấy:

**1. Mình đang điều khiển một pháp sư mạnh mẽ** — thông qua skill đẹp, shader đẹp, khinh công nhanh[cite: 1].

**2. Mình đang làm chủ cự ly chiến đấu** — xoay tua giữa chiêu cận chiến sát thương khủng, chiêu tầm trung khống chế và chiêu tầm xa an toàn. Cách build cực kỳ đa dạng phụ thuộc hoàn toàn vào kỹ năng cá nhân.

**3. Mình đang đấu pháp thuật** — không chỉ né skill, mà còn chặn skill, chém đứt hoặc triệt tiêu skill thông qua phản ứng nguyên tố tất định[cite: 1].

---

## III. TRỤ CỘT GAMEPLAY (CẬP NHẬT)

### Trụ cột 1 — Khinh Công

Khinh công không chỉ để né, mà là một phần của combat[cite: 1].

Người chơi có thể: Lướt · Bật tường · Xuyên khoảng ngắn · Thoát khống chế[cite: 1].

Đặc điểm: phản hồi tức thì ở Client, Host xác nhận kết quả (từ Giai đoạn 1 trở đi khi có networking)[cite: 1].

Mỗi hệ có dư ảnh riêng: Hỏa để lại tàn lửa, Thủy để lại hơi nước, Mộc để lại lá cây (Lưu ảnh, Motion Blur, Dư ảnh nguyên tố)[cite: 1].

### Trụ cột 2 — Đấu Pháp (Hệ thống va chạm chiêu thức)

Skill có thể va chạm trực tiếp với nhau, không chỉ va chạm với người. Để kiểm soát scope của dự án solo, **Đấu Pháp được giới hạn nghiêm ngặt chỉ áp dụng cho chiêu loại Tầm xa (Projectile)** ở giai đoạn đầu (Mỗi hệ có đúng 1 Projectile trong 4 chiêu, tổng cộng có 5x5 cặp tương tác hằng số)[cite: 1]. Các cặp Projectile chưa có luật riêng mặc định sẽ đi xuyên qua nhau[cite: 1].

Quản lý bằng một ma trận dữ liệu tĩnh **Clash Matrix (5x5)** thuần túy (Deterministic) phía CPU để phân giải kết quả va chạm thuần dữ liệu, loại bỏ hoàn toàn các hàm số thực float phụ thuộc phần cứng nhằm phục vụ đồng bộ mạng sau này, tránh code if-else lồng nhau vô tận.
* **Hỏa vs Thủy** $\rightarrow$ `CLASH_STEAM_EXPLODE` (Hỏa bị dập, Thủy bốc hơi thành hơi nước)[cite: 1]
* **Kim vs Mộc** $\rightarrow$ `CLASH_CANCEL` (Phi kiếm chém đứt dây leo, cả hai triệt tiêu)[cite: 1]
* **Hỏa vs Mộc** $\rightarrow$ `CLASH_IGNITE_TRAIL` (Cháy lan theo quỹ đạo bay)[cite: 1]
* **Thổ vs Thủy** $\rightarrow$ `CLASH_MUD_ZONE` (Sinh bùn làm chậm)[cite: 1]
* **Kim vs Hỏa** $\rightarrow$ `CLASH_HEAT_UP` (Phi kiếm nóng đỏ, tăng sát thương xuyên giáp)[cite: 1]

* **Melee vs Projectile (Nâng cao):** Nếu người chơi tung chiêu cận chiến có thuộc tính "Phản đòn/Chém đứt" chính xác vào thời điểm Projectile của địch bay tới, họ có thể triệt tiêu hoàn toàn hoặc phản ngược nó về phía đối phương.

Một điểm cần quyết định sớm vì ảnh hưởng đến Giai đoạn 1 (PvP networking): **ai là người phân giải kết quả Đấu Pháp?** Vì model là peer-hosted, hợp lý nhất là Host luôn là bên tính toán va chạm/kết quả clash (giống cách Host đã xử lý va chạm, sát thương — xem mục XVI), miễn là logic phân giải hoàn toàn tất định (deterministic) dựa trên dữ liệu đã đồng bộ, để không có sai lệch giữa hai client[cite: 1].

### Trụ cột 3 — Đấu Trường Thời Tiết (Map Modifiers)

Bản đồ được thiết kế sẵn theo các file cấu trúc tĩnh (Static Layout) để bảo vệ Netcode khỏi lỗi lệch map (Desync). Để tạo bất ngờ và kích thích việc chọn bộ chiêu, mỗi trận đấu sẽ bốc thăm ngẫu nhiên một hiệu ứng Thời tiết đầu trận, tác động trực tiếp vào thuộc tính nhân vật và chiêu thức thông qua các hệ số nhân Multiplier hằng số:

* **Trận Mưa Rào (Thủy thuận lợi):** Giảm 20% Cooldown cho các chiêu hệ Thủy, tăng 15% kích thước (Scale) đồ họa hệ Thủy. Giảm 20% sát thương của các chiêu hệ Hỏa.
* **Hạn Hán (Hỏa thuận lợi):** Tăng 20% sát thương cho toàn bộ chiêu hệ Hỏa. Thời gian gây trạng thái cháy (`Burn`) lên mục tiêu kéo dài gấp đôi.
* **Mùa Xuân Đâm Chồi (Mộc thuận lợi):** Tăng tốc độ hồi Thể lực (Stamina) cho nhân vật mang hệ Mộc, tăng thời gian trói chân của cạm bẫy dây leo.
* **Bão Cát (Thổ thuận lợi):** Tăng tốc độ bay cho Đan Đá hệ Thổ, giảm tầm nhìn tổng thể trên đấu trường.

---

## IV. KIẾN TRÚC TƯƠNG TÁC TINH GIẢM

Bỏ qua tương tác lưới ô dưới đất, kiến trúc tương tác 4 tầng cũ được tinh giản tập trung vào va chạm thực thể động:

* **Tầng 1 — Skill $\leftrightarrow$ Skill:** Hệ thống Đấu Pháp va chạm Projectile (Xử lý bằng ma trận va chạm dữ liệu hằng số hệt như Trụ cột 2)[cite: 1].
* **Tầng 2 — Melee $\leftrightarrow$ Projectile:** Đòn cận chiến đánh chặn hoặc phản hồi chiêu tầm xa địch dựa trên căn thời gian chính xác.
* **Tầng 3 — Weather $\leftrightarrow$ Skill / Character:** Hệ thống thời tiết tăng/giảm trực tiếp thuộc tính sát thương, kích thước chiêu hoặc tốc độ hồi tài nguyên của nhân vật.
* **Tầng 4 — Skill $\leftrightarrow$ Character:** Hệ thống trạng thái bất lợi tiêu chuẩn áp trực tiếp lên nhân vật (Burn, Slow, Armor Break, Frozen, Poison), hoàn toàn độc lập với môi trường nền đất[cite: 1].

---

## V. HỆ THỐNG NGŨ HÀNH (PHIÊN BẢN CỰ LY CHIẾN ĐẤU)

### Triết lý phân cấp cự ly:
* **Chiêu Cận chiến (Melee):** Sát thương lúc nào cũng to nhất, tốc độ ra chiêu cực nhanh, kèm thuộc tính gây khựng hoặc phá giáp để bù đắp rủi ro phải áp sát mục tiêu.
* **Chiêu Tầm trung (AoE):** Sát thương trung bình, vùng ảnh hưởng rộng, đi kèm hiệu ứng khống chế kiểm soát (đẩy lùi, làm chậm).
* **Chiêu Tầm xa (Projectile):** Sát thương thấp nhất nhưng an toàn nhất, tầm bắn bao quát. Là công cụ để kích hoạt Đấu Pháp[cite: 1].
* **Chiêu Cạm bẫy / Phụ trợ (Trap/Utility):** Đặt bẫy tại chỗ ép góc đối thủ hoặc lướt né chiêu, tạo khiên hộ thể, hồi phục tài nguyên[cite: 1].

### Ma trận kỹ năng 5 hệ:

* **THỦY** — Thủy Tiễn (Tầm xa) · Thủy Long (Tầm trung) · Băng Đao (Cận chiến) · Hàn Băng Trói (Cạm bẫy)
* **HỎA** — Hỏa Cầu (Tầm xa) · Liệt Diệm Trận (Tầm trung) · Hỏa Quyền (Cận chiến) · Hỏa Bộ (Phụ trợ)
* **MỘC** — Dây Leo (Tầm xa) · Gai Đất (Tầm trung) · Mộc Roi (Cận chiến) · Hồi Sinh Khí (Phụ trợ)
* **THỔ** — Đạn Đá (Tầm xa) · Địa Chấn (Tầm trung) · Thổ Chùy (Cận chiến) · Tường Đá (Phụ trợ)
* **KIM** — Phi Kiếm (Tầm xa) · Kiếm Trận (Tầm trung) · Vạn Kiếm Trảm (Cận chiến) · Kim Giáp (Phụ trợ)

### Thứ tự ưu tiên triển khai

* **Giai đoạn 0:** Đủ 5 hệ, 20 kỹ năng — Sử dụng mã nguồn hệ Mộc hiện tại làm Prompt mẫu (Few-Shot Prompting) để AI sinh nhanh 4 hệ còn lại tuân thủ cấu trúc Multi-channel Emitter.
* **Giai đoạn 1:** không cần thêm nội dung hệ/skill mới — tập trung vào wave/upgrade system và networking PvP (xem mục XII)[cite: 1].

---

## VI. HỆ THỐNG BUILD SKILL

Người chơi mang tối đa 4 kỹ năng. Không giới hạn nguyên tố. Khuyến khích phối hợp đa hệ[cite: 1]. Cách build hoàn toàn tự do phụ thuộc kỹ năng người chơi (ví dụ: Thuần cận chiến sát thủ, thuần thả diều tầm xa hoặc đa dụng thích nghi thời tiết). Người chơi đưa ra quyết định build một cách bí mật trước khi bốc thăm map để tăng kích thích.

---

## VII. CHẾ ĐỘ CAMPAIGN

**Giai đoạn 0:** Offline 100%, không wave, không upgrade. Vào thẳng Arena, đánh Boss đổi hệ theo Phase trận đấu để kiểm thử toàn diện ma trận cự ly và Đấu Pháp, thắng hoặc thua[cite: 1].

**Giai đoạn 1 trở đi:** Bổ sung cấu trúc đầy đủ —

* Arena cố định, quái xuất hiện theo đợt, người chơi vượt sóng quái[cite: 1]
* Sau mỗi Wave: chọn 1 trong 3 nâng cấp ngẫu nhiên (ví dụ +20% sát thương Thủy, +1 lần phản xạ, +15% tốc độ hồi thể lực), chỉ tồn tại trong trận hiện tại[cite: 1]

---

## VIII. ĐỊNH HƯỚNG ĐỒ HỌA

Phong cách: Fantasy phương Đông. Không theo hướng hoạt hình hài hước. Không theo hướng MMORPG truyền thống[cite: 1].

Nguồn cảm hứng: Ori and the Blind Forest, Battlerite, Nine Sols, Tru Tiên, Tiên Kiếm[cite: 1].

Ưu tiên: Shader đẹp hơn số lượng texture. Hiệu ứng sống động hơn mô hình phức tạp[cite: 1].

Mỗi nguyên tố phải có bản sắc riêng[cite: 1]:
* Thủy: Mềm mại - linh động - phát sáng[cite: 1]
* Hỏa: Mạnh mẽ - bùng nổ[cite: 1]
* Mộc: Sinh trưởng - lan rộng[cite: 1]
* Thổ: Nặng nề - vững chắc[cite: 1]
* Kim: Sắc bén - tốc độ cao[cite: 1]

**Lưu ý hiệu năng Android:** Motion Blur, dư ảnh nguyên tố, hiệu ứng Đấu Pháp đều có thể nặng cho GPU Android tầm trung/thấp[cite: 1]. 
* **Giải pháp kiến trúc bắt buộc:** Toàn bộ Game Loop chỉ sử dụng duy nhất một Canvas tổng (`Global RenderTexture`) có kích thước bằng màn hình, các hệ thống skill vẽ chung lên các kênh R, G, B của Canvas này bằng lệnh `rlBegin(RL_TRIANGLES)` rồi nạp qua Shader xử lý 1 lần duy nhất để né bẫy sụt giảm FPS do chuyển đổi Render Target.

---

# PHẦN B — LỘ TRÌNH PHÁT TRIỂN & KỸ THUẬT

## IX. CHIẾN LƯỢC PHÁT TRIỂN TỔNG THỂ

Rủi ro lớn nhất của dự án không nằm ở kỹ thuật mạng, mà ở câu hỏi: **combat, Đấu Pháp và hệ thống phân cấp cự ly có thật sự "đã tay" không?** Nếu câu trả lời là không, mọi nỗ lực networking, ranking, hay nội dung mở rộng đều vô nghĩa[cite: 1].

Lộ trình chia thành 4 giai đoạn, trong đó có một **Giai đoạn 0 hoàn toàn offline, không networking**, dùng để kiểm chứng phần cốt lõi này trước khi đầu tư vào hạ tầng nhiều người chơi[cite: 1].

| Giai đoạn | Mục tiêu chính | Có networking? |
|---|---|---|
| 0 — Test nội bộ | Kiểm chứng combat feel, Đấu Pháp, và hệ thống cự ly chiêu thức | Không |
| 1 — MVP | Hoàn thiện vòng lặp chơi đơn + thêm PvP 1v1 | Có (peer-hosted) |
| 2 — Mở rộng | Thêm nội dung, Bốc thăm Map Thời tiết, PvP 2v2 | Có |
| 3 — Hoàn thiện | Ranking, trang phục, sự kiện mùa | Có (cân nhắc dedicated server cho ranked) |

---

## X. GIAI ĐOẠN 0 — BẢN TEST NỘI BỘ

### Mục tiêu

Trả lời các câu hỏi sau, không bị nhiễu bởi bất kỳ hệ thống nào khác:
1. Combat một-người-một-boss có vui không (di chuyển, khinh công, dùng skill phân cấp cự ly)?
2. Đấu Pháp Projectile (Tầng 1) và Đánh chặn Cận chiến (Tầng 2) có tạo cảm giác "đấu pháp thuật" thật sự không, hay chỉ là hiệu ứng đẹp mà không ảnh hưởng chiến thuật?
3. Hệ thống Gộp Canvas tổng hoạt động có mượt mà và duy trì $\ge 60\text{FPS}$ ổn định trên Android không?

### Phạm vi

* 1 Arena duy nhất cấu trúc tĩnh[cite: 1]
* 1 Boss duy nhất, pattern tấn công cố định, tự động chuyển đổi qua lại giữa 2-3 hệ nguyên tố theo từng phase trận đấu[cite: 1]
* **Không** wave system, **không** hệ thống nâng cấp ngẫu nhiên. Vào thẳng trận đấu với boss, thắng hoặc thua, hết[cite: 1].
* Đủ 5 hệ, 20 skill sinh bằng AI Prompt mẫu từ framework hệ Mộc có sẵn.
* Đấu Pháp giới hạn ở các cặp Projectile đã liệt kê cụ thể trong mục III (mở rộng dần, không cần làm hết 5×5 ngay)[cite: 1].
* Khinh công đầy đủ kèm dư ảnh nguyên tố[cite: 1]
* **Không networking dưới bất kỳ hình thức nào**[cite: 1]

### Lưu ý khi build

* Combat Layer vẫn chạy fixed-tick (30Hz cố định, `dt = 1.0f / 30.0f`), tách khỏi Render Layer ngay từ đầu — không đổi so với bản trước[cite: 1].
* Content không còn là nút thắt nhờ mẫu mã nguồn hệ Mộc có sẵn, nhưng balance cự ly là nút thắt mới: Gợi ý bật dần từng hệ, rồi mới bật Đấu Pháp để dễ cô lập vấn đề thay vì đánh giá cùng lúc.
* Vì Đấu Pháp cần phân giải tất định (deterministic) để chuẩn bị cho PvP ở Giai đoạn 1, nên viết logic phân giải va chạm skill theo cách thuần dữ liệu (tra bảng hằng số hị phân, không phụ thuộc trạng thái ẩn hay random không seed) ngay từ bây giờ[cite: 1].

### Tiêu chí hoàn thành (Definition of Done)

* Người chơi có thể đánh bại hoặc thua trước Boss trong một trận hoàn chỉnh[cite: 1]
* Cả 20 skill (5 hệ) hoạt động đúng vai trò cự ly, có cảm giác khác biệt rõ ràng giữa các lối build cận chiến vs tầm xa.
* Ít nhất các cặp Đấu Pháp Projectile cốt lõi hoạt động đúng hằng số ma trận tra cứu.
* Khinh công + dư ảnh nguyên tố hoạt động mượt, đúng bản sắc hệ[cite: 1].
* Chạy mượt mà ổn định không giật lag trên thiết bị Android thật.

---

## XI. GIAI ĐOẠN 1 — MVP (PvP 1v1 PEER-HOSTED)

Chỉ bắt đầu sau khi Giai đoạn 0 đã chứng minh combat feel ổn[cite: 1].

### Mở rộng nội dung
* Nội dung skill, Đấu Pháp đã hoàn thành phần lõi từ Giai đoạn 0 — Giai đoạn 1 chủ yếu cân bằng lại dựa trên dữ liệu test nội bộ, và mở rộng thêm các cặp Đấu Pháp chưa làm ở Giai đoạn 0[cite: 1]
* Hoàn thiện Chế độ Campaign: thêm wave system + nâng cấp ngẫu nhiên sau mỗi wave[cite: 1]
* Hệ thống Build Skill: mang tối đa 4 kỹ năng, không giới hạn nguyên tố, không phạt đồng hệ[cite: 1]

### Thêm PvP 1v1 — Peer-hosted qua Firebase & Kiến trúc đa luồng tự tạo
* **Giải pháp luồng song song với raylib:** Do raylib chạy đơn luồng đồ họa, hệ thống tự thiết lập luồng phụ gánh netcode: Luồng chính (Raylib Thread) xử lý Combat logic 30Hz và vẽ đồ họa; Luồng mạng phụ (ENet Worker Thread) chạy ngầm chặn đợi nhận/gửi gói tin qua ENet UDP, đẩy dữ liệu vào một Thread-safe Queue chung để luồng chính xử lý, triệt tiêu hiện tượng khựng game do lag mạng.
* **Vai trò của Firebase:** chỉ dùng để matchmaking và signaling — không xử lý logic trận đấu[cite: 1]. Với quy mô tối đa ~100 người chơi cùng lúc, gói miễn phí của Firebase dư sức đáp ứng[cite: 1].
* **NAT traversal:** Firebase chỉ giúp hai client "tìm thấy nhau", chưa giúp họ kết nối trực tiếp được[cite: 1]. Kết nối P2P thật cần STUN (miễn phí) và đôi khi TURN relay dự phòng khi xuyên NAT thất bại — thường gặp với mạng di động (carrier-grade NAT) tại Việt Nam[cite: 1]. **Khuyến nghị: đo tỷ lệ kết nối thất bại thực tế trên mạng di động trước khi commit hoàn toàn vào peer-hosted thuần.**[cite: 1]
* **Host advantage và rủi ro gian lận:** Host xử lý va chạm/sát thương/kết quả Đấu Pháp — vừa là người chơi vừa là trọng tài[cite: 1]. Chấp nhận được ở PvP thường, nhưng là khoản nợ kỹ thuật cần xử lý trước khi có ranking (xem mục XIV)[cite: 1].
* **Mobile Auto-Targeting:** Thiết lập thuật toán tự động khóa hồng tâm thông minh (ưu tiên bắt hướng Projectile địch đang bay đến hoặc bắt Boss) để giải quyết bài toán UX trên màn hình cảm ứng di động khi người chơi phải bấm tổ hợp lướt né + xả chiêu cùng lúc.

---

## XIII. GIAI ĐOẠN 2

* 30 kỹ năng[cite: 1]
* PvP 2v2[cite: 1]
* Tích hợp 3 bản đồ cấu trúc tĩnh hoàn chỉnh (Đỉnh Hoa Sơn, Rừng Ngập Mặn, Phế Tích Thổ Thần) kết hợp hệ thống bốc thăm ngẫu nhiên Thời tiết đầu trận.
* Roguelike nâng cao[cite: 1]

**Lưu ý kỹ thuật:** PvP 2v2 peer-hosted phức tạp hơn 1v1 đáng kể khi một người rời trận (host migration)[cite: 1]. Nên thiết kế phương án "trận tự kết thúc nếu host rời" thay vì cố gắng chuyển host giữa chừng[cite: 1].

---

## XIV. GIAI ĐOẠN 3

* 50 kỹ năng[cite: 1]
* PvP 3v3[cite: 1]
* Hệ thống xếp hạng[cite: 1]
* Trang phục[cite: 1]
* Sự kiện mùa[cite: 1]

**Lưu ý quan trọng về Ranking:** Vì host tự xử lý kết quả trận đấu (bao gồm cả kết quả Đấu Pháp), đây là điểm yếu cho tính công bằng của ranked match[cite: 1]. Trước khi ra mắt ranking, cân nhắc: (a) chỉ casual/campaign dùng peer-hosted, ranked dùng dedicated server nhẹ để xác thực và phân giải kết quả; hoặc (b) thêm cơ chế phát hiện gian lận cơ bản bằng cách so sánh log trận đấu giữa hai client[cite: 1].

---

## XV. KIẾN TRÚC PHẦN MỀM

Tầng 1: Network Layer[cite: 1]
Tầng 2: Game State Layer[cite: 1]
Tầng 3: Combat Layer[cite: 1]
Tầng 4: Physics & AI Layer[cite: 1]
Tầng 5: Rendering Layer[cite: 1]

Mỗi tầng hoạt động độc lập. Render không ảnh hưởng Gameplay. Gameplay không phụ thuộc FPS[cite: 1].

**Áp dụng từ Giai đoạn 0:** nên giữ kỷ luật tách Render khỏi Gameplay ngay từ bản test offline đầu tiên, vì retrofit kiến trúc fixed-tick vào một game đã quen chạy theo frame tốn công hơn nhiều so với làm đúng từ đầu[cite: 1].

**Lưu ý riêng cho raylib:** raylib không có sẵn lớp networking — khi sang Giai đoạn 1 sẽ cần tích hợp thư viện ngoài (ví dụ ENet cho UDP)[cite: 1]. Nên định nghĩa interface của Network Layer ngay từ Giai đoạn 0 (kể cả khi bên trong chỉ là implementation rỗng/local), để Game State Layer và Combat Layer gọi qua interface đó thay vì gọi thẳng logic local[cite: 1].

---

## XVI. HỆ THỐNG NETWORK (áp dụng từ Giai đoạn 1)

### Tickrate
Logic: 30Hz cố định[cite: 1]
Render: 60~120Hz[cite: 1]

### Đồng bộ dữ liệu
Client gửi: Input, hướng xoay, kích hoạt skill[cite: 1]
Host xử lý: Va chạm, sát thương, cooldown, stamina, trạng thái, kết quả Đấu Pháp[cite: 1]
Client chỉ hiển thị kết quả[cite: 1].

### Nội suy
Linear Interpolation để làm mượt vị trí giữa các gói tin[cite: 1].

### Mô hình kết nối: Peer-hosted + Firebase signaling
* Firebase: matchmaking + trao đổi thông tin kết nối (signaling)[cite: 1]
* STUN (miễn phí): xuyên NAT để kết nối P2P trực tiếp[cite: 1]
* TURN (có chi phí khi cần, nhưng nhỏ ở quy mô 100 ccu): relay dự phòng khi STUN thất bại[cite: 1]
* Rủi ro đã ghi nhận: host advantage nhẹ, khả năng gian lận phía host (xem mục XII, XIV)[cite: 1]

---

## XVII. RỦI RO CẦN THEO DÕI (TỔNG HỢP & CẬP NHẬT)

| Rủi ro | Giai đoạn ảnh hưởng | Hướng xử lý |
|---|---|---|
| Combat/Đấu Pháp không đủ hấp dẫn | 0 | Test sớm; bật dần từng hệ thống (skill cự ly $\rightarrow$ Đấu Pháp) để cô lập vấn đề[cite: 1] |
| Đấu Pháp mở rộng vô hạn nếu định nghĩa luật cho mọi cặp skill | 0 | Giới hạn nghiêm ngặt ở chiêu loại Projectile trước, mở rộng dần theo giá trị thiết kế[cite: 1] |
| Sụt giảm FPS trên Android do đổi Render Target | 0 trở đi | Áp dụng kỷ luật **Gộp Canvas tổng duy nhất** ngay từ đầu, cấm tạo RenderTexture riêng cho từng skill. |
| Tính tất định của Đấu Pháp chưa đảm bảo cho PvP sau này | 0 $\rightarrow$ 1 | Viết logic phân giải va chạm thuần dữ liệu (ma trận hằng số) ngay từ Giai đoạn 0[cite: 1] |
| NAT traversal thất bại trên mạng di động | 1 | Đo tỷ lệ thất bại thực tế trên hạ tầng 4G/5G Việt Nam, chuẩn bị TURN dự phòng[cite: 1] |
| Host advantage / gian lận phía host | 1, đặc biệt 3 | Chấp nhận ở PvP thường, cần máy chủ Dedicated Server riêng cho ranked[cite: 1] |
| raylib không có sẵn networking | 1 | Định nghĩa interface Network Layer từ Giai đoạn 0, chọn thư viện ENet trước khi code PvP[cite: 1] |
| Thao tác UX điều khiển cảm ứng quá khó khăn | 1 | Prototype và kiểm thử module **Auto-targeting** thông minh hỗ trợ bắt dính hướng đạn để đối đòn. |
| Host migration trong PvP nhiều người (2v2, 3v3) | 2 | Thiết kế phương án "trận tự kết thúc khi host rời" thay vì chuyển host giữa chừng[cite: 1] |

---

## XVIII. MỤC TIÊU HOÀN THÀNH PHIÊN BẢN 1.0 (sau Giai đoạn 1)

* 1 Arena tĩnh (mở rộng từ Giai đoạn 0)[cite: 1]
* 1 Campaign đầy đủ (wave + upgrade)[cite: 1]
* 1 PvP 1v1 (peer-hosted qua Firebase và ENet đa luồng tự tạo với cơ chế Auto-targeting)
* 20 Skill, 5 Hệ phân cấp cự ly rõ ràng
* Đấu Pháp (Tầng 1) cho các cặp Projectile chính[cite: 1]
* Hệ thống nạp hệ số nhân Thời tiết tự động đầu trận
* Khinh Công[cite: 1]
* Shader Ngũ Hành hiển thị qua một Canvas tổng duy nhất
* Android + PC[cite: 1]
* Có thể chơi hoàn chỉnh từ đầu tới cuối, đã được kiểm chứng combat feel và Đấu Pháp từ Giai đoạn 0 trước khi mở rộng[cite: 1]