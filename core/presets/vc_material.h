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
} VFX_ElementMaterial;

// Luôn trả về entry hợp lệ (id sai → VC_MAT_TAIJI); mọi con trỏ trong entry đều non-NULL.
const VFX_ElementMaterial* VFX_Material(VC_MaterialId id);

// Gắn alpha lên màu material tại call site — material chỉ giữ RGB nhận diện,
// cường độ (alpha) là quyết định của từng layer.
static inline Color VC_WithAlpha(Color c, unsigned char a) { c.a = a; return c; }

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
