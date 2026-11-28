/* front.c: the 3DS front end. The buttons as the game's keys (the port's modern-keys and
   rune-keys enhancements), the spell hotkeys, the screens' layout and the port menu (START),
   which pauses the game.

   Layout: while you explore, the top screen is the game's 3D view alone, as tall as the screen,
   with stereoscopic depth from the 3D slider. The bottom screen is the whole game screen,
   played with the stylus as the mouse (inventory, icons, runes, the scroll); where its small 3D
   view would be, the port draws a pointer pad: slide on it to move the game's pointer inside
   the view (seen on the top screen), tap to click. So the view is on one screen only. While you
   talk, the game's whole screen is on the bottom as it is and the top shows the view in a frame.
   Under the game is a strip of tabs: Game, Map, Status, Log, Journal, Spells. The
   other tabs swap the dashboard's pages in for the game on the bottom screen; SELECT steps
   through them. Before you are in the Abyss (the menus, character creation) the game fills the
   bottom screen and the top screen shows the title.

   Buttons: Circle Pad walk/run/back and turn; C-stick slide and look up and down; L attack
   (held to charge); A use (the confirm button), X look (at the cursor); Y the game's map; ZR jump;
   R held makes the stylus the right button; B escape (the back button; Settings can swap A and B); D-pad the spell hotkeys; SELECT the next page
   (held with the Circle Pad: scroll it; with L or R: the page's previous or next item);
   START the port menu. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "plat3ds.h"
#include "ui.h"
#include "uwsnap.h"
#include "front.h"
#include "uwglue.h"
#include "strings3ds.h"

enum { TAB_GAME, TAB_MAP, TAB_STATUS, TAB_LOG, TAB_JOURNAL, TAB_SPELLS, TABS };
static const char *const tab_names[TABS] = { "Game", "Map", "Status", "Log", "Journal", "Spells" };

/* the bottom screen: the game (or a page) above the tab strip */
#define STRIP_H 32                       /* tabs 26 px tall, 4 px clear of the bottom edge (rule 11) */
#define PG_W BOT_W
#define PG_H (BOT_H - STRIP_H)
enum { OV_NONE, OV_MENU, OV_HOTKEYS, OV_MAP, OV_MANUAL, OV_QUIT, OV_OPTIONS, OV_LOAD };

static int tab, overlay;
static int drag_acc, drag_mom_tab = -1;   /* the page drag's rows and glide (UW-10): reset by a menu or a page change */
static float drag_mom;
static int log_back, journal_person = -1, journal_back, spells_scroll;
static int hotkey[4] = { -1, -1, -1, -1 };     /* up, right, down, left: a spell's index */
static int pick_spell = -1, pick_scroll;
static char toast[96];
static unsigned toast_until;
static int moved_since_touch;
static int pad_touch;                   /* the stylus is down on the pointer pad (its edge lights up) */
static int select_used;
static int lefty;                       /* Settings: left-handed play, the D-pad and A/B/X/Y swap roles */
static int tip_seen;                    /* the first-steps tip was shown once (kept in 3ds.cfg) */
static unsigned tip_until;
static int swap_ab;                     /* Settings: swap the A and B buttons (A confirms by default) */
#define CONFIRM_KEY (swap_ab ? KEY_B : KEY_A)
#define BACK_KEY (swap_ab ? KEY_A : KEY_B)
unsigned front_confirm_key(void) { return CONFIRM_KEY; }
unsigned front_back_key(void) { return BACK_KEY; }
int front_lefty(void) { return lefty; }
/* The name of the button the player presses for a logical key (left-handed mode swaps D-pad and A/B/X/Y). */
const char *front_key_name(unsigned k)
{
    if (k == KEY_A) return lefty ? "D-Right" : "A";
    if (k == KEY_B) return lefty ? "D-Down" : "B";
    if (k == KEY_X) return lefty ? "D-Left" : "X";
    if (k == KEY_Y) return lefty ? "D-Up" : "Y";
    return "?";
}

/* the game's 3D view on its 320x200 screen (UWEDIT.C's place_3d_view: x, the bottom row counted
   from the screen's bottom, width, height) */
#ifdef GAME_UW2
#define VIEW_X 16                       /* place_3d_view(16, 182, 208, 128) */
#define VIEW_Y 17
#define VIEW_W 208
#define VIEW_H 128
#else
#define VIEW_X 52                       /* place_3d_view(52, 180, 172, 113) */
#define VIEW_Y 19
#define VIEW_W 172
#define VIEW_H 113
#endif

/* The pointer pad: the small view's place on the touch screen (the game's 200 lines are
   stretched to the PG_H above the tabs), a pixel wider than the stretch so no row of the view
   shows beside it. */
#define PAD_X ((float)VIEW_X)
#define PAD_Y ((float)(VIEW_Y * PG_H / 200))
#define PAD_W ((float)VIEW_W)
#define PAD_H ((float)(((VIEW_Y + VIEW_H) * PG_H + 199) / 200 - VIEW_Y * PG_H / 200))

static void say(const char *m)
{
    snprintf(toast, sizeof toast, "%s", m);
    toast_until = p3_frame() + 180;
}

/* slot 4 on the card: bit 0 the quicksave, bit 1 the save before it (SAVE4.BAK). Read when the
   port menu opens, never every frame. */
static int qs_state;

/* ---- the front end's own settings: the page, the hotkeys ---- */

#define OPTS UW3DS_HOME "/3ds.cfg"

/* UW-13: 3ds.cfg is written only when a setting changed (opts_save just marks it), and then when
   the menu closes, 3 s after the last change (a page tab tapped in play), or at exit; never twice
   with the same text. Temp file, checked write and close, then swapped in. */
static int opts_dirty;
static unsigned opts_stamp;
static char opts_last[256];

static int opts_format(char *buf, size_t sz)
{
    return snprintf(buf, sz, "page=%d\nzoom=%d\nhotkeys=%d,%d,%d,%d\nswapab=%d\nlefty=%d\ntip=%d\n", tab, map_zoom_level(), hotkey[0], hotkey[1], hotkey[2], hotkey[3], swap_ab, lefty, tip_seen);
}

static void opts_flush(void)
{
    char buf[256];
    FILE *f;
    int n;
    if (!opts_dirty) return;
    opts_dirty = 0;
    n = opts_format(buf, sizeof buf);
    if (n <= 0 || n >= (int)sizeof buf || !strcmp(buf, opts_last)) return;
    f = fopen(OPTS ".tmp", "w");
    if (!f) return;
    if (fwrite(buf, 1, (size_t)n, f) != (size_t)n || fclose(f) != 0) { remove(OPTS ".tmp"); return; }
    remove(OPTS ".bak");
    rename(OPTS, OPTS ".bak");
    if (rename(OPTS ".tmp", OPTS) != 0) { rename(OPTS ".bak", OPTS); return; }
    strcpy(opts_last, buf);
}

static void opts_save(void)
{
    opts_dirty = 1;
    opts_stamp = p3_frame();
}

