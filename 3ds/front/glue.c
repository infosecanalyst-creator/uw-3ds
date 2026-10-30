/* glue.c: the meeting point of the game's thread (3ds/game/uwstate.c, through uwglue.h) and
   the front end's (uwsnap.h). One lock guards the snapshot, the spell table, the requests, the
   message log and the journal; the game's thread holds it only for copies.

   The message log is the message scroll's lines, joined from the pieces scroll_print gets and
   cleaned of the scroll's escapes. The journal is every conversation's lines, by the person
   spoken to, kept in the home folder (journal.txt) so it lasts between runs. */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "plat3ds.h"
#include "uwglue.h"
#include "strings3ds.h"

static LightLock lk;
static int lk_ready;

static void lock(void) { if (!lk_ready) { LightLock_Init(&lk); lk_ready = 1; } LightLock_Lock(&lk); }
static void unlock(void) { LightLock_Unlock(&lk); }

/* ---- the snapshot ---- */

static struct uwsnap pub, mine;
static struct uwspell spells[SNAP_SPELLS];
static int nspells, spells_named;
static volatile int safe;

struct uwsnap *uwglue_snap_begin(void)
{
    if (!lk_ready) { LightLock_Init(&lk); lk_ready = 1; }
    if (LightLock_TryLock(&lk)) return NULL;
    return &pub;
}

void uwglue_snap_end(void)
{
    pub.seq++;
    unlock();
}

void uwglue_set_spells(const struct uwspell *tab, int n)
{
    lock();
    if (n > SNAP_SPELLS) n = SNAP_SPELLS;
    memcpy(spells, tab, (size_t)n * sizeof *tab);
    nspells = n;
    spells_named = 0;
    unlock();
}

void uwglue_set_safe(int s) { safe = s; }
int uw_can_save(void) { return safe; }

const struct uwsnap *uw_snap(void) { return &mine; }

const struct uwspell *uw_spells(int *n)
{
    int i;
    if (!spells_named && nspells) {
        /* the names: block 6 from string 0x100 in the table's order (the game's spell names) */
        for (i = 0; i < nspells; i++) uw_name(6, 0x100 + i, spells[i].name, sizeof spells[i].name);
        spells_named = 1;
    }
    *n = nspells;
    return spells;
}

static const char *const runes[24] = {
    "An", "Bet", "Corp", "Des", "Ex", "Flam", "Grav", "Hur", "In", "Jux", "Kal", "Lor",
    "Mani", "Nox", "Ort", "Por", "Quas", "Rel", "Sanct", "Tym", "Uus", "Vas", "Wis", "Ylem" };

const char *uw_rune_name(int r) { return r >= 0 && r < 24 ? runes[r] : ""; }

const char *uw_skill_name(int i)
{
    static char names[20][24];
    static int done;
    if (!done) {
        int k;
        for (k = 0; k < 20; k++) uw_name(2, 0x1F + k, names[k], sizeof names[k]);
        done = 1;
    }
    return i >= 0 && i < 20 ? names[i] : "";
}

/* ---- requests ---- */

static volatile int req, req_slot;
static char result[96];
static volatile int result_new;

int uw_request_save(int slot, const char *desc)
{
    (void)desc;
    if (req || !safe) return 0;
    req_slot = slot; req = 1;
    return 1;
}

int uw_request_restore(int slot)
{
    if (req || !safe) return 0;
    req_slot = slot; req = 2;
    return 1;
}

int uwglue_take_request(int *slot)
{
    int r = req;
    *slot = req_slot;
    req = 0;
    return r;
}

void uwglue_result(const char *msg)
{
    lock();
    snprintf(result, sizeof result, "%s", msg);
    result_new = 1;
    unlock();
}

const char *uw_request_result(void)
{
    static char copy[96];
    if (!result_new) return NULL;
    lock();
    strcpy(copy, result);
    result_new = 0;
    unlock();
    return copy;
}

/* ---- the message log ---- */

#define LOGN 300
#define LOGW 160
static char loglines[LOGN][LOGW];
static int loghead, logcount;
static char part[3][LOGW];             /* the line being built, per scroll */
static int partn[3];

static void log_add(const char *s)
{
    snprintf(loglines[loghead], LOGW, "%s", s);
    loghead = (loghead + 1) % LOGN;
    if (logcount < LOGN) logcount++;
}

void uw_log_lock(void) { lock(); }
void uw_log_unlock(void) { unlock(); }
int uw_log_count(void) { return logcount; }
const char *uw_log_line(int i) { return loglines[(loghead - logcount + i + LOGN) % LOGN]; }

/* ---- the journal ---- */

