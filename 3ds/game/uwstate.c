/* uwstate.c: the game's side of the 3DS dashboard. Runs on the game's thread only: at
   port_front_point, once each pass of the game's main loop (MAINLOOP.C, between passes, where
   the port's own test cheats run), it copies what the dashboard shows into a plain snapshot
   (front/uwsnap.h) and carries out the front end's requests (quicksave, quickload); the
   scroll and conversation hooks (SCROLLIO.C's scroll_print, CONVERSE.C's play_say) hand the
   text they print to the message log and the journal (front/log.c).

   Like every port file that reads the game's records, it includes compat.h first (the game's
   pack(1) layout and Turbo C's view); it calls nothing that touches the game's files. */
#include "compat.h"
#include "combat.h"
#include "conv.h"
#include "file.h"
#include "inv.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"
#include "uwsnap.h"
#include "uwglue.h"

#ifdef GAME_UW2
#define NUM_RUNE_SPELLS 0x40           /* RUNES.C's try_cast searches the first 64 */
#else
extern struct Spell far spells[NUM_SPELLS];   /* SPELLS.C */
#endif

/* UW2's headers name fewer of these; the values are the same in both games */
#ifndef GIM_NONE
#define GIM_NONE 0
#endif
#ifndef ATTR_STR
#define ATTR_STR 0
#define ATTR_DEX 1
#define ATTR_INT 2
#endif
#ifndef IMODE_DEFAULT
#define IMODE_DEFAULT 0
#endif
#ifndef MODE_CONV
#define MODE_CONV 4
#endif

const char *port_quicksave_desc;

static int snap_mode(void)
{
    if (!in_game) return SNAP_MENU;
    if (scrnum == 0) return SNAP_VIEW;
    if (scrnum == 1) return SNAP_MAP;
    if (scrnum == 2) return SNAP_CONV;
    return SNAP_NONE;
}

static int safe_now(void)
{
    return in_game && scrnum == 0 && GameInputMode == GIM_NONE && CursorObjPtr == 0 && NewPlayerX == 0
        && ThePlayer != 0 && ThePlayer->hp > 0 && PlayerLevel != 9;
}

static void fill(struct uwsnap *s)
{
    int i;
    unsigned char ic[3];
    s->mode = snap_mode();
    s->in_game = in_game;
    s->scrnum = scrnum;
    s->gim = GameInputMode;
    if (!in_game || !ThePlayer || !player) return;
    s->level = PlayerLevel;
    s->px = PN.x / 256.0f;
    s->py = PN.y / 256.0f;
    s->heading = ((OBJ_HEADING(ThePlayer) << 5) + OBJ_FINEHEAD(ThePlayer)) & 0xFF;
    s->hp = ThePlayer->hp;
    s->hpmax = playerdat ? playerdat->avghit : 0;
    s->mana = player->play_mana;
    s->manamax = player->max_mana;
    s->xlevel = player->level;
    s->skill_points = player->skill_points;
    s->exp = player->exp;
    if (playerdat) {
        s->str = playerdat->attr[ATTR_STR];
        s->dex = playerdat->attr[ATTR_DEX];
        s->intel = playerdat->attr[ATTR_INT];
    }
    for (i = 0; i < 20; i++) s->skills[i] = player->skills[i];
    s->poison = player->poison;
    s->hunger = player->hunger;
    s->fatigue = player->fatigue;
    s->drunk = player->drunk;
    s->light = player->light >> 4;
    s->nspells = player->active_spells > 3 ? 3 : player->active_spells;
    parse_aspells(ic);
    for (i = 0; i < 3; i++) {
        s->spell_steps[i] = i < s->nspells ? player->spells[i] >> 8 : 0;
        s->spell_id[i] = i < s->nspells ? ic[i] + 0x180 : 0;
    }
    s->timestop = TimeStop; s->hasted = Hasted; s->wizeye = WizEye; s->blessed = Blessed;
    s->game_clock = player->game_clock;
    s->runes = 0;
    for (i = 0; i < NUM_RUNES; i++)
        if ((player->runebag[i >> 3] >> (7 - (i & 7))) & 1) s->runes |= 1u << i;
    for (i = 0; i < 3; i++) s->shelf[i] = player->shelf[i];
    s->nrunes = player->nrunes;
    memcpy(s->automap, PlayersMap, sizeof s->automap);
    memcpy(s->name, player->name, sizeof s->name - 2);
    s->name[sizeof s->name - 2] = 0;
    s->pclass = player->pclass;
    s->female = player->female;
}

static int spells_done;

static void fill_spells(void)
{
    struct uwspell tab[NUM_RUNE_SPELLS];
    int i;
    for (i = 0; i < NUM_RUNE_SPELLS; i++) {
        unsigned r = (unsigned)spells[i].runes;
        tab[i].rune[0] = (r >> 10) & 0x1F;
        tab[i].rune[1] = (r >> 5) & 0x1F;
        tab[i].rune[2] = r & 0x1F;
        tab[i].name[0] = 0;
    }
    uwglue_set_spells(tab, NUM_RUNE_SPELLS);
    spells_done = 1;
}

volatile int uw_busy;                  /* set while the game saves or loads: the dashboard shows "Working..." */

static void run_request(void)
{
    int slot;
    int r = uwglue_take_request(&slot);
    if (!r) return;
    if (!safe_now()) {
        uwglue_result(r == 1 ? "You cannot save just now." : "You cannot restore just now.");
        return;
    }
    uw_busy = 1;
    if (r == 1) {
        port_quicksave_desc = "Quicksave";
        DoSaveRest(0, slot);
        port_quicksave_desc = 0;
        uwglue_result("Quicksaved.");
    } else {
        DoSaveRest(1, slot);
        RightButtonThing = IMODE_DEFAULT;
        punt_fightmode();
        uwglue_result("Quickloaded.");
    }
    uw_busy = 0;
}

volatile unsigned uw_passes;          /* the game's main loop passes, for the log */

void port_front_point(void)
{
    uw_passes++;
    struct uwsnap *s;
    if (in_game && !spells_done) fill_spells();
    s = uwglue_snap_begin();
    if (s) {
        fill(s);
        uwglue_snap_end();
    }
    uwglue_set_safe(safe_now());
    run_request();
}

static int talk_info(int *whoami, int *item)
{
    if (scrmode != MODE_CONV || !talking_to) { *whoami = -1; *item = -1; return -1; }
    *whoami = talking_to->whoami;
    *item = OBJ_ITEM(talking_to);
    return cnv_id;
}

void port_scroll_hook(const char *s, int mode)
{
    int who, item, conv = talk_info(&who, &item);
    uwglue_scroll(s, mode, conv, who, item);
}

void port_said_hook(const char *s)
{
    int who, item, conv = talk_info(&who, &item);
    uwglue_said(s, conv, who, item);
}

/* The game waits for typed text (wdialog, the character's name): the front end shows the
   system keyboard and types what was entered (front/glue.c). */
void port_text_request(const char *prompt, int maxlen, int digits)
{
    uwglue_text_request(prompt, maxlen, digits);
}
