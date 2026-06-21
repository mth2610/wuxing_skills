# THIẾT KẾ TỔNG THỂ DỰ ÁN: NGŨ HÀNH TỶ VÕ
### Phiên bản cải tiến 2.0 — tối ưu cho lộ trình 1 người + AI

---

# PHẦN A — THIẾT KẾ CỐT LÕI

## I. TẦM NHÌN DỰ ÁN

Ngũ Hành Tỷ Võ là một game hành động đấu trường 2.5D góc nhìn nghiêng, lấy cảm hứng từ văn hóa phương Đông và triết lý Ngũ Hành.

Người chơi điều khiển các nhân vật sử dụng pháp thuật Kim - Mộc - Thủy - Hỏa - Thổ để chiến đấu trong các đấu trường nhỏ, nơi kỹ năng không chỉ gây sát thương mà còn tác động lên môi trường và tạo ra các phản ứng ngũ hành thời gian thực.

Mục tiêu thiết kế:

* Dễ tiếp cận
* Chiến đấu có chiều sâu
* Hiệu ứng đẹp bằng Shader
* Chạy mượt trên Android
* Chi phí vận hành gần như bằng 0

**Bối cảnh phát triển:** Dự án do một người làm, có hỗ trợ của AI, dùng raylib làm engine và đã có module xây dựng skill riêng.

---

## II. TUYÊN NGÔN THIẾT KẾ

> Một đấu trường sống, nơi người chơi sử dụng Ngũ Hành để thay đổi môi trường, triệt tiêu pháp thuật đối phương và tạo ra các phản ứng nguyên tố theo thời gian thực.

Khác với các game kỹ năng thông thường:

* Skill tác động lên môi trường
* Skill tác động lên skill khác
* Skill tác động lên nhân vật

Người chơi không chỉ đánh nhau. Người chơi đang thay đổi cả chiến trường.

### Mục tiêu cốt lõi

Người chơi phải luôn cảm thấy:

**1. Mình đang điều khiển một pháp sư mạnh mẽ** — thông qua skill đẹp, shader đẹp, khinh công nhanh.

**2. Mình đang thao túng chiến trường** — đốt rừng, làm ngập bản đồ, dựng tường đá, trồng dây leo. Arena liên tục thay đổi.

**3. Mình đang đấu pháp thuật** — không chỉ né skill, mà còn chặn skill, triệt tiêu skill, tạo phản ứng nguyên tố.

---

## III. TRỤ CỘT GAMEPLAY

### Trụ cột 1 — Khinh Công

Khinh công không chỉ để né, mà là một phần của combat.

Người chơi có thể: Lướt · Bật tường · Xuyên khoảng ngắn · Thoát khống chế.

Đặc điểm: phản hồi tức thì ở Client, Host xác nhận kết quả (từ Giai đoạn 1 trở đi khi có networking).

Mỗi hệ có dư ảnh riêng: Hỏa để lại tàn lửa, Thủy để lại hơi nước, Mộc để lại lá cây (Lưu ảnh, Motion Blur, Dư ảnh nguyên tố).

### Trụ cột 2 — Đấu Pháp (cơ chế mới)

Skill có thể va chạm trực tiếp với nhau, không chỉ va chạm với người hoặc môi trường.

Ví dụ:

* **Hỏa vs Thủy** → Hỏa bị dập, Thủy bốc hơi → sinh ra **STEAM**
* **Kim vs Mộc** → Phi kiếm chém đứt dây leo
* **Hỏa vs Mộc** → Cháy lan
* **Thổ vs Thủy** → Sinh bùn (**MUD**)
* **Kim vs Hỏa** → Kim nóng đỏ

Khi hai luồng pháp thuật gặp nhau, kết quả có thể là: Triệt tiêu · Đẩy lùi · Hợp nhất · Biến đổi.

