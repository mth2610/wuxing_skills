#ifndef CORE_VC_MATERIAL_H
#define CORE_VC_MATERIAL_H

// Element Material Table — nguồn sự thật duy nhất cho "nguyên tố X trông thế nào"
// ở tầng composition: đổi look một nguyên tố = sửa một entry (trong vfx_presets.c).
// Header này cố tình tối giản dependency (chỉ raylib + color_gradient + force_field)
// để visual_composer.h include được mà không dính vòng include qua skill_helper.h
// (DecalPresetType/EffectPresetType nằm trong skill_helper.h — phần preset cần
// chúng vẫn ở vfx_presets.h).

#include "raylib.h"
#include "core/color_gradient.h"
#include "core/force_field.h"
#include "core/decals/decal_material_types.h"

typedef enum {
    VC_MAT_FIRE,
    VC_MAT_ICE,
    VC_MAT_WATER,
    VC_MAT_LIGHTNING,
    VC_MAT_EARTH,
    VC_MAT_WOOD,
    VC_MAT_METAL,
    VC_MAT_TAIJI,
    VC_MAT_HOLY,
    VC_MAT_VOID,
    VC_MAT_POISON,
    VC_MAT_QI,
    VC_MAT_COUNT
} VC_MaterialId;

typedef struct {
    Color body;                   // màu bản sắc nguyên tố (shell, ribbon, rune)
    Color glow;                   // màu điểm nóng phát sáng (beam, ember)
    Color soft;                   // pastel nhạt cho aura/VFXLight/glint (bảng qi aura cũ)
    int   blendMode;              // blend khuyến nghị cho layer sheet/beam (BLEND_ADDITIVE/BLEND_ALPHA)
    const ColorGradient *grad;    // gradient hạt chuẩn của nguyên tố
    const ColorGradient *hotGrad; // biến thể sáng hơn (đảo chiều); nếu không có thì trỏ về grad
    const ForceField *fld;        // trường lực chuẩn của nguyên tố
    const char *runeDecal;        // texture vòng rune dưới đất (shield/charge)
    DecalMaterialId decalMaterial; // policy-only decal material; no texture path
} VFX_ElementMaterial;

// Luôn trả về entry hợp lệ (id sai → VC_MAT_TAIJI); mọi con trỏ trong entry đều non-NULL.
const VFX_ElementMaterial* VFX_Material(VC_MaterialId id);

// Gắn alpha lên màu material tại call site — material chỉ giữ RGB nhận diện,
// cường độ (alpha) là quyết định của từng layer.
static inline Color VC_WithAlpha(Color c, unsigned char a) { c.a = a; return c; }

// PREMULTIPLIED tint for a fixed-function draw (an immediate-mode ribbon or a
// raw rlBegin quad) submitted under BLEND_ALPHA_PREMULTIPLY.
//
// WHY THIS EXISTS AT ALL. `ColorAlpha(c, a)` is STRAIGHT: RGB is the colour and
// A is coverage, and every blend state we use except this one re-multiplies
// them in hardware — raylib's BLEND_ALPHA is (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
// and its BLEND_ADDITIVE is (SRC_ALPHA, ONE), both of which apply the alpha
// themselves. BLEND_ALPHA_PREMULTIPLY is (ONE, ONE_MINUS_SRC_ALPHA) and does
// not, so a straight tint handed to it is every soft edge scaled by 1/alpha.
// Measured 20/08/2026 on the trail presets, that mistake raised cover% ~4x on a
// DARK background — where the blend law itself changes almost nothing — and
// drove darken% to 0.0 everywhere, because the extra light swamped the body.
//
// NOTE THE TEXTURE IS HALF OF THIS. The fixed-function path multiplies vertex
// colour by the texel, so the SHEET must be premultiplied too (RGB = its own
// alpha) or the mask scales A without scaling RGB and the result is straight
// again over the mask's gradient. A white-RGB alpha mask is exactly that trap.
static inline Color VC_Premultiply(Color c, float a)
{
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    c.r = (unsigned char)((float)c.r * a);
    c.g = (unsigned char)((float)c.g * a);
    c.b = (unsigned char)((float)c.b * a);
    c.a = (unsigned char)(a * 255.0f);
    return c;
}

// Pha màu material về phía trắng (t: 0 = giữ nguyên, 1 = trắng). Dùng cho "lõi
// nóng": một màu nguyên tố bão hòa (vd. glow của LIGHTNING = 0,185,255) khi cộng
// dồn additive sẽ kẹt ở đúng hue đó và không bao giờ ra trắng — muốn có lõi
// trắng-nóng thì phải chừa sẵn kênh ở nguồn, emissiveBoost một mình không làm
// được (nó nhân, mà nhân 0 vẫn là 0).
static inline Color VC_Whiten(Color c, float t)
{
    if (t <= 0.0f) return c;
    if (t > 1.0f) t = 1.0f;
    c.r = (unsigned char)(c.r + (255 - c.r) * t);
    c.g = (unsigned char)(c.g + (255 - c.g) * t);
    c.b = (unsigned char)(c.b + (255 - c.b) * t);
    return c;
}

#endif // CORE_VC_MATERIAL_H
