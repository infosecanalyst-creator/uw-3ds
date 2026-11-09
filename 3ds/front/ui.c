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
    y = (float)(int)(y + 0.5f);         /* whole pixels down: no glyph row is split, so text stays crisp */
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

int ui_button_en(float x, float y, float w, float h, const char *label, int lit, int enabled)
{
    if (!enabled) {
        ui_frame(x, y, w, h, COL_BG, COL_PANEL2);
        ui_text_fit(x + w / 2, y + (h - 16) / 2, 0.5f, C2D_Color32(96, 90, 80, 255), C2D_AlignCenter, w - 6, label);
        return 0;
    }
    return ui_button(x, y, w, h, label, lit);
}

/* ---- buttons without the stylus ----
   A rule of all these ports: anything you can tap you can also reach with the buttons, so a
   menu works when its screen can't be touched (swapped to the top) as well as when it can.
   While ui_nav_frame(1) is in force, every button drawn registers its rectangle; the D-pad
   moves a gold highlight to the nearest button in that direction (by the rectangles of the
   frame before) and A presses the highlighted one, with the same press animation as a tap. */
#define NAV_MAX 64
static float nav_r[NAV_MAX][4], nav_prev[NAV_MAX][4];
static int nav_n, nav_n_prev, nav_on, nav_focus, nav_fire, nav_flash;

void ui_nav_frame(int on)
{
    memcpy(nav_prev, nav_r, sizeof nav_r);
    nav_n_prev = nav_n;
    nav_n = 0;
    if (on && !nav_on) nav_focus = 0;
    nav_on = on;
    if (nav_focus >= nav_n_prev && nav_n_prev) nav_focus = nav_n_prev - 1;
    if (nav_flash) nav_flash--;
}

void ui_nav_reset(void) { nav_focus = 0; nav_fire = 0; }

void ui_nav(int dx, int dy, int press)
{
    int i, best = -1;
    float cx, cy, bd = 1e9f;
    if (!nav_on || !nav_n_prev) return;
    if (press) { nav_fire = 1; nav_flash = 6; }
    if (!dx && !dy) return;
    cx = nav_prev[nav_focus][0] + nav_prev[nav_focus][2] / 2;
    cy = nav_prev[nav_focus][1] + nav_prev[nav_focus][3] / 2;
    for (i = 0; i < nav_n_prev; i++) {
        float x = nav_prev[i][0] + nav_prev[i][2] / 2, y = nav_prev[i][1] + nav_prev[i][3] / 2;
        float ax = (x - cx) * dx, ay = (y - cy) * dy, along, across, d;
        if (i == nav_focus) continue;
        along = dx ? ax : ay;                       /* how far in the asked direction */
        across = dx ? (y - cy) : (x - cx);          /* how far off that line */
        if (along < 2) continue;
        d = along + 2.5f * (across < 0 ? -across : across);
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) nav_focus = best;
}

int ui_button(float x, float y, float w, float h, const char *label, int lit)
{
    int in = t_x >= x && t_x < x + w && t_y >= y && t_y < y + h;
    int in0 = t_x0 >= x && t_x0 < x + w && t_y0 >= y && t_y0 < y + h;
    int idx = nav_n < NAV_MAX ? nav_n++ : -1;
    int focused = nav_on && idx == nav_focus;
    int pressed = (t_now && in && in0) || (focused && nav_flash);
    if (idx >= 0) { nav_r[idx][0] = x; nav_r[idx][1] = y; nav_r[idx][2] = w; nav_r[idx][3] = h; }
    if (focused) {                                  /* a second gold ring outside: the highlight */
        ui_rect(x - 3, y - 3, w + 6, 2, COL_GOLD); ui_rect(x - 3, y + h + 1, w + 6, 2, COL_GOLD);
        ui_rect(x - 3, y - 1, 2, h + 2, COL_GOLD); ui_rect(x + w + 1, y - 1, 2, h + 2, COL_GOLD);
    }
    ui_frame(x, y, w, h, pressed ? COL_EDGE : (lit || focused) ? COL_PANEL2 : COL_PANEL, (lit || focused) ? COL_GOLD : COL_EDGE);
    ui_text_fit(x + w / 2, y + (h - 16) / 2, 0.5f, (lit || focused) ? COL_GOLD : COL_TEXT, C2D_AlignCenter, w - 6, label);
    if (focused && nav_fire) { nav_fire = 0; return 1; }
    return !t_now && t_was && in && in0;
}

float ui_text_fit(float x, float y, float size, u32 col, int flags, float maxw, const char *s)
{
    float sz = size;
    while (sz > 0.28f && ui_text_width(sz, s) > maxw) sz *= 0.94f;
    return ui_text(x, y + (size - sz) * 15.0f, sz, col, flags & ~C2D_WordWrap, 0, s);
}
