/* strings3ds.h: the game's strings for the front end (strings.c). Blocks as the game's ui.h
   numbers them: 1 messages, 2 character creation (classes from 0x17, skills from 0x1F), 4 item
   names, 6 spells and enchantments, 7 conversation words and NPC names (from 0x10). */
#ifndef UW3DS_STRINGS_H
#define UW3DS_STRINGS_H
int uw_strings_load(void);
int uw_string_raw(int block, int idx, char *out, int outsz);
const char *uw_name(int block, int idx, char *out, int outsz);
#endif
