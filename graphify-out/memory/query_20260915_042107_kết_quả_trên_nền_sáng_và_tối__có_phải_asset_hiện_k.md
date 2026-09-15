---
type: "query"
date: "2026-09-15T04:21:07.371685+00:00"
question: "kết quả trên nền sáng và tối. Có phải asset hiện không đủ chất lượng? ở mỗi frame lửa quá ít biến đổi?"
contributor: "graphify"
outcome: "useful"
source_nodes: ["ParticleConfig", "flame_motion_test.c", "texture_packing_test.c", "Shader"]
---

# Q: kết quả trên nền sáng và tối. Có phải asset hiện không đủ chất lượng? ở mỗi frame lửa quá ít biến đổi?

## Answer

Expanded from original query via vocab: [flame, volume, particle, texture, motion, render, shader, emission]. Kết luận: đúng, asset hiện không đủ cho vai trò ngọn lửa chính. Nó là directionless radial puff, không phải directional flame tongue; silhouette phát sáng của hai frame kề nhau chồng trung bình 96.0%, median 97.3%, nên motion vectors chỉ làm mượt chứ không tạo thêm topology. Runtime làm lộ nhược điểm vì chỉ 18 body particles, dù tài liệu bake từng ghi giảm từ 90 xuống 26 khiến sprite/card lộ rõ. Ngoài ra shader xuất alpha 0 trong premultiplied pass, tức radiance additive thuần, nên nền sáng làm lửa bạc/pastel. Hướng đúng là bake asset pure directional flame tongue mới với emission/temperature + coverage và motion vectors; smoke vẫn là sub-emitter 6-way riêng.

## Outcome

- Signal: useful

## Source Nodes

- ParticleConfig
- flame_motion_test.c
- texture_packing_test.c
- Shader