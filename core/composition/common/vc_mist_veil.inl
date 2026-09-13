// ── vc_mist_veil.inl — VFX_ComposeMistVeil (Vụ Khí / Sương Ẩn Trận) ─────────
//
// A multi-layered atmospheric mist composition combining physical 3D
// participating media with procedural ground haze and ascending dew motes.
//
// DESIGN & AESTHETICS (Xianxia / Wuxia Mist Veil):
//   1. SEAMLESS TEXTURE: Generates an ultra-soft, organic harmonic mist puff
//      (s_mistSoftTex) with quartic edge falloff — zero hard edges, zero potato clumps!
//   2. CONTINUOUS GROUND VEIL: Spawns large, wide, low-opacity (alpha ~24, ~9%)
//      overlapping billows covering the entire ground disc including the center.
//      They fuse into one silky, undulating, semi-transparent blanket of mist.
//   3. TRUE 3D VOLUMETRIC RAYMARCHING: Calls FogVolume_SpawnTransient() so
//      sunlight cuts through the sphere to form natural Mie-scattered god-rays.
//   4. ASCENDING DEW CONDENSATION: Gentle, tiny, glistening micro-motes floating
//      upward within the mist shroud.

#include "core/resource_manager.h"
#include "environment/environment_system.h"

static Texture2D s_mistSoftTex = {0};
static Texture2D s_dewTex = {0};
static SkillCurve s_mistGrow = {0};
static SkillCurve s_mistFade = {0};
static SkillCurve s_dewFade = {0};
static bool s_mistVeilReady = false;

static void MistVeil_Init(void)
{
    if (s_mistVeilReady) return;

    // 1. Tạo texture sương mù mờ ảo mềm mại không góc cạnh (Quartic falloff + harmonic swirl)
    const int S = 128;
    Image mistImg = GenImageColor(S, S, BLANK);
    for (int y = 0; y < S; y++) {
        float ny = ((float)y + 0.5f) / (float)S * 2.0f - 1.0f;
        for (int x = 0; x < S; x++) {
            float nx = ((float)x + 0.5f) / (float)S * 2.0f - 1.0f;
            float r = sqrtf(nx * nx + ny * ny);
            if (r >= 1.0f) continue;

            // Đường dốc mượt bậc 4: biên tiếp xúc phẳng hoàn toàn bằng 0, không lộ viền quad
            float falloff = (1.0f - r * r);
            falloff = falloff * falloff;

            // Xoáy điều hòa nhẹ nhàng tạo dải khí bồng bềnh hữu cơ
            float angle = atan2f(ny, nx);
            float swirl = 1.0f + 0.20f * sinf(3.0f * angle + 2.5f * r)
                               + 0.12f * cosf(5.0f * angle - 3.2f * r);
            float density = falloff * swirl;
            if (density > 1.0f) density = 1.0f;
            if (density < 0.0f) density = 0.0f;

            unsigned char a = (unsigned char)(density * 255.0f);
            ImageDrawPixel(&mistImg, x, y, (Color){ 255, 255, 255, a });
        }
    }
    s_mistSoftTex = LoadTextureFromImage(mistImg);
    UnloadImage(mistImg);
    if (s_mistSoftTex.id != 0) {
        SetTextureFilter(s_mistSoftTex, TEXTURE_FILTER_BILINEAR);
    }

    // 2. Tạo texture hạt sương ngưng tụ hình tròn mềm (Gaussian falloff)
    const int D = 32;
    Image dewImg = GenImageColor(D, D, BLANK);
    for (int y = 0; y < D; y++) {
        float v = ((float)y + 0.5f) / (float)D * 2.0f - 1.0f;
        for (int x = 0; x < D; x++) {
            float u = ((float)x + 0.5f) / (float)D * 2.0f - 1.0f;
            float r2 = u * u + v * v;
            if (r2 < 1.0f) {
                float a = expf(-r2 * 4.0f);
                unsigned char val = (unsigned char)(a * 255.0f);
                ImageDrawPixel(&dewImg, x, y, (Color){ 255, 255, 255, val });
            }
        }
    }
    s_dewTex = LoadTextureFromImage(dewImg);
    UnloadImage(dewImg);
    if (s_dewTex.id != 0) {
        SetTextureFilter(s_dewTex, TEXTURE_FILTER_BILINEAR);
    }

    // 3. Đường cong độ lớn: Nở từ từ khi lan tỏa
    FloatCurve_AddStop(&s_mistGrow, 0.0f, 0.85f);
    FloatCurve_AddStop(&s_mistGrow, 0.35f, 1.15f);
    FloatCurve_AddStop(&s_mistGrow, 1.0f, 1.42f);

    // 4. Đường cong độ mờ: Hiện êm dịu, giữ lâu, tan dần hòa vào không khí
    FloatCurve_AddStop(&s_mistFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_mistFade, 0.20f, 1.0f);
    FloatCurve_AddStop(&s_mistFade, 0.65f, 0.85f);
    FloatCurve_AddStop(&s_mistFade, 1.0f, 0.0f);

    // 5. Đường cong lấp lánh cho giọt sương
    FloatCurve_AddStop(&s_dewFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_dewFade, 0.25f, 1.0f);
    FloatCurve_AddStop(&s_dewFade, 0.70f, 0.75f);
    FloatCurve_AddStop(&s_dewFade, 1.0f, 0.0f);

    s_mistVeilReady = true;
}

