// ui/ui_theme.c
#include "ui/ui_theme.h"
#include <stddef.h>

static Font s_wuxiaFont = { 0 };
static bool s_wuxiaFontLoaded = false;
static bool s_wuxiaFontCustom = false;

void WuxiaUI_Init(void) {
    if (s_wuxiaFontLoaded) return;
    s_wuxiaFontLoaded = true;
    const char *path = "assets/fonts/wuxia_ui.ttf";
    Font f = LoadFontEx(path, 36, NULL, 0);
    if (f.texture.id > 0 && f.glyphCount > 0) {
        SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
        s_wuxiaFont = f;
        s_wuxiaFontCustom = true;
        TraceLog(LOG_INFO, "WUXIA_UI: Loaded crisp serif font '%s' (%d glyphs)", path, f.glyphCount);
    } else {
        s_wuxiaFont = GetFontDefault();
        s_wuxiaFontCustom = false;
    }
}

void WuxiaUI_Shutdown(void) {
    if (s_wuxiaFontLoaded && s_wuxiaFontCustom) {
        UnloadFont(s_wuxiaFont);
    }
    s_wuxiaFont = (Font){ 0 };
    s_wuxiaFontLoaded = false;
    s_wuxiaFontCustom = false;
}

Font WuxiaUI_GetFont(void) {
    if (!s_wuxiaFontLoaded) WuxiaUI_Init();
    return s_wuxiaFont;
}

static float TextSpacing(float fontSize) {
    return s_wuxiaFontCustom ? (fontSize * 0.04f) : 1.0f;
}

float WuxiaUI_MeasureText(const char *text, float fontSize) {
    if (!text || !text[0]) return 0.0f;
    Font f = WuxiaUI_GetFont();
    Vector2 sz = MeasureTextEx(f, text, fontSize, TextSpacing(fontSize));
    return sz.x;
}

void WuxiaUI_DrawText(const char *text, float x, float y, float fontSize, Color color) {
    if (!text || !text[0]) return;
    Font f = WuxiaUI_GetFont();
    float sp = TextSpacing(fontSize);
    Color shadow = { 6, 8, 12, (unsigned char)(color.a * 0.65f) };
    DrawTextEx(f, text, (Vector2){ x + 1.0f, y + 1.2f }, fontSize, sp, shadow);
    DrawTextEx(f, text, (Vector2){ x, y }, fontSize, sp, color);
}

void WuxiaUI_DrawTextCentered(const char *text, Rectangle bounds, float fontSize, Color color) {
    if (!text || !text[0]) return;
    Font f = WuxiaUI_GetFont();
    float sp = TextSpacing(fontSize);
    Vector2 sz = MeasureTextEx(f, text, fontSize, sp);
    float tx = bounds.x + (bounds.width - sz.x) * 0.5f;
    float ty = bounds.y + (bounds.height - sz.y) * 0.5f;
    WuxiaUI_DrawText(text, tx, ty, fontSize, color);
}

void WuxiaUI_DrawPanel(Rectangle r, Color accent, bool withCorners) {
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                           WUXIA_BG_PANEL_TOP, WUXIA_BG_PANEL);
    // Subtle top sheen
    DrawRectangle((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, 1, ColorAlpha(WHITE, 0.08f));
    DrawRectangleLinesEx(r, 1.0f, ColorAlpha(accent, 0.42f));

    if (withCorners) {
        float arm = 8.0f;
        if (arm > r.width * 0.2f) arm = r.width * 0.2f;
        if (arm > r.height * 0.2f) arm = r.height * 0.2f;
        Color c = ColorAlpha(accent, 0.92f);
        float t = 1.8f;
        // Top-left
        DrawLineEx((Vector2){ r.x, r.y }, (Vector2){ r.x + arm, r.y }, t, c);
        DrawLineEx((Vector2){ r.x, r.y }, (Vector2){ r.x, r.y + arm }, t, c);
        // Top-right
        DrawLineEx((Vector2){ r.x + r.width - arm, r.y }, (Vector2){ r.x + r.width, r.y }, t, c);
        DrawLineEx((Vector2){ r.x + r.width, r.y }, (Vector2){ r.x + r.width, r.y + arm }, t, c);
        // Bottom-left
        DrawLineEx((Vector2){ r.x, r.y + r.height - arm }, (Vector2){ r.x, r.y + r.height }, t, c);
        DrawLineEx((Vector2){ r.x, r.y + r.height }, (Vector2){ r.x + arm, r.y + r.height }, t, c);
        // Bottom-right
        DrawLineEx((Vector2){ r.x + r.width - arm, r.y + r.height }, (Vector2){ r.x + r.width, r.y + r.height }, t, c);
        DrawLineEx((Vector2){ r.x + r.width, r.y + r.height - arm }, (Vector2){ r.x + r.width, r.y + r.height }, t, c);
    }
}

