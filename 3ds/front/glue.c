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

int uw_request_pending(void) { return req != 0; }

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
static char jchar[64];                 /* whose journal.txt this is: name|class|female (UW-12) */
/* UW-07: the journal's new lines wait here (appended under the lock, no file call there); the
   file is written outside the lock, by the I/O thread every two seconds (front_tick) or at once
   by uw_journal_flush (HOME, exit). jio keeps two writers from interleaving. */
static char *jpend;
static size_t jpend_n, jpend_cap;
static LightLock jio;
static int jio_ready;

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
    if (save && jfile) {
        char head[64];
        size_t hn = (size_t)snprintf(head, sizeof head, "%d|%d|%d|%d|", conv, whoami, item, who), tn = strlen(text);
        if (jpend_n + hn + tn + 1 > jpend_cap) {
            size_t nc = jpend_cap ? jpend_cap : 4096;
            char *nb;
            while (nc < jpend_n + hn + tn + 1) nc *= 2;
            nb = realloc(jpend, nc);
            if (!nb) return;
            jpend = nb; jpend_cap = nc;
        }
        memcpy(jpend + jpend_n, head, hn); jpend_n += hn;
        memcpy(jpend + jpend_n, text, tn); jpend_n += tn;
        jpend[jpend_n++] = '\n';
        jdirty = 1;
    }
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
            if (!strncmp(line, "#char|", 6)) { snprintf(jchar, sizeof jchar, "%s", line + 6); continue; }
            if (sscanf(line, "%d|%d|%d|%d|%n", &conv, &whoami, &item, &who, &n) == 4 && n)
                journal_add(conv, whoami, item, who, line + n, 0);
        }
        fclose(f);
    }
    latest = -1;
    if (!jio_ready) { LightLock_Init(&jio); jio_ready = 1; }
    jfile = fopen(UW3DS_HOME "/journal.txt", "a");
    uw_io_start();
}

static void journal_forget(void)
{
    int i, k;
    for (i = 0; i < npeople; i++) {
        for (k = 0; k < people[i].n; k++) free(people[i].lines[k].text);
        free(people[i].lines);
        memset(&people[i], 0, sizeof people[i]);
    }
    npeople = 0;
    latest = -1;
}

/* UW-12: the journal belongs to one character. Called once the game is running with the
   character's name: the first time it only claims the file (an older file with no owner is taken
   to be this character's); a different character moves journal.txt aside as journal-N.txt (never
   deleted) and starts a fresh one. */
int uw_journal_character(const char *name, int pclass, int female)
{
    int fresh = 0;
    char id[64];
    if (!name || !*name) return 0;
    snprintf(id, sizeof id, "%s|%d|%d", name, pclass, female);
    if (!jio_ready) { LightLock_Init(&jio); jio_ready = 1; }
    LightLock_Lock(&jio);                /* UW-07: no journal write on the I/O thread meanwhile */
    lock();
    if (!strcmp(id, jchar)) { unlock(); LightLock_Unlock(&jio); return 0; }
    if (jchar[0]) {
        char to[200];
        int k;
        if (jfile && jpend_n) fwrite(jpend, 1, jpend_n, jfile);   /* the old character's last lines go in its file */
        jpend_n = 0; jdirty = 0;
        if (jfile) { fclose(jfile); jfile = NULL; }
        for (k = 1; k < 1000; k++) {
            FILE *t;
            snprintf(to, sizeof to, UW3DS_HOME "/journal-%d.txt", k);
            if (!(t = fopen(to, "r"))) break;
            fclose(t);
        }
        rename(UW3DS_HOME "/journal.txt", to);
        journal_forget();
        jfile = fopen(UW3DS_HOME "/journal.txt", "a");
        fresh = 1;
    }
    snprintf(jchar, sizeof jchar, "%s", id);
    if (jfile) { fprintf(jfile, "#char|%s\n", id); fflush(jfile); }
    unlock();
    LightLock_Unlock(&jio);
    return fresh;
}

/* Takes the pending lines under the lock (a pointer swap), then writes them without it. */
void uw_journal_flush(void)
{
    char *buf;
    size_t n;
    if (!jio_ready) { LightLock_Init(&jio); jio_ready = 1; }
    LightLock_Lock(&jio);
    lock();
    buf = jpend; n = jpend_n;
    if (!jdirty || !jfile) buf = NULL;
    if (buf) { jpend = NULL; jpend_n = jpend_cap = 0; jdirty = 0; }
    unlock();
    if (buf) {
        if (n) fwrite(buf, 1, n, jfile);
        fflush(jfile);
        free(buf);
    }
    LightLock_Unlock(&jio);
}

/* ---- UW-07: the I/O thread ----
   The dashboard map re-read the map's notes (every 3 s) and another level's map (every 5 s)
   from SAVE0's LEV.ARK, and front_tick wrote the journal, on the main (screens) thread: an SD
   card read or write in the middle of a frame. Those refreshes now run here, on the screens'
   core below the screens' priority (it runs while they wait for the vertical blank); map.c
   picks the result up when it is ready. A level picked by hand is still read at once, as
   before, so the map never shows an empty frame. */
