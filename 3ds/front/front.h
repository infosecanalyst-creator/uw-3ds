/* front.h: the front end's modules, for each other. */
#ifndef UW3DS_FRONT_H
#define UW3DS_FRONT_H
#include <3ds.h>

/* manual.c */
void manual_load(void);
int manual_chapters(void);
const char *manual_title(int chapter);
void manual_open(int chapter);
void manual_input(u32 down, u32 held, circlePosition cp, circlePosition cs);
int manual_current(void);
void manual_draw_top(void);

/* glue.c, the main thread's side */
void uw_glue_tick(void);
void uw_journal_open(void);
void uw_journal_flush(void);
int uw_text_wanted(char *prompt, int psz, int *maxlen, int *digits);

/* map.c */
void map_draw(float x, float y, float w, float h);
void map_zoom(int d);
void map_level(int d);                  /* look at another level (0 back to your own) */
void map_follow(void);
int map_viewing(void);                  /* the level shown, 0 for your own */
int map_zoom_level(void);

#endif