static void opts_load(void)
{
    char l[128];
    FILE *f = fopen(OPTS, "r");
    if (!f) return;
    while (fgets(l, sizeof l, f)) {
        int z;
        if (sscanf(l, "page=%d", &tab) == 1 && (tab < 0 || tab >= TABS)) tab = 0;
        if (sscanf(l, "zoom=%d", &z) == 1) map_zoom(z - map_zoom_level());
        sscanf(l, "hotkeys=%d,%d,%d,%d", &hotkey[0], &hotkey[1], &hotkey[2], &hotkey[3]);
        if (sscanf(l, "swapab=%d", &z) == 1) swap_ab = z != 0;
        if (sscanf(l, "lefty=%d", &z) == 1) lefty = z != 0;
        if (sscanf(l, "tip=%d", &z) == 1) tip_seen = z != 0;
    }
    fclose(f);
    opts_format(opts_last, sizeof opts_last);   /* what is on the card now: writing the same again is skipped */
}

/* Left-handed play: the D-pad and the face buttons swap jobs (Y<->up, A<->right, B<->down, X<->left).
   Applied once to the pad state before anything reads it. */
u32 front_remap(u32 m)
{
    static const struct { u32 a, b; } pr[4] = { { KEY_Y, KEY_DUP }, { KEY_A, KEY_DRIGHT }, { KEY_B, KEY_DDOWN }, { KEY_X, KEY_DLEFT } };
    u32 r = m;
    int i;
    if (!lefty) return m;
    for (i = 0; i < 4; i++) r &= ~(pr[i].a | pr[i].b);
    for (i = 0; i < 4; i++) {
        if (m & pr[i].a) r |= pr[i].b;
        if (m & pr[i].b) r |= pr[i].a;
    }
    return r;
}

void front_init(void)
{
    ui_init();
    manual_load();
    opts_load();
    atexit(opts_flush);
    uw_journal_open();
}

/* ---- key sequences (a spell hotkey's runes), one step every other frame ---- */

static struct { int code, down; } seq[256];
static int nseq, seqi;
static unsigned seq_next;

static void seq_add(int code, int down);

/* one character as the PC keyboard types it: its key, with Shift for capitals and symbols */
static void type_char(int c)
{
    static const char *const plain = "1234567890-=qwertyuiop[]asdfghjkl;'`\\zxcvbnm,./ ";
    static const char *const shift = "!@#$%^&*()_+QWERTYUIOP{}ASDFGHJKL:\"~|ZXCVBNM<>? ";
    static const unsigned char code[] = { 0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B, 0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2B,
        0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x39 };
    const char *p;
    int k, sh = 0;
    if ((p = strchr(plain, c)) != NULL && c) k = (int)(p - plain);
    else if ((p = strchr(shift, c)) != NULL && c) { k = (int)(p - shift); sh = 1; }
    else return;
    if (sh) seq_add(K_LSHIFT, 1);
    seq_add(code[k], 1); seq_add(code[k], 0);
    if (sh) seq_add(K_LSHIFT, 0);
}

static void seq_add(int code, int down) { if (nseq < 256) { seq[nseq].code = code; seq[nseq].down = down; nseq++; } }

static void seq_run(void)
{
    if (seqi >= nseq) { nseq = seqi = 0; return; }
    if ((int)(p3_frame() - seq_next) < 0) return;
    p3_key(seq[seqi].code, seq[seqi].down);
    seqi++;
    seq_next = p3_frame() + 2;
}

/* the letters' scan codes: the rune-keys letters a..y */
static const int letter[26] = { K_A, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M,
                                K_N, K_O, K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z };

static int spell_known(const struct uwspell *sp, uint32_t runes)
{
    int k;
    for (k = 0; k < 3; k++)
        if (sp->rune[k] < 24 && !(runes & (1u << sp->rune[k]))) return 0;
    return 1;
}

static void cast_hotkey(int slot)
{
    int n, k;
    const struct uwspell *sp = uw_spells(&n);
    int i = hotkey[slot];
    if (i < 0 || i >= n) { say("No spell on that direction: START, Spell hotkeys."); return; }
    if (nseq) return;
    /* Ctrl+Alt held: Backspace clears the shelf, each rune's letter puts it on, Space casts */
    seq_add(K_LCTRL, 1); seq_add(K_LALT, 1);
    seq_add(K_BKSP, 1); seq_add(K_BKSP, 0);
    for (k = 0; k < 3; k++)
        if (sp[i].rune[k] < 24) {
            int r = sp[i].rune[k], c = r == 23 ? letter['y' - 'a'] : letter[r];
            seq_add(c, 1); seq_add(c, 0);
        }
    seq_add(K_SPACE, 1); seq_add(K_SPACE, 0);
    seq_add(K_LALT, 0); seq_add(K_LCTRL, 0);
    seq_next = p3_frame();
}

/* ---- the game's controls ---- */

static void stick_keys(circlePosition cp, circlePosition cs, int scrolling)
{
    int fwd, run, back, l, r;
    if (scrolling) { cp.dx = cp.dy = 0; }
    fwd = cp.dy > 40; run = cp.dy > 115; back = cp.dy < -40;
    l = cp.dx < -50; r = cp.dx > 50;
    if (fwd || back || l || r || cs.dx < -40 || cs.dx > 40) moved_since_touch = 1;
    p3_key(K_W, run);
    p3_key(K_X, fwd && !run);
    p3_key(K_S, back);
    /* Circle Pad: walk, run, back and strafe (UW's A and D slide); C-stick: turn (the arrows)
       and look up and down */
    p3_key(K_A, l);
    p3_key(K_D, r);
    p3_key(K_LEFT, cs.dx < -40);
    p3_key(K_RIGHT, cs.dx > 40);
    p3_key(K_UP, cs.dy > 40);
    p3_key(K_DOWN, cs.dy < -40);
}

/* An action at the cursor: after walking with the sticks it acts straight ahead, at the
   view's centre, rather than where the stylus last was. */
static void at_cursor(int code)
{
    const struct uwsnap *s = uw_snap();
    if (moved_since_touch && s->mode == SNAP_VIEW) {
        p3_mouse_move(VIEW_X + VIEW_W / 2, VIEW_Y + VIEW_H / 2);
        moved_since_touch = 0;
    }
    p3_tap(code, 2);
}

static void open_menu(int ov)
{
    overlay = ov;
    if (ov == OV_MENU) qs_state = uw_slot_state(4);
    drag_mom = 0; drag_acc = 0;
    p3_release_keys();
    p3_pause(ov != OV_NONE);
}

static void tab_scroll(int d, int lr)
{
    switch (tab) {
    case TAB_LOG: log_back += d; break;
    case TAB_JOURNAL:
        if (lr) {
            int n = uw_journal_people();
            if (journal_person < 0) journal_person = uw_journal_latest();
            journal_person += lr;
            if (journal_person < 0) journal_person = n - 1;
            if (journal_person >= n) journal_person = 0;
            journal_back = 0;
        } else
            journal_back += d;
        break;
    case TAB_SPELLS: spells_scroll += d; break;
    case TAB_MAP: if (lr) map_level(lr); else if (d) map_zoom(d > 0 ? -1 : 1); break;
    }
    if (log_back < 0) log_back = 0;
    if (journal_back < 0) journal_back = 0;
    if (spells_scroll < 0) spells_scroll = 0;
}

