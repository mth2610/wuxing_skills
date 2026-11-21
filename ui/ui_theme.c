// ui/ui_theme.c
#include "ui/ui_theme.h"
#include "core/resource_manager.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static Font s_fontBodySm = { 0 }; // 20px sans-serif (HUD tags, keycaps, tooltips)
static Font s_fontBodyMd = { 0 }; // 30px sans-serif (Buttons, cards, quest titles)
static Font s_fontTitleLg = { 0 }; // 44px serif (Headers, banners, realm intros)
static bool s_wuxiaFontLoaded = false;

static WuxiaUIContext s_ctx = { 0 };

void WuxiaUI_Init(void) {
    if (s_wuxiaFontLoaded) return;
    s_wuxiaFontLoaded = true;
    s_fontBodySm  = ResourceManager_LoadFont("assets/fonts/ui_font.ttf", 20);
    s_fontBodyMd  = ResourceManager_LoadFont("assets/fonts/ui_font.ttf", 30);
    s_fontTitleLg = ResourceManager_LoadFont("assets/fonts/wuxia_ui.ttf", 44);
    if (s_fontBodySm.texture.id == 0)  s_fontBodySm  = GetFontDefault();
    if (s_fontBodyMd.texture.id == 0)  s_fontBodyMd  = s_fontBodySm;
    if (s_fontTitleLg.texture.id == 0) s_fontTitleLg = s_fontBodyMd;
    TraceLog(LOG_INFO, "WUXIA_UI: Initialized 3-tier Vietnamese font atlases (sm=%d, md=%d, lg=%d)",
             s_fontBodySm.glyphCount, s_fontBodyMd.glyphCount, s_fontTitleLg.glyphCount);
}

void WuxiaUI_Shutdown(void) {
    // Fonts are owned & freed by ResourceManager_Unload().
    s_fontBodySm = (Font){ 0 };
    s_fontBodyMd = (Font){ 0 };
    s_fontTitleLg = (Font){ 0 };
    s_wuxiaFontLoaded = false;
}

Font WuxiaUI_GetFont(void) {
    if (!s_wuxiaFontLoaded) WuxiaUI_Init();
    return s_fontBodyMd;
}

Font WuxiaUI_GetFontForSize(float fontSize, bool isTitle) {
    if (!s_wuxiaFontLoaded) WuxiaUI_Init();
    if (isTitle || fontSize >= 25.0f) return s_fontTitleLg;
    if (fontSize <= 16.0f) return s_fontBodySm;
    return s_fontBodyMd;
}

void WuxiaUI_BeginFrame(bool isTouchMode) {
    if (!s_wuxiaFontLoaded) WuxiaUI_Init();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float s = sh / 720.0f;
    if (s < 0.78f) s = 0.78f;
    if (s > 1.45f) s = 1.45f;

    s_ctx.screenW = sw;
    s_ctx.screenH = sh;
    s_ctx.scale   = s;
    s_ctx.isTouchMode = isTouchMode;

#if defined(PLATFORM_ANDROID) || defined(__ANDROID__)
    // ENGINE_LANDMINES §5: top 84px is OS gesture zone on Mali devices
    float topPad = isTouchMode ? 90.0f : 14.0f;
#else
    float topPad = isTouchMode ? 24.0f : 14.0f;
#endif
    float sidePad = 16.0f * s;
    s_ctx.safeRect = (Rectangle){
        sidePad, topPad,
        sw - sidePad * 2.0f,
        sh - topPad - 14.0f * s
    };

    if (GetTouchPointCount() > 0) {
        s_ctx.pointerPos = GetTouchPosition(0);
    } else {
        s_ctx.pointerPos = GetMousePosition();
    }
    s_ctx.pointerPressed  = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    s_ctx.pointerDown     = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    s_ctx.pointerReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    s_ctx.pointerConsumed = false;
}

const WuxiaUIContext *WuxiaUI_GetContext(void) {
    if (s_ctx.screenW <= 0.0f) {
#if defined(PLATFORM_ANDROID) || defined(__ANDROID__)
        WuxiaUI_BeginFrame(true);
#else
        WuxiaUI_BeginFrame(false);
#endif
    }
    return &s_ctx;
}

bool WuxiaUI_IsPointerConsumed(void) {
    return s_ctx.pointerConsumed;
}

void WuxiaUI_ConsumePointer(void) {
    s_ctx.pointerConsumed = true;
}

static float TextSpacing(Font f, float fontSize) {
    return (f.texture.id > 0 && f.baseSize > 0) ? (fontSize * 0.03f) : 1.0f;
}