**Lưu ý phạm vi triển khai (quan trọng để không vỡ scope):** Đấu Pháp là một cơ chế hoàn toàn mới so với bản thiết kế gốc — về mặt kỹ thuật, đây không phải là mở rộng của Cell Grid mà là một subsystem riêng: phát hiện va chạm giữa hai object skill đang bay trong không gian, rồi tra một bảng luật (skill nào × skill nào → kết quả gì). Nếu cố định nghĩa luật va chạm cho mọi cặp trong 20 skill, số tổ hợp sẽ tăng rất nhanh và không thể tinh chỉnh hết bằng tay trong Giai đoạn 0. Khuyến nghị: **giới hạn Đấu Pháp chỉ áp dụng cho skill loại Projectile** ở giai đoạn đầu (mỗi hệ có đúng 1 Projectile trong 4 skill, nên chỉ còn tối đa 5×5 cặp cần định nghĩa, không phải 20×20), và chỉ implement trước các cặp đã có ví dụ cụ thể ở trên. Các cặp Projectile còn chưa có luật riêng thì mặc định "đi xuyên qua nhau" (không clash) cho đến khi được thiết kế thêm.

Một điểm cần quyết định sớm vì ảnh hưởng đến Giai đoạn 1 (PvP networking): **ai là người phân giải kết quả Đấu Pháp?** Vì model là peer-hosted, hợp lý nhất là Host luôn là bên tính toán va chạm/kết quả clash (giống cách Host đã xử lý va chạm, sát thương — xem mục XVI), miễn là logic phân giải hoàn toàn tất định (deterministic) dựa trên dữ liệu đã đồng bộ, để không có sai lệch giữa hai client.

### Trụ cột 3 — Đấu Trường Sống

Arena không cố định. Người chơi liên tục thay đổi nó.

Ví dụ một chuỗi biến đổi bản đồ:

* Thủy Long → tạo vùng **WET**
* Mộc Khí → biến WET thành rừng (**PLANT**)
* Viêm Long → đốt rừng (**BURNING**)
* Thủy Triều → tạo hơi nước (**STEAM**)
* Kim Kiếm → kích hoạt sương kim loại

Sau 3 phút chiến đấu, bản đồ đã khác hoàn toàn lúc bắt đầu.

---

## IV. KIẾN TRÚC TƯƠNG TÁC 4 TẦNG

Đây là phần thay thế và mở rộng mô hình Cell Grid đơn giản (1 ô = 1 trạng thái) của bản thiết kế gốc. Bốn tầng tương tác:

**Tầng 1 — Skill ↔ Environment**
Ví dụ: Hỏa Cầu → BURNING

**Tầng 2 — Skill ↔ Skill** *(chính là cơ chế Đấu Pháp, xem Trụ cột 2)*
Ví dụ: Viêm Long + Thủy Long → STEAM EXPLOSION

**Tầng 3 — Environment ↔ Environment**
Ví dụ: WET + BURNING → STEAM

**Tầng 4 — Skill ↔ Character**
Ví dụ: Hỏa → Burn · Thủy → Slow · Kim → Armor Break

### Ghi chú kiến trúc kỹ thuật cho từng tầng

* **Tầng 1 & 3** dùng chung một hệ trạng thái ô (xem mục VI). Vì một ô có thể mang nhiều trạng thái cùng lúc, trạng thái ô nên được lưu dưới dạng **tập hợp cờ (bitflags/set)** thay vì một enum đơn như bản thiết kế gốc — đây là thay đổi kiến trúc quan trọng nhất của bản 2.0, cần làm đúng ngay từ đầu vì sửa lại sau sẽ tốn công.
* **Tầng 2 (Đấu Pháp)** là một subsystem riêng biệt về code (phát hiện va chạm giữa các skill projectile đang bay), tách khỏi Cell Grid — không nên cố gắng nhét chung logic này vào hệ thống ô.
* **Tầng 4** là một hệ thống status effect tiêu chuẩn (Burn/Slow/Armor Break...) áp lên Character, tách biệt khỏi Cell Grid và khỏi Đấu Pháp — đây là phần ít rủi ro kỹ thuật nhất trong 4 tầng vì là mẫu hình rất phổ biến trong game hành động.

Việc tách 4 tầng thành các subsystem độc lập (thay vì một hệ thống "tương tác chung chung") giúp mỗi phần có thể test riêng và không gây hiệu ứng phụ chéo nhau khi balance.