void VFX_ComposeMistVeilEx(Vector3 pos, VC_MaterialId matId, float radius, float duration)
{
    MistVeil_Init();

    if (radius <= 0.0f) radius = 5.5f;
    if (duration <= 0.0f) duration = 4.5f;

    const VFX_ElementMaterial *mat = VFX_Material(matId);

    // =========================================================================
    // LAYER 1: KHỐI SƯƠNG MÙ THỂ TÍCH 3D (Physical Volumetric Participating Medium)
    // Tự động tương tác với ánh nắng ban mai để tạo luồng God-rays xuyên qua sương
    // =========================================================================
    Color volCol = { 210, 235, 250, 255 };
    if (mat) {
        volCol = (Color){
            (unsigned char)((volCol.r * 2 + mat->soft.r) / 3),
            (unsigned char)((volCol.g * 2 + mat->soft.g) / 3),
            (unsigned char)((volCol.b * 2 + mat->soft.b) / 3),
            255
        };
    }
    FogVolume_SpawnTransient(pos, radius * 1.25f, volCol, 0.22f, duration);

    // =========================================================================
    // LAYER 2: MÀN SƯƠNG LỤA CUỘN LÀ LÀ MẶT ĐẤT (Seamless Ground Mist Shroud)
    // Kích thước hạt LỚN (3.5 - 5.5m), độ mờ NHẠT (alpha ~24, ~9% opacity).
    // Phân bố đều từ tâm ra ngoài, các dải sương đè lên nhau hòa quyện thành
    // một làn sương huyền ảo bồng bềnh liền mạch, KHÔNG BỊ VÓN CỤC hay thủng lỗ ở giữa!
    // =========================================================================
    int billowCount = (int)(15.0f * (radius / 5.0f));
    if (billowCount < 12) billowCount = 12;
    if (billowCount > 22) billowCount = 22;

    // Sắc xanh thiên thanh thoảng nhẹ của sương sớm
    Color baseMist = { 215, 235, 248, 24 }; // Rất mỏng, trong trẻo
    if (mat) {
        baseMist.r = (unsigned char)((baseMist.r * 3 + mat->body.r) / 4);
        baseMist.g = (unsigned char)((baseMist.g * 3 + mat->body.g) / 4);
        baseMist.b = (unsigned char)((baseMist.b * 3 + mat->body.b) / 4);
    }

    for (int i = 0; i < billowCount; i++) {
        float ang = Random01() * 2.0f * PI;
        // Phân bố diện tích đều từ tâm phủ kín cả vị trí người chơi
        float rDist = sqrtf(Random01()) * (radius * 0.65f);
        Vector3 p = {
            pos.x + cosf(ang) * rDist,
            pos.y + 0.05f + Random01() * 0.25f,
            pos.z + sinf(ang) * rDist
        };

        // Trôi dạt chậm rãi, xoay nhẹ tự nhiên
        float driftSpeed = Math_Mix(0.06f, 0.16f, Random01());
        Vector3 vel = {
            cosf(ang) * driftSpeed,
            0.015f + Random01() * 0.035f,
            sinf(ang) * driftSpeed
        };

        float pLife = duration * Math_Mix(0.85f, 1.0f, Random01());
        // Bán kính lớn để các dải sương hòa tan hoàn toàn vào nhau
        float pRadius = Math_Mix(3.2f, 5.0f, Random01()) * (radius / 5.0f);

        SpawnParticle((ParticleConfig){
            .position = p,
            .velocity = vel,
            .radius = pRadius,
            .lifetime = pLife,
            .colorStart = baseMist,
            .colorEnd = VC_WithAlpha(baseMist, 0),
            .radiusCurve = &s_mistGrow,
            .alphaCurve = &s_mistFade,
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 0.05f, // Xoay cực kỳ chậm
            .render = {
                .texture = s_mistSoftTex,
                .blendMode = VFX_BLEND_ALPHA, // Mềm mại, lọc mờ hậu cảnh
            }
        });
    }

    // =========================================================================
    // LAYER 3: HƠI NƯỚC / GIỌT SƯƠNG NGƯNG TỤ BAY BỔNG (Ascending Dew Motes)
    // Blend: VFX_BLEND_ADDITIVE + unlit (Lấp lánh như bụi trăng / linh khí ngưng tụ)
    // =========================================================================
    int moteCount = (int)(28.0f * (radius / 5.0f));
    if (moteCount > 40) moteCount = 40;

    Color sparkCol = { 190, 240, 255, 125 };
    if (mat) {
        sparkCol.r = (unsigned char)((sparkCol.r + mat->glow.r) / 2);
        sparkCol.g = (unsigned char)((sparkCol.g + mat->glow.g) / 2);
        sparkCol.b = (unsigned char)((sparkCol.b + mat->glow.b) / 2);
    }

    for (int i = 0; i < moteCount; i++) {
        float ang = Random01() * 2.0f * PI;
        float rDist = sqrtf(Random01()) * radius * 0.85f;
        Vector3 p = {
            pos.x + cosf(ang) * rDist,
            pos.y + 0.10f + Random01() * 1.2f,
            pos.z + sinf(ang) * rDist
        };
        Vector3 vel = {
            (Random01() - 0.5f) * 0.08f,
            0.08f + Random01() * 0.18f, // Bay nhẹ nhàng lên trên
            (Random01() - 0.5f) * 0.08f
        };
        float mLife = Math_Mix(1.4f, 2.6f, Random01());

        SpawnParticle((ParticleConfig){
            .position = p,
            .velocity = vel,
            .radius = Math_Mix(0.025f, 0.055f, Random01()),
            .lifetime = mLife,
            .colorStart = sparkCol,
            .colorEnd = VC_WithAlpha(sparkCol, 0),
            .alphaCurve = &s_dewFade,
            .render = {
                .texture = s_dewTex,
                .blendMode = VFX_BLEND_ADDITIVE,
                .unlit = 1,
                .emissiveBoost = 1.6f,
            }
        });
    }
}

void VFX_ComposeMistVeil(Vector3 pos, float radius, float duration)
{
    VFX_ComposeMistVeilEx(pos, VC_MAT_WATER, radius, duration);
}