float WuxiaUI_MeasureText(const char *text, float fontSize) {
    if (!text || !text[0]) return 0.0f;
    Font f = WuxiaUI_GetFontForSize(fontSize, false);
    Vector2 sz = MeasureTextEx(f, text, fontSize, TextSpacing(f, fontSize));
    return sz.x;
}

float WuxiaUI_MeasureTextTitle(const char *text, float fontSize) {
    if (!text || !text[0]) return 0.0f;
    Font f = WuxiaUI_GetFontForSize(fontSize, true);
    Vector2 sz = MeasureTextEx(f, text, fontSize, TextSpacing(f, fontSize));
    return sz.x;
}

static void DrawTextInternal(Font f, const char *text, float x, float y, float fontSize, Color color) {
    if (!text || !text[0]) return;
    float sp = TextSpacing(f, fontSize);
    Color shadow = { 5, 7, 11, (unsigned char)(color.a * 0.72f) };
    DrawTextEx(f, text, (Vector2){ x + 1.0f, y + 1.2f }, fontSize, sp, shadow);
    DrawTextEx(f, text, (Vector2){ x, y }, fontSize, sp, color);
}

void WuxiaUI_DrawText(const char *text, float x, float y, float fontSize, Color color) {
    if (!text || !text[0]) return;
    Font f = WuxiaUI_GetFontForSize(fontSize, false);
    DrawTextInternal(f, text, x, y, fontSize, color);
}

void WuxiaUI_DrawTextTitle(const char *text, float x, float y, float fontSize, Color color) {
    if (!text || !text[0]) return;
    Font f = WuxiaUI_GetFontForSize(fontSize, true);
    DrawTextInternal(f, text, x, y, fontSize, color);
}

void WuxiaUI_DrawTextCentered(const char *text, Rectangle bounds, float fontSize, Color color) {
    if (!text || !text[0]) return;
    Font f = WuxiaUI_GetFontForSize(fontSize, false);
    float sp = TextSpacing(f, fontSize);
    Vector2 sz = MeasureTextEx(f, text, fontSize, sp);
    float tx = bounds.x + (bounds.width - sz.x) * 0.5f;
    float ty = bounds.y + (bounds.height - sz.y) * 0.5f;
    DrawTextInternal(f, text, tx, ty, fontSize, color);
}

void WuxiaUI_DrawTextRight(const char *text, float rightX, float y, float fontSize, Color color) {
    if (!text || !text[0]) return;
    float w = WuxiaUI_MeasureText(text, fontSize);
    WuxiaUI_DrawText(text, rightX - w, y, fontSize, color);
}

void WuxiaUI_DrawTextWrapped(const char *text, Rectangle bounds, float fontSize, Color color, float lineSpacing) {
    if (!text || !text[0]) return;
    char lineBuf[320];
    lineBuf[0] = '\0';
    const char *ptr = text;
    float curY = bounds.y;
    float lineStep = fontSize + lineSpacing;

    while (*ptr != '\0') {
        while (*ptr == ' ') ptr++;
        if (*ptr == '\0') break;

        if (*ptr == '\n') {
            if (lineBuf[0] != '\0') {
                WuxiaUI_DrawText(lineBuf, bounds.x, curY, fontSize, color);
                lineBuf[0] = '\0';
            }
            curY += lineStep;
            ptr++;
            continue;
        }

        const char *wordStart = ptr;
        while (*ptr != '\0' && *ptr != ' ' && *ptr != '\n') ptr++;
        int wordLen = (int)(ptr - wordStart);

        char testBuf[320];
        if (lineBuf[0] == '\0') {
            snprintf(testBuf, sizeof(testBuf), "%.*s", wordLen, wordStart);
        } else {
            snprintf(testBuf, sizeof(testBuf), "%s %.*s", lineBuf, wordLen, wordStart);
        }

        if (WuxiaUI_MeasureText(testBuf, fontSize) > bounds.width && lineBuf[0] != '\0') {
            WuxiaUI_DrawText(lineBuf, bounds.x, curY, fontSize, color);
            curY += lineStep;
            if (curY + fontSize > bounds.y + bounds.height) return;
            snprintf(lineBuf, sizeof(lineBuf), "%.*s", wordLen, wordStart);
        } else {
            strncpy(lineBuf, testBuf, sizeof(lineBuf) - 1);
            lineBuf[sizeof(lineBuf) - 1] = '\0';
        }
    }
    if (lineBuf[0] != '\0' && curY <= bounds.y + bounds.height) {
        WuxiaUI_DrawText(lineBuf, bounds.x, curY, fontSize, color);
    }
}