static int menu_input(u32 down, u32 held, circlePosition cp, circlePosition cs, int touching, touchPosition t);
static void manual_lefty(void);

int front_menu_open(void) { return overlay != 0; }

/* The pointer pad is up: exploring (the game's own map, a conversation, a full-screen cutscene,
   the menus and character creation redraw the whole game screen, so it is off there), the Game
   page, no port menu. */
extern volatile int uw_cuts_full;      /* game/uwstate.c: a full-screen cutscene is playing */
static int pad_on(void)
{
    const struct uwsnap *s = uw_snap();
    return !overlay && s->in_game && s->mode == SNAP_VIEW && !uw_cuts_full && tab == TAB_GAME;
}

int front_pad_at(int px, int py)
{
    return pad_on() && px >= PAD_X && px < PAD_X + PAD_W && py >= PAD_Y && py < PAD_Y + PAD_H;
}

void front_pad_view(float *x0, float *y0, float *x1, float *y1)
{
    *x0 = VIEW_X; *y0 = VIEW_Y; *x1 = VIEW_X + VIEW_W - 1; *y1 = VIEW_Y + VIEW_H - 1;
}

float front_game_height(void)
{
    const struct uwsnap *s = uw_snap();
    if (overlay) return BOT_H;                  /* the port menu covers whatever is under it */
    if (!s->in_game) return BOT_H;              /* menus, character creation: the whole screen */
    return tab == TAB_GAME ? PG_H : 0;
}

int front_input(u32 down, u32 held, u32 up, circlePosition cp, circlePosition cs, int touching, touchPosition t)
{
    static unsigned scroll_at;
    static int was_touching, touch_mine;
    const char *r = uw_request_result();
    int mine;
    if (r) say(r);
    seq_run();
    /* rule 13: the manual, and the Log, Journal and Spells pages, scroll by dragging */
    ui_drag_scroll(overlay == OV_MANUAL || (!overlay && (tab == TAB_LOG || tab == TAB_JOURNAL || tab == TAB_SPELLS)));
    ui_touch(touching, t.px, t.py);
    if (overlay) { drag_mom = 0; drag_acc = 0; was_touching = touching; touch_mine = 0; return menu_input(down, held, cp, cs, touching, t); }
    /* a touch belongs to the front end (the tabs, a page) or to the game, by where it started */
    if (touching && !was_touching) {
        float gh = front_game_height();
        touch_mine = gh <= 0 || t.py >= gh;
        pad_touch = !touch_mine && front_pad_at(t.px, t.py);
    }
    if (!touching) pad_touch = 0;
    mine = (touching || was_touching) && touch_mine;
    was_touching = touching;
    {   /* drag a page: the text follows the stylus, one entry or row per 14 px, with a little momentum */
        int dy = 0;
        if (tab != drag_mom_tab) { drag_mom_tab = tab; drag_mom = 0; drag_acc = 0; }   /* UW-10: a page change ends the glide */
        if (mine && ui_dragging()) {
            dy = ui_drag_dy();
            drag_mom = dy > 60 ? 60.0f : dy < -60 ? -60.0f : (float)dy;   /* a flick can't throw the page a whole log */
        } else if (!touching && (drag_mom > 0.5f || drag_mom < -0.5f)) {
            dy = (int)drag_mom;
            drag_mom *= 0.88f;
        } else {
            drag_mom = 0; drag_acc = 0;
        }
        if (dy && tab != TAB_GAME && tab != TAB_MAP) {
            drag_acc += tab == TAB_SPELLS ? -dy : dy;   /* Log/Journal: newest at the bottom, so down = older */
            while (drag_acc >= 14) { tab_scroll(1, 0); drag_acc -= 14; }
            while (drag_acc <= -14) { tab_scroll(-1, 0); drag_acc += 14; }
        } else if (dy) {
            drag_mom = 0; drag_acc = 0;     /* the Game and Map pages don't scroll: no glide to carry over */
        }
    }
    if (touching && !mine) moved_since_touch = 0;
    if (down & KEY_START) { open_menu(OV_MENU); return 1; }
    /* SELECT: alone, the next page; held, the sticks and L R work the page */
    if (held & KEY_SELECT) {
        if (down & KEY_L) { tab_scroll(0, -1); select_used = 1; }
        if (down & KEY_R) { tab_scroll(0, 1); select_used = 1; }
        if ((cp.dy > 40 || cp.dy < -40) && p3_frame() - scroll_at > 4) {
            tab_scroll(cp.dy > 0 ? 1 : -1, 0);
            scroll_at = p3_frame();
            select_used = 1;
        }
    }
    if (up & KEY_SELECT) {
        if (!select_used && uw_snap()->in_game) { tab = (tab + 1) % TABS; opts_save(); }
        select_used = 0;
    }
    stick_keys(cp, cs, (held & KEY_SELECT) != 0);
    if (held & KEY_SELECT) return mine;
    p3_key(K_SPACE, (held & KEY_L) != 0);       /* attack, held to charge */
    if (down & CONFIRM_KEY) at_cursor(K_E);
    if (down & KEY_X) at_cursor(K_Q);
    if (down & KEY_Y) p3_tap(K_Z, 2);           /* the game's map */
    if (down & KEY_ZR) p3_tap(K_J, 2);          /* jump */
    if (down & BACK_KEY) p3_tap(K_ESC, 2);
    if (uw_snap()->in_game) {                   /* no spell hotkeys (or their notice) on the logos and menus */
        if (down & KEY_DUP) cast_hotkey(0);
        if (down & KEY_DRIGHT) cast_hotkey(1);
        if (down & KEY_DDOWN) cast_hotkey(2);
        if (down & KEY_DLEFT) cast_hotkey(3);
    }
    return mine;
}

unsigned front_touch_button(void)
{
    return (hidKeysHeld() & KEY_R) ? PLAT_BUTTON_RIGHT : PLAT_BUTTON_LEFT;
}

static void keyboard(void)
{
    char prompt[80], out[64];
    int maxlen, digits, i;
    SwkbdState kb;
    SwkbdButton b;
    if (!uw_text_wanted(prompt, sizeof prompt, &maxlen, &digits)) return;
    if (maxlen > 60) maxlen = 60;
    p3_release_keys();
    swkbdInit(&kb, digits ? SWKBD_TYPE_NUMPAD : SWKBD_TYPE_QWERTY, 2, maxlen);
    swkbdSetHintText(&kb, prompt[0] ? prompt : "Type here");
    swkbdSetValidation(&kb, SWKBD_ANYTHING, 0, 0);
    out[0] = 0;
    b = swkbdInputText(&kb, out, sizeof out);
    nseq = seqi = 0;
    if (b == SWKBD_BUTTON_CONFIRM) {
        for (i = 0; out[i] && nseq < 240; i++) type_char((unsigned char)out[i]);
        seq_add(K_ENTER, 1); seq_add(K_ENTER, 0);
    } else {
        seq_add(K_ESC, 1); seq_add(K_ESC, 0);
    }
    seq_next = p3_frame() + 2;
}