---

## V. HỆ THỐNG NGŨ HÀNH

### Năm nguyên tố — phong cách và trạng thái tạo ra

| Hệ | Phong cách | Trạng thái tạo ra |
|---|---|---|
| **THỦY** | Linh hoạt, kiểm soát | WET, STEAM |
| **HỎA** | Áp lực, bùng nổ | BURNING |
| **MỘC** | Mở rộng chiến trường | PLANT |
| **THỔ** | Thay đổi địa hình | ROCK, MUD |
| **KIM** | Chính xác, phản đòn | METAL, SHARP FIELD |

### Vai trò kỹ năng

Mỗi hệ đều có: 1 Projectile, 1 Area Skill, 1 Utility Skill, 1 Ultimate

**THỦY** — Thủy Tiễn · Thủy Long · Hàn Băng Trói · Thủy Triều
**HỎA** — Hỏa Cầu · Liệt Diệm Trận · Hỏa Bộ · Viêm Long
**MỘC** — Dây Leo · Gai Đất · Hồi Sinh Mộc Khí · Cổ Thụ Trấn
**THỔ** — Đạn Đá · Tường Đá · Địa Chấn · Sơn Hà Trấn
**KIM** — Phi Kiếm · Kiếm Trận · Kim Giáp · Vạn Kiếm Quy Tông

### Thứ tự ưu tiên triển khai

* **Giai đoạn 0:** đủ 5 hệ, 20 kỹ năng — khả thi nhờ module xây dựng skill có sẵn trong raylib
* **Giai đoạn 1:** không cần thêm nội dung hệ/skill mới — tập trung vào wave/upgrade system và networking PvP (xem mục XII)

---

## VI. HỆ THỐNG TRẠNG THÁI MÔI TRƯỜNG

Bản đồ sử dụng Cell Grid. Khác với bản thiết kế gốc, **mỗi ô giờ có thể chứa nhiều trạng thái cùng lúc** thay vì chỉ một.

Ví dụ:

```
WET + PLANT = Rừng ẩm
```

Nếu thêm Hỏa vào ô đó:

```
WET + PLANT + BURNING → STEAM FOREST
```

Hiệu ứng của STEAM FOREST: tầm nhìn giảm, gây sát thương nhẹ.

Lưu ý đây là một ví dụ đẹp về thiết kế nhất quán: STEAM có thể sinh ra theo hai con đường khác nhau — từ Tầng 2 (Đấu Pháp: Hỏa vs Thủy va chạm trực tiếp) hoặc từ Tầng 3 (Environment ↔ Environment: WET gặp BURNING trên cùng một ô). Hai con đường cùng dẫn tới một kết quả nguyên tố hợp lý là điều nên giữ làm nguyên tắc thiết kế xuyên suốt khi thêm các tổ hợp mới.

### Cách quản lý độ phức tạp tổ hợp (quan trọng)

Với trạng thái ô là tập hợp cờ, số tổ hợp khả dĩ tăng theo cấp số nhân (2 trạng thái sinh ra 1 tổ hợp đặc biệt, nhưng 5 trạng thái có thể sinh ra hàng chục tổ hợp). **Không khả thi và không cần thiết phải định nghĩa luật cho mọi tổ hợp.** Cách làm thực tế:

* Mỗi trạng thái đơn lẻ luôn có hiệu ứng mặc định riêng (WET làm trơn trượt, BURNING gây sát thương theo thời gian...), áp dụng độc lập nếu không có luật tổ hợp nào khớp.
* Chỉ định nghĩa thêm "composite reaction" (như STEAM FOREST) cho những tổ hợp **có giá trị thiết kế rõ ràng** — tạo cảm giác bất ngờ, mở ra chiến thuật mới, hoặc đã được liệt kê cụ thể trong tài liệu này. Mở rộng danh sách này dần theo thời gian, không cố làm hết một lần ở Giai đoạn 0.
* Vẫn cần quy định: thời gian suy giảm của từng trạng thái đơn lẻ (BURNING tự tắt sau bao lâu, WET khô sau bao lâu), và thứ tự ưu tiên đánh giá khi nhiều luật tổ hợp có thể cùng khớp trên một ô.

