/* front.c: the 3DS front end. The buttons as the game's keys (the port's modern-keys and
   rune-keys enhancements), the spell hotkeys, the screens' layout and the port menu (START),
   which pauses the game.

   Layout: while you explore (and talk), the top screen is the game's 3D view, about twice its
   size, with stereoscopic depth from the 3D slider. The bottom screen is the whole game screen,
   played with the stylus as the mouse (inventory, icons, runes, the scroll, the small view to
   tap things in the world), above a strip of tabs: Game, Map, Status, Log, Journal, Spells. The
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
#include "strings3ds.h"

enum { TAB_GAME, TAB_MAP, TAB_STATUS, TAB_LOG, TAB_JOURNAL, TAB_SPELLS, TABS };
static const char *const tab_names[TABS] = { "Game", "Map", "Status", "Log", "Journal", "Spells" };

/* the bottom screen: the game (or a page) above the tab strip */
#define STRIP_H 26
#define PG_W BOT_W
#define PG_H (BOT_H - STRIP_H)
enum { OV_NONE, OV_MENU, OV_HOTKEYS, OV_MAP, OV_MANUAL, OV_QUIT };

static int tab, overlay;
static int log_back, journal_person = -1, journal_back, spells_scroll;
static int hotkey[4] = { -1, -1, -1, -1 };     /* up, right, down, left: a spell's index */
static int pick_spell = -1, pick_scroll;
static char toast[96];
static unsigned toast_until;
static int moved_since_touch;
static int select_used;
static int swap_ab;                     /* Settings: swap the A and B buttons (A confirms by default) */
#define CONFIRM_KEY (swap_ab ? KEY_B : KEY_A)
#define BACK_KEY (swap_ab ? KEY_A : KEY_B)
unsigned front_confirm_key(void) { return CONFIRM_KEY; }
unsigned front_back_key(void) { return BACK_KEY; }

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

static void say(const char *m)
{
    snprintf(toast, sizeof toast, "%s", m);
    toast_until = p3_frame() + 180;
}

/* ---- the front end's own settings: the page, the hotkeys ---- */

#define OPTS UW3DS_HOME "/3ds.cfg"

static void opts_save(void)
{
    FILE *f = fopen(OPTS, "w");
    if (!f) return;
    fprintf(f, "page=%d\nzoom=%d\nhotkeys=%d,%d,%d,%d\nswapab=%d\n", tab, map_zoom_level(), hotkey[0], hotkey[1], hotkey[2], hotkey[3], swap_ab);
    fclose(f);
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
    }
    fclose(f);
}

void front_init(void)
{
    ui_init();
    manual_load();
    opts_load();
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
    p3_key(K_LEFT, l);
    p3_key(K_RIGHT, r);
    p3_key(K_A, cs.dx < -40);
    p3_key(K_D, cs.dx > 40);
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
    ui_touch(touching, t.px, t.py);
    if (overlay) { was_touching = touching; touch_mine = 0; return menu_input(down, held, cp, cs, touching, t); }
    /* a touch belongs to the front end (the tabs, a page) or to the game, by where it started */
    if (touching && !was_touching) {
        float gh = front_game_height();
        touch_mine = gh <= 0 || t.py >= gh;
    }
    mine = (touching || was_touching) && touch_mine;
    was_touching = touching;
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
    if (down & KEY_DUP) cast_hotkey(0);
    if (down & KEY_DRIGHT) cast_hotkey(1);
    if (down & KEY_DDOWN) cast_hotkey(2);
    if (down & KEY_DLEFT) cast_hotkey(3);
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
    if (p3_frame() - last_flush > 120) { uw_journal_flush(); last_flush = p3_frame(); }
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
    if (!s->in_game) { empty_page("Your status shows once you are in the Abyss."); return; }
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
        ui_text_fit(x, yy, 0.36f, s->skills[i] ? COL_TEXT : COL_DIM, 0, 100, b);
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
        h = 14.0f * (1 + (int)(ui_text_width(0.42f, l) / (PG_W - 16)));
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
        float h = 13.0f * (1 + (int)(ui_text_width(0.4f, l) / (PG_W - 24)));
        y -= h + 2;
        if (y < 18) break;
        ui_text(who == 1 ? 18 : 8, y, 0.4f, who == 1 ? COL_GOLD : who == 2 ? COL_DIM : COL_TEXT, C2D_WordWrap, PG_W - 26, l);
    }
    ui_rect(0, 0, PG_W, 16, COL_PANEL2);
    ui_text_fit(8, 1, 0.42f, COL_GOLD, 0, PG_W - 120, uw_journal_name(p));
    snprintf(b, sizeof b, "%d of %d  SELECT+L/R", p + 1, uw_journal_people());
    ui_text_fit(PG_W - 6, 2, 0.34f, COL_DIM, C2D_AlignRight, 108, b);
    uw_log_unlock();
}