void front_tick(void)
{
    static unsigned last_flush;
    uw_glue_tick();
    keyboard();
    if (p3_frame() - last_flush > 120) { uw_journal_flush_async(); last_flush = p3_frame(); }   /* UW-07: I/O thread */
    if (opts_dirty && p3_frame() - opts_stamp > 180) opts_flush();
    {   /* UW-12: once per run into the game, tell the journal whose it is; a new character starts a new one */
        static int told;
        const struct uwsnap *s = uw_snap();
        if (s->in_game && !tip_seen && s->mode == SNAP_VIEW && !overlay) {   /* once ever: the first steps */
            tip_until = p3_frame() + 600;       /* ten seconds in the band on the top view (front_draw_top words it) */
            tip_seen = 1; opts_save();
        }
        if (!s->in_game) told = 0;
        else if (!told && s->name[0]) {
            told = 1;
            if (uw_journal_character(s->name, s->pclass, s->female)) { journal_person = -1; journal_back = 0; }
        }
    }
}

/* ---- the dashboard's pages, on the bottom screen above the tabs (PG_W x PG_H) ---- */

static const char *spell_name(int id)
{
    static char b[3][40];
    static int k;
    k = (k + 1) % 3;
    uw_name(6, id, b[k], sizeof b[k]);
    return b[k];
}

static void empty_page(const char *msg)
{
    ui_text(16, 40, 0.48f, COL_DIM, C2D_WordWrap, PG_W - 32, msg);
}

static void draw_status(void)
{
    const struct uwsnap *s = uw_snap();
    char b[96], nm[40];
    int i, y;
    static const char *const hunger_txt[] = { "starving", "famished", "very hungry", "hungry", "peckish", "satisfied", "fed", "well fed", "full" };
    static const char *const tired_txt[] = { "rested", "awake", "tired", "very tired", "drowsy", "exhausted" };
    if (!s->in_game) { empty_page("Your status shows once you are in " UW3DS_PLACE "."); return; }
    uw_name(2, 0x17 + s->pclass, nm, sizeof nm);
    ui_text_fit(8, 3, 0.52f, COL_GOLD, 0, PG_W - 16, s->name);
    snprintf(b, sizeof b, "%s, level %d   %lu exp   %d skill points", nm, s->xlevel, (unsigned long)(s->exp / 10), s->skill_points);
    ui_text_fit(8, 21, 0.4f, COL_DIM, 0, PG_W - 16, b);
    snprintf(b, sizeof b, "Vitality %d / %d", s->hp, s->hpmax);
    ui_bar(8, 37, 148, 16, s->hpmax ? (float)s->hp / s->hpmax : 0, s->poison ? COL_POISON : COL_RED, b);
    snprintf(b, sizeof b, "Mana %d / %d", s->mana, s->manamax);
    ui_bar(164, 37, 148, 16, s->manamax ? (float)s->mana / s->manamax : 0, COL_BLUE, b);
    i = s->hunger / 0x1E; if (i > 8) i = 8;
    y = s->fatigue / 0x17; if (y > 5) y = 5;
    snprintf(b, sizeof b, "You are %s and %s.%s%s", hunger_txt[i], tired_txt[y], s->poison ? "  Poisoned!" : "", s->drunk ? "  Drunk." : "");
    ui_text_fit(8, 57, 0.4f, COL_TEXT, 0, PG_W - 16, b);
    snprintf(b, sizeof b, "Str %d   Dex %d   Int %d   Light %d", s->str, s->dex, s->intel, s->light);
    ui_text_fit(8, 72, 0.4f, COL_TEXT, 0, PG_W - 16, b);
    /* the spells working on you */
    y = 88;
    ui_text_fit(8, y, 0.4f, COL_GOLD, 0, 84, "Spells on you:");
    if (!s->nspells && !s->hasted && !s->timestop && !s->wizeye && !s->blessed)
        ui_text(96, y, 0.4f, COL_DIM, 0, 0, "none");
    for (i = 0; i < s->nspells; i++) {
        snprintf(b, sizeof b, "%s  (%s)", spell_name(s->spell_id[i]),
                 s->spell_steps[i] <= 2 ? "nearly done" : s->spell_steps[i] <= 10 ? "unstable" : "stable");
        ui_text_fit(96, y + i * 13, 0.4f, COL_TEXT, 0, PG_W - 104, b);
    }
    /* the skills, three columns of seven */
    y = 132;
    for (i = 0; i < 20; i++) {
        float x = 8 + (i / 7) * 104, yy = y + (i % 7) * 11;
        snprintf(b, sizeof b, "%s %d", uw_skill_name(i), s->skills[i]);
        ui_text_fit(x, yy, 0.38f, s->skills[i] ? COL_TEXT : COL_DIM, 0, 100, b);
    }
}

static void draw_log(void)
{
    int n, i, lines = 14;
    float y = PG_H - 4;
    uw_log_lock();
    n = uw_log_count();
    if (log_back > n - 1) log_back = n > 0 ? n - 1 : 0;
    for (i = n - 1 - log_back; i >= 0 && y > 4; i--) {
        float h;
        char l[200];
        snprintf(l, sizeof l, "%s", uw_log_line(i));
        h = 14.0f * ui_wrap_lines(0.42f, l, PG_W - 16);   /* UW-11: the real wrapped height */
        y -= h;
        if (y < 2) break;
        ui_text(8, y, 0.42f, COL_TEXT, C2D_WordWrap, PG_W - 16, l);
        if (--lines < 0) break;
    }
    uw_log_unlock();
    if (!n) empty_page("Messages from the game's scroll gather here.");
    if (log_back) {
        char b[24];
        snprintf(b, sizeof b, "%d back", log_back);
        ui_rect(PG_W - 64, 0, 64, 15, COL_SHADE);
        ui_text_fit(PG_W - 60, 1, 0.38f, COL_GOLD, 0, 56, b);
    }
}

static void draw_journal(void)
{
    int p = journal_person >= 0 ? journal_person : uw_journal_latest(), n, i;
    float y = PG_H - 2;
    char b[48];
    uw_log_lock();
    if (p < 0 && uw_journal_people()) p = uw_journal_people() - 1;
    if (p < 0) {
        uw_log_unlock();
        empty_page("Everything said in your conversations is kept here, by the person you spoke to.");
        return;
    }
    n = uw_journal_count(p);
    if (journal_back > n - 1) journal_back = n > 0 ? n - 1 : 0;
    for (i = n - 1 - journal_back; i >= 0; i--) {
        int who;
        const char *l = uw_journal_line(p, i, &who);
        float h = 13.0f * ui_wrap_lines(0.4f, l, PG_W - 26);   /* UW-11: the width ui_text wraps at */
        y -= h + 2;
        if (y < 18) break;
        ui_text(who == 1 ? 18 : 8, y, 0.4f, who == 1 ? COL_GOLD : who == 2 ? COL_DIM : COL_TEXT, C2D_WordWrap, PG_W - 26, l);
    }
    ui_rect(0, 0, PG_W, 16, COL_PANEL2);
    ui_text_fit(8, 1, 0.42f, COL_GOLD, 0, PG_W - 120, uw_journal_name(p));
    snprintf(b, sizeof b, "%d of %d  SELECT+L/R", p + 1, uw_journal_people());
    ui_text_fit(PG_W - 6, 2, 0.38f, COL_DIM, C2D_AlignRight, 108, b);
    uw_log_unlock();
}