---

## VII. HỆ THỐNG BUILD SKILL

Người chơi mang tối đa 4 kỹ năng. Không giới hạn nguyên tố. Khuyến khích phối hợp đa hệ. Không áp dụng hình phạt đồng hệ. Người chơi được tự do sáng tạo lối chơi.

---

## VIII. CHẾ ĐỘ CAMPAIGN

**Giai đoạn 0:** Offline 100%, không wave, không upgrade. Vào thẳng Arena, đánh Boss, thắng hoặc thua.

**Giai đoạn 1 trở đi:** Bổ sung cấu trúc đầy đủ —

* Arena cố định, quái xuất hiện theo đợt, người chơi vượt sóng quái
* Sau mỗi Wave: chọn 1 trong 3 nâng cấp ngẫu nhiên (ví dụ +20% sát thương Thủy, +1 lần phản xạ, +15% tốc độ hồi thể lực), chỉ tồn tại trong trận hiện tại

---

## IX. ĐỊNH HƯỚNG ĐỒ HỌA

Phong cách: Fantasy phương Đông. Không theo hướng hoạt hình hài hước. Không theo hướng MMORPG truyền thống.

Nguồn cảm hứng: Ori and the Blind Forest, Battlerite, Nine Sols, Tru Tiên, Tiên Kiếm.

Ưu tiên: Shader đẹp hơn số lượng texture. Hiệu ứng sống động hơn mô hình phức tạp.

Mỗi nguyên tố phải có bản sắc riêng:

* Thủy: Mềm mại - linh động - phát sáng
* Hỏa: Mạnh mẽ - bùng nổ
* Mộc: Sinh trưởng - lan rộng
* Thổ: Nặng nề - vững chắc
* Kim: Sắc bén - tốc độ cao

**Lưu ý hiệu năng Android:** Motion Blur, dư ảnh nguyên tố, Cell Grid đa trạng thái cập nhật real-time, và hiệu ứng Đấu Pháp đều có thể nặng cho GPU Android tầm trung/thấp. Nên đặt ngân sách hiệu năng (frame budget) ngay từ Giai đoạn 0 và test trên thiết bị Android tầm trung thật.

---

# PHẦN B — LỘ TRÌNH PHÁT TRIỂN & KỸ THUẬT

## X. CHIẾN LƯỢC PHÁT TRIỂN TỔNG THỂ

Rủi ro lớn nhất của dự án không nằm ở kỹ thuật mạng, mà ở câu hỏi: **combat, Đấu Pháp và hệ tương tác 4 tầng có thật sự "đã tay" không?** Nếu câu trả lời là không, mọi nỗ lực networking, ranking, hay nội dung mở rộng đều vô nghĩa.

Lộ trình chia thành 4 giai đoạn, trong đó có một **Giai đoạn 0 hoàn toàn offline, không networking**, dùng để kiểm chứng phần cốt lõi này trước khi đầu tư vào hạ tầng nhiều người chơi.

| Giai đoạn | Mục tiêu chính | Có networking? |
|---|---|---|
| 0 — Test nội bộ | Kiểm chứng combat feel, Đấu Pháp, và hệ tương tác 4 tầng | Không |
| 1 — MVP | Hoàn thiện vòng lặp chơi đơn + thêm PvP 1v1 | Có (peer-hosted) |
| 2 — Mở rộng | Thêm nội dung, PvP 2v2, boss ngũ hành | Có |
| 3 — Hoàn thiện | Ranking, trang phục, sự kiện mùa | Có (cân nhắc dedicated server cho ranked) |

---

## XI. GIAI ĐOẠN 0 — BẢN TEST NỘI BỘ

### Mục tiêu

Trả lời các câu hỏi sau, không bị nhiễu bởi bất kỳ hệ thống nào khác:

