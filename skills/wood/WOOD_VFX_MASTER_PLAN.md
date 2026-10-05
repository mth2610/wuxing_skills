# KẾ HOẠCH TỔNG THỂ HỆ MỘC: 10 PRIMARY VFX & 2 COMPLETE SKILLS (AAA QUALITY)

---

## 1. TRIẾT LÝ NGHỆ THUẬT & QUY TẮC HIỂN THỊ (WUXING ART DIRECTION)

Tham chiếu: `WUXING_ART_DIRECTION.md` và `third_party/vulkan/docs/BRIGHT_BACKGROUND_VFX_SPEC.md`.

- **Bản sắc cốt lõi Hệ Mộc (Wood - Life)**:
  - *Không có gì mọc tức thì*: Vạn vật sinh sôi từ lòng đất, mở rộng hữu cơ (Organic Growth). Chuyển động luôn uốn cong sinh động, không dùng hình học thẳng tắp hay góc chết cơ khí.
  - *4 Động từ cốt lõi*: **Mọc (Grow)** $\rightarrow$ **Quấn (Wrap/Constrict)** $\rightarrow$ **Nở (Bloom)** $\rightarrow$ **Tàn (Wither/Decay)**.
- **Tiêu chuẩn chất lượng AAA & Tương phản nền sáng (Bright-Background Spec)**:
  - Hiệu ứng phải giữ vững hình khối (silhouette), độ tương phản nội tại (internal structure) và màu sắc đặc trưng trên cả 5 loại nền (tối, xám trung tính, trắng chói, nền ấm, nền lạnh).
  - Tách bạch 3 lớp:
    1. *Coverage / Body (AlphaComposite `ONE, ONE_MINUS_SRC_ALPHA`)*: Thân cây, cành lá, vỏ gỗ có độ cản quang thực tế, làm tối nền sáng (Darken% > 0) để tạo độ nổi khối.
    2. *Radiance / Core (HDR Emission)*: Nhựa sống ngọc bích phát sáng (`Sap Pulse`), nhụy hoa phát quang, phấn hoa bồng bềnh.
    3. *Optical Spread (Bloom)*: Tán xạ ánh sáng qua hệ thống Bloom Pyramid 6 tầng, tuyệt đối không lạm dụng additive để biến hiệu ứng thành một đốm trắng vô định.
  - Hiệu ứng tàn lụi (`Wither`): Cây cỏ không bao giờ biến mất đột ngột; lá úa vàng/nâu, rũ xuống theo trọng lực, cuộn lại và tan thành thảm lá rụng hoặc tro bụi dinh dưỡng.

---

## 2. THIẾT KẾ 2 SKILL HOÀN CHỈNH (COMPLETE SKILLS)

### 2.1. Skill 1: Ma Đằng Thắt Hồn (Demon Vine Constriction)
- **Archetype**: `ground` (AoE Target Control).
- **Ý đồ Gameplay**: Trói chân khống chế cứng (Root/Stun), kéo siết kẻ địch về tâm, gây sát thương nghiền nát theo nhịp xung nhựa cây, phát nổ văng mảnh vụn gỗ và gai khi kết thúc.
- **Timeline 7 Pha Kịch Tính (Narrative Flow)**:
  1. *Birth (0.0s - 0.2s)*: Dấu hiệu cảnh báo (telegraph decal) nứt đất xanh rêu tỏa ra dưới chân mục tiêu kèm đất đá văng nhẹ (`WoodGroundBloomDecal`).
  2. *Gather (0.2s - 0.5s)*: 3–4 dây leo lớn trồi lên khỏi mặt đất theo thuật toán random walk hướng tâm, bám sát bề mặt địa hình.
  3. *Tension (0.5s - 0.9s)*: Dây leo cuộn xoắn quanh mục tiêu theo đường xoắn ốc logarithmic $R(s) = a \cdot e^{b\theta}$, bán kính co dần tạo cảm giác siết chặt (có lò xo đàn hồi nảy nhẹ).
  4. *Release / Spike (0.9s - 1.2s)*: Hàng loạt gai nhọn uốn cong móc ngược bung ra (`easeOutBack` overshoot) dọc theo thân dây theo góc vàng Phyllotaxis. Sát thương lần đầu kích hoạt.
  5. *Hold / Constrict (1.2s - 2.5s)*: Xung nhựa cây (`SapPulse`) màu ngọc bích chạy dọc thân dây leo, rung lắc vi mô theo nhịp siết. Khói độc/bào tử rỉ ra từ vết siết.
  6. *Wither / Fracture (2.5s - 3.0s)*: Dây leo căng đứt gãy hoặc tàn lụi: thân cây gãy vụn bắn ra dăm bào gỗ (`WoodChip`), gai văng ra xung quanh, vỏ cây chuyển nâu vàng và biến mất thành thảm lá rụng.
  7. *Fade (3.0s - 3.5s)*: Khí độc mỏng và bụi đất tan dần.