static const char *const dirs[4] = { "Up", "Right", "Down", "Left" };

static void draw_spells(void)
{
    const struct uwsnap *s = uw_snap();
    int n, i, row = 0, k;
    const struct uwspell *sp = uw_spells(&n);
    if (!n) { empty_page("Your spell book fills once you are in " UW3DS_PLACE "."); return; }
    for (i = 0; i < n; i++) {
        char runes[48] = "", num[8];
        const char *hk = "";
        float y;
        if (!spell_known(&sp[i], s->runes)) continue;
        if (row++ < spells_scroll) continue;
        y = 4 + (row - 1 - spells_scroll) * 14;
        if (y > PG_H - 14) break;
        for (k = 0; k < 3; k++)
            if (sp[i].rune[k] < 24) { strcat(runes, uw_rune_name(sp[i].rune[k])); strcat(runes, " "); }
        for (k = 0; k < 4; k++) if (hotkey[k] == i) hk = dirs[k];
        snprintf(num, sizeof num, "%d", i / 6 + 1);
        ui_text(6, y, 0.4f, COL_DIM, 0, 0, num);
        ui_text_fit(20, y, 0.4f, COL_TEXT, 0, 130, sp[i].name);
        ui_text_fit(154, y, 0.4f, COL_GOLD, 0, *hk ? 116 : 160, runes);
        if (*hk) ui_text_fit(PG_W - 4, y, 0.4f, COL_GREEN, C2D_AlignRight, 40, hk);
    }
    if (!row) empty_page("No spells yet: find runestones to learn them.");
}

/* The pointer pad, over the game's small 3D view on the touch screen: what it does, and the
   standing hints that were the top screen's strip (the top is the view alone now). */
static void draw_pad(void)
{
    float x = PAD_X, y = PAD_Y, w = PAD_W, h = PAD_H, cx = x + w / 2, ty, lh;
    char b[64];
    ui_frame(x, y, w, h, COL_BG, pad_touch ? COL_GOLD : COL_EDGE);
    ui_rect(x + 1, y + 1, w - 2, 17, pad_touch ? COL_PANEL2 : COL_PANEL);
    ui_text_fit(cx, y + 2, 0.42f, COL_GOLD, C2D_AlignCenter, w - 10, "POINTER PAD");
    lh = (h - 22) / 7; ty = y + 20;
    ui_text_fit(cx, ty, 0.4f, COL_TEXT, C2D_AlignCenter, w - 10, "Slide: move the pointer"); ty += lh;
    ui_text_fit(cx, ty, 0.4f, COL_TEXT, C2D_AlignCenter, w - 10, "Tap: click where it points"); ty += lh;
    ui_text_fit(cx, ty, 0.4f, COL_TEXT, C2D_AlignCenter, w - 10, "R + tap: right click"); ty += lh;
    snprintf(b, sizeof b, "%s: use   %s: look", front_key_name(CONFIRM_KEY), front_key_name(KEY_X));
    ui_text_fit(cx, ty, 0.4f, COL_TEXT, C2D_AlignCenter, w - 10, b); ty += lh;
    ui_text_fit(cx, ty, 0.4f, COL_DIM, C2D_AlignCenter, w - 10, "Circle Pad: walk   L: attack"); ty += lh;
    ui_text_fit(cx, ty, 0.4f, COL_DIM, C2D_AlignCenter, w - 10, "C-Stick: turn and look"); ty += lh;
    ui_text_fit(cx, ty, 0.4f, COL_DIM, C2D_AlignCenter, w - 10, "START: menu   ZL: swap");
}

static void draw_tabs(void)
{
    int i;
    float w = (float)BOT_W / TABS;
    ui_rect(0, PG_H, BOT_W, STRIP_H, COL_PANEL);
    for (i = 0; i < TABS; i++)
        if (ui_button(i * w + 1, PG_H + 2, w - 2, STRIP_H - 6, tab_names[i], i == tab) && tab != i) {
            tab = i;
            opts_save();
        }
}

/* ---- the top screen ---- */

/* The game's 3D view (or the conversation in its place), about twice its size, floating behind
   the screen's frame by the 3D slider. */
static void draw_big_view(int eye, float iod)
{
    float sc = (TOP_W - 8.0f) / VIEW_W, w, h, x, y, shift = (eye ? 1 : -1) * iod * 7.0f;
    if (216.0f / VIEW_H < sc) sc = 216.0f / VIEW_H;   /* clear of the hint strip at y 224 */
    w = VIEW_W * sc; h = VIEW_H * sc;
    x = (TOP_W - w) / 2; y = (224 - h) / 2;
    p3_draw_picture(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, x - shift, y, w, h);
    /* a frame at the screen's depth, covering the picture's shifted edges */
    ui_rect(0, 0, x + 2, TOP_H, COL_BG);
    ui_rect(x + w - 2, 0, TOP_W - x - w + 2, TOP_H, COL_BG);
    ui_rect(x - 2, y - 4, w + 4, 4, COL_EDGE);
    ui_rect(x - 2, y + h, w + 4, 4, COL_EDGE);
    ui_rect(x - 2, y - 4, 4, h + 8, COL_EDGE);
    ui_rect(x + w - 2, y - 4, 4, h + 8, COL_EDGE);
}

/* Exploring (stage 1 of the new layout): the 3D view alone, as tall as the screen and in its own
   shape (UW1 172x113 -> 365x240, UW2 208x128 -> 390x240), black each side; no frame, no strip.
   The 3D slider floats it behind the screen as before; the black sides widen by the shift so
   both eyes get the same edges. The source is taken half a pixel in, so the smoothing never
   pulls in the frame's pixels around the view. */
static void draw_full_view(int eye, float iod)
{
    u32 black = C2D_Color32(0, 0, 0, 255);
    float w = (VIEW_W - 1) * (float)TOP_H / (VIEW_H - 1), x = (TOP_W - w) / 2;
    float shift = (eye ? 1 : -1) * iod * 7.0f, cover = iod > 0 ? iod * 7.0f : 0;
    ui_rect(0, 0, TOP_W, TOP_H, black);
    p3_draw_picture(VIEW_X + 0.5f, VIEW_Y + 0.5f, VIEW_W - 1, VIEW_H - 1, x - shift, 0, w, TOP_H);
    if (cover > 0) {
        ui_rect(0, 0, x + cover, TOP_H, black);
        ui_rect(x + w - cover, 0, TOP_W - x - w + cover, TOP_H, black);
    }
}

