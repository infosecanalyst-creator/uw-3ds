/* plat_3ds.c: the New Nintendo 3DS backend of Exhume's platform API (plat.h), in place of
   platform/sdl3/plat_sdl3.c. The same model as the desktop's: the game runs on a thread of its
   own (on the New 3DS's third core when the system gives it, else the second), this file's loop
   runs on the main thread, reads the emulated VGA's picture each screen refresh and presents
   it, and turns the buttons and the touch screen into the PC's keyboard and mouse.

   The bottom screen shows the whole game, played with the stylus as the mouse: its 320x200
   picture stretched to the lines the front end gives it (240, the 4:3 shape a CRT gave it, in
   the menus; 214 above the front end's tabs in play), so a touch lands where it is drawn. The
   top screen is the front end's (3ds/front): the game's 3D view enlarged, or the title. */
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "plat.h"
#include "plat3ds.h"
extern int ui_top_w;   /* front/ui.c: dashboard layout width */
#include "ui/settings.h"
u32 front_remap(u32 keys);   /* front.c: left-handed play */
int front_save_now(unsigned pickup_ms, unsigned limit_ms);   /* front.c: UW-05's quicksave, on the main thread */

void port_pause(int on);               /* the runtime's sys/pit.c */
void port_crash_handlers(void);        /* sys/crash.c */
uint32_t pit_bios_ticks(void);         /* sys/pit.c */

static const PlatHooks *hooks;
static PlatConfig live;
static volatile int game_done, game_status, quit_req;
static Thread game_thread_h;
static uint64_t t0;
static unsigned frames;

/* ---- start-up: the SD card's folders, the log ---- */

static void mkdirs(const char *p)
{
    char b[256];
    size_t i;
    snprintf(b, sizeof b, "%s", p);
    for (i = 1; b[i]; i++)
        if (b[i] == '/') { b[i] = 0; mkdir(b, 0777); b[i] = '/'; }
    mkdir(b, 0777);
}

static void pick_core(void);

/* Before main: the port's home and the game's folder, which main.c reads from the environment,
   and the log in place of a terminal. */
__attribute__((constructor)) static void p3_early(void)
{
    osSetSpeedupEnable(true);           /* the New 3DS's 804 MHz and L2 cache */
    mkdirs(UW3DS_HOME);
    mkdirs(UW3DS_DATA);
#if defined(GAME_UW2)
    setenv("UW2PORT_HOME", UW3DS_HOME, 0);
    setenv("UW2PORT_DATA", UW3DS_DATA, 0);
#else
    setenv("UW1PORT_HOME", UW3DS_HOME, 0);
    setenv("UW1PORT_DATA", UW3DS_DATA, 0);
#endif
    setenv("HOME", UW3DS_ROOT, 0);
    {
        /* the first run's settings: the 3DS layout's enhancements on (each can be turned off in
           the settings screen), no settings screen at start, no session recordings on the card */
        FILE *f = fopen(UW3DS_HOME "/" PORT_CONFIG_FILE, "r");
        if (f) fclose(f);
        else if ((f = fopen(UW3DS_HOME "/" PORT_CONFIG_FILE, "w")) != NULL) {
            fputs("# " PORT_NAME " settings (3DS)\n"
                  "settings-at-start=0\n"
                  "recording=0\n"
                  "enhance=skip-intro,modern-keys,rune-keys,wrap-menu,fast-panels,perspective,full-sprites,tidy-objects,restore-at-rest\n", f);
            fclose(f);
        }
    }
    if (!freopen(UW3DS_ROOT "/log.txt", "w", stderr)) { }
    setvbuf(stderr, NULL, _IOLBF, 0);
    romfsInit();
    pick_core();
}

/* ---- time and threads ---- */

/* ---- leaving: every other thread parks itself before the app exits ----
   The game's thread (and the timer's and the synthesizer's) run on their own cores. Returning
   from main while they run lets the C library free the heap, their stacks with it, under them
   (a data abort as HOME closes the app); ending the process abruptly instead skips telling the
   system we left (the console restarted). So, on the way out, park_req is set and each of those
   threads, the next time it asks the time, sleeps, or reaches the game's main loop, stops in a
   kernel wait that never ends; then the app exits the normal way and the kernel ends them. */
static volatile int park_req, parked_game;
static Handle park_event;

void p3_park_point(void)
{
    if (!park_req || !threadGetCurrent()) return;       /* never the main thread */
    if (threadGetCurrent() == game_thread_h) parked_game = 1;
    __dsb();
    for (;;) svcWaitSynchronization(park_event, U64_MAX);
}

uint64_t plat_counter(void) { p3_park_point(); return svcGetSystemTick(); }
uint64_t plat_counter_hz(void) { return SYSCLOCK_ARM11; }
void plat_sleep_ns(uint64_t ns) { p3_park_point(); svcSleepThread((s64)ns); }
uint64_t p3_ms(void) { return (svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000); }
unsigned p3_frame(void) { return frames; }