void WuxiaUI_DrawSpecularRule(float x, float y, float width, Color accent, bool withCenterStar) {
    if (width <= 4.0f) return;
    float half = width * 0.5f;
    Color fadeZero = ColorAlpha(accent, 0.0f);
    Color midCol   = ColorAlpha(accent, 0.52f);
    DrawRectangleGradientH((int)x, (int)y, (int)half, 1, fadeZero, midCol);
    DrawRectangleGradientH((int)(x + half), (int)y, (int)half, 1, midCol, fadeZero);
    if (withCenterStar) {
        float cx = x + half;
        DrawRectangleGradientH((int)(cx - 18.0f), (int)y, 18, 1, midCol, ColorAlpha(WUXIA_GOLD_BRIGHT, 0.92f));
        DrawRectangleGradientH((int)cx, (int)y, 18, 1, ColorAlpha(WUXIA_GOLD_BRIGHT, 0.92f), midCol);
        WuxiaUI_DrawDiamond((Vector2){ cx, y + 0.5f }, 3.2f, WUXIA_GOLD_BRIGHT, ColorAlpha(WHITE, 0.85f));
    }
}

void WuxiaUI_DrawKeycap(float x, float y, const char *keyText, bool highlighted) {
    if (!keyText || !keyText[0]) return;
    float fontSize = 10.5f;
    float tw = WuxiaUI_MeasureText(keyText, fontSize);
    float w = tw + 9.0f;
    if (w < 18.0f) w = 18.0f;
    float h = 16.0f;
    Rectangle r = { x, y, w, h };
    Color bg = highlighted ? (Color){ 48, 42, 24, 235 } : (Color){ 16, 19, 28, 220 };
    Color bd = highlighted ? WUXIA_GOLD_BRIGHT : ColorAlpha(WUXIA_TEXT_MUTED, 0.55f);
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1.0f, bd);
    WuxiaUI_DrawTextCentered(keyText, r, fontSize, highlighted ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_PRIMARY);
}

