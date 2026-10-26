# CHarth

An original block-shooting puzzle game for [CHGame](https://github.com/bateske/CHGame), set in a 1979 cargo-bay terminal: amber,
phosphor green and cyan on a scanlined CRT. Formations of blocks with gaps in them creep down the screen; fire blocks up a column
to stick under them, and fill the gaps so a formation becomes a solid rectangle.

## Controls

| Control | Action |
|---|---|
| Left/Right | Move the ship |
| A or B | Fire a block |
| Down | Hurry the next row |
| START | Pause menu |
| START (hold 3 s) | Back to the CHGame menu |

## Rules

- Slide along the bottom and fire a block up a column: it sticks under the lowest block there.
- Complete a whole shape, so that its bounding rectangle (2x2 or bigger) is full, and it vanishes. The bigger the rectangle,
  the more it scores.
- A formation that reaches the bottom row ends the game.

The game has a pause menu and keeps a saved game and the best score.

## Building

You need the CHGame board package and a CHGame board. From this folder:

    ./cl build      # build
    ./cl upload     # flash it to the board

`dist/CHarth.CHG` is the packed game for the CHGame SD menu.

## Developer notes

- `CHarth/CHarth.ino` is the whole game. The sprites and the logo come from `tools/sprites.py` (grids in `art/`), and the
  music, a 16-bar psytrance-style loop for the one-voice piezo, from `tools/music.py`; both write the headers in `CHarth/`,
  which are checked in, so building needs neither tool.

## Credits and licence

Game, art, music and code: Jadflat, MIT (see `LICENSE`). The sprites and the logo were rendered with the author's own
CHGame-tuned "pixel-art-gen" Claude skill from grids drawn for this game.

Built on CHGame, Copyright 2026 Kevin Bates (bateske), Apache-2.0: the framework is vendored in `chgame/`
(see `chgame/NOTICE` and `chgame/LICENSE`).