struct thr { int (*fn)(void *); void *arg; };

static void thr_entry(void *p)
{
    struct thr t = *(struct thr *)p;
    free(p);
    t.fn(t.arg);
}

/* The game's core: the New 3DS's third (2) when the system lets an application use it, else
   the second (1, with its time limit raised), else the first. */
static int game_core = 0;
static int timer_core = -1;       /* the PIT's (and with it the FM chips'): core 1 when the game has core 2 */

static void noop_thread(void *p) { (void)p; }

static void pick_core(void)
{
    s32 prio;
    Thread t;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    APT_SetAppCpuTimeLimit(80);
    game_core = 0;
    /* a probe: can a thread start on core 2, then core 1? */
    {
        bool n3ds = false;
        APT_CheckNew3DS(&n3ds);
        if (n3ds) {
            t = threadCreate(noop_thread, NULL, 4096, prio + 1, 2, false);
            if (t) {
                threadJoin(t, U64_MAX); threadFree(t); game_core = 2;
                /* the timer shares the game's core, above it: the system core's time slices
                   (APT's limit) left it waiting seconds at a time */
                return;
            }
        }
        t = threadCreate(noop_thread, NULL, 4096, prio + 1, 1, false);
        if (t) { threadJoin(t, U64_MAX); threadFree(t); game_core = 1; return; }
    }
}

/* The PIT and sound threads go on the game's core above its priority (they sleep most of the
   time and must wake on time; the 3DS does not share a core between threads of one priority). */
int plat_thread_start(const char *name, int (*fn)(void *), void *arg)
{
    struct thr *t = malloc(sizeof *t);
    Thread h = NULL;
    int core = timer_core;
    if (!t) return -1;
    t->fn = fn; t->arg = arg;
    if (name && !strcmp(name, "synth")) {
        /* the music's synthesis (sound/audio.c): the system core (1), which APT lends the
           application, away from the game's; else the screens' core, below the screens */
        core = 1;
        h = threadCreate(thr_entry, t, 64 * 1024, 0x20, 1, true);
        if (!h) { core = 0; h = threadCreate(thr_entry, t, 64 * 1024, 0x31, 0, true); }
    } else {
        if (core >= 0) h = threadCreate(thr_entry, t, 64 * 1024, 0x18, core, true);
        if (!h) { core = game_core; h = threadCreate(thr_entry, t, 64 * 1024, 0x18, core, true); }
    }
    if (!h) { free(t); return -1; }
    fprintf(stderr, PLAT_NAME ": thread %s on core %d\n", name ? name : "?", core);
    return 0;
}

static int (*game_fn)(void *);
static void *game_arg;

static void game_entry(void *p)
{
    (void)p;
    port_crash_handlers();
    game_status = game_fn(game_arg);
    game_done = 1;
}

void plat_game_exit(int status)
{
    game_status = status;
    game_done = 1;
    for (;;) svcSleepThread(1000000000LL);
}

void plat_game_park(void)
{
    for (;;) svcSleepThread(1000000000LL);
}

/* ---- sound: NDSP, stereo 16-bit, fed from the port's ring by a thread of its own ---- */

#define AFRAMES 1024
#define ABUFS 4
static void (*audio_fill)(int16_t *, int);
static ndspWaveBuf wbuf[ABUFS];
static int16_t *abuf;
static LightEvent aev;
static volatile int audio_on;

static int ndsp_held;                /* 1 while we hold our ndspInit reference */

static void ndsp_cb(void *ud) { (void)ud; LightEvent_Signal(&aev); }

/* Give our ndsp reference back. Left held, the DSP thread keeps running while exit() frees the
   heap its stack lives in, and it faults in ndspiReadChnState (the JA2 exit crash, Nov 20 2026).
   Called at the end of plat_run and, for exit() from anywhere else (port_fatal's exit(1)),
   from atexit. ndsp_held makes the second call do nothing: an unmatched ndspExit would
   unbalance libctru's reference count. */
static void audio_close(void)
{
    if (!ndsp_held) return;
    ndsp_held = 0;
    audio_on = 0;
    LightEvent_Signal(&aev);
    svcSleepThread(30000000LL);          /* the sound thread sees audio_on and stops */
    ndspSetCallback(NULL, NULL);
    ndspChnWaveBufClear(0);
    ndspExit();
}

static void audio_thread(void *p)
{
    (void)p;
    while (audio_on) {
        int i;
        for (i = 0; i < ABUFS; i++)
            if (wbuf[i].status == NDSP_WBUF_DONE || wbuf[i].status == NDSP_WBUF_FREE) {
                audio_fill(wbuf[i].data_pcm16, AFRAMES);
                DSP_FlushDataCache(wbuf[i].data_pcm16, AFRAMES * 4);
                ndspChnWaveBufAdd(0, &wbuf[i]);
            }
        LightEvent_Wait(&aev);
    }
}