void WuxiaUI_DrawIcon(WuxiaIconId icon, Vector2 c, float r, Color col) {
    if (icon == WUXIA_ICON_NONE || r <= 1.0f) return;
    float t = (r >= 10.0f) ? 1.6f : 1.2f;
    switch (icon) {
        case WUXIA_ICON_SWORD: {
            Vector2 tip = { c.x + r * 0.70f, c.y - r * 0.70f };
            Vector2 hilt = { c.x - r * 0.45f, c.y + r * 0.45f };
            Vector2 pom = { c.x - r * 0.72f, c.y + r * 0.72f };
            DrawLineEx(hilt, tip, t * 1.25f, col);
            DrawLineEx(pom, hilt, t, ColorAlpha(WUXIA_GOLD, 0.9f));
            DrawLineEx((Vector2){ c.x - r * 0.65f, c.y + r * 0.15f },
                       (Vector2){ c.x - r * 0.15f, c.y + r * 0.65f }, t, WUXIA_GOLD_BRIGHT);
            break;
        }
        case WUXIA_ICON_FIST: {
            Rectangle fist = { c.x - r * 0.52f, c.y - r * 0.42f, r * 1.04f, r * 0.84f };
            DrawRectangleLinesEx(fist, t, col);
            DrawLineEx((Vector2){ fist.x + r * 0.25f, fist.y }, (Vector2){ fist.x + r * 0.25f, fist.y + r * 0.35f }, t * 0.8f, col);
            DrawLineEx((Vector2){ fist.x + r * 0.52f, fist.y }, (Vector2){ fist.x + r * 0.52f, fist.y + r * 0.35f }, t * 0.8f, col);
            DrawLineEx((Vector2){ fist.x + r * 0.78f, fist.y }, (Vector2){ fist.x + r * 0.78f, fist.y + r * 0.35f }, t * 0.8f, col);
            break;
        }
        case WUXIA_ICON_BAG: {
            Rectangle body = { c.x - r * 0.55f, c.y - r * 0.28f, r * 1.10f, r * 0.88f };
            DrawRectangleLinesEx(body, t, col);
            DrawLineEx((Vector2){ c.x - r * 0.32f, body.y }, (Vector2){ c.x - r * 0.20f, c.y - r * 0.62f }, t, col);
            DrawLineEx((Vector2){ c.x + r * 0.32f, body.y }, (Vector2){ c.x + r * 0.20f, c.y - r * 0.62f }, t, col);
            DrawLineEx((Vector2){ c.x - r * 0.20f, c.y - r * 0.62f }, (Vector2){ c.x + r * 0.20f, c.y - r * 0.62f }, t, col);
            DrawLineEx((Vector2){ body.x, c.y + r * 0.12f }, (Vector2){ body.x + body.width, c.y + r * 0.12f }, t * 0.85f, WUXIA_GOLD);
            break;
        }
        case WUXIA_ICON_SCROLL: {
            Rectangle sc = { c.x - r * 0.58f, c.y - r * 0.45f, r * 1.16f, r * 0.90f };
            DrawRectangleLinesEx(sc, t, col);
            DrawLineEx((Vector2){ sc.x - r * 0.15f, sc.y }, (Vector2){ sc.x - r * 0.15f, sc.y + sc.height }, t * 1.2f, WUXIA_GOLD);
            DrawLineEx((Vector2){ sc.x + r * 0.22f, c.y - r * 0.16f }, (Vector2){ sc.x + r * 0.90f, c.y - r * 0.16f }, t * 0.85f, col);
            DrawLineEx((Vector2){ sc.x + r * 0.22f, c.y + r * 0.14f }, (Vector2){ sc.x + r * 0.78f, c.y + r * 0.14f }, t * 0.85f, col);
            break;
        }
        case WUXIA_ICON_SHOP: {
            // Pagoda eaves + lantern
            DrawTriangleLines((Vector2){ c.x, c.y - r * 0.68f },
                              (Vector2){ c.x - r * 0.75f, c.y - r * 0.15f },
                              (Vector2){ c.x + r * 0.75f, c.y - r * 0.15f }, WUXIA_GOLD);
            Rectangle hall = { c.x - r * 0.52f, c.y - r * 0.15f, r * 1.04f, r * 0.72f };
            DrawRectangleLinesEx(hall, t, col);
            DrawRectangle((int)(c.x - r * 0.16f), (int)(c.y + r * 0.12f), (int)(r * 0.32f), (int)(r * 0.45f), ColorAlpha(WUXIA_GOLD, 0.45f));
            break;
        }
        case WUXIA_ICON_COMPASS: {
            DrawRing(c, r * 0.65f, r * 0.78f, 0.0f, 360.0f, 24, col);
            DrawTriangle((Vector2){ c.x, c.y - r * 0.52f }, (Vector2){ c.x - r * 0.18f, c.y }, (Vector2){ c.x + r * 0.18f, c.y }, WUXIA_GOLD_BRIGHT);
            DrawTriangle((Vector2){ c.x, c.y + r * 0.52f }, (Vector2){ c.x + r * 0.18f, c.y }, (Vector2){ c.x - r * 0.18f, c.y }, ColorAlpha(col, 0.55f));
            break;
        }
        case WUXIA_ICON_CROSSED_SWORDS: {
            DrawLineEx((Vector2){ c.x - r * 0.62f, c.y + r * 0.62f }, (Vector2){ c.x + r * 0.62f, c.y - r * 0.62f }, t * 1.1f, col);
            DrawLineEx((Vector2){ c.x + r * 0.62f, c.y + r * 0.62f }, (Vector2){ c.x - r * 0.62f, c.y - r * 0.62f }, t * 1.1f, col);
            DrawCircleV(c, r * 0.16f, WUXIA_GOLD_BRIGHT);
            break;
        }
        case WUXIA_ICON_LOTUS: {
            DrawTriangleLines((Vector2){ c.x, c.y - r * 0.65f }, (Vector2){ c.x - r * 0.28f, c.y + r * 0.35f }, (Vector2){ c.x + r * 0.28f, c.y + r * 0.35f }, col);
            DrawLineEx((Vector2){ c.x - r * 0.72f, c.y + r * 0.15f }, (Vector2){ c.x, c.y + r * 0.55f }, t, col);
            DrawLineEx((Vector2){ c.x + r * 0.72f, c.y + r * 0.15f }, (Vector2){ c.x, c.y + r * 0.55f }, t, col);
            break;
        }
        case WUXIA_ICON_WIND_BOOT: {
            DrawLineEx((Vector2){ c.x - r * 0.65f, c.y - r * 0.32f }, (Vector2){ c.x + r * 0.45f, c.y - r * 0.32f }, t, col);
            DrawLineEx((Vector2){ c.x - r * 0.40f, c.y }, (Vector2){ c.x + r * 0.68f, c.y }, t, WUXIA_JADE);
            DrawLineEx((Vector2){ c.x - r * 0.55f, c.y + r * 0.32f }, (Vector2){ c.x + r * 0.35f, c.y + r * 0.32f }, t, col);
            break;
        }
        case WUXIA_ICON_POTION: {
            DrawCircleLines((int)c.x, (int)(c.y + r * 0.18f), r * 0.48f, col);
            DrawRectangle((int)(c.x - r * 0.16f), (int)(c.y - r * 0.56f), (int)(r * 0.32f), (int)(r * 0.30f), col);
            DrawCircleV((Vector2){ c.x, c.y + r * 0.22f }, r * 0.28f, ColorAlpha(col, 0.55f));
            break;
        }
        case WUXIA_ICON_SHIELD_PK: {
            Vector2 pts[5] = {
                { c.x - r * 0.55f, c.y - r * 0.55f },
                { c.x + r * 0.55f, c.y - r * 0.55f },
                { c.x + r * 0.50f, c.y + r * 0.10f },
                { c.x,             c.y + r * 0.68f },
                { c.x - r * 0.50f, c.y + r * 0.10f }
            };
            for (int i = 0; i < 5; i++) DrawLineEx(pts[i], pts[(i + 1) % 5], t, col);
            break;
        }
        case WUXIA_ICON_SPARK_EVENT: {
            WuxiaUI_DrawDiamond(c, r * 0.58f, ColorAlpha(col, 0.28f), col);
            DrawLineEx((Vector2){ c.x - r * 0.78f, c.y }, (Vector2){ c.x + r * 0.78f, c.y }, t * 0.8f, WUXIA_GOLD_BRIGHT);
            DrawLineEx((Vector2){ c.x, c.y - r * 0.78f }, (Vector2){ c.x, c.y + r * 0.78f }, t * 0.8f, WUXIA_GOLD_BRIGHT);
            break;
        }
        case WUXIA_ICON_CHAT: {
            Rectangle b = { c.x - r * 0.62f, c.y - r * 0.45f, r * 1.24f, r * 0.78f };
            DrawRectangleLinesEx(b, t, col);
            DrawLineEx((Vector2){ c.x - r * 0.25f, b.y + b.height }, (Vector2){ c.x - r * 0.42f, c.y + r * 0.62f }, t, col);
            break;
        }
        case WUXIA_ICON_MIC: {
            Rectangle cap = { c.x - r * 0.26f, c.y - r * 0.60f, r * 0.52f, r * 0.72f };
            DrawRectangleLinesEx(cap, t, col);
            DrawLineEx((Vector2){ c.x, c.y + r * 0.14f }, (Vector2){ c.x, c.y + r * 0.56f }, t, col);
            DrawLineEx((Vector2){ c.x - r * 0.35f, c.y + r * 0.56f }, (Vector2){ c.x + r * 0.35f, c.y + r * 0.56f }, t, col);
            break;
        }
        case WUXIA_ICON_GEAR: {
            DrawRing(c, r * 0.30f, r * 0.52f, 0.0f, 360.0f, 16, col);
            for (int i = 0; i < 6; i++) {
                float a = (float)i * (PI / 3.0f);
                DrawLineEx((Vector2){ c.x + cosf(a) * r * 0.48f, c.y + sinf(a) * r * 0.48f },
                           (Vector2){ c.x + cosf(a) * r * 0.76f, c.y + sinf(a) * r * 0.76f }, t * 1.1f, col);
            }
            break;
        }
        case WUXIA_ICON_CLOSE: {
            DrawLineEx((Vector2){ c.x - r * 0.48f, c.y - r * 0.48f }, (Vector2){ c.x + r * 0.48f, c.y + r * 0.48f }, t * 1.15f, col);
            DrawLineEx((Vector2){ c.x + r * 0.48f, c.y - r * 0.48f }, (Vector2){ c.x - r * 0.48f, c.y + r * 0.48f }, t * 1.15f, col);
            break;
        }
        case WUXIA_ICON_LOCK: {
            Rectangle body = { c.x - r * 0.44f, c.y - r * 0.08f, r * 0.88f, r * 0.66f };
            DrawRectangleLinesEx(body, t, col);
            DrawRing((Vector2){ c.x, body.y }, r * 0.24f, r * 0.34f, 180.0f, 360.0f, 12, col);
            break;
        }
        default: break;
    }
}

