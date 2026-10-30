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

void manual_input(u32 down, u32 held, circlePosition cp)
{
    if (down & (KEY_DRIGHT | KEY_R)) { if (cur < nch - 1) { cur++; scroll = 0; } }
    if (down & (KEY_DLEFT | KEY_L)) { if (cur > 0) { cur--; scroll = 0; } }
    if ((held & KEY_DDOWN) || cp.dy < -40) scroll += 3;
    if ((held & KEY_DUP) || cp.dy > 40) scroll -= 3;
    if (cp.dx > 100 && (down & 0) == 0) { }
    if (scroll < 0) scroll = 0;
}

void manual_draw_top(void)
{
    float y;
    const char *p;
    char para[1200];
    ui_rect(0, 0, TOP_W, TOP_H, COL_BG);
    if (!nch) { ui_text(16, 16, 0.5f, COL_TEXT, 0, 0, "The manual is missing from this build."); return; }
    ui_rect(0, 0, TOP_W, 26, COL_PANEL);
    ui_textf(10, 5, 0.55f, COL_GOLD, "%s", ch[cur].title);
    ui_textf(TOP_W - 10 - 60, 7, 0.42f, COL_DIM, "%d / %d", cur + 1, nch);
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
    if (y + scroll < 34 + scroll) { }
    ui_rect(0, TOP_H - 16, TOP_W, 16, COL_PANEL);
    ui_text(TOP_W / 2, TOP_H - 15, 0.4f, COL_DIM, C2D_AlignCenter, 0, "L R: chapter    Circle Pad: scroll    B: close");
}

const char *manual_title(int chapter)
{
    return chapter >= 0 && chapter < nch ? ch[chapter].title : "";
}