1. Combat một-người-một-boss có vui không (di chuyển, khinh công, dùng skill)?
2. Đấu Pháp (Tầng 2) có tạo cảm giác "đấu pháp thuật" thật sự không, hay chỉ là hiệu ứng đẹp mà không ảnh hưởng chiến thuật?
3. Hệ trạng thái môi trường đa lớp (Tầng 1 & 3) có tạo ra chiều sâu chiến thuật, hay người chơi không nhận ra/không quan tâm các tổ hợp?

### Phạm vi

* 1 Arena duy nhất
* 1 Boss duy nhất, pattern tấn công cố định, có thể đoán trước (không cần AI thông minh) — nên cho Boss đổi giữa 2-3 hệ nguyên tố theo từng phase trận đấu để khai thác hết bề mặt tương tác đã build
* **Không** wave system, **không** hệ thống nâng cấp ngẫu nhiên. Vào thẳng trận đấu với boss, thắng hoặc thua, hết.
* Đủ 5 hệ, 20 skill (nhờ module có sẵn trong raylib)
* Đấu Pháp giới hạn ở các cặp Projectile đã liệt kê cụ thể trong mục III (mở rộng dần, không cần làm hết 5×5 ngay)
* Hệ trạng thái môi trường đa lớp với một số composite reaction tiêu biểu đã liệt kê (STEAM, STEAM FOREST, MUD...), không cần định nghĩa hết mọi tổ hợp khả dĩ
* Khinh công đầy đủ kèm dư ảnh nguyên tố
* **Không networking dưới bất kỳ hình thức nào**

### Lưu ý khi build

* Combat Layer vẫn chạy fixed-tick, tách khỏi Render Layer ngay từ đầu (xem mục XV) — không đổi so với bản trước.
* Content không còn là nút thắt nhờ tooling có sẵn, nhưng **balance + độ phức tạp tổ hợp** là nút thắt mới: gợi ý bật dần từng hệ, rồi mới bật Đấu Pháp, rồi mới bật các composite reaction phức tạp — để dễ cô lập vấn đề thay vì bật hết và đánh giá cùng lúc.
* Vì Đấu Pháp cần phân giải tất định (deterministic) để chuẩn bị cho PvP ở Giai đoạn 1, nên viết logic phân giải va chạm skill theo cách thuần dữ liệu (input → output rõ ràng, không phụ thuộc trạng thái ẩn hay random không seed) ngay từ bây giờ.

### Tiêu chí hoàn thành (Definition of Done)

* Người chơi có thể đánh bại hoặc thua trước Boss trong một trận hoàn chỉnh
* Cả 20 skill (5 hệ) hoạt động đúng vai trò, có cảm giác khác biệt rõ ràng giữa các hệ
* Ít nhất các cặp Đấu Pháp đã liệt kê (Hỏa vs Thủy, Kim vs Mộc, Hỏa vs Mộc, Thổ vs Thủy, Kim vs Hỏa) hoạt động đúng và tạo cảm giác chiến thuật rõ ràng
* Ít nhất các composite reaction tiêu biểu (WET+PLANT, WET+PLANT+BURNING, WET+BURNING→STEAM) đã hoạt động và thử nghiệm thực tế trong trận
* Khinh công + dư ảnh nguyên tố hoạt động mượt, đúng bản sắc hệ
* Tự đặt câu hỏi sau khi chơi thử nhiều lần: "Nếu không có gì khác ngoài cái này, mình có muốn chơi tiếp không?" — nếu có, mới sang Giai đoạn 1.

---

## XII. GIAI ĐOẠN 1 — MVP

Chỉ bắt đầu sau khi Giai đoạn 0 đã chứng minh combat feel ổn.

### Mở rộng nội dung

* Nội dung skill, Đấu Pháp, và hệ trạng thái môi trường đã hoàn thành phần lõi từ Giai đoạn 0 — Giai đoạn 1 chủ yếu cân bằng lại dựa trên dữ liệu test nội bộ, và mở rộng thêm các composite reaction/cặp Đấu Pháp chưa làm ở Giai đoạn 0
* Hoàn thiện Chế độ Campaign: thêm wave system + nâng cấp ngẫu nhiên sau mỗi wave
* Hệ thống Build Skill: mang tối đa 4 kỹ năng, không giới hạn nguyên tố, không phạt đồng hệ