void WuxiaUI_DrawPanel(Rectangle r, Color accent, bool withCorners) {
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                           WUXIA_BG_PANEL_TOP, WUXIA_BG_PANEL);
    // Subtle top sheen
    DrawRectangle((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, 1, ColorAlpha(WHITE, 0.10f));
    DrawRectangleLinesEx(r, 1.0f, ColorAlpha(accent, 0.40f));

    if (withCorners) {
        float arm = 8.0f;
        if (arm > r.width * 0.2f) arm = r.width * 0.2f;
        if (arm > r.height * 0.2f) arm = r.height * 0.2f;
        Color c = ColorAlpha(accent, 0.92f);
        float t = 1.8f;
        DrawLineEx((Vector2){ r.x, r.y }, (Vector2){ r.x + arm, r.y }, t, c);
        DrawLineEx((Vector2){ r.x, r.y }, (Vector2){ r.x, r.y + arm }, t, c);
        DrawLineEx((Vector2){ r.x + r.width - arm, r.y }, (Vector2){ r.x + r.width, r.y }, t, c);
        DrawLineEx((Vector2){ r.x + r.width, r.y }, (Vector2){ r.x + r.width, r.y + arm }, t, c);
        DrawLineEx((Vector2){ r.x, r.y + r.height - arm }, (Vector2){ r.x, r.y + r.height }, t, c);
        DrawLineEx((Vector2){ r.x, r.y + r.height }, (Vector2){ r.x + arm, r.y + r.height }, t, c);
        DrawLineEx((Vector2){ r.x + r.width - arm, r.y + r.height }, (Vector2){ r.x + r.width, r.y + r.height }, t, c);
        DrawLineEx((Vector2){ r.x + r.width, r.y + r.height - arm }, (Vector2){ r.x + r.width, r.y + r.height }, t, c);
    }
}