#define PEOPLE 160
struct jline { char *text; unsigned char who; };
static struct person {
    int conv, whoami, item;
    char name[40];
    int named;
    struct jline *lines;
    int n, cap;
} people[PEOPLE];
static int npeople, latest = -1;
static FILE *jfile;
static int jdirty;

static int person_of(int conv, int whoami, int item)
{
    int i;
    for (i = 0; i < npeople; i++)
        if (people[i].conv == conv && people[i].whoami == whoami && people[i].item == item) return i;
    if (npeople == PEOPLE) return PEOPLE - 1;
    people[npeople].conv = conv; people[npeople].whoami = whoami; people[npeople].item = item;
    people[npeople].named = 0;
    return npeople++;
}

static void journal_add(int conv, int whoami, int item, int who, const char *text, int save)
{
    int p;
    struct person *q;
    if (!*text) return;
    p = person_of(conv, whoami, item);
    q = &people[p];
    if (q->n == q->cap) {
        int nc = q->cap ? q->cap * 2 : 32;
        struct jline *nl = realloc(q->lines, (size_t)nc * sizeof *nl);
        if (!nl) return;
        q->lines = nl; q->cap = nc;
    }
    q->lines[q->n].text = strdup(text);
    q->lines[q->n].who = (unsigned char)who;
    if (q->lines[q->n].text) q->n++;
    latest = p;
    if (save && jfile) { fprintf(jfile, "%d|%d|%d|%d|%s\n", conv, whoami, item, who, text); jdirty = 1; }
}

int uw_journal_people(void) { return npeople; }
int uw_journal_latest(void) { return latest; }
int uw_journal_count(int p) { return p >= 0 && p < npeople ? people[p].n : 0; }

const char *uw_journal_line(int p, int i, int *who)
{
    if (p < 0 || p >= npeople || i < 0 || i >= people[p].n) return "";
    if (who) *who = people[p].lines[i].who;
    return people[p].lines[i].text;
}

const char *uw_journal_name(int p)
{
    struct person *q;
    if (p < 0 || p >= npeople) return "";
    q = &people[p];
    if (!q->named) {
        q->name[0] = 0;
        if (q->whoami > 0 && q->whoami < 0xF0) uw_name(7, q->whoami + 0x10, q->name, sizeof q->name);
        if (!q->name[0] && q->item >= 0) uw_name(4, q->item, q->name, sizeof q->name);
        if (!q->name[0]) snprintf(q->name, sizeof q->name, "someone (%d)", q->conv);
        if (q->name[0] >= 'a' && q->name[0] <= 'z') q->name[0] -= 'a' - 'A';
        q->named = 1;
    }
    return q->name;
}

void uw_journal_open(void)
{
    char line[600];
    FILE *f = fopen(UW3DS_HOME "/journal.txt", "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            int conv, whoami, item, who, n = 0;
            line[strcspn(line, "\r\n")] = 0;
            if (sscanf(line, "%d|%d|%d|%d|%n", &conv, &whoami, &item, &who, &n) == 4 && n)
                journal_add(conv, whoami, item, who, line + n, 0);
        }
        fclose(f);
    }
    latest = -1;
    jfile = fopen(UW3DS_HOME "/journal.txt", "a");
}

void uw_journal_flush(void)
{
    lock();
    if (jdirty && jfile) { fflush(jfile); jdirty = 0; }
    unlock();
}

/* ---- the text from the game ---- */

static int said_next;                  /* the next NPC-scroll line is the player's chosen answer */

/* Copies s into the line being built for scroll MODE, dropping the escapes (\0..\6 colours,
   \p \P pauses, \m more); a newline ends a line, which FN gets. */
static void feed(int m, const char *s, void (*fn)(int m, const char *line, int colour), int *colour)
{
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            if (*s >= '0' && *s <= '9') *colour = *s - '0';
            else if (*s == 'n') goto eol;
            continue;
        }
        if (*s == '\n') {
        eol:
            part[m][partn[m]] = 0;
            if (partn[m]) fn(m, part[m], *colour);
            partn[m] = 0;
            continue;
        }
        if (partn[m] < LOGW - 1) part[m][partn[m]++] = *s == '_' ? ' ' : *s;
    }
}

static int cur_conv = -1, cur_who = -1, cur_item = -1;
static int colour[3];

static void line_done(int m, const char *line, int c)
{
    const char *p = line;
    while (*p == ' ') p++;
    if (!*p) return;
    if (cur_conv < 0 || m == 0) {
        log_add(p);
        return;
    }
    if (m == 2) {
        /* the player's scroll in a conversation: the menu's options ("1. ...") stay out */
        if (p[0] >= '0' && p[0] <= '9' && p[1] == '.') return;
        journal_add(cur_conv, cur_who, cur_item, 1, p, 1);
        return;
    }
    journal_add(cur_conv, cur_who, cur_item, said_next || c == 1 ? 1 : c == 2 ? 2 : 0, p, 1);
}

