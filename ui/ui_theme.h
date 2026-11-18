// ui/ui_theme.h
// Shared "Where Winds Meet" (Wuxia Ink-Wash & Antique Gold) UI Theme & Primitives.
#ifndef UI_THEME_H
#define UI_THEME_H

#include "raylib.h"
#include <stdbool.h>

// Ink-wash & Gold Wuxia Palette
#define WUXIA_BG_PANEL      ((Color){ 12, 15, 22, 226 })
#define WUXIA_BG_PANEL_TOP  ((Color){ 20, 25, 36, 236 })
#define WUXIA_BG_CARD       ((Color){ 18, 22, 33, 218 })
#define WUXIA_BG_CARD_HOV   ((Color){ 32, 38, 54, 238 })
#define WUXIA_GOLD          ((Color){ 226, 192, 118, 255 })
#define WUXIA_GOLD_BRIGHT   ((Color){ 250, 226, 162, 255 })
#define WUXIA_GOLD_DIM      ((Color){ 165, 138, 82, 165 })
#define WUXIA_JADE          ((Color){ 96, 208, 182, 255 })
#define WUXIA_CRIMSON       ((Color){ 214, 78, 72, 255 })
#define WUXIA_AZURE         ((Color){ 84, 168, 238, 255 })
#define WUXIA_TEXT_PRIMARY  ((Color){ 244, 238, 226, 255 })
#define WUXIA_TEXT_MUTED    ((Color){ 170, 178, 194, 235 })
#define WUXIA_BORDER_SUBTLE ((Color){ 95, 102, 122, 135 })

void WuxiaUI_Init(void);
void WuxiaUI_Shutdown(void);
Font WuxiaUI_GetFont(void);

// Crisp anti-aliased text with subtle ink shadow for readability over bright sand or dark arenas.
void  WuxiaUI_DrawText(const char *text, float x, float y, float fontSize, Color color);
void  WuxiaUI_DrawTextCentered(const char *text, Rectangle bounds, float fontSize, Color color);
float WuxiaUI_MeasureText(const char *text, float fontSize);

// Signature Wuxia translucent ink-wash panel with hairline border and L-shaped gold corner brackets.
void WuxiaUI_DrawPanel(Rectangle r, Color accent, bool withCorners);

// Sleek gradient-filled Wuxia pill/button; returns true on click/tap release inside bounds.
bool WuxiaUI_DrawButton(Rectangle r, const char *label, Color accent, bool active, float fontSize);

// Minimalist horizontal ink-wash ribbon with fading edges (used for zone / status banners).
void WuxiaUI_DrawRibbon(Rectangle r, Color accent);

// Crisp resource bar (HP / MP / Boss HP) with dark backing, gradient fill, and top specular hairline.
void WuxiaUI_DrawResourceBar(Rectangle r, float ratio, Color fillTop, Color fillBot, Color border,
                             const char *label, float fontSize);

// Small rotated diamond crest (for elemental badges / quest markers).
void WuxiaUI_DrawDiamond(Vector2 center, float radius, Color fill, Color border);

#endif // UI_THEME_H