### 2.2. Skill 2: Cổ Thụ Trường Sinh (Ancient Tree Sanctuary)
- **Archetype**: `ground` (Domain AoE Sanctuary).
- **Ý đồ Gameplay**: Tạo lập lãnh địa thần mộc. Cổ thụ trồi lên vững chãi, tán cây che chở hồi máu liên tục cho đồng minh, rễ ngầm cản bước kẻ thù, khi tàn tạo đợt nổ bão lá thanh tẩy.
- **Timeline 7 Pha Kịch Tính (Narrative Flow)**:
  1. *Birth (0.0s - 0.3s)*: Một quả cầu hạt mầm linh mộc đáp xuống đất. Vòng tròn rễ cây ngầm phát sáng lan tỏa từ tâm ra chu vi 8m bằng kỹ thuật Arrival-Time Decal.
  2. *Gather / Eruption (0.3s - 0.9s)*: Đất đá phồng lên nứt toác, thân cây đại thụ xù xì, gân guốc vặn xoắn trồi lên theo trục $Y$, các rễ con bám chặt xuống đất.
  3. *Tension / Branching (0.9s - 1.4s)*: Thân cây đạt độ cao 4.5m, các cành nhánh cấp 1 bung ra theo phân cấp (cành con chỉ mọc khi cành mẹ đã phát triển tới điểm phân nhánh).
  4. *Release / Canopy Bloom (1.4s - 1.8s)*: Tán lá rậm rạp xanh ngắt bung tỏa, đón gió uốn lượn. Các nụ hoa linh mộc khổng lồ ở đầu cành nở bung theo đường xoắn ốc Vogel, phát ra luồng sóng ánh sáng xanh ngọc (`ShockwaveRing`).
  5. *Active / Sanctuary (1.8s - 5.0s)*: Lãnh địa cổ thụ hoạt động:
     - Thảm mầm non (`SproutField`) mọc phủ xanh mặt đất bên dưới.
     - Đám mây phấn hoa / bào tử lấp lánh (`SporeWisp`) bay lơ lửng bồng bềnh hướng lên vòm trời.
     - Những chiếc lá cây chao liệng rơi xuống đất theo quy luật khí động học lá bay (`LeafPetalSwarm`).
     - Hai luồng xung năng lượng nhựa sống (`SapPulse`) luân chuyển liên tục từ rễ lên tán lá.
  6. *Wither / Transcendence (5.0s - 6.0s)*: Cổ thụ không biến mất đột ngột: Toàn bộ tán lá chuyển dần sang sắc vàng thu, rũ xuống theo trọng lực, gió cuốn tán lá thành vòng xoáy thanh tẩy; thân gỗ rạn nứt thành ánh sáng tro tàn và hòa vào lòng đất.
  7. *Fade (6.0s - 6.5s)*: Chỉ còn lại thảm rêu xanh và vài đốm phấn hoa bay lượn rồi tắt hẳn.

---

## 3. DANH SÁCH 10 PRIMARY WOOD VFX (AAA BUILDING BLOCKS)

