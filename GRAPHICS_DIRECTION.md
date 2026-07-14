# GRAPHICS DIRECTION — Định hướng đồ họa tổng thể

> Chốt 13/07/2026. Đây là **đổi hướng lớn** so với art direction gốc
> (`nguhanhtyvo_kehoach.md`: low-poly + đêm + sương để CHE thiếu chi tiết &
> tối ưu Android). Hướng mới: **đẹp mãn nhãn, stylized-realism võ hiệp** —
> vẫn chạy được máy yếu / Android GLES. `WUXING_ART_DIRECTION.md` vẫn giữ
> nguyên (nó nói về VFX của skill); tài liệu này nói về **render surface +
> khí quyển + hậu kỳ** của toàn cảnh.

## 1. Look đích: Thiên Nhai Minh Nguyệt Đao (Moonlight Blade)

Stylized-**realism** võ hiệp — KHÔNG cel/anime:
- Nhân vật tỉ lệ thật, da/vải/kim loại **có chất liệu** nhưng lý tưởng hóa
  (sạch, mượt, ánh sáng đẹp). KHÔNG cel-ramp bậc, KHÔNG outline nét anime.
- Cái đẹp đến từ **material + khí quyển + hậu kỳ điện ảnh**, KHÔNG phải độ
  chính xác vật lý. Đây là lý do nó chạy được máy yếu.
- Chạy máy cùi nhờ **né real-time shadow + GI** (thứ đắt nhất). Bóng giả +
  ánh sáng baked/fake là đủ đẹp — đã được người dùng xác nhận là chủ ý.

## 2. Ràng buộc nền tảng: Android GLES (giữ nguyên)

- Shader phải chạy GLES (đã có kiến trúc 2-nhánh, xem memory
  `android-shader-pipeline`: matModel identity, precision mediump/highp,
  f-suffix...). Mỗi shader mới PHẢI có nhánh GLES.
- Forward rendering, ít đèn (1 mặt trời + ambient + fill). KHÔNG deferred
  MRT nặng.
- Hậu kỳ chọn lọc: bloom (đã có mip-chain), tone map, color grade, fog,
  god-ray fake đều rẻ. TRÁNH: DOF bokeh nặng, SSAO, volumetric raymarch
  thật (để PC-only hoặc bỏ).
- Ngân sách dư vì cảnh nhỏ (1 map, ≤8 người, minion đơn giản) → đổ vào chất
  lượng pixel, không phải số lượng.

## 3. Hiện trạng render (khảo sát 13/07)

- **Nhân vật: surface shader stylized ✅ (G2)** — thay shader mặc định UNLIT
  của raylib bằng `core/surface_material` + `surface_lit.vs/.fs`: half-Lambert
  + Blinn sheen + Fresnel rim mát (viền trăng) + fog, lấy đèn từ
  environment_system. Áp qua `SurfaceMaterial_Apply` khi load model. Nhân vật
  giờ có khối/đổ sáng thật, hết "mannequin". (Enemy/dummy vẫn hình que
  DrawCharacter3D tới khi có asset.)
- **Tone mapping + HDR ✅** — pipeline giờ là **true HDR**: scene buffer
  (`core/screen_distort.c` renderTex) + bloom pyramid + composite đều dùng
  16-bit half-float (R16G16B16A16), nên additive/emissive giữ giá trị > 1.0
  cho tới khi ACES filmic nén HDR→LDR ở composite. Có probe/fallback RGBA8 cho
  GLES2 (`ScreenDistort_IsHDR()` = cờ quyết định, `PostFX_IsHDR()` bám theo).
  Bloom đã có mip-chain (bright/downsample/upsample/blur) — nền tốt.
- Có: post-fx chain (bloom, chromatic, vignette, color grade), compute
  particle, VFX composition mạnh, environment (sun + ambient + fog + ToD),
  fake blob shadow. Tái dùng ~60%.

## 4. Lộ trình (Đợt G) — thứ tự theo tác động / công sức

| # | Đợt | Nội dung | Cần asset user? |
|---|---|---|---|
| **G1 ✅** | HDR pipeline + tone mapping | Scene/bloom/composite → RGBA16F float (true HDR); ACES filmic HDR→LDR; probe+fallback GLES2 | Không |
| **G2** | Surface shader nhân vật/môi trường | Half-Lambert mượt + Blinn specular + normal map + rim + fog. Thay shader mặc định. GLES 2-nhánh | Không (đẹp ngay trên model hiện có; normal map nâng thêm) |
| **G3** | Khí quyển võ hiệp | Height fog nâng cấp + god-ray/light-shaft (billboard, GLES-rẻ) + bụi/tàn lơ lửng + aerial perspective | Không |
| **G4** | Fake shadow chất lượng | Blob mềm gradient / projected đơn giản thay blob cứng | Không |
| **G5** | Color grading điện ảnh + nước stylized | LUT võ hiệp (trăng lạnh/hổ phách); nước fresnel+foam+distortion | Không |
| **G6** | Tích hợp asset chất lượng | Model võ hiệp + props môi trường (Synty/chợ) qua shader mới | **Có** |

G1-G5 là engine/shader (Claude làm, không chờ asset). G6 là lúc asset của
bạn vào và "nở hoa" nhờ nền shader đã dựng.

## 5. Pipeline asset (khuyến nghị)

- **Nhân vật**: model tỉ lệ thật kiểu võ hiệp — ArtStation/CGTrader/Sketchfab
  ("wuxia/xianxia/chinese warrior"), Synty POLYGON Oriental. **Ưu tiên
  model có normal map** để G2 phát huy. Rig/anim qua Mixamo. Xuất GLB.
- **Môi trường**: Synty POLYGON (Fantasy/Samurai/Oriental), Quaternius/Kenney
  (free) cho props stylized.
- **Ramp/LUT/gradient**: tự tạo Blender/GIMP.
- **Công cụ trung tâm**: Blender (free).
- KHÔNG dùng VRoid (đó là anime — sai hướng).

## 6. Nguyên tắc khi code (mọi shader mới)

1. Luôn có nhánh GLES + PC (Rules A-E, memory `android-shader-pipeline`).
2. Tone map là bước CUỐI của chain, sau bloom, trước color-grade UI.
3. Material shader mới đi qua `core/material/` hoặc shader riêng — KHÔNG hard-
   code màu (dùng texture/uniform).
4. Mọi hiệu ứng khí quyển phải rẻ trên GLES — fake trước, thật sau (nếu PC).
5. Giữ readability gameplay: đừng để hậu kỳ nuốt mất chỉ báo skill/phe.
