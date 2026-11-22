/* uwglue.h: what the game's side (3ds/game/uwstate.c, on the game's thread) hands the front
   end's side (front/glue.c), which keeps it under a lock for the main thread. */
#ifndef UW3DS_UWGLUE_H
#define UW3DS_UWGLUE_H
#include "uwsnap.h"

struct uwsnap *uwglue_snap_begin(void);     /* NULL: the main thread holds it; skip this pass */
void uwglue_snap_end(void);
void uwglue_set_spells(const struct uwspell *tab, int n);
void uwglue_set_safe(int safe);
int uwglue_take_request(int *slot);         /* 0 none, 1 save, 2 restore, 3 restore the save before */
void uwglue_result(const char *msg);
/* UW-01: the quicksave is written into a scratch slot, then swapped in: SAVE<to> becomes
   SAVE<to>.BAK (the older .BAK is removed) and SAVE<from> becomes SAVE<to>. 0 on success. */
int uwglue_swap_slot(int from, int to);
int uwglue_swap_bak(int slot);              /* SAVEn <-> SAVEn.BAK, for "load the save before it" */
extern volatile int uw_last_save_refused;   /* the game refused the last save request (a panel open, an item held) */
extern volatile int uw_last_save_ok;   /* the last quicksave reached the card (game thread sets it) */
/* The text scroll_print printed (MODE: 0 the message scroll, 1 the NPC's, 2 the player's),
   and in a conversation its id, the NPC's whoami and item (else -1). */
void uwglue_scroll(const char *s, int mode, int conv, int whoami, int item);
void uwglue_said(const char *s, int conv, int whoami, int item);
void uwglue_text_request(const char *prompt, int maxlen, int digits);

#endif
