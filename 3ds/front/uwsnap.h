/* uwsnap.h: the game's state as the dashboard sees it, copied on the game's thread at a safe
   point each pass of its main loop (3ds/game/uwstate.c) and read by the front end on the main
   thread. Plain types only: the front end never includes the game's headers. */
#ifndef UW3DS_UWSNAP_H
#define UW3DS_UWSNAP_H
#include <stdint.h>

enum { SNAP_NONE, SNAP_MENU, SNAP_VIEW, SNAP_MAP, SNAP_CONV };

#define SNAP_SPELLS 64
#define SNAP_RUNES 24

struct uwsnap {
    unsigned seq;                       /* bumped by each copy */
    int mode;                           /* SNAP_* */
    int in_game, scrnum, gim;           /* GameInputMode: something held or a target wanted */
    int level;                          /* the dungeon level, 1..9 */
    float px, py;                       /* the player's position, in tiles (y grows north) */
    int heading;                        /* 0..255, a full turn; 0 north, growing clockwise */
    int hp, hpmax, mana, manamax;
    int xlevel, skill_points;
    uint32_t exp;                       /* in tenths */
    int str, dex, intel;
    int skills[20];
    int poison, hunger, fatigue, drunk;
    int light;                          /* the brightest light, 0..15 */
    int nspells;
    int spell_id[3];                    /* the spells working on the player: string ids (block 6) */
    int spell_steps[3];
    int timestop, hasted, wizeye, blessed;
    uint32_t game_clock;
    uint32_t runes;                     /* a bit per rune owned, bit r for rune r (An = 0) */
    int shelf[3], nrunes;
    uint8_t automap[64][64];            /* PlayersMap of this level, [y][x] */
    char name[32];
    int pclass, female;
};

/* The spells a player can cast from runes, in the game's table order: the runes (rune numbers,
   0x18 for none) and the name from the game's strings. Filled once the game has started. */
struct uwspell {
    uint8_t rune[3];
    char name[32];
};

const struct uwsnap *uw_snap(void);     /* the last copy (the main thread's own) */
const struct uwspell *uw_spells(int *n);
const char *uw_skill_name(int i);
const char *uw_rune_name(int r);

/* The message log and the journal (uwstate.c keeps them; the main thread reads under the lock). */
void uw_log_lock(void);
void uw_log_unlock(void);
int uw_log_count(void);
const char *uw_log_line(int i);         /* 0 the oldest kept */
int uw_journal_people(void);
const char *uw_journal_name(int person);
int uw_journal_count(int person);
const char *uw_journal_line(int person, int i, int *who);   /* who: 0 they said, 1 you said, 2 narration */
int uw_journal_latest(void);            /* the person spoken to last, or -1 */

/* Requests run on the game's thread at its next safe point: 1 if taken. */
int uw_request_save(int slot, const char *desc);
int uw_request_restore(int slot);
const char *uw_request_result(void);    /* the last request's outcome, for a message; NULL none */
int uw_can_save(void);

/* The notes written on the game's map for a level (from SAVE0's LEV.ARK, written when the map
   screen closes or the level changes). Coordinates in tiles. */
struct uwnote { float x, y; char text[52]; };
int uw_notes(int level, struct uwnote *out, int max);
/* The explored map of another level from SAVE0's LEV.ARK (the current one is in the snapshot). */
int uw_level_automap(int level, uint8_t out[64][64]);

#endif