### Thêm PvP 1v1 — Peer-hosted qua Firebase

**Vai trò của Firebase:** chỉ dùng để matchmaking và signaling — không xử lý logic trận đấu. Với quy mô tối đa ~100 người chơi cùng lúc, gói miễn phí của Firebase dư sức đáp ứng.

**NAT traversal:** Firebase chỉ giúp hai client "tìm thấy nhau", chưa giúp họ kết nối trực tiếp được. Kết nối P2P thật cần STUN (miễn phí) và đôi khi TURN relay dự phòng khi xuyên NAT thất bại — thường gặp với mạng di động (carrier-grade NAT). **Khuyến nghị: đo tỷ lệ kết nối thất bại thực tế trên mạng di động trước khi commit hoàn toàn vào peer-hosted thuần.**

**Host advantage và rủi ro gian lận:** Host xử lý va chạm/sát thương/kết quả Đấu Pháp — vừa là người chơi vừa là trọng tài. Chấp nhận được ở PvP thường, nhưng là khoản nợ kỹ thuật cần xử lý trước khi có ranking (xem mục XIV).

**Tickrate và độ chính xác:** Logic 30Hz / render 60-120Hz / nội suy tuyến tính. Vì khinh công và Đấu Pháp đều đòi hỏi timing chính xác, nên kiểm tra cảm giác thực tế qua mạng sớm, có input buffering để bù độ trễ.

---

## XIII. GIAI ĐOẠN 2

* 30 kỹ năng
* PvP 2v2
* 3 bản đồ
* Boss Ngũ Hành (phối hợp nhiều hệ, kế thừa trực tiếp từ Boss đổi hệ theo phase đã test ở Giai đoạn 0)
* Roguelike nâng cao

**Lưu ý kỹ thuật:** PvP 2v2 peer-hosted phức tạp hơn 1v1 đáng kể khi một người rời trận (host migration). Nên thiết kế phương án "trận tự kết thúc nếu host rời" thay vì cố gắng chuyển host giữa chừng.

---

## XIV. GIAI ĐOẠN 3

* 50 kỹ năng
* PvP 3v3
* Hệ thống xếp hạng
* Trang phục
* Sự kiện mùa

**Lưu ý quan trọng về Ranking:** Vì host tự xử lý kết quả trận đấu (bao gồm cả kết quả Đấu Pháp), đây là điểm yếu cho tính công bằng của ranked match. Trước khi ra mắt ranking, cân nhắc: (a) chỉ casual/campaign dùng peer-hosted, ranked dùng dedicated server nhẹ để xác thực kết quả; hoặc (b) thêm cơ chế phát hiện gian lận cơ bản bằng cách so sánh log trận đấu giữa hai client.

---

## XV. KIẾN TRÚC PHẦN MỀM

Tầng 1: Network Layer
Tầng 2: Game State Layer
Tầng 3: Combat Layer
Tầng 4: Physics & AI Layer
Tầng 5: Rendering Layer

Mỗi tầng hoạt động độc lập. Render không ảnh hưởng Gameplay. Gameplay không phụ thuộc FPS.

**Áp dụng từ Giai đoạn 0:** nên giữ kỷ luật tách Render khỏi Gameplay ngay từ bản test offline đầu tiên, vì retrofit kiến trúc fixed-tick vào một game đã quen chạy theo frame tốn công hơn nhiều so với làm đúng từ đầu.

**Lưu ý riêng cho raylib:** raylib không có sẵn lớp networking — khi sang Giai đoạn 1 sẽ cần tích hợp thư viện ngoài (ví dụ ENet cho UDP). Nên định nghĩa interface của Network Layer ngay từ Giai đoạn 0 (kể cả khi bên trong chỉ là implementation rỗng/local), để Game State Layer và Combat Layer gọi qua interface đó thay vì gọi thẳng logic local.