bool WuxiaUI_DrawButton(Rectangle r, const char *label, Color accent, bool active, float fontSize) {
    Vector2 m = GetMousePosition();
    bool hov = CheckCollisionPointRec(m, r);
    Color top = active ? (Color){ (unsigned char)(accent.r / 4 + 28), (unsigned char)(accent.g / 4 + 28), (unsigned char)(accent.b / 4 + 28), 242 }
                       : (hov ? WUXIA_BG_CARD_HOV : WUXIA_BG_CARD);
    Color bot = (Color){ (unsigned char)(top.r * 0.72f), (unsigned char)(top.g * 0.72f), (unsigned char)(top.b * 0.72f), top.a };
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, top, bot);
    DrawRectangle((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, 1, ColorAlpha(WHITE, hov ? 0.16f : 0.08f));
    DrawRectangleLinesEx(r, (hov || active) ? 1.5f : 1.0f,
                         (hov || active) ? accent : ColorAlpha(accent, 0.65f));
    WuxiaUI_DrawTextCentered(label, r, fontSize, (hov || active) ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_PRIMARY);
    return hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

void WuxiaUI_DrawRibbon(Rectangle r, Color accent) {
    float fadeW = r.width * 0.16f;
    float midW = r.width - fadeW * 2.0f;
    DrawRectangleGradientH((int)r.x, (int)r.y, (int)fadeW, (int)r.height,
                           (Color){ 10, 12, 18, 0 }, (Color){ 14, 18, 28, 215 });
    DrawRectangle((int)(r.x + fadeW), (int)r.y, (int)midW, (int)r.height, (Color){ 14, 18, 28, 215 });
    DrawRectangleGradientH((int)(r.x + fadeW + midW), (int)r.y, (int)fadeW, (int)r.height,
                           (Color){ 14, 18, 28, 215 }, (Color){ 10, 12, 18, 0 });
    DrawLineEx((Vector2){ r.x + fadeW * 0.5f, r.y }, (Vector2){ r.x + r.width - fadeW * 0.5f, r.y },
               1.0f, ColorAlpha(accent, 0.55f));
    DrawLineEx((Vector2){ r.x + fadeW * 0.5f, r.y + r.height }, (Vector2){ r.x + r.width - fadeW * 0.5f, r.y + r.height },
               1.0f, ColorAlpha(accent, 0.55f));
}

void WuxiaUI_DrawResourceBar(Rectangle r, float ratio, Color fillTop, Color fillBot, Color border,
                             const char *label, float fontSize) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    DrawRectangleRec(r, (Color){ 10, 12, 18, 225 });
    int fw = (int)(r.width * ratio);
    if (fw > 0) {
        DrawRectangleGradientV((int)r.x, (int)r.y, fw, (int)r.height, fillTop, fillBot);
        DrawRectangle((int)r.x, (int)r.y, fw, 1, ColorAlpha(WHITE, 0.28f));
    }
    DrawRectangleLinesEx(r, 1.0f, ColorAlpha(border, 0.65f));
    if (label && label[0]) {
        WuxiaUI_DrawText(label, r.x + 8.0f, r.y + (r.height - fontSize) * 0.5f, fontSize, WUXIA_TEXT_PRIMARY);
    }
}

void WuxiaUI_DrawDiamond(Vector2 center, float radius, Color fill, Color border) {
    Vector2 pts[4] = {
        { center.x, center.y - radius },
        { center.x + radius, center.y },
        { center.x, center.y + radius },
        { center.x - radius, center.y }
    };
    DrawTriangle(pts[0], pts[3], pts[2], fill);
    DrawTriangle(pts[0], pts[2], pts[1], fill);
    for (int i = 0; i < 4; i++) {
        DrawLineEx(pts[i], pts[(i + 1) % 4], 1.3f, border);
    }
}
