/* manual.c: the built-in manual (romfs:/manual/uw.txt), shown on the top screen a chapter at a
   time. Chapters start with "# Title"; the rest is paragraphs. "/3ds/uw1" in the text becomes the
   running game's folder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat3ds.h"
#include "ui.h"
#include "front.h"

#define MAXCH 40
static char *book;
static struct { const char *title; const char *body; } ch[MAXCH];
static int nch;
static int cur, scroll;

static void subst(char *s)
{
    char *p = s;
    while ((p = strstr(p, "/3ds/uw1")) != NULL) { memcpy(p, UW3DS_ROOT, 8); p += 8; }
}

void manual_load(void)
{
    FILE *f = fopen("romfs:/manual/uw.txt", "rb");
    long n;
    char *p;
    if (!f) return;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    book = malloc((size_t)n + 1);
    if (!book) { fclose(f); return; }
    n = (long)fread(book, 1, (size_t)n, f);
    book[n] = 0;
    fclose(f);
    subst(book);
    /* each chapter ends where the next one's "# " line starts */
    for (p = book; (p = strstr(p, "\n# ")) != NULL; p += 1) *p = 0;
    for (p = book; p < book + n && nch < MAXCH; p += strlen(p) + 1) {
        char *e;
        if (p[0] != '#' || p[1] != ' ') continue;
        e = strchr(p, '\n');
        ch[nch].title = p + 2;
        if (e) { *e = 0; ch[nch].body = e + 1; }
        else ch[nch].body = "";
        nch++;
        if (e) p = e;           /* strlen(p) + 1 from here skips the title, the body follows */
    }
}

int manual_chapters(void) { return nch; }

void manual_open(int chapter)
{
    cur = chapter < 0 ? 0 : chapter >= nch ? nch - 1 : chapter;
    scroll = 0;
}

int manual_current(void) { return cur; }

/* The manual's controls, the same in every one of these 3DS ports: the Circle Pad and the
   C-stick scroll the text (held, it keeps scrolling); the D-pad moves between the section buttons
   on the bottom screen (two columns: left and right step, up and down a row); L and R the
   previous or next section; a tap on a section opens it; B or START close. */
void manual_input(u32 down, u32 held, circlePosition cp, circlePosition cs)
{
    int to = cur;
    (void)held;
    if (down & KEY_R) to = cur + 1;
    if (down & KEY_L) to = cur - 1;
    if (down & KEY_DRIGHT) to = cur + 1;
    if (down & KEY_DLEFT) to = cur - 1;
    if (down & KEY_DDOWN) to = cur + 2;
    if (down & KEY_DUP) to = cur - 2;
    if (to != cur && to >= 0 && to < nch) { cur = to; scroll = 0; }
    if (cp.dy < -40 || cs.dy < -40) scroll += (cp.dy < -110 || cs.dy < -110) ? 6 : 3;
    if (cp.dy > 40 || cs.dy > 40) scroll -= (cp.dy > 110 || cs.dy > 110) ? 6 : 3;
    if (scroll < 0) scroll = 0;
}

void manual_draw_top(void)
{
    float y;
    const char *p;
    char para[1200];
    ui_rect(0, 0, TOP_W, TOP_H, COL_BG);
    if (!nch) { ui_text(16, 16, 0.5f, COL_TEXT, 0, 0, "The manual is missing from this build."); return; }
    y = 34 - (float)scroll;
    for (p = ch[cur].body; *p;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= sizeof para) n = sizeof para - 1;
        memcpy(para, p, n);
        para[n] = 0;
        if (n) {
            float h = 0;
            if (y > -60 && y < TOP_H) h = ui_text(10, y, 0.46f, COL_TEXT, C2D_WordWrap, TOP_W - 20, para);
            else h = 14.0f * (1 + (int)(ui_text_width(0.46f, para) / (TOP_W - 20)));
            y += h + 4;
        } else
            y += 4;
        p = e ? e + 1 : p + n;
    }
    /* header and footer last, so scrolled text can never show through them */
    ui_rect(0, 0, TOP_W, 26, COL_PANEL);
    ui_textf(10, 5, 0.55f, COL_GOLD, "%s", ch[cur].title);
    ui_textf(TOP_W - 10 - 60, 7, 0.42f, COL_DIM, "%d / %d", cur + 1, nch);
    ui_rect(0, TOP_H - 16, TOP_W, 16, COL_PANEL);
    ui_text_fit(TOP_W / 2, TOP_H - 15, 0.4f, COL_DIM, C2D_AlignCenter, TOP_W - 10, front_lefty() ? "Sticks: scroll   A/B/X/Y: section   L R: previous / next   D-Down / START: close"
                                                                                 : "Sticks: scroll   D-pad: section   L R: previous / next   B / START: close");
}

const char *manual_title(int chapter)
{
    return chapter >= 0 && chapter < nch ? ch[chapter].title : "";
}