int plat_audio_open(int rate, void (*fill)(int16_t *, int))
{
    float mix[12];
    int i;
    s32 prio;
    if (ndspInit()) {
        fprintf(stderr, PLAT_NAME ": no DSP (dspfirm.cdc missing from /3ds?): silent\n");
        return -1;
    }
    audio_fill = fill;
    abuf = linearAlloc(AFRAMES * 4 * ABUFS);
    if (!abuf) { ndspExit(); return -1; }
    memset(abuf, 0, AFRAMES * 4 * ABUFS);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)rate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    memset(mix, 0, sizeof mix);
    mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);
    memset(wbuf, 0, sizeof wbuf);
    for (i = 0; i < ABUFS; i++) {
        wbuf[i].data_pcm16 = abuf + i * AFRAMES * 2;
        wbuf[i].nsamples = AFRAMES;
        wbuf[i].status = NDSP_WBUF_FREE;
    }
    LightEvent_Init(&aev, RESET_ONESHOT);
    ndsp_held = 1;
    {
        static int registered;
        if (!registered) { registered = 1; atexit(audio_close); }
    }
    ndspSetCallback(ndsp_cb, NULL);
    audio_on = 1;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    if (!threadCreate(audio_thread, NULL, 32 * 1024, prio - 1, 0, true)) { audio_on = 0; return -1; }
    LightEvent_Signal(&aev);
    return 0;
}

/* ---- files and dialogs ---- */

const char *plat_base_dir(void) { return UW3DS_ROOT "/"; }

void plat_message(int error, const char *title, const char *text)
{
    errorConf e;
    char buf[1900];
    fprintf(stderr, PLAT_NAME ": %s\n%s\n", title, text);
    snprintf(buf, sizeof buf, "%s\n\n%s", title, text);
    errorInit(&e, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
    errorText(&e, buf);
    errorDisp(&e);
    (void)error;
}

int plat_choose_folder(const char *title, const char *text, char *out, size_t outsz)
{
    char buf[3000];
    (void)out; (void)outsz;
    snprintf(buf, sizeof buf, "%s\n\nCopy your game's folder (UW.EXE, DATA, CRIT, CUTS, SOUND), or GOG's "
             "game.gog, to the SD card as\n" UW3DS_DATA "\nand start again.", text);
    plat_message(1, title, buf);
    return -1;
}

/* ---- the display options of the desktop: one fixed layout here ---- */

int plat_max_scale(int aspect) { (void)aspect; return 1; }
void plat_set_display(int fullscreen, int scale, int aspect, int integer_scale)
{
    live.fullscreen = fullscreen; live.scale = scale; live.aspect = aspect; live.integer_scale = integer_scale;
}
void plat_set_mouse_lock(int on) { live.mouse_lock = on; }

/* ---- the keyboard and mouse ---- */

static uint8_t held[512];              /* keys down, by code (E0 codes at +256) */
static unsigned buttons;
static float mx = 160, my = 100;
static volatile int capture;           /* mouse-look asked for the pointer */
static int was_open;

void plat_pointer_capture(int on) { capture = on; }
int p3_mouse_captured(void) { return capture; }
void p3_mouse_pos(float *x, float *y) { *x = mx; *y = my; }

static void send_key(int code, int down)
{
    if (!hooks || !hooks->key) return;
    if (code & K_E0) hooks->key(0xE0);
    hooks->key((uint8_t)((code & 0x7F) | (down ? 0 : 0x80)));
}

void p3_key(int code, int down)
{
    int i = (code & 0x7F) | ((code & K_E0) ? 256 : 0);
    if (down == held[i]) return;
    held[i] = (uint8_t)down;
    send_key(code, down);
}

static struct { int code; unsigned until; } taps[32];

void p3_tap(int code, int fr)
{
    int i;
    p3_key(code, 0);
    p3_key(code, 1);
    for (i = 0; i < 32; i++)
        if (!taps[i].code) { taps[i].code = code; taps[i].until = frames + (unsigned)(fr < 1 ? 1 : fr); return; }
    p3_key(code, 0);
}

static void taps_run(void)
{
    int i;
    for (i = 0; i < 32; i++)
        if (taps[i].code && (int)(frames - taps[i].until) >= 0) {
            p3_key(taps[i].code, 0);
            taps[i].code = 0;
        }
}

void p3_release_keys(void)
{
    int i;
    for (i = 0; i < 512; i++)
        if (held[i]) p3_key(i >= 256 ? (K_E0 | (i - 256)) : i, 0);
    memset(taps, 0, sizeof taps);
}

static void pointer(int type, unsigned b, float dx, float dy)
{
    PlatPointer p;
    if (!hooks || !hooks->pointer) return;
    memset(&p, 0, sizeof p);
    p.type = type;
    p.x = mx; p.y = my;
    p.dx = dx; p.dy = dy;
    p.button = b;
    p.buttons = buttons;
    p.absolute = !capture;
    hooks->pointer(&p);
}

void p3_mouse_move(float x, float y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > 319) x = 319;
    if (y > 199) y = 199;
    if (x == mx && y == my) return;
    float dx = x - mx, dy = y - my;
    mx = x; my = y;
    pointer(PLAT_POINTER_MOVE, 0, dx, dy);
}

