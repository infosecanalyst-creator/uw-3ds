# Changelog

## 2026-11-21

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
