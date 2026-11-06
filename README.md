# Ultima Underworld I & II for the New Nintendo 3DS

A dual-screen 3DS port of *Ultima Underworld: The Stygian Abyss* and *Ultima Underworld II: Labyrinth of Worlds*, built on [Underworld Exhumed](https://github.com/abedegno/underworld-exhumed) — byte-matching decompilations of both games with native ports — through a 3DS backend for its platform layer.

**You need your own copy of the games (the GOG releases).** This repository holds no game data.

## What it is
- **Top screen:** the game's 3D view (and conversations) at about twice the size, with stereoscopic depth from the 3D slider.
- **Bottom screen:** the whole game screen, played with the stylus as the mouse (hold R for the right button), above tabs for dashboard pages: live Map (from the game's own automap, with your map notes), Status, message Log, conversation Journal and Spells (your spell book and hotkeys).
- **Controls:** Circle Pad walks and turns, C-Stick slides and looks, L attacks, A uses (confirm), X looks, Y the map, ZR jumps, B goes back, D-pad casts spell hotkeys, SELECT changes the page, ZL swaps the screens, START opens the port menu (quicksave/quickload, hotkeys, map, manual, settings, an A/B swap, exit).
- The system keyboard for typed text (character name, mantras, conversation answers).
- A built-in manual.
- Underworld Exhumed's enhancements and bug fixes, on by default and switchable in Settings.

## Layout
- `3ds/platform` — the 3DS backend of Exhume's platform API (`plat.h`): screens, threads, sound (NDSP), input.
- `3ds/front` — the front end: dashboard, port menu, manual, map, journal, hotkeys.
- `3ds/game` — the game-side reader (copies game state each main-loop pass; quicksave requests).
- `3ds/romfs` — the manual's text. `3ds/meta` — icons.
- `patches/` — the small, `__3DS__`-guarded hooks added to the upstream game sources and runtime.
- `tools/build3ds.py` — builds `uw1` or `uw2` with devkitARM into a `.3dsx`.

## Building
Needs devkitARM with libctru and citro2d (e.g. the `devkitpro/devkitarm` Docker image).

```
sh tools/setup.sh            # fetches upstream at the pinned commits and applies patches/
python3 tools/build3ds.py uw1
python3 tools/build3ds.py uw2
```

## Installing
Copy `uw1-3ds.3dsx` to `/3ds/uw1/` and your UW1 folder (UW.EXE, DATA, CRIT, CUTS, SOUND) to `/3ds/uw1/game/`; likewise `/3ds/uw2/`. Saves and settings go to `/3ds/uwN/home/`, the log to `/3ds/uwN/log.txt`. Sound needs `/3ds/dspfirm.cdc`.

## Licences
The port's own code (`3ds/`, `tools/`, `patches/`) is MIT. Underworld Exhumed's own work is MIT; its decompiled code derives from the games, whose copyright is Electronic Arts' (see its NOTICE). Nuked OPL3 is LGPL-2.1 and is fetched, not included. Ultima Underworld is a trademark of Electronic Arts. Not affiliated with or endorsed by the rights holders.