static void draw_title(void)
{
    ui_header(TOP_W, UW3DS_TITLE, "New 3DS");
    ui_text_fit(TOP_W / 2, 50, 0.95f, COL_GOLD, C2D_AlignCenter, TOP_W - 30, UW3DS_TITLE);
    ui_text_fit(TOP_W / 2, 92, 0.5f, COL_TEXT, C2D_AlignCenter, TOP_W - 30, "New 3DS edition");
    ui_text(TOP_W / 2, 128, 0.42f, COL_DIM, C2D_AlignCenter | C2D_WordWrap, TOP_W - 60,
            "Play the menus on the touch screen with the stylus. Once you are in " UW3DS_PLACE ", the 3D view moves up here.");
}

/* "Working...": shown while the game saves or loads (uw_busy, set on its thread), so a long
   pause does not look like a freeze. */
extern volatile int uw_busy;
static void busy_draw(void)
{
    if (!uw_busy) return;
    {
        float w = 150, x = (TOP_W - w) / 2, y = 30, ph = (float)(p3_frame() % 60) / 60.0f;
        float seg = 50, sx = x + 2 + (w - 4 - seg) * (ph < 0.5f ? ph * 2 : (1 - ph) * 2);
        ui_frame(x, y, w, 24, COL_PANEL2, COL_GOLD);
        ui_text_fit(TOP_W / 2, y + 1, 0.42f, COL_TEXT, C2D_AlignCenter, w - 10, "Working...");
        ui_rect(x + 2, y + 18, w - 4, 4, COL_SHADE);
        ui_rect(sx, y + 18, seg, 4, COL_GOLD);
    }
}

void front_draw_top(int eye, float iod)
{
    const struct uwsnap *s = uw_snap();
    /* the view alone: exploring, on the real top screen (ZL swapped lays the old page out at 320) */
    int full = s->in_game && s->mode == SNAP_VIEW && !uw_cuts_full && TOP_W >= 400;
    ui_begin();
    ui_rect(0, 0, TOP_W, TOP_H, COL_BG);
    if (overlay == OV_MANUAL) { manual_draw_top(); return; }
    if (full) draw_full_view(eye, iod);
    else if (s->mode == SNAP_VIEW || s->mode == SNAP_CONV) draw_big_view(eye, iod);
    else if (s->in_game) map_draw(0, 0, TOP_W, TOP_H);     /* the game's own map is on the bottom */
    else draw_title();
    /* the hint strip: there in every mode but the view alone (its standing hints are on the
       pointer pad), same place in every port; toasts, the first-steps tip and the menu notice
       take its place, opaque, drawn last: over the view alone they are a short band that goes away */
    if (toast_until && (int)(toast_until - p3_frame()) > 0) ui_hint(TOP_W, toast, 1);
    else if (tip_until && (int)(tip_until - p3_frame()) > 0 && !overlay && s->in_game) {
        char h[96];
        snprintf(h, sizeof h, TOP_W < 400 ? "Tap things in the small view. %s: use" : "Slide on the pointer pad, tap it to click.  %s: use what is ahead", front_key_name(CONFIRM_KEY));
        ui_hint(TOP_W, h, 1);
    } else if (overlay && overlay != OV_MANUAL) {
        char h[80];                             /* the back button's real name (left hand, A/B swap) */
        snprintf(h, sizeof h, TOP_W < 400 ? "Port menu   %s: back   START: close" : "Port menu open    %s: back    START: close", front_key_name(BACK_KEY));
        ui_hint(TOP_W, h, 0);
    }
    else if (full) { }
    else if (s->in_game) ui_hint(TOP_W, TOP_W < 400 ? "START menu  ZL swap  Pad walk  C-Stick look" : "START menu    ZL swap screens    Circle Pad walk    C-Stick look", 0);
    else ui_hint(TOP_W, "START menu    ZL swap screens", 0);
    busy_draw();
}

/* ---- the port menu, on the bottom screen ---- */

static int quit_state;                 /* 0 asking, 1 saving, 2 not saved: exit anyway? */

static int menu_input(u32 down, u32 held, circlePosition cp, circlePosition cs, int touching, touchPosition t)
{
    (void)touching; (void)t;
    if (overlay == OV_MANUAL) {
        manual_input(down, held, cp, cs);
        /* A presses the last-row cell the D-pad is on: LEFT HAND or CLOSE */
        if ((down & CONFIRM_KEY) && manual_bar() == 1) manual_lefty();
        /* B or START (or A on CLOSE) close the manual and go back to the game, as in the other ports */
        if ((down & (BACK_KEY | KEY_START)) || ((down & CONFIRM_KEY) && manual_bar() == 2)) {
            overlay = OV_NONE; p3_pause(0); opts_save(); opts_flush();
        }
        return 1;
    }
    if (overlay == OV_QUIT && quit_state == 1) return 1;   /* the exit save is running: nothing to press */
    /* every button here can be reached without the stylus: the D-pad moves the gold
       highlight, the confirm button (A) presses it */
    ui_nav((down & KEY_DRIGHT) ? 1 : (down & KEY_DLEFT) ? -1 : 0,
           (down & KEY_DDOWN) ? 1 : (down & KEY_DUP) ? -1 : 0, (down & CONFIRM_KEY) != 0);
    if (overlay == OV_HOTKEYS) {
        if (cp.dy > 40 || cs.dy > 40) { if (pick_scroll > 0 && (p3_frame() & 3) == 0) pick_scroll--; }
        if (cp.dy < -40 || cs.dy < -40) { if ((p3_frame() & 3) == 0) pick_scroll++; }
    }
    if (down & BACK_KEY) {
        if (overlay == OV_QUIT) quit_state = 0;
        overlay = overlay == OV_MENU ? OV_NONE : (overlay == OV_HOTKEYS || overlay == OV_MAP) ? OV_OPTIONS : OV_MENU;
    }
    if (down & KEY_START) { if (overlay == OV_QUIT) quit_state = 0; overlay = OV_NONE; }
    if (overlay == OV_NONE) { p3_pause(0); opts_save(); opts_flush(); }
    return 1;
}

/* The house START menu, in the house order: RESUME, MANUAL, save, load, OPTIONS, EXIT GAME, one
   column. QUICKLOAD sits under QUICKSAVE as in the other ports; it is safe there because it asks
   first (NO in focus), and a stray QUICKSAVE keeps the save it replaced (SAVE4.BAK). */
static void menu_main(void)
{
    const struct uwsnap *s = uw_snap();
    ui_header(BOT_W, UW3DS_TITLE, "MENU");
    if (ui_button(8, 28, 304, 30, "RESUME", 0)) { overlay = OV_NONE; p3_pause(0); }
    if (ui_button(8, 63, 304, 30, "MANUAL", 0)) { overlay = OV_MANUAL; manual_open(0); }
    if (ui_button_en(8, 98, 304, 30, "QUICKSAVE (SLOT 4)", 0, s->in_game)) {
        overlay = OV_NONE; p3_pause(0);
        if (!uw_request_save(4, "Quicksave")) say("You cannot save just now.");
    }
    if (ui_button_en(8, 133, 304, 30, "QUICKLOAD (SLOT 4)", 0, s->in_game && qs_state)) overlay = OV_LOAD;
    if (ui_button(8, 168, 304, 30, "OPTIONS", 0)) overlay = OV_OPTIONS;
    if (ui_button(8, 203, 304, 30, "EXIT GAME", 0)) { quit_state = 0; overlay = OV_QUIT; }
}