**Lưu ý riêng cho 4 tầng tương tác (mục IV):** Skill↔Environment, Skill↔Skill (Đấu Pháp), Environment↔Environment nên là 3 subsystem riêng biệt trong Combat Layer, có interface rõ ràng giữa chúng, thay vì gộp chung thành một hệ thống lớn khó kiểm soát.

---

## XVI. HỆ THỐNG NETWORK (áp dụng từ Giai đoạn 1)

### Tickrate

Logic: 30Hz cố định
Render: 60~120Hz

### Đồng bộ dữ liệu

Client gửi: Input, hướng xoay, kích hoạt skill
Host xử lý: Va chạm, sát thương, cooldown, stamina, trạng thái, kết quả Đấu Pháp
Client chỉ hiển thị kết quả.

### Nội suy

Linear Interpolation để làm mượt vị trí giữa các gói tin.

### Mô hình kết nối: Peer-hosted + Firebase signaling

* Firebase: matchmaking + trao đổi thông tin kết nối (signaling)
* STUN (miễn phí): xuyên NAT để kết nối P2P trực tiếp
* TURN (có chi phí khi cần, nhưng nhỏ ở quy mô 100 ccu): relay dự phòng khi STUN thất bại
* Rủi ro đã ghi nhận: host advantage nhẹ, khả năng gian lận phía host (xem mục XII, XIV)

---

## XVII. RỦI RO CẦN THEO DÕI (TỔNG HỢP)

| Rủi ro | Giai đoạn ảnh hưởng | Hướng xử lý |
|---|---|---|
| Combat/Đấu Pháp/hệ tương tác không đủ hấp dẫn | 0 | Test sớm; bật dần từng hệ thống (skill → Đấu Pháp → composite reaction) để cô lập vấn đề |
| Đấu Pháp mở rộng vô hạn nếu định nghĩa luật cho mọi cặp skill | 0 | Giới hạn ở skill loại Projectile trước, mở rộng dần theo giá trị thiết kế |
| Trạng thái môi trường đa lớp tăng tổ hợp theo cấp số nhân | 0 | Chỉ định nghĩa composite reaction có giá trị rõ ràng, còn lại dùng hiệu ứng mặc định của từng trạng thái đơn |
| Tính tất định của Đấu Pháp chưa đảm bảo cho PvP sau này | 0 → 1 | Viết logic phân giải va chạm thuần dữ liệu ngay từ Giai đoạn 0 |
| NAT traversal thất bại trên mạng di động | 1 | Đo tỷ lệ thất bại thực tế, chuẩn bị TURN dự phòng |
| Host advantage / gian lận phía host | 1, đặc biệt 3 | Chấp nhận ở PvP thường, cần xử lý riêng cho ranked |
| raylib không có sẵn networking | 1 | Định nghĩa interface Network Layer từ Giai đoạn 0, chọn thư viện (ví dụ ENet) trước khi code PvP |
| Hiệu năng shader trên Android tầm trung/thấp | 0 trở đi | Đặt ngân sách hiệu năng sớm, test trên máy thật |
| Điều khiển cảm ứng cho combat 4-skill + né chiêu + Đấu Pháp | 1 (khi build UI mobile) | Cần prototype UI điều khiển riêng, đây là bài toán UX khó |
| Host migration trong PvP nhiều người (2v2, 3v3) | 2 | Thiết kế phương án "trận tự kết thúc khi host rời" thay vì chuyển host giữa chừng |

---

## XVIII. MỤC TIÊU HOÀN THÀNH PHIÊN BẢN 1.0 (sau Giai đoạn 1)

* 1 Arena (mở rộng từ Giai đoạn 0)
* 1 Campaign đầy đủ (wave + upgrade)
* 1 PvP 1v1 (peer-hosted qua Firebase)
* 20 Skill, 5 Hệ
* Đấu Pháp (Tầng 2) cho các cặp Projectile chính
* Hệ trạng thái môi trường đa lớp với composite reaction tiêu biểu
* Khinh Công
* Shader Ngũ Hành
* Android + PC
* Có thể chơi hoàn chỉnh từ đầu tới cuối, đã được kiểm chứng combat feel và Đấu Pháp từ Giai đoạn 0 trước khi mở rộng