| STT | Primary VFX Primitive | Loại hình | Thuật toán / Nguyên lý kỹ thuật cốt lõi (AAA) | File dự kiến |
|---|---|---|---|---|
| **1** | `VFX_Wood_RMF_Tube` | Mesh Sweep | **Rotation Minimizing Frame (Double Reflection - Wang 2008)**: Khắc phục triệt để lỗi xoắn vặn Frenet. Đường cong Catmull-Rom/Bezier mượt. Bán kính taper bướu đốt $r(t) = r_0(1-t)^k(1+\text{knot})$. Shader mọc phân cấp qua `aArc`, `aBirth`, `aSpan`. | `core/geometry/pm_rmf_tube.inl` |
| **2** | `VFX_Wood_PhyllotaxisThorns` | Instanced Mesh | **Phyllotaxis Golden Angle ($137.508^\circ$)**: Bố trí gai tự nhiên quanh thân dây. Hình học nón móc cong Bezier. Diễn hoạt nảy `easeOutBack` có overshoot. | `core/composition/wood/vc_wood_thorns.inl` |
| **3** | `VFX_Wood_TranslucentLeaves` | Instanced Mesh | **Crytek 3-Tier Wind Hierarchy (GPU Gems 3)**: Uốn cành (R), dao động nhánh (G), rung mép lá (B). Shading 2 mặt với truyền sáng Subsurface Translucency $\text{pow}(\text{dot}(-L, V), p) \cdot \text{thinness}$. | `core/composition/wood/vc_wood_leaves.inl` |
| **4** | `VFX_Wood_VogelBloom` | Procedural Mesh | **Vogel Spiral ($\theta_i = i \cdot 2.39996\text{ rad}, r_i = c\sqrt{i}$)**: Cánh hoa chén Bezier xếp lớp. Diễn hoạt nở tuần tự từ ngoài vào trong bằng ma trận quay Rodrigues. | `core/composition/wood/vc_wood_bloom.inl` |
| **5** | `VFX_Wood_ApicalSprout` | Procedural Mesh | **Apical Hook Uncurling**: Mầm cây trồi đất từ tư thế gập móc câu vươn thẳng. 2 lá mầm mở ra. Đĩa đất phồng vertex displacement + hạt đất văng. | `core/composition/wood/vc_wood_sprout.inl` |
| **6** | `VFX_Wood_LeafPetalSwarm` | GPU/CPU Particle | **Aerodynamic Leaf Flutter Physics**: Tốc độ rơi phụ thuộc góc nghiêng $v_{\text{fall}} = v_t \cdot \text{lerp}(0.35, 1.0, \|\sin\theta\|)$ và trượt ngang $\sin(2\theta)$. Trường lực Curl Noise + Wind. | `core/composition/wood/vc_wood_particles.inl` |
| **7** | `VFX_Wood_SporePollenWisp` | Particle Emitter | **Brownian Motion + Buoyancy**: Hạt phấn hoa bay bổng nhẹ nhàng. Độ sáng nhấp nháy đa pha $\text{pow}(0.5 + 0.5\sin(\omega t + \phi), 4.0)$, lõi phát quang HDR. | `core/composition/wood/vc_wood_spores.inl` |
| **8** | `VFX_Wood_WoodChips` | Instanced Debris | **Voronoi Elongated Splinters**: Mảnh dăm bào mỏng dài theo thớ gỗ. Quỹ đạo nón đạn đạo, va chạm đàn hồi mặt đất ($e \approx 0.35$) kèm xoay góc tốc độ cao. | `core/composition/wood/vc_wood_debris.inl` |
| **9** | `VFX_Wood_ArrivalGroundBloom` | Dynamic Decal | **Arrival-Time Root Network Decal**: Texture bake thời gian tới (R), độ dày (G), nhiễu vỏ (B). Mép nhựa sống dẫn đường $\text{growth} - \text{arrival} < w$. | `core/decals/wood_root_decal.inl` |
| **10** | `wood_fx.glsl` | Shader Chunk | **Shared Wood Modifiers**: Module hàm chung cho Vertex & Fragment: `Wood_GrowthOffset`, `Wood_SwayHierarchy`, `Wood_SapPulse`, `Wood_WitherColor`, `Wood_Translucency`. | `core/shaders/common/wood_fx.glsl` |

---

## 4. CHI TIẾT THUẬT TOÁN KỸ THUẬT & TOÁN HỌC

### 4.1. Khung Tham Chiếu RMF (Double Reflection Method - Wang 2008)
Cho đoạn đường cong với điểm $\mathbf{x}_0$, vector trục $\mathbf{r}_0$, tiếp tuyến $\mathbf{t}_0$ và điểm tiếp theo $\mathbf{x}_1$, tiếp tuyến $\mathbf{t}_1$:
$$\mathbf{v}_1 = \mathbf{x}_1 - \mathbf{x}_0, \quad c_1 = \mathbf{v}_1 \cdot \mathbf{v}_1$$
$$\mathbf{r}_0^L = \mathbf{r}_0 - \frac{2}{c_1}(\mathbf{v}_1 \cdot \mathbf{r}_0)\mathbf{v}_1, \quad \mathbf{t}_0^L = \mathbf{t}_0 - \frac{2}{c_1}(\mathbf{v}_1 \cdot \mathbf{t}_0)\mathbf{v}_1$$
$$\mathbf{v}_2 = \mathbf{t}_1 - \mathbf{t}_0^L, \quad c_2 = \mathbf{v}_2 \cdot \mathbf{v}_2$$
$$\mathbf{r}_1 = \mathbf{r}_0^L - \frac{2}{c_2}(\mathbf{v}_2 \cdot \mathbf{r}_0^L)\mathbf{v}_2, \quad \mathbf{s}_1 = \mathbf{t}_1 \times \mathbf{r}_1$$
Kết quả: Khung trực chuẩn $(\mathbf{t}_1, \mathbf{r}_1, \mathbf{s}_1)$ chuyển tiếp mượt mà dọc đường cong, hoàn toàn triệt tiêu hiện tượng xoắn vặn (twist) và bất biến khi đường cong thẳng tắp.

