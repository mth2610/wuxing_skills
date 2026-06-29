# DOC_MAINTENANCE.md
> Quy tắc bắt buộc khi cập nhật `WUXING_SKILL_CORE_API.md` (hoặc bất kỳ tài liệu API nào khác trong project). Áp dụng cho cả người và AI — nhưng đặc biệt bắt buộc với AI, vì AI có xu hướng viết mọi câu với giọng văn tự tin đồng nhất, bất kể đó là fact đã verify hay chỉ là suy luận hợp lý.

---

## 1. Phân loại nguồn gốc thông tin — bắt buộc đánh dấu

Mọi tuyên bố kỹ thuật mới thêm vào tài liệu phải thuộc đúng 1 trong 3 cấp độ sau:

### Cấp 1 — Ground-truth
Đọc trực tiếp từ source thật (`.h`, `.c`, `.glsl`) đã được cung cấp hoặc đọc được trong phiên làm việc.
→ Viết thẳng như fact, **không cần** đánh dấu gì thêm.

### Cấp 2 — Inferred (suy luận)
Suy ra từ việc đọc code mẫu/sample (vd: 1 skill cụ thể) khi **chưa** đọc được source gốc định nghĩa hành vi đó (vd: chưa đọc `procedural_mesh_utils.c` nhưng suy UV convention từ cách `tube.vs` dùng `vertexTexCoord`).
→ Bắt buộc bọc trong block cảnh báo, ghi rõ chữ **"(inferred — confirm against `<file>` source)"**:

```markdown
> [!NOTE]
> **(inferred — confirm against `procedural_mesh_utils.c`):** ...nội dung suy luận...
```

### Cấp 3 — Convention / Decision
Quy ước do người hoặc team chọn, không thể suy ra được từ đọc code (vd: hướng sáng `lightDir` hard-code, quy tắc đặt tên thư mục).
→ Đánh dấu rõ là quy ước, không phải hành vi tự nhiên của engine:

```markdown
> [!NOTE]
> **(project convention):** ...nội dung quy ước...
```

**Vì sao bắt buộc:** Nếu không phân loại, một AI đọc lại tài liệu trong phiên làm việc sau sẽ coi "inferred" ngang hàng với "ground-truth" và tiếp tục lan truyền sai sót — đây chính là lỗi đã xảy ra ở các phiên bản trước của `WUXING_SKILL_CORE_API.md` (mục GLSL Shader Guidelines), trước khi có file `vs_header.glsl`/`fs_header.glsl`/`lighting.glsl` thật để đối chiếu.

---

## 2. Mỗi tuyên bố kỹ thuật phải trỏ về đúng 1 file nguồn

Không viết chung dạng "Provided by the engine" hay "Handled automatically". Luôn ghi rõ **tên file cụ thể** (`vs_header.glsl`, `lighting.glsl`, `force_field.c`...).

Lý do: khi file nguồn đó thay đổi trong tương lai, người/AI cập nhật tài liệu biết ngay cần tra lại đúng đoạn nào trong `.md`, tra theo tên file — thay vì phải đọc lại toàn bộ tài liệu để đoán đoạn nào bị ảnh hưởng.

---

## 3. Cấm tự suy luận rồi viết như fact khi thiếu source

Đây là quy tắc quan trọng nhất dành cho AI.

Khi được yêu cầu cập nhật/bổ sung tài liệu mà **không có** file nguồn liên quan trong tay, AI **không được** tự nội suy hành vi rồi viết với giọng khẳng định. Phải chọn 1 trong 3 hướng:

1. **Hỏi người dùng** xin file nguồn liên quan trước khi viết.
2. Nếu người dùng xác nhận muốn cứ viết tạm dựa trên suy luận → viết theo đúng format Cấp 2 ở mục 1, không được bỏ qua disclaimer.
3. Nếu không chắc và không cần viết ngay → để nguyên nội dung cũ, gắn comment:
   ```html
   <!-- TODO: verify against force_field.c — chưa đọc source, mục này đang dựa trên header comment -->
   ```

