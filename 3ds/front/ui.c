/* ui.c: drawing helpers for the front end (ui.h). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"

static C2D_TextBuf tb;
float ui_eye_shift;

static int t_now, t_was, t_x, t_y, t_x0, t_y0;

void ui_init(void)
{
    tb = C2D_TextBufNew(16384);
}

void ui_begin(void)
{
    C2D_TextBufClear(tb);
}

float ui_text(float x, float y, float size, u32 col, int flags, float wrap, const char *s)
{
    C2D_Text t;
    float w, h;
    if (!s || !*s) return 0;
    if (!C2D_TextParse(&t, tb, s)) return 0;
    C2D_TextOptimize(&t);
    if (flags & C2D_WordWrap)
        C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, size, size, col, wrap);
    else
        C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, size, size, col);
    if (flags & C2D_WordWrap) {
        /* the wrapped height: C2D gives the unwrapped; count the lines by width */
        C2D_TextGetDimensions(&t, size, size, &w, &h);
        return h * (1 + (int)(w / (wrap > 1 ? wrap : 1)));
    }
    C2D_TextGetDimensions(&t, size, size, &w, &h);
    return h;
}

float ui_textf(float x, float y, float size, u32 col, const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    return ui_text(x, y, size, col, 0, 0, b);
}

float ui_text_width(float size, const char *s)
{
    C2D_Text t;
    float w, h;
    if (!s || !*s || !C2D_TextParse(&t, tb, s)) return 0;
    C2D_TextGetDimensions(&t, size, size, &w, &h);
    return w;
}

void ui_rect(float x, float y, float w, float h, u32 col)
{
    C2D_DrawRectSolid(x, y, 0.5f, w, h, col);
}

void ui_frame(float x, float y, float w, float h, u32 fill, u32 edge)
{
    ui_rect(x, y, w, h, edge);
    ui_rect(x + 1, y + 1, w - 2, h - 2, fill);
}

void ui_bar(float x, float y, float w, float h, float frac, u32 col, const char *label)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    ui_frame(x, y, w, h, COL_BG, COL_EDGE);
    ui_rect(x + 1, y + 1, (w - 2) * frac, h - 2, col);
    if (label) ui_text(x + w / 2, y + (h - 13) / 2, 0.42f, COL_TEXT, C2D_AlignCenter, 0, label);
}

void ui_touch(int touching, int x, int y)
{
    t_was = t_now;
    t_now = touching;
    if (touching) {
        if (!t_was) { t_x0 = x; t_y0 = y; }
        t_x = x; t_y = y;
    }
}

int ui_button(float x, float y, float w, float h, const char *label, int lit)
{
    int in = t_x >= x && t_x < x + w && t_y >= y && t_y < y + h;
    int in0 = t_x0 >= x && t_x0 < x + w && t_y0 >= y && t_y0 < y + h;
    int pressed = t_now && in && in0;
    ui_frame(x, y, w, h, pressed ? COL_EDGE : lit ? COL_PANEL2 : COL_PANEL, lit ? COL_GOLD : COL_EDGE);
    ui_text_fit(x + w / 2, y + (h - 16) / 2, 0.5f, lit ? COL_GOLD : COL_TEXT, C2D_AlignCenter, w - 6, label);
    return !t_now && t_was && in && in0;
}

float ui_text_fit(float x, float y, float size, u32 col, int flags, float maxw, const char *s)
{
    float sz = size;
    while (sz > 0.28f && ui_text_width(sz, s) > maxw) sz *= 0.94f;
    return ui_text(x, y + (size - sz) * 15.0f, sz, col, flags & ~C2D_WordWrap, 0, s);
}