bool WuxiaUI_DrawButton(Rectangle r, const char *label, Color accent, bool active, float fontSize) {
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    Vector2 m = ctx->pointerPos;
    bool hov = CheckCollisionPointRec(m, r);
    Color top = active ? (Color){ (unsigned char)(accent.r / 4 + 28), (unsigned char)(accent.g / 4 + 28), (unsigned char)(accent.b / 4 + 28), 242 }
                       : (hov ? WUXIA_BG_CARD_HOV : WUXIA_BG_CARD);
    Color bot = (Color){ (unsigned char)(top.r * 0.72f), (unsigned char)(top.g * 0.72f), (unsigned char)(top.b * 0.72f), top.a };
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, top, bot);
    DrawRectangle((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, 1, ColorAlpha(WHITE, hov ? 0.18f : 0.08f));
    DrawRectangleLinesEx(r, (hov || active) ? 1.5f : 1.0f,
                         (hov || active) ? accent : ColorAlpha(accent, 0.58f));
    WuxiaUI_DrawTextCentered(label, r, fontSize, (hov || active) ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_PRIMARY);

    if (hov && (ctx->pointerPressed || ctx->pointerDown || ctx->pointerReleased)) {
        WuxiaUI_ConsumePointer();
    }
    return hov && ctx->pointerReleased;
}

void WuxiaUI_DrawSectionHeader(float x, float y, float width, const char *title) {
    WuxiaUI_DrawText(title, x, y, 13.0f, WUXIA_TEXT_MUTED);
    DrawRectangleGradientH((int)x, (int)(y + 17.0f), (int)width, 1,
                           ColorAlpha(WUXIA_GOLD, 0.42f), ColorAlpha(WUXIA_GOLD, 0.0f));
}

bool WuxiaUI_DrawFeatureTile(Rectangle r, WuxiaIconId icon, const char *title,
                             const char *subtitle, const char *hotkey,
                             Color accent, bool featured, bool locked) {
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    Vector2 m = ctx->pointerPos;
    bool hov = CheckCollisionPointRec(m, r);

    Color top = locked ? (Color){ 14, 16, 22, 210 }
              : (hov ? (Color){ 36, 40, 54, 240 }
                     : (featured ? (Color){ 24, 26, 36, 232 } : (Color){ 17, 20, 29, 224 }));
    Color bot = (Color){ (unsigned char)(top.r * 0.74f), (unsigned char)(top.g * 0.74f), (unsigned char)(top.b * 0.74f), top.a };
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, top, bot);

    Color borderCol = locked ? (Color){ 55, 60, 72, 140 }
                    : (hov ? WUXIA_GOLD_BRIGHT : (featured ? ColorAlpha(accent, 0.65f) : WUXIA_BORDER_SUBTLE));
    DrawRectangleLinesEx(r, (hov || featured) ? 1.35f : 1.0f, borderCol);

    if (featured || hov) {
        WuxiaUI_DrawSpecularRule(r.x + 8.0f, r.y + r.height - 1.0f, r.width - 16.0f, accent, true);
    }

    if (!ctx->isTouchMode && hotkey && hotkey[0]) {
        WuxiaUI_DrawKeycap(r.x + 6.0f, r.y + 6.0f, hotkey, hov);
    }

    bool compact = (r.height < 64.0f);
    bool wide    = (r.width >= r.height * 1.9f);
    Color iconCol = locked ? WUXIA_TEXT_MUTED : (hov ? WUXIA_GOLD_BRIGHT : accent);

    if (wide) {
        Vector2 ic = { r.x + 32.0f, r.y + r.height * 0.5f };
        WuxiaUI_DrawIcon(icon, ic, compact ? 13.0f : 16.0f, iconCol);
        float tx = r.x + 58.0f;
        if (subtitle && subtitle[0]) {
            WuxiaUI_DrawText(title, tx, r.y + r.height * 0.5f - 16.0f, 15.0f,
                             locked ? WUXIA_TEXT_MUTED : (hov ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_PRIMARY));
            WuxiaUI_DrawText(subtitle, tx, r.y + r.height * 0.5f + 3.0f, 11.5f, WUXIA_TEXT_MUTED);
        } else {
            WuxiaUI_DrawText(title, tx, r.y + r.height * 0.5f - 8.0f, 15.5f,
                             locked ? WUXIA_TEXT_MUTED : (hov ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_PRIMARY));
        }
    } else {
        Vector2 ic = { r.x + r.width * 0.5f, r.y + r.height * 0.38f };
        WuxiaUI_DrawIcon(icon, ic, compact ? 12.0f : 15.0f, iconCol);
        WuxiaUI_DrawTextCentered(title, (Rectangle){ r.x + 4.0f, r.y + r.height * 0.60f, r.width - 8.0f, 20.0f },
                                 13.5f, locked ? WUXIA_TEXT_MUTED : (hov ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_PRIMARY));
    }

    if (locked) {
        WuxiaUI_DrawIcon(WUXIA_ICON_LOCK, (Vector2){ r.x + r.width - 14.0f, r.y + 14.0f }, 6.5f, WUXIA_TEXT_MUTED);
    }

    if (hov && (ctx->pointerPressed || ctx->pointerDown || ctx->pointerReleased)) {
        WuxiaUI_ConsumePointer();
    }
    return !locked && hov && ctx->pointerReleased;
}