**Không được làm:** âm thầm "làm tròn" suy luận thành câu khẳng định để tài liệu trông liền mạch, chuyên nghiệp hơn. Văn phong liền mạch không quan trọng hơn độ chính xác.

---

## 4. Patch Log — ghi lại mỗi lần sửa

Cuối file `WUXING_SKILL_CORE_API.md` (hoặc tài liệu tương đương) cần có bảng patch log, cập nhật mỗi lần có thay đổi:

```markdown
## Patch Log

| Ngày | Người/AI sửa | Mục sửa | Dựa trên nguồn nào | Cấp độ |
|---|---|---|---|---|
| 2026-06-29 | Claude | §10 GLSL Shader Guidelines | vs_header.glsl, fs_header.glsl, lighting.glsl (đọc trực tiếp) | Ground-truth |
| 2026-06-29 | Claude | §9 Procedural Mesh — UV convention của Tube | suy luận từ tube.vs/tube.fs | Inferred — chưa confirm |
```

Mục đích: khi phát hiện tài liệu sai ở đâu đó, có thể truy ngược ngay đoạn đó được thêm vào dựa trên fact hay giả định, từ phiên làm việc nào, để biết mức độ tin cậy mà không phải hỏi lại từ đầu.

---

## 5. Khi có mâu thuẫn giữa các nguồn — thứ tự ưu tiên

Khi phát hiện mâu thuẫn (vd: code mẫu của 1 skill làm khác với tài liệu, hoặc 2 skill mẫu làm khác nhau), áp dụng thứ tự ưu tiên sau, **không** mặc định tài liệu luôn đúng hoặc code mẫu luôn đúng:

1. **Core engine source** (`core/*.h`, `core/*.c`, `core/shaders/common/*.glsl`) — nguồn gần engine nhất, đáng tin nhất.
2. **Sample skill đã được xác nhận là chuẩn** (vd: skill được đánh dấu rõ là reference implementation).
3. **Tài liệu `.md`** — chỉ nên là bản tóm tắt của 2 nguồn trên, không phải nguồn chân lý độc lập.

Khi sửa, phải xét xem mâu thuẫn đó là do **tài liệu lỗi thời** hay do **code mẫu đang làm sai/lỗi thời theo rule mới** — không tự động giả định một phía luôn đúng. Nếu không chắc, hỏi người dùng thay vì chọn đại 1 phía để sửa.

---

## 6. Giới hạn phạm vi sửa — không tự ý viết lại toàn bộ

Khi được yêu cầu cập nhật **một phần cụ thể** của tài liệu, AI chỉ sửa đúng phạm vi đó. Các mục khác — dù AI nhận thấy có thể cải thiện, có thiếu sót, hoặc có cơ hội "viết lại cho gọn" — phải **giữ nguyên**.

Nếu thấy vấn đề ở mục khác ngoài phạm vi được yêu cầu: nêu ra như một **đề xuất riêng** ở cuối câu trả lời, không tự động chỉnh sửa vào file.

Lý do: phạm vi sửa lan rộng ngoài yêu cầu làm diff khó review, dễ lẫn lộn giữa thay đổi được yêu cầu và thay đổi AI tự quyết, và tăng rủi ro đưa nội dung suy luận (Cấp 2) vào những chỗ người dùng chưa kịp duyệt.

---

## 7. Checklist nhanh trước khi commit một bản sửa tài liệu

- [ ] Mọi câu mới thêm đã được gắn đúng cấp độ (Ground-truth / Inferred / Convention)?
- [ ] Mọi tuyên bố có trỏ rõ về 1 file nguồn cụ thể (nếu là ground-truth)?
- [ ] Không có đoạn nào suy luận bị viết như fact tuyệt đối?
- [ ] Đã cập nhật Patch Log?
- [ ] Phạm vi sửa có đúng với những gì được yêu cầu, không lan ra ngoài?
- [ ] Nếu có mâu thuẫn giữa nguồn, đã áp dụng đúng thứ tự ưu tiên ở mục 5, hoặc đã hỏi lại người dùng khi không chắc?