### 4.2. Mọc Phân Cấp Dọc Thân & Đầu Búp Bằng Vertex Shader
Vertex mang theo thuộc tính:
- `aArc`: Tọa độ chuẩn hóa dọc nhánh $[0.0, 1.0]$.
- `aBirth`: Thời điểm nhánh này bắt đầu mọc trong cây phân cấp.
- `aSpan`: Khoảng thời gian để nhánh này mọc hoàn thiện.
- `aCenterPos`: Tâm đường tim của vành lát cắt.

```glsl
float localProgress = clamp((u_growth - aBirth) / aSpan, 0.0, 1.0);
float k = smoothstep(localProgress - u_tipLength, localProgress, aArc);
// Co ngọn búp nhọn tự nhiên, phần chưa mọc co về tâm hoặc gốc
vec3 displacedPos = aCenterPos + (vertexPosition - aCenterPos) * (1.0 - k * k);
```

### 4.3. Động Học Lá Bay (Aerodynamic Flutter Physics)
Dao động góc nghiêng $pitch = A \cdot \sin(\omega t + \phi_i)$.
Tốc độ rơi điều chỉnh theo diện tích cản gió:
$$v_{\text{fall}} = v_t \cdot \text{lerp}(0.35, 1.0, |\sin(pitch)|)$$
Lực lướt khí động học ngang vuông góc với trục nghiêng:
$$v_{\text{glide}} = v_{\text{lift}} \cdot \sin(2 \cdot pitch) \cdot \mathbf{d}_{\text{tilt}}$$

### 4.4. Truyền Sáng Lá & Cánh Hoa (Subsurface Translucency)
Trong fragment shader:
$$I_{\text{trans}} = \text{pow}(\text{clamp}(\mathbf{L} \cdot -\mathbf{V}, 0.0, 1.0), 3.0) \cdot \text{thickness} \cdot \text{leafColor}$$
Cho phép lá mỏng phát sáng màu ngọc bích khi ngược sáng mặt trời.

---

## 5. THỨ TỰ THỰC HIỆN TỪNG BƯỚC

1. **Bước 1 (Đang thực hiện)**: Tạo `core/shaders/common/wood_fx.glsl` & Module hình học RMF Tube `core/geometry/pm_rmf_tube.inl` (hỗ trợ bán kính taper, bướu đốt vỏ cây, mọc bằng shader, RMF Wang 2008).
2. **Bước 2**: Gai Phyllotaxis `vc_wood_thorns.inl` & Lá phân tầng gió `vc_wood_leaves.inl`.
3. **Bước 3**: Hoa nở Vogel `vc_wood_bloom.inl` & Mầm cây `vc_wood_sprout.inl`.
4. **Bước 4**: Bầy lá lượn `vc_wood_particles.inl` & Bào tử phấn hoa `vc_wood_spores.inl`.
5. **Bước 5**: Dăm bào gỗ `vc_wood_debris.inl` & Decal rễ Arrival-Time `wood_root_decal.inl`.
6. **Bước 6**: Tích hợp Fixture vào `sandbox/vfx_test.c` (NEW FX Wood) và kiểm thử ma trận `scripts/render_vfx_matrix.sh`.
7. **Bước 7**: Ghép Skill 1 - Ma Đằng Thắt Hồn (`demon_vine_skill`).
8. **Bước 8**: Ghép Skill 2 - Cổ Thụ Trường Sinh (`ancient_tree_skill`).
9. **Bước 9**: Cân chỉnh Tunables và kiểm thử tự động `WUXING_AUTOTEST=1`.

---

## 6. NGÂN SÁCH HIỆU NĂNG (BUDGET & CONSTRAINTS)

- **Không `malloc` lúc chạy**: Dùng pool tĩnh cố định.
- **Draw Call**: Gộp gai, lá, hoa, mầm qua Mesh Instancing ($\le 4$ draw calls).
- **Số lượng tam giác**: Mỗi dây leo $\le 450$ tris; toàn bộ cây cổ thụ $\le 2.500$ tris.
- **Tương thích**: Vulkan 1.1 `rlvk` (ưu tiên) + OpenGL 3.3 Core / GLES.
- **Ràng buộc tuyệt đối**: Không truy cập thư mục `unreal-engine-starter-content-main/`, `build/`, `_deps/`.
