# Changelog

## 2026-11-21

- QUICKLOAD asks first ("Load the last save in slot 4?", NO in focus) and is greyed out until slot 4 holds a save. It no longer sits beside QUICKSAVE.
- Slot 4 keeps the save it last replaced: QUICKLOAD > LOAD THE SAVE BEFORE IT brings it back (an EXIT GAME or HOME save no longer wipes out your only quicksave).
- Port menu in the house shape: one column (RESUME, QUICKSAVE, MANUAL, OPTIONS, QUICKLOAD, EXIT GAME). Spell hotkeys, map, A/B, left hand and the game's settings moved to OPTIONS.
- Underworld II: the manual, the title card and the empty Status and Spells pages now name the right game (they said "Stygian Abyss"); its files chapter asks for UW2.EXE; 1993 credit.
- Manual: B or START (or CLOSE) goes straight back to the game, as in the other ports. Section buttons are 26 px tall, 4 px apart. New paragraph on tapping small things. Wording uses the buttons' real names.
- Tabs under the game are 26 px tall and 4 px clear of the bottom edge.
- The D-pad no longer prints "No spell on that direction" on the logos and menus.
- Left hand / A-B swap: the "Port menu open" hint names the button that really goes back.
- First time in the dungeon: a one-time tip on the top strip.
- EXIT GAME: buttons do nothing while the exit save is running (START there could leave the menu half-done).
- Leaving the game: the music synthesizer now stops between blocks, so it can no longer still be working when the program ends (a possible crash right after the exit save).
- Manual: the text no longer scrolls past its last line to an empty page.
- The game stays smooth while the map, notes and journal are saved or loaded: that file work now happens off the screen thread, and the map is stored in runs (smaller, faster).
- Dragging a page (Log, Journal, Spells, manual) glides, but the glide stops when you change page or open a menu, and is capped.
- Log and journal page heights use the real wrapped line count, so the last lines are never cut off.
- A new character starts a new journal; the old one is kept as journal-1.txt, journal-2.txt and so on.
- 3ds.cfg (your settings) is written only when something changed, through a temporary file.
- Quieter touch click.

## 2026-11-20

- Quit tells you the real reason when it cannot save (a panel is open or you hold an item, or the SD card is full) and asks "Exit anyway?".
- HOME > Close quicksaves first (for at most a few seconds); pressing HOME only pauses the game. Fixed a crash on exit.
- The manual shows the build number.
- Install paths: /3ds/uw1-3ds.3dsx and /3ds/uw2-3ds.3dsx.
- Licence notices added for the Nuked OPL3 sound code.

## 2026-11-19

- Quicksave is safer: written to a scratch slot and swapped in, the game waits for it, and it never exits silently after a failure.
- The Log, Journal and Spells pages scroll by dragging, with momentum.
- Manual: drag the stylus to scroll the text, with a little momentum.