void p3_mouse_rel(float dx, float dy)
{
    if (!dx && !dy) return;
    pointer(PLAT_POINTER_MOVE, 0, dx, dy);
}

void p3_mouse_button(unsigned b, int down)
{
    if (!!(buttons & b) == !!down) return;
    buttons = down ? buttons | b : buttons & ~b;
    pointer(down ? PLAT_POINTER_DOWN : PLAT_POINTER_UP, b, 0, 0);
}

static void release_all(void)
{
    if (buttons & PLAT_BUTTON_LEFT) p3_mouse_button(PLAT_BUTTON_LEFT, 0);
    if (buttons & PLAT_BUTTON_RIGHT) p3_mouse_button(PLAT_BUTTON_RIGHT, 0);
    if (buttons & PLAT_BUTTON_MIDDLE) p3_mouse_button(PLAT_BUTTON_MIDDLE, 0);
    p3_release_keys();
}

/* An input script's bytes and events (the desktop's tests; nothing on the 3DS sends them, but the
   runtime links them). */
void plat_key_byte(uint8_t b)
{
    if (hooks && hooks->key) hooks->key(b);
}

void plat_pointer_event(const PlatPointer *ev)
{
    if (!hooks || !hooks->pointer) return;
    if (ev->absolute) { mx = ev->x; my = ev->y; }
    hooks->pointer(ev);
}

void plat_window_click(float x, float y, int release)
{
    mx = x * 320.0f / 320.0f; my = y * 200.0f / (front_game_height() > 0 ? front_game_height() : 240.0f);
    p3_mouse_button(PLAT_BUTTON_LEFT, 1);
    if (release) p3_mouse_button(PLAT_BUTTON_LEFT, 0);
}

/* ---- the settings screen ---- */

static int paused_by_front;

static void sync_open(void)
{
    int o = settings_open();
    if (o == was_open) return;
    was_open = o;
    if (o) { release_all(); port_pause(1); }
    else if (!paused_by_front) port_pause(0);
}

void p3_settings(int open)
{
    if (open && !settings_open()) settings_show(1);
    else if (!open && settings_open()) settings_key(SET_KEY_F11);
    sync_open();
}

int p3_settings_open(void) { return settings_open(); }

void p3_pause(int on)
{
    paused_by_front = on;
    if (on) { release_all(); port_pause(1); }
    else if (!settings_open()) port_pause(0);
}

void p3_quit(void)
{
    quit_req = 1;
}

float p3_slider(void) { return osGet3DSliderState(); }

/* ---- the picture ---- */

static uint8_t pix[640 * 480];
static uint8_t pal[768];
static int pw = 320, ph = 200;
static uint16_t *lin;                  /* the picture in RGB565, 512x512, for the GPU's transfer */
static C3D_Tex gtex;
static C2D_Image gimg;
static Tex3DS_SubTexture gsub;
static uint32_t *layer;                /* the settings screen, 640x400 RGBA */
static uint16_t *llin;
static C3D_Tex ltex;
static C2D_Image limg;
static Tex3DS_SubTexture lsub;
static C3D_RenderTarget *top_l, *top_r, *bot;

const uint8_t *p3_picture(int *w, int *h, const uint8_t **rgb6)
{
    *w = pw; *h = ph;
    if (rgb6) *rgb6 = pal;
    return pix;
}

#define TRANSFER (GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | \
                  GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) | \
                  GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

/* A texture's pixel (x, y), y from the image's top: 8x8 tiles in rows, Morton order inside each
   (the GPU's tiled layout; with the subtexture's top at 1.0 the image's first row is its top). */