static const char *const dirs[4] = { "Up", "Right", "Down", "Left" };

static void draw_spells(void)
{
    const struct uwsnap *s = uw_snap();
    int n, i, row = 0, k;
    const struct uwspell *sp = uw_spells(&n);
    if (!n) { empty_page("Your spell book fills once you are in the Abyss."); return; }
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

static void draw_tabs(void)
{
    int i;
    float w = (float)BOT_W / TABS;
    ui_rect(0, PG_H, BOT_W, STRIP_H, COL_PANEL);
    for (i = 0; i < TABS; i++)
        if (ui_button(i * w + 1, PG_H + 2, w - 2, STRIP_H - 3, tab_names[i], i == tab) && tab != i) {
            tab = i;
            opts_save();
        }
}

/* ---- the top screen ---- */

/* The game's 3D view (or the conversation in its place), about twice its size, floating behind
   the screen's frame by the 3D slider. */
static void draw_big_view(int eye, float iod)
{
    float sc = 392.0f / VIEW_W, w, h, x, y, shift = (eye ? 1 : -1) * iod * 7.0f;
    if (232.0f / VIEW_H < sc) sc = 232.0f / VIEW_H;
    w = VIEW_W * sc; h = VIEW_H * sc;
    x = (TOP_W - w) / 2; y = (TOP_H - h) / 2;
    p3_draw_picture(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, x - shift, y, w, h);
    /* a frame at the screen's depth, covering the picture's shifted edges */
    ui_rect(0, 0, x + 2, TOP_H, COL_BG);
    ui_rect(x + w - 2, 0, TOP_W - x - w + 2, TOP_H, COL_BG);
    ui_rect(x - 2, y - 4, w + 4, 4, COL_EDGE);
    ui_rect(x - 2, y + h, w + 4, 4, COL_EDGE);
    ui_rect(x - 2, y - 4, 4, h + 8, COL_EDGE);
    ui_rect(x + w - 2, y - 4, 4, h + 8, COL_EDGE);
}

static void draw_title(void)
{
    ui_text_fit(TOP_W / 2, 70, 0.95f, COL_GOLD, C2D_AlignCenter, TOP_W - 30, UW3DS_TITLE);
    ui_text_fit(TOP_W / 2, 110, 0.5f, COL_TEXT, C2D_AlignCenter, TOP_W - 30, "New 3DS edition");
    ui_text(TOP_W / 2, 150, 0.42f, COL_DIM, C2D_AlignCenter | C2D_WordWrap, TOP_W - 60,
            "Play the menus on the touch screen with the stylus. Once you are in the Abyss, the 3D view moves up here.");
    /* kept above the toast box (TOP_H-26) and the "Paused" bar (TOP_H-14) so nothing overlaps */
    ui_text_fit(TOP_W / 2, TOP_H - 42, 0.38f, COL_DIM, C2D_AlignCenter, TOP_W - 20, "START: port menu and manual");
}

/* "Working...": the game's loop has not come round for a third of a second (loading a level,
   saving, a long spell), so say so with a moving bar rather than look frozen. */
extern volatile unsigned uw_passes;
static int busy_frames;
static void busy_update(void)
{
    static unsigned last_pass, last_frame;
    unsigned f = p3_frame();
    if (f == last_frame) return;            /* once a frame, not once an eye */
    last_frame = f;
    if (uw_passes != last_pass) { last_pass = uw_passes; busy_frames = 0; }
    else if (busy_frames < 100000) busy_frames++;
}

static void busy_draw(void)
{
    if (busy_frames < 20) return;
    {
        float w = 150, x = (TOP_W - w) / 2, y = 8, ph = (float)(p3_frame() % 60) / 60.0f;
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
    busy_update();
    ui_begin();
    ui_rect(0, 0, TOP_W, TOP_H, COL_BG);
    if (overlay == OV_MANUAL) { manual_draw_top(); return; }
    if (s->mode == SNAP_VIEW || s->mode == SNAP_CONV) draw_big_view(eye, iod);
    else if (s->in_game) map_draw(0, 0, TOP_W, TOP_H);     /* the game's own map is on the bottom */
    else draw_title();
    if (toast_until && (int)(toast_until - p3_frame()) > 0) {
        float w = ui_text_width(0.46f, toast) + 20;
        if (w > TOP_W - 8) w = TOP_W - 8;
        ui_frame((TOP_W - w) / 2, TOP_H - 26, w, 20, COL_PANEL2, COL_GOLD);
        ui_text_fit(TOP_W / 2, TOP_H - 24, 0.46f, COL_TEXT, C2D_AlignCenter, w - 10, toast);
    }
    if (overlay && overlay != OV_MANUAL) {
        ui_rect(0, TOP_H - 14, TOP_W, 14, COL_SHADE);
        ui_text(TOP_W / 2, TOP_H - 14, 0.38f, COL_GOLD, C2D_AlignCenter, 0, "Paused: the port menu is open");
    }
    busy_draw();
}

/* ---- the port menu, on the bottom screen ---- */

static int menu_input(u32 down, u32 held, circlePosition cp, circlePosition cs, int touching, touchPosition t)
{
    (void)touching; (void)t;
    if (overlay == OV_MANUAL) {
        manual_input(down, held, cp, cs);
        if (down & (BACK_KEY | KEY_START)) overlay = OV_MENU;
        return 1;
    }
    if (overlay == OV_MAP) {
        if (down & KEY_DUP) map_zoom(1);
        if (down & KEY_DDOWN) map_zoom(-1);
        if (down & KEY_DLEFT) map_level(-1);
        if (down & KEY_DRIGHT) map_level(1);
    }
    if (overlay == OV_HOTKEYS) {
        if ((held & KEY_DUP) || cp.dy > 40) { if (pick_scroll > 0 && (p3_frame() & 3) == 0) pick_scroll--; }
        if ((held & KEY_DDOWN) || cp.dy < -40) { if ((p3_frame() & 3) == 0) pick_scroll++; }
    }
    if (down & BACK_KEY) overlay = overlay == OV_MENU ? OV_NONE : OV_MENU;
    if (down & KEY_START) overlay = OV_NONE;
    if (overlay == OV_NONE) { p3_pause(0); opts_save(); }
    return 1;
}

static void menu_main(void)
{
    const struct uwsnap *s = uw_snap();
    ui_text(BOT_W / 2, 6, 0.6f, COL_GOLD, C2D_AlignCenter, 0, UW3DS_TITLE);
    if (ui_button(10, 34, 145, 34, "Resume", 0)) { overlay = OV_NONE; p3_pause(0); }
    if (ui_button(165, 34, 145, 34, "Manual", 0)) { overlay = OV_MANUAL; manual_open(0); }
    if (ui_button_en(10, 74, 145, 34, "Quicksave", 0, s->in_game)) {
        overlay = OV_NONE; p3_pause(0);
        if (!uw_request_save(4, "Quicksave")) say("You cannot save just now.");
    }
    if (ui_button_en(165, 74, 145, 34, "Quickload", 0, s->in_game)) {
        overlay = OV_NONE; p3_pause(0);
        if (!uw_request_restore(4)) say("You cannot restore just now.");
    }
    if (ui_button_en(10, 114, 145, 34, "Spell hotkeys", 0, s->in_game)) { overlay = OV_HOTKEYS; pick_spell = -1; }
    if (ui_button_en(165, 114, 145, 34, "Map", 0, s->in_game)) overlay = OV_MAP;
    if (ui_button(10, 154, 145, 34, "Settings", 0)) { overlay = OV_NONE; p3_pause(0); p3_settings(1); }
    if (ui_button(165, 154, 145, 34, "Exit game", 0)) overlay = OV_QUIT;
    if (ui_button(10, 194, 300, 24, swap_ab ? "A and B: swapped (B confirms)" : "A and B: normal (A confirms)", swap_ab)) {
        swap_ab = !swap_ab;
        opts_save();
    }
    ui_text_fit(BOT_W / 2, 222, 0.4f, COL_DIM, C2D_AlignCenter, BOT_W - 12, "Quicksave and Quickload use the game's slot 4.");
}

static void menu_hotkeys(void)
{
    const struct uwsnap *s = uw_snap();
    int n, i, row = 0, k;
    const struct uwspell *sp = uw_spells(&n);
    ui_text(BOT_W / 2, 4, 0.5f, COL_GOLD, C2D_AlignCenter, 0, pick_spell < 0 ? "Tap a spell" : "Now tap a direction");
    for (i = 0; i < n; i++) {
        float y;
        if (!spell_known(&sp[i], s->runes)) continue;
        if (row++ < pick_scroll) continue;
        y = 26 + (row - 1 - pick_scroll) * 26;
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
    ui_text(212, 158, 0.36f, COL_DIM, C2D_WordWrap, 104, "Tap a direction alone to clear it.");
    for (k = 0; k < 4; k++)
        ui_textf(8, 162 + k * 13, 0.38f, COL_TEXT, "%-5s %s", dirs[k], hotkey[k] >= 0 && hotkey[k] < n ? sp[hotkey[k]].name : "-");
    if (ui_button(230, 206, 84, 28, "Done", 0)) overlay = OV_MENU;
}

static void menu_map(void)
{
    int v = map_viewing();
    char b[48];
    ui_text(BOT_W / 2, 6, 0.55f, COL_GOLD, C2D_AlignCenter, 0, "Map");
    if (ui_button(10, 40, 145, 34, "Zoom in", 0)) map_zoom(1);
    if (ui_button(165, 40, 145, 34, "Zoom out", 0)) map_zoom(-1);
    if (ui_button(10, 84, 145, 34, "Level up", 0)) map_level(-1);
    if (ui_button(165, 84, 145, 34, "Level down", 0)) map_level(1);
    snprintf(b, sizeof b, "%s", v ? "Back to my level" : "Showing your level");
    if (ui_button(10, 128, 300, 34, b, !v)) map_follow();
    ui_text(BOT_W / 2, 172, 0.4f, COL_DIM, C2D_AlignCenter, 0, "D-pad: up/down zoom, left/right level");
    if (ui_button(230, 200, 84, 30, "Done", 0)) overlay = OV_MENU;
}

static void menu_manual(void)
{
    int i, n = manual_chapters();
    ui_text(BOT_W / 2, 4, 0.5f, COL_GOLD, C2D_AlignCenter, 0, "Manual");
    for (i = 0; i < n && i < 16; i++) {
        float x = 6 + (i % 2) * 156, y = 24 + (i / 2) * 24;
        if (ui_button(x, y, 152, 22, manual_title(i), i == manual_current())) manual_open(i);
    }
    if (ui_button(230, 214, 84, 24, "Close", 0)) overlay = OV_MENU;
}

static void menu_quit(void)
{
    ui_text(BOT_W / 2, 60, 0.55f, COL_TEXT, C2D_AlignCenter, 0, "Leave the game?");
    ui_text(BOT_W / 2, 84, 0.42f, COL_DIM, C2D_AlignCenter, 0, "Anything not saved is lost.");
    if (ui_button(20, 130, 130, 40, "Quit", 0)) p3_quit();
    if (ui_button(170, 130, 130, 40, "Stay", 0)) overlay = OV_MENU;
}

int front_draw_bottom(void)
{
    if (!overlay) {
        if (!uw_snap()->in_game) return 0;
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
    ui_rect(0, 0, BOT_W, BOT_H, C2D_Color32(18, 16, 14, 235));
    switch (overlay) {
    case OV_MENU: menu_main(); break;
    case OV_HOTKEYS: menu_hotkeys(); break;
    case OV_MAP: menu_map(); break;
    case OV_MANUAL: menu_manual(); break;
    case OV_QUIT: menu_quit(); break;
    }
    if (overlay == OV_NONE) { p3_pause(0); opts_save(); }
    return 1;
}