/* OPTIONS: the things set once. GAME SETTINGS is the game port's own screen (sound,
   enhancements, fixes). */
static void menu_options(void)
{
    const struct uwsnap *s = uw_snap();
    ui_header(BOT_W, "Options", NULL);
    if (ui_button_en(8, 30, 148, 34, "SPELL HOTKEYS", 0, s->in_game)) { overlay = OV_HOTKEYS; pick_spell = -1; }
    if (ui_button_en(164, 30, 148, 34, "MAP", 0, s->in_game)) overlay = OV_MAP;
    if (ui_button(8, 72, 148, 34, swap_ab ? "A/B: SWAPPED" : "A/B: NORMAL", swap_ab)) {
        swap_ab = !swap_ab;
        opts_save();
    }
    if (ui_button(164, 72, 148, 34, lefty ? "LEFT HAND: ON" : "LEFT HAND: OFF", lefty)) {
        lefty = !lefty;
        opts_save();
    }
    if (ui_button(8, 114, 304, 34, "GAME SETTINGS", 0)) { overlay = OV_NONE; p3_pause(0); p3_settings(1); }
    ui_text_fit(BOT_W / 2, 156, 0.4f, COL_DIM, C2D_AlignCenter, BOT_W - 12, "Game settings: sound, enhancements and fixes.");
    if (ui_button(230, 200, 84, 30, "DONE", 0)) overlay = OV_MENU;
}

/* QUICKLOAD asks first, NO in focus. Slot 4 keeps the save it last replaced (SAVE4.BAK: an
   EXIT GAME or HOME save replaces the quicksave too), and that one can be loaded from here. */
static void menu_load(void)
{
    ui_header(BOT_W, "Quickload", NULL);
    ui_text(BOT_W / 2, 34, 0.55f, COL_TEXT, C2D_AlignCenter, 0, "Load the last save in slot 4?");
    ui_text_fit(BOT_W / 2, 58, 0.42f, COL_DIM, C2D_AlignCenter, BOT_W - 16, "What you did since that save is lost.");
    /* NO is drawn first so the D-pad focus starts on it */
    if (ui_button(170, 84, 130, 40, "NO", 0)) overlay = OV_MENU;
    if (ui_button_en(20, 84, 130, 40, "YES", 0, qs_state & 1)) {
        overlay = OV_NONE; p3_pause(0);
        if (!uw_request_restore(4)) say("You cannot restore just now.");
    }
    if (ui_button_en(20, 140, 280, 34, "LOAD THE SAVE BEFORE IT", 0, (qs_state & 2) != 0)) {
        overlay = OV_NONE; p3_pause(0);
        if (!uw_request_restore_prev(4)) say("You cannot restore just now.");
    }
    ui_text(16, 182, 0.4f, COL_DIM, C2D_WordWrap, BOT_W - 32,
            "Slot 4 keeps one older save. QUICKSAVE, EXIT GAME and HOME > Close all save to slot 4.");
}

static void menu_hotkeys(void)
{
    const struct uwsnap *s = uw_snap();
    int n, i, row = 0, k;
    const struct uwspell *sp = uw_spells(&n);
    ui_header(BOT_W, pick_spell < 0 ? "Choose a spell" : "Now choose a direction", NULL);
    for (i = 0; i < n; i++) {
        float y;
        if (!spell_known(&sp[i], s->runes)) continue;
        if (row++ < pick_scroll) continue;
        y = 28 + (row - 1 - pick_scroll) * 26;
        if (y > 150) break;
        if (ui_button(6, y, 200, 24, sp[i].name, pick_spell == i)) pick_spell = i;
    }
    if (!row) ui_text(10, 40, 0.45f, COL_DIM, C2D_WordWrap, 190, "You know no spells yet.");
    if (pick_scroll && row <= pick_scroll) pick_scroll = 0;
    /* the four directions, a D-pad's shape */
    for (k = 0; k < 4; k++) {
        static const float bx[4] = { 238, 268, 238, 208 }, by[4] = { 40, 80, 120, 80 };
        char l[48];
        snprintf(l, sizeof l, "%c", "^>v<"[k]);
        if (ui_button(bx[k], by[k], 34, 34, l, hotkey[k] >= 0)) {
            if (pick_spell >= 0) { for (i = 0; i < 4; i++) if (hotkey[i] == pick_spell) hotkey[i] = -1; hotkey[k] = pick_spell; pick_spell = -1; opts_save(); }
            else { hotkey[k] = -1; opts_save(); }
        }
    }
    ui_text(212, 158, 0.38f, COL_DIM, C2D_WordWrap, 104, "Choose a direction alone to clear it.");
    for (k = 0; k < 4; k++)
        ui_textf(8, 162 + k * 13, 0.38f, COL_TEXT, "%-5s %s", dirs[k], hotkey[k] >= 0 && hotkey[k] < n ? sp[hotkey[k]].name : "-");
    if (ui_button(230, 206, 84, 28, "DONE", 0)) overlay = OV_OPTIONS;
}

static void menu_map(void)
{
    int v = map_viewing();
    char b[48];
    ui_header(BOT_W, "Map", NULL);
    if (ui_button(10, 40, 145, 34, "Zoom in", 0)) map_zoom(1);
    if (ui_button(165, 40, 145, 34, "Zoom out", 0)) map_zoom(-1);
    if (ui_button(10, 84, 145, 34, "Level up", 0)) map_level(-1);
    if (ui_button(165, 84, 145, 34, "Level down", 0)) map_level(1);
    snprintf(b, sizeof b, "%s", v ? "Back to my level" : "Showing your level");
    if (ui_button(10, 128, 300, 34, b, !v)) map_follow();
    snprintf(b, sizeof b, "%s: choose a button    %s: press it", lefty ? "A/B/X/Y" : "D-pad", front_key_name(CONFIRM_KEY));
    ui_text_fit(BOT_W / 2, 172, 0.4f, COL_DIM, C2D_AlignCenter, BOT_W - 12, b);
    if (ui_button(230, 200, 84, 30, "DONE", 0)) overlay = OV_OPTIONS;
}

/* The manual's sections, two columns, with LEFT HAND and CLOSE as the last two cells (as in the
   other ports' manuals): 26 px buttons 4 px apart while they fit in seven rows (rule 11), the older
   tighter grid for a longer book. The gold look marks the open section, or the last-row cell the
   D-pad is on (manual_bar); A presses that cell (menu_input). */