bool WuxiaUI_DrawIconPill(Rectangle r, WuxiaIconId icon, const char *label,
                          const char *hotkey, Color accent, bool active) {
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    Vector2 m = ctx->pointerPos;
    bool hov = CheckCollisionPointRec(m, r);

    Color bg = active ? (Color){ (unsigned char)(accent.r / 4 + 26), (unsigned char)(accent.g / 4 + 26), (unsigned char)(accent.b / 4 + 26), 232 }
             : (hov ? (Color){ 32, 37, 50, 232 } : (Color){ 14, 18, 26, 208 });
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, (hov || active) ? 1.4f : 1.0f,
                         (hov || active) ? accent : ColorAlpha(accent, 0.45f));

    float iconR = r.height * 0.30f;
    if (iconR > 10.0f) iconR = 10.0f;
    Vector2 ic = (label && label[0])
        ? (Vector2){ r.x + 16.0f, r.y + r.height * 0.5f }
        : (Vector2){ r.x + r.width * 0.5f, r.y + r.height * 0.5f };
    WuxiaUI_DrawIcon(icon, ic, iconR, (hov || active) ? WUXIA_GOLD_BRIGHT : accent);

    if (label && label[0]) {
        WuxiaUI_DrawText(label, r.x + 30.0f, r.y + (r.height - 13.0f) * 0.5f, 13.0f,
                         (hov || active) ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_PRIMARY);
    }
    if (!ctx->isTouchMode && hotkey && hotkey[0]) {
        WuxiaUI_DrawKeycap(r.x + r.width - WuxiaUI_MeasureText(hotkey, 10.5f) - 12.0f,
                           r.y + r.height - 10.0f, hotkey, hov || active);
    }

    if (hov && (ctx->pointerPressed || ctx->pointerDown || ctx->pointerReleased)) {
        WuxiaUI_ConsumePointer();
    }
    return hov && ctx->pointerReleased;
}

