/* plat3ds.h: what the 3DS backend (plat_3ds.c) offers the front end (3ds/front), and what the
   front end gives it back. The backend owns the screens, the threads, the sound and the
   game's keyboard and mouse; the front end decides what the buttons mean and draws the top
   screen and the overlays on the bottom one. */
#ifndef UW3DS_PLAT3DS_H
#define UW3DS_PLAT3DS_H
#include <3ds.h>
#include <citro2d.h>
#include <stdint.h>

#if defined(GAME_UW2)
#define UW3DS_GAME "uw2"
#define UW3DS_TITLE "Ultima Underworld II"
#else
#define UW3DS_GAME "uw1"
#define UW3DS_TITLE "Ultima Underworld"
#endif
#define UW3DS_ROOT "/3ds/" UW3DS_GAME          /* the app's folder on the SD card */
#define UW3DS_HOME UW3DS_ROOT "/home"          /* saved games and settings */
#define UW3DS_DATA UW3DS_ROOT "/game"          /* the user's own copy of the game */

/* PC set-1 scan codes; E0 marks the extended keys (the grey arrows). */
#define K_E0 0x100
enum {
    K_ESC = 0x01, K_1 = 0x02, K_2 = 0x03, K_3 = 0x04, K_4, K_5, K_6, K_7, K_8, K_9, K_0 = 0x0B,
    K_BKSP = 0x0E, K_TAB = 0x0F, K_Q = 0x10, K_W = 0x11, K_E = 0x12, K_R = 0x13, K_T = 0x14,
    K_Y = 0x15, K_U = 0x16, K_I = 0x17, K_O = 0x18, K_P = 0x19, K_ENTER = 0x1C, K_LCTRL = 0x1D,
    K_A = 0x1E, K_S = 0x1F, K_D = 0x20, K_F = 0x21, K_G = 0x22, K_H = 0x23, K_J = 0x24, K_K = 0x25,
    K_L = 0x26, K_SEMI = 0x27, K_GRAVE = 0x29, K_LSHIFT = 0x2A, K_Z = 0x2C, K_X = 0x2D, K_C = 0x2E,
    K_V = 0x2F, K_B = 0x30, K_N = 0x31, K_M = 0x32, K_PERIOD = 0x34, K_LALT = 0x38, K_SPACE = 0x39,
    K_F1 = 0x3B, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10 = 0x44,
    K_KP5 = 0x4C,
    K_UP = K_E0 | 0x48, K_LEFT = K_E0 | 0x4B, K_RIGHT = K_E0 | 0x4D, K_DOWN = K_E0 | 0x50,
};

/* The game's keyboard: a key held down (1) or let go (0). Repeated presses of a key already
   down are dropped, so the front end can set keys from the buttons every frame. */
void p3_key(int code, int down);
/* A tap: down now, up after FRAMES screen frames (0: the next frame). */
void p3_tap(int code, int frames);
/* Every key the front end holds, let go. */
void p3_release_keys(void);

/* The game's mouse, in the game's screen pixels (0..319, 0..199). */
void p3_mouse_move(float x, float y);
void p3_mouse_button(unsigned button, int down);    /* PLAT_BUTTON_* */
void p3_mouse_rel(float dx, float dy);              /* relative motion (mouse-look) */
int p3_mouse_captured(void);                        /* the game asked for mouse-look's capture */
void p3_mouse_pos(float *x, float *y);

/* The game's picture as last scanned out: indexed pixels, its size and the DAC's palette. */
const uint8_t *p3_picture(int *w, int *h, const uint8_t **rgb6);

/* Draws part of the game's picture (SX, SY, SW, SH in its pixels) at DX, DY, DW, DH on the
   screen being drawn (the stereo page's raised view). */
void p3_draw_picture(float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh);

/* The settings screen of the port (F11 on the desktop). */
void p3_settings(int open);
int p3_settings_open(void);

/* A frame counter (one per screen refresh), and milliseconds since start. */
unsigned p3_frame(void);
uint64_t p3_ms(void);

/* The 3D slider's depth (0 off .. 1), for the stereo tab. */
float p3_slider(void);

/* The game's own clock stood still (the port menu is open) or going (0). */
void p3_pause(int on);

/* Asks the backend to quit (the game's files are already written by the game). */
void p3_quit(void);

/* ---- the front end (3ds/front) ---- */
void front_init(void);
/* The buttons this frame, the sticks and the touch. Returns 1 when the front end took the
   touch itself (an overlay), so the backend does not give it to the game. */
int front_input(u32 down, u32 held, u32 up, circlePosition cp, circlePosition cs, int touching, touchPosition t);
/* Which mouse button a new touch presses: PLAT_BUTTON_LEFT, or _RIGHT while a modifier is held. */
unsigned front_touch_button(void);
/* Draws the top screen (the dashboard). EYE is 0 for the left image (or mono), 1 for the right. */
void front_draw_top(int eye, float iod);
/* Draws over the game on the bottom screen (the port menu, hints); 1 if it covered the game. */
int front_draw_bottom(void);
/* Once a frame, after the game's picture was scanned out (the front end reads the game's state). */
void front_tick(void);

#endif