static void manual_lefty(void) { lefty = !lefty; opts_save(); }
static void menu_manual(void)
{
    int i, n = manual_chapters(), b = manual_bar();
    int roomy = (n + 3) / 2 <= 7;
    float h = roomy ? 26 : 22, pitch = roomy ? 30 : 23;
    const char *lh = lefty ? "LEFT HAND: ON" : "LEFT HAND: OFF";
    ui_header(BOT_W, "Manual", NULL);
    for (i = 0; i < n && i < 16; i++) {
        float x = 6 + (i % 2) * 156, y = 27 + (i / 2) * pitch;
        if (ui_button(x, y, 152, h, manual_title(i), !b && i == manual_current())) manual_open(i);
    }
    if (roomy ? ui_button(6 + (n % 2) * 156, 27 + (n / 2) * pitch, 152, h, lh, b == 1)
              : ui_button(6, 213, 150, 24, lh, b == 1))
        manual_lefty();
    if (roomy ? ui_button(6 + ((n + 1) % 2) * 156, 27 + ((n + 1) / 2) * pitch, 152, h, "CLOSE", b == 2)
              : ui_button(230, 213, 84, 24, "CLOSE", b == 2)) {
        overlay = OV_NONE; p3_pause(0);
    }
}

/* Rule 17 (never lose progress): leaving from the game quicksaves first (slot 4, the one
   QUICKLOAD and the title's load list use). The save runs on the game's thread; the quit
   waits for it to finish (UW-02: never quit or park mid-save), and when the save could not
   be made or failed it asks first (UW-03, UW-04). */
static const char *quit_why;
static u64 quit_t0;
static void menu_quit(void)
{
    const struct uwsnap *s = uw_snap();
    ui_header(BOT_W, "Exit game", NULL);
    if (quit_state == 1) {
        u64 t = osGetTime() - quit_t0;
        ui_text(BOT_W / 2, 90, 0.55f, COL_TEXT, C2D_AlignCenter, 0, "Saving...");
        if (!uw_request_pending() && !uw_busy && t > 300) {
            if (uw_last_save_ok) p3_quit();
            else {
                quit_state = 2; p3_pause(1);
                quit_why = uw_last_save_refused ? "You can't save just now (a panel is open or you hold an item)."
                                                : "The quicksave failed. Is the SD card full?";
            }
        } else if (uw_request_pending() && t > 10000) {   /* never picked up: the game can't save now */
            quit_state = 2; quit_why = "The game could not save just now."; p3_pause(1);
        }
        return;
    }
    if (quit_state == 2) {
        ui_text(BOT_W / 2, 52, 0.55f, COL_TEXT, C2D_AlignCenter, 0, "Not saved!");
        ui_text_fit(BOT_W / 2, 76, 0.42f, COL_DIM, C2D_AlignCenter, BOT_W - 16, quit_why ? quit_why : "");
        ui_text(BOT_W / 2, 96, 0.42f, COL_DIM, C2D_AlignCenter, 0, "Exit anyway? NO goes back to the menu.");
        if (ui_button(170, 130, 130, 40, "NO", 0)) { quit_state = 0; overlay = OV_MENU; }
        if (ui_button(20, 130, 130, 40, "EXIT ANYWAY", 0)) p3_quit();
        return;
    }
    ui_text_fit(BOT_W / 2, 60, 0.55f, COL_TEXT, C2D_AlignCenter, BOT_W - 16, "Exit " UW3DS_TITLE "?");
    ui_text_fit(BOT_W / 2, 84, 0.42f, COL_DIM, C2D_AlignCenter, BOT_W - 16,
                s->in_game ? "It is saved first (slot 4)." : "No game is running: nothing to save.");
    /* NO is drawn first so the D-pad focus starts on it */
    if (ui_button(170, 130, 130, 40, "NO", 0)) overlay = OV_MENU;
    if (ui_button(20, 130, 130, 40, "YES", 0)) {
        if (!s->in_game) p3_quit();
        else {
            uw_last_save_ok = 0;
            uw_last_save_refused = 0;
            if (uw_request_save(4, "Quicksave")) { quit_state = 1; quit_t0 = osGetTime(); p3_pause(0); }
            else { quit_state = 2; quit_why = "You can't save just now (a panel is open or you hold an item)."; }
        }
    }
}

/* UW-05: HOME -> Close (aptMainLoop returned false). The platform (plat_3ds.c) calls this on the
   main thread, after its loop and outside every APT hook, with the game's clock running and every
   key released; the save itself runs on the game's thread exactly as the menu's QUICKSAVE / EXIT
   GAME do (slot 4 through the scratch slot and swap, UW-01), and uw_request_save refuses where
   EXIT GAME's would (a panel open, an item held). 1 saved, 0 not (not in a game, refused, not
   picked up within PICKUP_MS, failed, or still writing at LIMIT_MS). A request that was never
   picked up is taken back so the game can't save later by surprise. Never waits past LIMIT_MS:
   the game's thread may be stuck, and a write cut off there leaves slot 4 as it was (scratch slot). */
int front_save_now(unsigned pickup_ms, unsigned limit_ms)
{
    const struct uwsnap *s = uw_snap();
    u64 t0 = osGetTime();
    if (!s->in_game) return 0;
    if (quit_state != 1 && !uw_request_pending() && !uw_busy) {   /* EXIT GAME's own save: just wait for it */
        uw_last_save_ok = 0;
        if (!uw_request_save(4, "Quicksave")) return 0;
    }
    for (;;) {
        u64 t = osGetTime() - t0;
        if (!uw_request_pending() && !uw_busy && t > 300) return uw_last_save_ok;
        if (uw_request_pending() && t > pickup_ms) {
            int slot;
            uwglue_take_request(&slot);         /* not picked up: take it back */
            svcSleepThread(50000000LL);
            while (uw_busy && osGetTime() - t0 < limit_ms) svcSleepThread(20000000LL);   /* picked up just now */
            return uw_busy ? 0 : uw_last_save_ok;
        }
        if (t > limit_ms) return 0;
        svcSleepThread(20000000LL);
    }
}

int front_draw_bottom(void)
{
    if (!overlay) {
        ui_nav_frame(0);
        if (!uw_snap()->in_game) return 0;
        if (pad_on()) draw_pad();
        if (tab != TAB_GAME) {
            ui_rect(0, 0, PG_W, PG_H, COL_BG);
            switch (tab) {
            case TAB_MAP: map_draw(0, 0, PG_W, PG_H); break;
            case TAB_STATUS: draw_status(); break;
            case TAB_LOG: draw_log(); break;
            case TAB_JOURNAL: draw_journal(); break;
            case TAB_SPELLS: draw_spells(); break;
            }
        }
        draw_tabs();
        return tab != TAB_GAME;
    }
    {
        static int last_ov;
        if (overlay != last_ov) { ui_nav_reset(); last_ov = overlay; }
    }
    ui_nav_frame(overlay != OV_MANUAL);
    ui_rect(0, 0, BOT_W, BOT_H, COL_BG);
    switch (overlay) {
    case OV_MENU: menu_main(); break;
    case OV_HOTKEYS: menu_hotkeys(); break;
    case OV_MAP: menu_map(); break;
    case OV_MANUAL: menu_manual(); break;
    case OV_QUIT: menu_quit(); break;
    case OV_OPTIONS: menu_options(); break;
    case OV_LOAD: menu_load(); break;
    }
    if (overlay == OV_NONE) { p3_pause(0); opts_save(); opts_flush(); }
    return 1;
}