bool WuxiaUI_DrawSkillNode(Vector2 center, float radius, WuxiaIconId icon,
                           const char *shortLabel, const char *hotkey,
                           Color elemColor, bool ready, bool active,
                           float cooldownRemain, float cooldownTotal, bool isDiamond) {
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    Vector2 m = ctx->pointerPos;
    float dx = m.x - center.x, dy = m.y - center.y;
    bool hov = (dx * dx + dy * dy <= (radius * 1.15f) * (radius * 1.15f));

    Color inner = active
        ? (Color){ (unsigned char)(elemColor.r / 3 + 38), (unsigned char)(elemColor.g / 3 + 38), (unsigned char)(elemColor.b / 3 + 38), 232 }
        : (ready ? (hov ? (Color){ 28, 34, 48, 232 } : (Color){ 16, 20, 30, 212 })
                 : (Color){ 10, 12, 17, 195 });
    Color outer = (Color){ (unsigned char)(inner.r * 0.65f), (unsigned char)(inner.g * 0.65f), (unsigned char)(inner.b * 0.65f), inner.a };
    Color ring  = active ? WUXIA_GOLD_BRIGHT : (ready ? (hov ? WUXIA_GOLD_BRIGHT : elemColor) : (Color){ 72, 78, 90, 155 });

    if (isDiamond) {
        Vector2 pts[4] = {
            { center.x, center.y - radius },
            { center.x + radius, center.y },
            { center.x, center.y + radius },
            { center.x - radius, center.y }
        };
        DrawTriangle(pts[0], pts[3], pts[2], inner);
        DrawTriangle(pts[0], pts[2], pts[1], inner);
        for (int i = 0; i < 4; i++) {
            DrawLineEx(pts[i], pts[(i + 1) % 4], active ? 2.2f : 1.5f, ring);
        }
        float rIn = radius * 0.84f;
        Vector2 inPts[4] = {
            { center.x, center.y - rIn },
            { center.x + rIn, center.y },
            { center.x, center.y + rIn },
            { center.x - rIn, center.y }
        };
        for (int i = 0; i < 4; i++) {
            DrawLineEx(inPts[i], inPts[(i + 1) % 4], 1.0f, ColorAlpha(WUXIA_GOLD, ready ? 0.38f : 0.14f));
        }
    } else {
        DrawCircleGradient(center, radius, inner, outer);
        DrawRing(center, radius - 2.2f, radius, 0.0f, 360.0f, 36, ring);
        DrawRing(center, radius - 4.8f, radius - 3.9f, 0.0f, 360.0f, 36, ColorAlpha(WUXIA_GOLD, ready ? 0.35f : 0.12f));
    }

    // Cooldown sweep indicator
    if (!ready && cooldownRemain > 0.05f) {
        float frac = (cooldownTotal > 0.05f) ? (cooldownRemain / cooldownTotal) : 0.5f;
        if (frac > 1.0f) frac = 1.0f;
        if (!isDiamond) {
            DrawRing(center, radius - 3.0f, radius, -90.0f, -90.0f + 360.0f * (1.0f - frac), 24, ColorAlpha(WUXIA_GOLD_BRIGHT, 0.45f));
        }
        const char *cdTxt = TextFormat("%.1f", cooldownRemain);
        float cdSize = radius * 0.56f;
        if (cdSize < 11.0f) cdSize = 11.0f;
        WuxiaUI_DrawTextCentered(cdTxt, (Rectangle){ center.x - radius, center.y - radius * 0.5f, radius * 2.0f, radius },
                                 cdSize, WUXIA_GOLD_BRIGHT);
    } else {
        if (icon != WUXIA_ICON_NONE) {
            Vector2 ic = (shortLabel && shortLabel[0])
                ? (Vector2){ center.x, center.y - radius * 0.18f }
                : center;
            WuxiaUI_DrawIcon(icon, ic, radius * 0.36f, ready ? WUXIA_TEXT_PRIMARY : WUXIA_TEXT_MUTED);
        }
        if (shortLabel && shortLabel[0]) {
            float fs = (radius >= 34.0f) ? 13.0f : 11.0f;
            float lw = WuxiaUI_MeasureText(shortLabel, fs);
            while (lw > radius * 1.65f && fs > 9.0f) {
                fs -= 0.5f;
                lw = WuxiaUI_MeasureText(shortLabel, fs);
            }
            float ty = (icon != WUXIA_ICON_NONE) ? (center.y + radius * 0.22f) : (center.y - fs * 0.52f);
            WuxiaUI_DrawText(shortLabel, center.x - lw * 0.5f, ty, fs,
                             ready ? WUXIA_TEXT_PRIMARY : WUXIA_TEXT_MUTED);
        }
    }

    if (!ctx->isTouchMode && hotkey && hotkey[0]) {
        float kw = WuxiaUI_MeasureText(hotkey, 10.5f) + 9.0f;
        if (kw < 18.0f) kw = 18.0f;
        WuxiaUI_DrawKeycap(center.x - kw * 0.5f, center.y + radius - 9.0f, hotkey, active || hov);
    }

    if (hov && (ctx->pointerPressed || ctx->pointerDown || ctx->pointerReleased)) {
        WuxiaUI_ConsumePointer();
    }
    return hov && ctx->pointerReleased;
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

void WuxiaUI_DrawSleekBar(Rectangle r, float ratio, float trailRatio,
                          Color fillStart, Color fillEnd, bool slanted) {
    (void)slanted;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    if (trailRatio < ratio) trailRatio = ratio;
    if (trailRatio > 1.0f) trailRatio = 1.0f;

    DrawRectangleRec(r, (Color){ 12, 14, 20, 215 });
    int tw = (int)(r.width * trailRatio);
    if (tw > 0) {
        DrawRectangle((int)r.x, (int)r.y, tw, (int)r.height, ColorAlpha(WUXIA_TEXT_PRIMARY, 0.55f));
    }
    int fw = (int)(r.width * ratio);
    if (fw > 0) {
        DrawRectangleGradientH((int)r.x, (int)r.y, fw, (int)r.height, fillStart, fillEnd);
        DrawRectangle((int)r.x, (int)r.y, fw, 1, ColorAlpha(WHITE, 0.35f));
    }
    DrawRectangleLinesEx(r, 1.0f, ColorAlpha(WUXIA_BORDER_SUBTLE, 0.72f));
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