static inline unsigned tile_ofs(unsigned x, unsigned y, unsigned w)
{
    return ((((y >> 3) * (w >> 3) + (x >> 3)) << 6) |
            ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
}

static void upload_game(void)
{
    static uint16_t lut[256];
    uint16_t *d = gtex.data;
    int i, x, y, tw = gtex.width, th;
    for (i = 0; i < 256; i++) {
        const uint8_t *c = pal + 3 * i;
        lut[i] = (uint16_t)(((c[0] >> 1) << 11) | (c[1] << 5) | (c[2] >> 1));
    }
    if (pw > tw) pw = tw;
    th = ph > (int)gtex.height ? (int)gtex.height : ph;
    for (y = 0; y < th; y++) {
        const uint8_t *s = pix + y * pw;
        for (x = 0; x < pw; x++) d[tile_ofs(x, y, tw)] = lut[s[x]];
    }
    C3D_TexFlush(&gtex);
    gsub.width = (u16)pw; gsub.height = (u16)th;
    gsub.left = 0; gsub.top = 1.0f;
    gsub.right = (float)pw / tw; gsub.bottom = 1.0f - (float)th / gtex.height;
}

static void upload_layer(void)
{
    uint16_t *d = ltex.data;
    int x, y;
    for (y = 0; y < SET_H; y++)
        for (x = 0; x < SET_W; x++) {
            uint32_t c = layer[y * SET_W + x];     /* ABGR8888 as the desktop's texture: R in the low byte */
            unsigned r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF, a = c >> 24;
            /* over the dimmed game: blend with black here, the alpha's darkening baked in */
            r = r * a / 255; g = g * a / 255; b = b * a / 255;
            d[tile_ofs(x, y, 1024)] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    C3D_TexFlush(&ltex);
    lsub.width = SET_W; lsub.height = SET_H;
    lsub.left = 0; lsub.top = 1.0f; lsub.right = (float)SET_W / 1024; lsub.bottom = 1.0f - (float)SET_H / 512;
}

void p3_draw_picture(float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh)
{
    Tex3DS_SubTexture st;
    C2D_Image im;
    float tw = gtex.width, th = gtex.height;
    st.width = (u16)sw; st.height = (u16)sh;
    st.left = sx / tw; st.right = (sx + sw) / tw;
    st.top = 1.0f - sy / th; st.bottom = 1.0f - (sy + sh) / th;
    im.tex = &gtex; im.subtex = &st;
    C2D_DrawImageAt(im, dx, dy, 0.5f, NULL, dw / sw, dh / sh);
}

/* ---- the loop ---- */

static void settings_input(u32 down, int touching, touchPosition t)
{
    static int was_touching;
    if (down & KEY_UP) settings_key(SET_KEY_UP);
    if (down & KEY_DOWN) settings_key(SET_KEY_DOWN);
    if (down & KEY_LEFT) settings_key(SET_KEY_LEFT);
    if (down & KEY_RIGHT) settings_key(SET_KEY_RIGHT);
    if (down & (KEY_L | KEY_R)) settings_key(SET_KEY_TAB);
    if (down & front_confirm_key()) settings_key(SET_KEY_ENTER);
    if (down & front_back_key()) settings_key(SET_KEY_ESC);
    if (down & (KEY_START | KEY_SELECT)) settings_key(SET_KEY_F11);
    if (touching || was_touching) {
        /* the bottom screen shows the 640x400 layer at half size, 20 pixels down */
        int x = t.px * 2, y = (t.py - 20) * 2;
        if (touching && !was_touching) settings_pointer(x, y, 0);
        settings_pointer(x, y, touching);
    }
    was_touching = touching;
    sync_open();
}

/* ZL swapped: the touchscreen moves the game's cursor like a laptop's touchpad (by how far the
   finger slides, not where it is) and a short tap clicks where the cursor is. The game draws its
   own cursor, so you see where it points on the top screen. */
static void touchpad(int touching, touchPosition t)
{
    static int was, moved, lx, ly, release_due;
    static unsigned start, b;
    if (front_menu_open()) { was = 0; return; }
    if (touching) {
        if (!was) { lx = t.px; ly = t.py; moved = 0; start = frames; }
        else {
            int dx = t.px - lx, dy = t.py - ly;
            if (dx || dy) {
                /* the game's mouse is in its 320x200 coordinates; a little faster than 1:1 so
                   one slide crosses the screen */
                p3_mouse_move(mx + dx * 1.25f, my + dy * 1.1f);
                moved += abs(dx) + abs(dy);
                lx = t.px; ly = t.py;
            }
        }
    } else if (was && moved < 8 && frames - start < 20) {
        b = front_touch_button();
        p3_mouse_button(b, 1);
        release_due = frames + 6;           /* held 100 ms, so the game's poll sees it */
    }
    if (release_due && (int)(frames - release_due) >= 0) { p3_mouse_button(b, 0); release_due = 0; }
    was = touching;
}

static void game_input(u32 down, u32 held_k, u32 up, circlePosition cp, circlePosition cs, int touching, touchPosition t)
{
    static int was_touching, release_due;
    static unsigned touch_b, pressed_at;
    int took = front_input(down, held_k, up, cp, cs, touching, t);
    if (took) {
        if (was_touching || release_due) p3_mouse_button(touch_b, 0);
        was_touching = release_due = 0;
        return;
    }
    if (touching) {
        /* the game's 320x200 is stretched to the front end's game lines */
        float gh = front_game_height();
        float x = (float)t.px * pw / 320.0f, y = (float)t.py * ph / (gh > 0 ? gh : 240.0f);
        p3_mouse_move(x, y);
        if (!was_touching && !release_due) {
            touch_b = front_touch_button();
            p3_mouse_button(touch_b, 1);
            pressed_at = frames;
        }
        release_due = 0;
    } else if (was_touching)
        release_due = 1;
    /* a tap is held at least 6 frames (100 ms): the game reads the mouse's buttons when it polls,
       and a press and release between two polls would be lost */
    if (release_due && frames - pressed_at >= 6) {
        p3_mouse_button(touch_b, 0);
        release_due = 0;
    }
    was_touching = touching;
}

static int swapped;                 /* ZL: the game on the top screen, the dashboard on the touchscreen */
/* Rule 16 (remember the setup): the swap is kept in a marker file next to the settings. */
#define SWAP_FILE UW3DS_HOME "/.swapped"
static int swap_loaded;
static void swap_load(void)
{
    struct stat sb;
    if (swap_loaded) return;
    swapped = stat(SWAP_FILE, &sb) == 0; swap_loaded = 1;
}
static void swap_save(void)
{
    if (swapped) { FILE *f = fopen(SWAP_FILE, "w"); if (f) fclose(f); }
    else remove(SWAP_FILE);
}
static C3D_RenderTarget *top_w;     /* the top screen at 800x240 (wide mode) */
static int wide_on;

/* Text on the top screen is sharper in its 800-pixel-wide mode, which the New 3DS has for 2D:
   the screen's own columns, where the normal mode doubles each pixel. So with the 3D slider down
   the top screen is drawn 800 wide (everything scaled 2x across, so the same 400x240 layout),
   and with the slider up it goes back to the two 400-wide eyes. */
static void top_mode(float iod)
{
    int want = iod <= 0.0f && top_w;
    if (want == wide_on) return;
    /* libctru keeps 3D and wide in one setting (gfxTopMode): setting one replaces the other,
       so call only the one wanted; calling both left it in neither */
    if (want) gfxSetWide(true);
    else gfxSet3D(true);
    wide_on = want;
}

/* begin drawing the top screen (eye 0 left, 1 right) in its 400x240 coordinates */
static void top_begin(int eye)
{
    C3D_RenderTarget *tg = wide_on ? top_w : eye ? top_r : top_l;
    C2D_TargetClear(tg, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(tg);
    C2D_ViewReset();
    /* no 2x scale here for the 800-wide target: citro2d already gives an 800x240 top target
       its 400x240 projection (C2D_SceneSize treats GSP_SCREEN_HEIGHT_TOP_2X like 400), so the
       400-wide layout fills it by itself. Scaling again drew everything 4x wide (title, dashboard
       and the 3D view ran off the right edge with the 3D slider down; seen on hardware Nov 18). */
}

/* the game screen (the picture and the front end's overlay), 320x240, at the screen's origin */
static void draw_game_layer(int sopen)
{
    if (sopen) {
        C2D_DrawImageAt(limg, 0, 20, 0.5f, NULL, 0.5f, 0.5f);
    } else {
        float gh = front_game_height();
        if (gh > 0) C2D_DrawImageAt(gimg, 0, 0, 0.5f, NULL, 320.0f / pw, gh / ph);
        front_draw_bottom();
    }
}

static void draw(void)
{
    float iod = osGet3DSliderState();
    int sopen = settings_open();
    top_mode(iod);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    if (swapped) {
        /* top: the game picture filling the whole screen (the touchscreen can't reach it, so the
           tabs are left off); with the port menu or the settings open, that layer centred at 1:1
           so its buttons read as drawn. bottom: the dashboard laid out again at 320 px (full-size text) */
        int eye, fill = !sopen && !front_menu_open();
        for (eye = 0; eye < (iod > 0.0f ? 2 : 1); eye++) {
            top_begin(eye);
            if (fill) {
                C2D_DrawImageAt(gimg, 0, 0, 0.5f, NULL, 400.0f / pw, 240.0f / ph);
            } else {
                C2D_ViewTranslate(40.0f, 0.0f);
                draw_game_layer(sopen);
            }
            C2D_ViewReset();
        }
        C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(bot);
        ui_top_w = 320;                 /* laid out again for 320 px at full size, not scaled */
        front_draw_top(0, 0.0f);
        ui_top_w = 400;
        C2D_ViewReset();
        C3D_FrameEnd(0);
        return;
    }
    /* top: the dashboard (or, while the settings screen is open, the screen at a size to read) */
    top_begin(0);
    if (sopen) {
        C2D_DrawImageAt(limg, 0, (240 - SET_H * 0.6f) / 2, 0.5f, NULL, 0.625f, 0.6f);
    } else
        front_draw_top(0, iod);
    C2D_ViewReset();
    if (iod > 0.0f && !sopen && !wide_on) {
        top_begin(1);
        front_draw_top(1, iod);
        C2D_ViewReset();
    }
    /* bottom: the game */
    C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(bot);
    draw_game_layer(sopen);
    C3D_FrameEnd(0);
}

/* UW-05: HOME pressed or the lid closed. libctru calls these hooks from inside
   aptJumpToHomeMenu() / aptHandleSleep() on the main thread, after the HOME transition has begun;
   a save (or any wait or file write) in there froze HOME on real hardware in Star Fleet II (Nov 20
   2026). So the hooks only set flags: the game's clock stops (port_pause is an atomic flag; the
   game's thread, which would go on running behind the HOME Menu, waits at its next clock read)
   and the main loop is told when we are back. The keys are let go and the journal is written on
   the main thread just before aptMainLoop handles a HOME press (apt_before); on the way back the
   keys are let go again and the clock restarts (apt_after). Nothing is saved on HOME itself;
   HOME -> Close saves after the loop. */
static int suspended;
static volatile int resumed;

static void apt_hook(APT_HookType h, void *p)
{
    (void)p;
    if (h == APTHOOK_ONSUSPEND || h == APTHOOK_ONSLEEP) {
        if (!suspended && !game_done && !quit_req) { suspended = 1; port_pause(1); }
    } else if (h == APTHOOK_ONRESTORE || h == APTHOOK_ONWAKEUP) {
        if (suspended) resumed = 1;
    }
}

/* Main thread, right before aptMainLoop: when a HOME press is waiting, let go of every key (the
   game sees no key stuck down behind the HOME Menu) and write the journal's pending lines (a
   small fflush, the same one front_tick makes every two seconds). */
static void apt_before(void)
{
    extern void uw_journal_flush(void);
    if (!aptShouldJumpToHome() || game_done || quit_req) return;
    release_all();
    uw_journal_flush();
}

/* Main thread, after aptMainLoop came back true: back from the HOME Menu or sleep. */
static void apt_after(void)
{
    if (!resumed) return;
    resumed = 0;
    suspended = 0;
    release_all();
    if (!paused_by_front && !settings_open()) port_pause(0);
    fprintf(stderr, PLAT_NAME ": back from the HOME Menu or sleep\n");
}

int plat_run(const PlatConfig *cfg, const PlatHooks *h, int (*game)(void *), void *arg)
{
    aptHookCookie cookie;
    Thread gt;
    s32 prio;
    hooks = h;
    live = *cfg;
    t0 = svcGetSystemTick();
    fprintf(stderr, PLAT_NAME ": plat_run\n");
    gfxInitDefault();
    irrstInit();                        /* the New 3DS's C-stick, ZL and ZR */
    gfxSet3D(true);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
    C2D_Prepare();
    top_l = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    /* the top screen's 800-wide mode, for when the 3D slider is down (see top_begin) */
    top_w = C3D_RenderTargetCreate(240, 800, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (top_w) C3D_RenderTargetSetOutput(top_w, GFX_TOP, GFX_LEFT,
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    top_r = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    /* the game's picture: up to 512x512 (a 640x480 mode is shown at what fits) */
    C3D_TexInit(&gtex, 512, 512, GPU_RGB565);
    C3D_TexSetFilter(&gtex, GPU_LINEAR, GPU_LINEAR);
    gimg.tex = &gtex; gimg.subtex = &gsub;
    C3D_TexInit(&ltex, 1024, 512, GPU_RGB565);
    C3D_TexSetFilter(&ltex, GPU_LINEAR, GPU_LINEAR);
    layer = malloc(SET_W * SET_H * 4);
    limg.tex = &ltex; limg.subtex = &lsub;
    if (!layer) { fprintf(stderr, PLAT_NAME ": out of memory for the screens\n"); return 1; }
    fprintf(stderr, PLAT_NAME ": screens ready\n");
    aptHook(&cookie, apt_hook, NULL);
    front_init();
    fprintf(stderr, PLAT_NAME ": front end ready\n");
    fprintf(stderr, PLAT_NAME ": the game runs on core %d\n", game_core);
    game_fn = game; game_arg = arg;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    /* a large stack: host frames are bigger than DOS's */
    svcCreateEvent(&park_event, RESET_ONESHOT);
    gt = threadCreate(game_entry, NULL, 4 << 20, prio + 1, game_core, true);
    if (!gt && game_core) gt = threadCreate(game_entry, NULL, 4 << 20, prio + 1, game_core == 2 ? 1 : 0, true);
    game_thread_h = gt;
    if (!gt) { plat_message(1, UW3DS_TITLE, "The game's thread could not start (out of memory)."); return 1; }
    for (;;) {
        u32 down, held_k, up;
        circlePosition cp, cs;
        touchPosition t;
        int touching;
        apt_before();
        if (!aptMainLoop() || game_done || quit_req) break;   /* false: HOME -> Close or power off */
        apt_after();
        hidScanInput();
        irrstScanInput();
        down = front_remap(hidKeysDown()); held_k = front_remap(hidKeysHeld()); up = front_remap(hidKeysUp());
        hidCircleRead(&cp);
        hidCstickRead(&cs);
        touching = (held_k & KEY_TOUCH) != 0;
        hidTouchRead(&t);
        swap_load();
        if (down & KEY_ZL) { swapped = !swapped; swap_save(); }   /* ZL swaps the screens at any time */
        down &= ~KEY_ZL; held_k &= ~KEY_ZL; up &= ~KEY_ZL;
        if (swapped && !settings_open()) {
            /* the touchscreen is a touchpad for the game on the top screen (or, with the port
               menu open, nothing: its buttons are reached with the D-pad and A) */
            touchpad(touching, t);
            touching = 0;
        }
        taps_run();
        if (settings_open()) settings_input(down, touching, t);
        else game_input(down, held_k, up, cp, cs, touching, t);
        if (hooks->tick) hooks->tick();
        sync_open();
        {
            static uint64_t acc[4];
            uint64_t a = svcGetSystemTick(), b, c, d, e;
            hooks->scanout(pix, &pw, &ph, pal);
            b = svcGetSystemTick();
            front_tick();
            upload_game();
            if (settings_open() && settings_draw(layer)) upload_layer();
            c = svcGetSystemTick();
            draw();
            d = svcGetSystemTick();
            acc[0] += b - a; acc[1] += c - b; acc[2] += d - c;
            frames++;
            {
                /* frame-time line, only with /3ds/<game>/debug.txt (checked once): every 300
                   frames the frame-to-frame time (avg, longest: an SD read or write on this
                   thread shows as a long frame) and the dashboard map's draw calls (UW-08) */
                static int dbg = -1;
                static uint64_t last_f, sum_f, max_f;
                static int max_obj;
                extern int map_objects;
                if (dbg < 0) { struct stat sb; dbg = stat(UW3DS_ROOT "/debug.txt", &sb) == 0; }
                if (dbg) {
                    uint64_t now_f = svcGetSystemTick();
                    if (last_f) { uint64_t df = now_f - last_f; sum_f += df; if (df > max_f) max_f = df; }
                    last_f = now_f;
                    if (map_objects > max_obj) max_obj = map_objects;
                    if (frames % 300 == 0) {
                        fprintf(stderr, PLAT_NAME ": frame: avg %.1f ms, max %.1f ms, draw %.1f ms, map draws max %d\n",
                                sum_f / 300.0 / (SYSCLOCK_ARM11 / 1000.0), max_f / (SYSCLOCK_ARM11 / 1000.0),
                                (d - c) / (SYSCLOCK_ARM11 / 1000.0), max_obj);
                        sum_f = max_f = 0; max_obj = 0;
                    }
                }
            }
            e = d;
            if (frames % 600 == 0) {         /* every 10 s: where the time goes */
                extern volatile unsigned uw_passes;
                unsigned audio_queue_depth(void), audio_queue_dropped(void), audio_underruns(void);
                static unsigned last_passes;
                unsigned passes = uw_passes - last_passes;
                last_passes = uw_passes;
                fprintf(stderr, PLAT_NAME ": %u s: game %.1f passes/s; screens: scanout %llu us, upload %llu us, draw %llu us a frame; "
                        "sound queue %u, dropped %u, underruns %u\n",
                        frames / 60, passes / 10.0, (unsigned long long)(acc[0] / 600 / (SYSCLOCK_ARM11 / 1000000)),
                        (unsigned long long)(acc[1] / 600 / (SYSCLOCK_ARM11 / 1000000)), (unsigned long long)(acc[2] / 600 / (SYSCLOCK_ARM11 / 1000000)),
                        audio_queue_depth(), audio_queue_dropped(), audio_underruns());
                memset(acc, 0, sizeof acc);
            }
            (void)e;
        }
    }
    if (!game_done && !quit_req) {
        /* UW-05: HOME -> Close or power off (aptMainLoop returned false), on the main thread,
           outside the APT hooks: the same quicksave EXIT GAME makes (slot 4, through the scratch
           slot and swap), with the clock running and no key held. Only where EXIT GAME could save
           (in a game, no panel open, no item held): otherwise nothing is saved. At most 1.5 s for
           the game to pick the request up and 4 s in all; then the game's thread is parked below
           (itself at most a second) and the services shut down, whatever the game is doing. */
        int ok;
        u64 c0 = osGetTime();
        release_all();
        port_pause(0);
        ok = front_save_now(1500, 4000);
        port_pause(1);
        fprintf(stderr, PLAT_NAME ": close: quicksave %s (%llu ms)\n", ok ? "made" : "not made",
                (unsigned long long)(osGetTime() - c0));
    }
    if (!game_done && hooks->lifecycle) hooks->lifecycle(PLAT_QUIT_REQUEST);
    {
        /* park every other thread (see p3_park_point), waiting up to a second for the game's */
        int i;
        extern void uw_journal_flush(void);
        extern void uw_io_stop(void);
        uw_io_stop();                       /* UW-07: the I/O thread finishes its read or write and ends */
        uw_journal_flush();                 /* before parking: the game can't be holding its lock then */
        park_req = 1;
        __dsb();
        for (i = 0; i < 50 && !parked_game && !game_done; i++) svcSleepThread(20000000LL);
        svcSleepThread(50000000LL);         /* the timer and synthesizer wake often: they stop too */
    }
    fprintf(stderr, PLAT_NAME ": %u frames\n", frames);
    {
        fflush(NULL);
        audio_close();                     /* stops the sound thread, then our ndsp reference */
        if (wide_on) { gfxSet3D(true); wide_on = 0; }
        C2D_Fini();
        C3D_Fini();
    }
    return game_status;
}
