// ui/ui_theme.h
// Shared "Where Winds Meet" (Wuxia Ink-Wash & Antique Gold) UI Theme & Primitives.
// Unified immediate-mode widget toolkit for both PC & Mobile (Touch) layouts.
#ifndef UI_THEME_H
#define UI_THEME_H

#include "raylib.h"
#include <stdbool.h>

// Ink-wash & Gold Wuxia Palette
#define WUXIA_BG_PANEL      ((Color){ 12, 15, 22, 228 })
#define WUXIA_BG_PANEL_TOP  ((Color){ 22, 27, 38, 238 })
#define WUXIA_BG_CARD       ((Color){ 18, 22, 33, 220 })
#define WUXIA_BG_CARD_HOV   ((Color){ 34, 40, 56, 240 })
#define WUXIA_GOLD          ((Color){ 226, 192, 118, 255 })
#define WUXIA_GOLD_BRIGHT   ((Color){ 250, 226, 162, 255 })
#define WUXIA_GOLD_DIM      ((Color){ 165, 138, 82, 165 })
#define WUXIA_JADE          ((Color){ 96, 208, 182, 255 })
#define WUXIA_CRIMSON       ((Color){ 214, 78, 72, 255 })
#define WUXIA_AZURE         ((Color){ 84, 168, 238, 255 })
#define WUXIA_TEXT_PRIMARY  ((Color){ 244, 238, 226, 255 })
#define WUXIA_TEXT_MUTED    ((Color){ 170, 178, 194, 235 })
#define WUXIA_BORDER_SUBTLE ((Color){ 95, 102, 122, 135 })

// --- Responsive Layout & Input Context ---
typedef struct {
    float     screenW;
    float     screenH;
    float     scale;           // clamped screenH / 720.0f (0.78 .. 1.45)
    Rectangle safeRect;        // respects Android top-84px gesture zone & screen margins
    bool      isTouchMode;     // true on Android or when touch controls are enabled
    Vector2   pointerPos;
    bool      pointerPressed;
    bool      pointerDown;
    bool      pointerReleased;
    bool      pointerConsumed; // true if any UI widget captured the pointer this frame
} WuxiaUIContext;

// Procedural Minimalist Vector Icons (no external texture dependency, crisp at any DPI)
typedef enum {
    WUXIA_ICON_NONE = 0,
    WUXIA_ICON_SWORD,          // Basic attack / Kiếm Pháp
    WUXIA_ICON_FIST,           // Quyền Cước
    WUXIA_ICON_BAG,            // Hành Trang / Tủ Đồ
    WUXIA_ICON_SCROLL,         // Võ Học / Tàng Kinh Các
    WUXIA_ICON_SHOP,           // Tiệm Dã Tẩu
    WUXIA_ICON_COMPASS,        // Khám Phá / Gió Dẫn Lối
    WUXIA_ICON_CROSSED_SWORDS, // Đấu Trường PvP
    WUXIA_ICON_LOTUS,          // Thiền Định
    WUXIA_ICON_WIND_BOOT,      // Khinh Công / Né
    WUXIA_ICON_POTION,         // Đan Dược
    WUXIA_ICON_SHIELD_PK,      // Chế độ PK
    WUXIA_ICON_SPARK_EVENT,    // Kỳ Ngộ / Boss
    WUXIA_ICON_CHAT,           // Giao Lưu
    WUXIA_ICON_MIC,            // Đàm Thoại
    WUXIA_ICON_GEAR,           // Cài Đặt / Đồ Họa
    WUXIA_ICON_CLOSE,          // Đóng
    WUXIA_ICON_LOCK            // Khóa
} WuxiaIconId;

void WuxiaUI_Init(void);
void WuxiaUI_Shutdown(void);
Font WuxiaUI_GetFont(void);
Font WuxiaUI_GetFontForSize(float fontSize, bool isTitle);

// Per-frame responsive context (call at start of UI/HUD pass)
void                  WuxiaUI_BeginFrame(bool isTouchMode);
const WuxiaUIContext *WuxiaUI_GetContext(void);
bool                  WuxiaUI_IsPointerConsumed(void);
void                  WuxiaUI_ConsumePointer(void);

// Crisp anti-aliased Vietnamese text with subtle ink shadow
void  WuxiaUI_DrawText(const char *text, float x, float y, float fontSize, Color color);
void  WuxiaUI_DrawTextTitle(const char *text, float x, float y, float fontSize, Color color);
void  WuxiaUI_DrawTextCentered(const char *text, Rectangle bounds, float fontSize, Color color);
void  WuxiaUI_DrawTextRight(const char *text, float rightX, float y, float fontSize, Color color);
void  WuxiaUI_DrawTextWrapped(const char *text, Rectangle bounds, float fontSize, Color color, float lineSpacing);
float WuxiaUI_MeasureText(const char *text, float fontSize);
float WuxiaUI_MeasureTextTitle(const char *text, float fontSize);

// Signature Wuxia translucent ink-wash panel with hairline border and L-shaped gold corner brackets
void WuxiaUI_DrawPanel(Rectangle r, Color accent, bool withCorners);

// "Where Winds Meet" decorative primitives
void WuxiaUI_DrawSpecularRule(float x, float y, float width, Color accent, bool withCenterStar);
void WuxiaUI_DrawKeycap(float x, float y, const char *keyText, bool highlighted);
void WuxiaUI_DrawIcon(WuxiaIconId icon, Vector2 center, float radius, Color color);
void WuxiaUI_DrawDiamond(Vector2 center, float radius, Color fill, Color border);
void WuxiaUI_DrawRibbon(Rectangle r, Color accent);

// Reusable WWM Components (shared between PC & Mobile)
bool WuxiaUI_DrawButton(Rectangle r, const char *label, Color accent, bool active, float fontSize);
void WuxiaUI_DrawSectionHeader(float x, float y, float width, const char *title);
bool WuxiaUI_DrawFeatureTile(Rectangle r, WuxiaIconId icon, const char *title,
                             const char *subtitle, const char *hotkey,
                             Color accent, bool featured, bool locked);
bool WuxiaUI_DrawIconPill(Rectangle r, WuxiaIconId icon, const char *label,
                          const char *hotkey, Color accent, bool active);
bool WuxiaUI_DrawSkillNode(Vector2 center, float radius, WuxiaIconId icon,
                           const char *shortLabel, const char *hotkey,
                           Color elemColor, bool ready, bool active,
                           float cooldownRemain, float cooldownTotal, bool isDiamond);

// Crisp resource bars
void WuxiaUI_DrawResourceBar(Rectangle r, float ratio, Color fillTop, Color fillBot, Color border,
                             const char *label, float fontSize);
void WuxiaUI_DrawSleekBar(Rectangle r, float ratio, float trailRatio,
                          Color fillStart, Color fillEnd, bool slanted);

#endif // UI_THEME_H