static Thread io_th;
static volatile int io_stop, io_flush_req, io_notes_req = -1, io_other_req = -1;
static LightLock iolk;
static struct uwnote io_notes[100];
static int io_nnotes, io_notes_level = -1, io_notes_ready;
static uint8_t io_other[64][64];
static int io_other_ok, io_other_level = -1, io_other_ready;

static void io_thread(void *arg)
{
    static struct uwnote nb[100];
    static uint8_t ob[64][64];
    (void)arg;
    while (!io_stop) {
        int l;
        svcSleepThread(50000000LL);
        if (io_stop) break;
        if (io_flush_req) { io_flush_req = 0; uw_journal_flush(); }
        if ((l = io_notes_req) >= 0) {
            int n = uw_notes(l, nb, 100);
            LightLock_Lock(&iolk);
            memcpy(io_notes, nb, sizeof io_notes); io_nnotes = n; io_notes_level = l; io_notes_ready = 1;
            io_notes_req = -1;
            LightLock_Unlock(&iolk);
        }
        if ((l = io_other_req) >= 0) {
            int ok = uw_level_automap(l, ob);
            LightLock_Lock(&iolk);
            memcpy(io_other, ob, sizeof io_other); io_other_ok = ok; io_other_level = l; io_other_ready = 1;
            io_other_req = -1;
            LightLock_Unlock(&iolk);
        }
    }
}

void uw_io_start(void)
{
    s32 prio = 0x30;
    if (io_th) return;
    LightLock_Init(&iolk);
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    io_th = threadCreate(io_thread, NULL, 32 * 1024, prio + 2 > 0x3F ? 0x3F : prio + 2, 0, false);
}

void uw_io_stop(void)
{
    if (!io_th) return;
    io_stop = 1;
    threadJoin(io_th, U64_MAX);
    threadFree(io_th);
    io_th = NULL;
}

/* A journal write on the I/O thread (front_tick); without the thread, here. */
void uw_journal_flush_async(void)
{
    if (io_th) io_flush_req = 1;
    else uw_journal_flush();
}

void uw_notes_request(int level) { if (io_th) io_notes_req = level; }
void uw_other_request(int level) { if (io_th) io_other_req = level; }
int uw_io_async(void) { return io_th != NULL; }

/* 1 and the notes when a read for this level has finished since the last call */
int uw_notes_take(int level, struct uwnote *out, int max, int *n)
{
    int got = 0;
    if (!io_th) return 0;
    LightLock_Lock(&iolk);
    if (io_notes_ready && io_notes_level == level) {
        int k = io_nnotes < max ? io_nnotes : max;
        memcpy(out, io_notes, (size_t)k * sizeof *out); *n = k; got = 1;
    }
    if (io_notes_ready) io_notes_ready = 0;
    LightLock_Unlock(&iolk);
    return got;
}

int uw_other_take(int level, uint8_t out[64][64], int *ok)
{
    int got = 0;
    if (!io_th) return 0;
    LightLock_Lock(&iolk);
    if (io_other_ready && io_other_level == level) { memcpy(out, io_other, sizeof io_other); *ok = io_other_ok; got = 1; }
    if (io_other_ready) io_other_ready = 0;
    LightLock_Unlock(&iolk);
    return got;
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

/* ---- crash-safe quicksave (UW-01) ---- */
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
volatile int uw_last_save_ok;
volatile int uw_last_save_refused;   /* UW-03: the game turned the last save request down (not safe now) */

static void rm_tree(const char *path)
{
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        char sub[512];
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
            struct stat st;
            if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode)) rm_tree(sub);
            else remove(sub);
        }
        closedir(d);
        rmdir(path);
    } else
        remove(path);
}

int uwglue_swap_slot(int from, int to)
{
    char dfrom[8], dto[8], pfrom[512], pto[512], pbak[520];
    struct stat st;
    snprintf(dfrom, sizeof dfrom, "SAVE%d", from);
    snprintf(dto, sizeof dto, "SAVE%d", to);
    if (plat_resolve(dfrom, PLAT_READ, pfrom, sizeof pfrom) || stat(pfrom, &st) != 0) return -1;
    if (plat_resolve(dto, PLAT_READ, pto, sizeof pto)) return -1;
    snprintf(pbak, sizeof pbak, "%s.BAK", pto);
    if (stat(pto, &st) == 0) {
        rm_tree(pbak);                         /* the save before last */
        if (rename(pto, pbak) != 0) return -1; /* the last good save stays as SAVEn.BAK */
    }
    if (rename(pfrom, pto) != 0) {
        rename(pbak, pto);                     /* put the last good save back */
        return -1;
    }
    return 0;
}
