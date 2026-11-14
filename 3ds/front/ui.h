/* ui.h: the front end's drawing helpers (citro2d): text in the system font, panels, a palette
   of the dashboard's colours. */
#ifndef UW3DS_UI_H
#define UW3DS_UI_H
#include <citro2d.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

/* The house palette shared by all of Ryan's 3DS ports (claude/3ds-look-and-feel.md). */
#define COL_BG       C2D_Color32(8, 12, 40, 255)
#define COL_PANEL    C2D_Color32(40, 56, 120, 255)
#define COL_PANEL2   C2D_Color32(56, 76, 150, 255)
#define COL_EDGE     C2D_Color32(120, 150, 230, 255)
#define COL_PRESS    C2D_Color32(220, 180, 60, 255)
#define COL_SHADOW   C2D_Color32(20, 30, 70, 255)
#define COL_TEXT     C2D_Color32(240, 240, 240, 255)
#define COL_DIM      C2D_Color32(150, 150, 165, 255)
#define COL_GOLD     C2D_Color32(255, 204, 64, 255)
#define COL_HINTBG   C2D_Color32(4, 6, 20, 255)
#define COL_RED      C2D_Color32(255, 80, 64, 255)
#define COL_BLUE     C2D_Color32(70, 110, 220, 255)
#define COL_GREEN    C2D_Color32(90, 180, 80, 255)
#define COL_POISON   C2D_Color32(120, 200, 60, 255)
#define COL_SHADE    C2D_Color32(0, 0, 0, 170)

void ui_init(void);
void ui_begin(void);                    /* once a frame, before any text */
/* Text at x, y (the top of the line) in the given size (1.0 is about 30 pixels high; 0.5 is
   the usual size). FLAGS: C2D_AlignLeft/Center/Right, C2D_WordWrap with WRAP's width. Returns
   the height drawn. */
float ui_text(float x, float y, float size, u32 col, int flags, float wrap, const char *s);
float ui_textf(float x, float y, float size, u32 col, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
float ui_text_width(float size, const char *s);
/* ui_text on one line, shrunk (down to 0.28) until it fits MAXW pixels: a label never runs off. */
float ui_text_fit(float x, float y, float size, u32 col, int flags, float maxw, const char *s);
void ui_rect(float x, float y, float w, float h, u32 col);
void ui_frame(float x, float y, float w, float h, u32 fill, u32 edge);
void ui_bar(float x, float y, float w, float h, float frac, u32 col, const char *label);
/* House layout pieces: the 22 px header bar (title in gold at x 8, optional status on the
   right) and the 16 px hint strip at y 224 (what the buttons do now; also where toasts go). */
void ui_header(float w, const char *title, const char *status);
void ui_hint(float w, const char *text, int toast);
/* A button on the bottom screen; 1 when the touch released inside it this frame. */
int ui_button(float x, float y, float w, float h, const char *label, int lit);
/* Buttons without the stylus (see ui.c): ui_nav_frame(on) before a screen's buttons are drawn,
   ui_nav(dx, dy, press) from the D-pad and A; ui_nav_reset() when a different menu opens. */
void ui_nav_frame(int on);
void ui_nav(int dx, int dy, int press);
void ui_nav_reset(void);
/* The same, but ENABLED 0 draws it greyed out: no press look, no click, always returns 0. */
int ui_button_en(float x, float y, float w, float h, const char *label, int lit, int enabled);
void ui_touch(int touching, int x, int y);     /* the frame's touch, for ui_button */
/* The stereo offset for the current eye: things drawn at depth D (0 the screen, positive
   in front of it) move by this times D. */
extern float ui_eye_shift;

#endif
