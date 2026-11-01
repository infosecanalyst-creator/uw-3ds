/* ui.h: the front end's drawing helpers (citro2d): text in the system font, panels, a palette
   of the dashboard's colours. */
#ifndef UW3DS_UI_H
#define UW3DS_UI_H
#include <citro2d.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

/* The dashboard's colours: dark stone, parchment, Britannian gold. */
#define COL_BG       C2D_Color32(18, 16, 14, 255)
#define COL_PANEL    C2D_Color32(38, 34, 30, 255)
#define COL_PANEL2   C2D_Color32(54, 48, 42, 255)
#define COL_EDGE     C2D_Color32(96, 84, 66, 255)
#define COL_TEXT     C2D_Color32(226, 214, 188, 255)
#define COL_DIM      C2D_Color32(150, 140, 120, 255)
#define COL_GOLD     C2D_Color32(232, 190, 92, 255)
#define COL_RED      C2D_Color32(200, 54, 44, 255)
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
/* A button on the bottom screen; 1 when the touch released inside it this frame. */
int ui_button(float x, float y, float w, float h, const char *label, int lit);
void ui_touch(int touching, int x, int y);     /* the frame's touch, for ui_button */
/* The stereo offset for the current eye: things drawn at depth D (0 the screen, positive
   in front of it) move by this times D. */
extern float ui_eye_shift;

#endif