void uwglue_scroll(const char *s, int mode, int conv, int whoami, int item)
{
    if (mode < 0 || mode > 2 || !s) return;
    lock();
    cur_conv = conv; cur_who = whoami; cur_item = item;
    feed(mode, s, line_done, &colour[mode]);
    if (mode == 1 && said_next && strchr(s, '\n')) said_next = 0;
    unlock();
}

void uwglue_said(const char *s, int conv, int whoami, int item)
{
    (void)s; (void)conv; (void)whoami; (void)item;
    lock();
    said_next = 1;
    unlock();
}

/* ---- once a frame, from the main thread ---- */

void uw_glue_tick(void)
{
    static unsigned last;
    if (lk_ready && !LightLock_TryLock(&lk)) {
        if (pub.seq != last) { mine = pub; last = pub.seq; }
        unlock();
    }
}

/* ---- other levels and the map's notes, from SAVE0's LEV.ARK ---- */

static unsigned char *lev_block(int blk, long *len)
{
#ifdef GAME_UW2
    /* UW2's LEV.ARK compresses its blocks: other levels and the notes are not read yet */
    (void)blk; *len = 0;
    return NULL;
#endif
    char path[1100];
    unsigned char hdr[2], *buf = NULL, *offs = NULL;
    unsigned count, i;
    unsigned long off, next = 0;
    long size;
    FILE *f;
    *len = 0;
    if (plat_resolve("SAVE0\\LEV.ARK", PLAT_READ, path, sizeof path)) return NULL;
    if (!(f = fopen(path, "rb"))) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fread(hdr, 1, 2, f) != 2) goto out;
    count = hdr[0] | hdr[1] << 8;
    if ((unsigned)blk >= count) goto out;
    offs = malloc(count * 4u);
    if (!offs || fread(offs, 4, count, f) != count) goto out;
    off = offs[blk * 4] | offs[blk * 4 + 1] << 8 | (unsigned long)offs[blk * 4 + 2] << 16 | (unsigned long)offs[blk * 4 + 3] << 24;
    if (!off) goto out;
    next = (unsigned long)size;
    for (i = 0; i < count; i++) {
        unsigned long o = offs[i * 4] | offs[i * 4 + 1] << 8 | (unsigned long)offs[i * 4 + 2] << 16 | (unsigned long)offs[i * 4 + 3] << 24;
        if (o > off && o < next) next = o;
    }
    if (next <= off || next - off > 0x10000) goto out;
    buf = malloc(next - off);
    if (!buf) goto out;
    fseek(f, (long)off, SEEK_SET);
    if (fread(buf, 1, next - off, f) != next - off) { free(buf); buf = NULL; goto out; }
    *len = (long)(next - off);
out:
    free(offs);
    fclose(f);
    return buf;
}

int uw_level_automap(int level, uint8_t out[64][64])
{
    long n;
    unsigned char *b = lev_block(level + 0x1A, &n);
    if (!b || n < 4096) { free(b); return 0; }
    memcpy(out, b, 4096);
    free(b);
    return 1;
}

int uw_notes(int level, struct uwnote *out, int max)
{
    long n;
    int i, k = 0;
    unsigned char *b = lev_block(level + 0x23, &n);
    if (!b) return 0;
    for (i = 0; i + 0x36 <= n && k < max; i += 0x36) {
        int x = (int16_t)(b[i + 0x32] | b[i + 0x33] << 8), y = (int16_t)(b[i + 0x34] | b[i + 0x35] << 8);
        char *s;
        if (x < 0) continue;
        /* the map screen's coordinates: tile x at (x-1)*3+10, y from the bottom at (y-1)*3+7 */
        out[k].x = (x - 10) / 3.0f + 1;
        out[k].y = (y - 7) / 3.0f + 1;
        memcpy(out[k].text, b + i, 0x32);
        out[k].text[0x32] = 0;
        for (s = out[k].text; *s; s++) if (*s == '_') *s = ' ';
        k++;
    }
    free(b);
    return k;
}

/* ---- typed text ---- */

static volatile int text_req;
static char text_prompt[80];
static int text_max, text_digits;

void uwglue_text_request(const char *prompt, int maxlen, int digits)
{
    lock();
    snprintf(text_prompt, sizeof text_prompt, "%s", prompt ? prompt : "");
    text_max = maxlen;
    text_digits = digits;
    text_req = 1;
    unlock();
}

int uw_text_wanted(char *prompt, int psz, int *maxlen, int *digits)
{
    if (!text_req) return 0;
    lock();
    snprintf(prompt, (size_t)psz, "%s", text_prompt);
    *maxlen = text_max;
    *digits = text_digits;
    text_req = 0;
    unlock();
    return 1;
}
