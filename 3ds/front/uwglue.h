/* uwglue.h: what the game's side (3ds/game/uwstate.c, on the game's thread) hands the front
   end's side (front/glue.c), which keeps it under a lock for the main thread. */
#ifndef UW3DS_UWGLUE_H
#define UW3DS_UWGLUE_H
#include "uwsnap.h"

struct uwsnap *uwglue_snap_begin(void);     /* NULL: the main thread holds it; skip this pass */
void uwglue_snap_end(void);
void uwglue_set_spells(const struct uwspell *tab, int n);
void uwglue_set_safe(int safe);
int uwglue_take_request(int *slot);         /* 0 none, 1 save, 2 restore */
void uwglue_result(const char *msg);
/* The text scroll_print printed (MODE: 0 the message scroll, 1 the NPC's, 2 the player's),
   and in a conversation its id, the NPC's whoami and item (else -1). */
void uwglue_scroll(const char *s, int mode, int conv, int whoami, int item);
void uwglue_said(const char *s, int conv, int whoami, int item);
void uwglue_text_request(const char *prompt, int maxlen, int digits);

#endif
