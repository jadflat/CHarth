// CHarth: an original block-shooting puzzle game for CHGame.
//
// Formations of blocks, with gaps in them, creep down the screen. Slide your ship along the bottom
// and fire a block up a column: it sticks under the lowest block there. Complete a whole shape, so
// that its bounding rectangle is full, and it vanishes; the bigger the shape, the more it scores.
// A formation that reaches the bottom row ends the game.
//
// Theme: a 1979 cargo-bay terminal. Amber, phosphor green, cyan and warning-red on a scanlined CRT
// with a beige bezel. All art is drawn with tools/sprites.py and the pixel art in art/.
#include <CHGame.h>
#include "Sprites.h"
#include "Music.h"

#define CHARTH_VERSION "1.0"

enum { COLS = 12, ROWS = 18, CELL = 6, OX = 2, OY = 2, SHIPY = OY + ROWS * CELL, PX = 78 };   // PX: the side panel
enum Col : uint8_t { C_BG, C_C1, C_C2, C_C3, C_C4, C_L1, C_L2, C_L3, C_L4, C_D1, C_D2, C_D3, C_D4, C_TEXT, C_SCAN, C_FRAME };
enum State : uint8_t { TITLE, PLAY, PAUSED, OVER };

enum Sfx : uint8_t { S_FIRE, S_LAND, S_CLEAR, S_BIG, S_OVER, S_LEVEL, S_COUNT };
AUDIO_STEPS(ST_FIRE)  = { AUDIO_STEP(1200, 2000, 40) };
AUDIO_STEPS(ST_LAND)  = { AUDIO_STEP(400, 300, 40) };
AUDIO_STEPS(ST_CLEAR) = { AUDIO_STEP(700, 0, 60), AUDIO_STEP(1047, 0, 60), AUDIO_STEP(1568, 0, 100) };
AUDIO_STEPS(ST_BIG)   = { AUDIO_STEP(700, 0, 60), AUDIO_STEP(1047, 0, 60), AUDIO_STEP(1568, 0, 60), AUDIO_STEP(2093, 0, 60), AUDIO_STEP(2637, 0, 180) };
AUDIO_STEPS(ST_OVER)  = { AUDIO_STEP(600, 0, 200), AUDIO_STEP(500, 0, 200), AUDIO_STEP(400, 0, 200), AUDIO_STEP(250, 0, 450) };
AUDIO_STEPS(ST_LEVEL) = { AUDIO_STEP(1047, 0, 80), AUDIO_STEP(1319, 0, 80), AUDIO_STEP(1568, 0, 80), AUDIO_STEP(2093, 0, 200) };
static const audio::Melody PSY = { PSY_TUNE, sizeof PSY_TUNE / 2, 50, 14 };
static const audio::Effect SOUNDS[] = {
    AUDIO_EFFECT(ST_FIRE, 2), AUDIO_EFFECT(ST_LAND, 3), AUDIO_EFFECT(ST_CLEAR, 5), AUDIO_EFFECT(ST_BIG, 6),
    AUDIO_EFFECT(ST_OVER, 8), AUDIO_EFFECT(ST_LEVEL, 7) };

// The title's head logo, drawn with the pixel-art tool (art/head.txt): the left half, mirrored.
static const char *const HEAD[24] = {
    ".........###",
    ".......#####",
    "......######",
    ".....#######",
    ".....#+#####",
    ".....#+#####",
    ".....+######",
    ".....#######",
    ".....#######",
    ".....#######",
    ".....#######",
    ".....#######",
    ".....#######",
    "......######",
    "......##..##",
    "......#.#.#.",
    "......#.#.#.",
    ".......#.#.#",
    "........#.#.",
    ".........###",
    "..........##",
    "..........##",
    "...........#",
    "...........#" };

static uint8_t g[ROWS][COLS];                  // 0 empty, 1 block, 2..8 a block clearing (the explosion's countdown)
static State state;
static int score, hiScore, level, cleared, frameNo, sc;
static int dropT, dropEvery, overT, yoff;      // dropEvery: frames per pixel of creep
static bool prevFire, shotOn;
static int titleT;                             // the title's typing clock
static int shotX, shotY, shotRow;              // shotY in pixels
static uint32_t rng = 54321;
static int muzzT, shakeT, bannerT, waveT, wx0, wy0, wx1, wy1;   // muzzle flash, screen shake, level banner, shockwave (pixels)
static int8_t stars[14][2];                    // the star field behind the blocks

static struct Part { int16_t x, y; int8_t vx, vy; uint8_t life, col; } parts[24];   // 1/8 pixel

static uint32_t rnd32() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int rnd(int n) { return (int)(rnd32() % (uint32_t)n); }

static void burst(int cx, int cy, uint8_t col) {
    for (int i = 0, n = 2; i < 24 && n > 0; i++)
        if (!parts[i].life) {
            parts[i] = { (int16_t)(cx * 8), (int16_t)(cy * 8), (int8_t)(rnd(33) - 16), (int8_t)(rnd(33) - 20),
                         (uint8_t)(12 + rnd(10)), col };
            n--;
        }
}

// ---- saving: the best score always, and a game in progress from each level's start or SAVE GAME
struct SaveData { int32_t hi, score; uint8_t level, cleared, dropEvery, pad; };
static const uint32_t MAGIC = save::magic("CHAR");
static SaveData sv;
static bool saveDue, saveFlag, hasSave;
static uint8_t pauseSel, titleSel, prevBtn;
static const char *pauseNote;

static void saveProgress(bool inGame) {
    sv = { hiScore, score, (uint8_t)level, (uint8_t)cleared, (uint8_t)dropEvery, 0 };
    saveFlag = inGame;
    saveDue = true;                                          // written after gfx_wait() in loop()
}

// ---- formations: a set of shapes, each a row of column heights (a digit per column, hanging from the top).
// A shot sticks under the lowest block of its column, so every gap is open from below and every shape can be
// completed. Shapes are mirrored at random and grow with the level (taller, and a thicker top bar).
static const char *const SHAPES[] = {
    "4114", "5115", "3131313", "5151515", "123456", "123432", "1234321", "5432345", "2531352", "13513",
    "62426", "26462", "611116", "622226", "123321", "7171717", "5252525", "44144", "3 4 2 4 3", "2 3 1 3 2",
    "6 1 4 1 6", "1 5 5 1", "35 3 53", "2 2 6 2 2", "4 2 2 4", "1 6 3 3 6 1", "5 3 1 3 5", "2 4 6 4 2 4", "8 1 1 1 8", "3 6 3 6 3" };
// The first levels use only wide, flat shapes with a few small gaps (2-5 shots to complete one).
static const char *const EASY[] = { "22322", "223322", "2233322", "32223", "322223", "3223", "232323", "2223222", "223 3 22", "3232" };
static int fLen[COLS], formW, formX, formRow, gapLeft;
static bool formOn;

static void newFormation() {
    const int nEasy = sizeof EASY / sizeof EASY[0], nAll = sizeof SHAPES / sizeof SHAPES[0];
    const char *t;
    if (level <= 2) t = EASY[rnd(nEasy)];                                     // levels 1-2: easy only
    else if (level <= 4) { int k = rnd(nEasy + 14); t = k < nEasy ? EASY[k] : SHAPES[k - nEasy]; }   // then the simpler shapes
    else t = SHAPES[rnd(nAll)];                                               // from level 5 all of them
    int n = 0, bonus = level >= 4 ? (level - 2) / 3 : 0, thick = level >= 5 ? rnd(2) : 0;
    bool flip = rnd(2);
    for (const char *c = t; *c && n < COLS; c++)
        if (*c >= '1' && *c <= '9') fLen[n++] = *c - '0';
    formW = n;
    int formH = 1;
    for (int x = 0; x < formW; x++) {
        int l = fLen[x] + thick + (fLen[x] > 2 ? bonus : 0);
        fLen[x] = constrain(l, 1, 11);
        formH = max(formH, fLen[x]);
    }
    if (flip) for (int x = 0; x < formW / 2; x++) { int tmp = fLen[x]; fLen[x] = fLen[formW - 1 - x]; fLen[formW - 1 - x] = tmp; }
    formX = constrain((COLS - formW) / 2 + rnd(3) - 1, 0, COLS - formW);
    formRow = formH - 1; formOn = true;                   // bottom row first
}

static void feedRow(uint8_t *out) {               // the next row to enter at the top
    memset(out, 0, COLS);
    if (gapLeft > 0) { gapLeft--; return; }
    if (!formOn) newFormation();
    for (int x = 0; x < formW; x++) if (fLen[x] > formRow) out[constrain(formX + x, 0, COLS - 1)] = 1;
    if (--formRow < 0) { formOn = false; gapLeft = level >= 5 ? 2 : 3; }
}

static void thrust() {                           // a spark from the ship's engine
    for (int i = 0; i < 24; i++)
        if (!parts[i].life) {
            parts[i] = { (int16_t)((OX + sc * CELL + 2 + rnd(3)) * 8), (int16_t)((SHIPY + CELL) * 8), (int8_t)(rnd(9) - 4),
                         (int8_t)(10 + rnd(8)), (uint8_t)(5 + rnd(4)), (uint8_t)(rnd(2) ? C_C4 : C_C1) };
            return;
        }
}

static void gameOver() {
    shotOn = false;
    state = OVER; overT = 40;
    audio::sfx(S_OVER); audio::stopMusic();
    saveProgress(false);                                  // keeps the best score, drops the saved game
}

static void drop() {                            // the whole formation creeps down a row
    for (int y = ROWS - 1; y > 0; y--) memcpy(g[y], g[y - 1], COLS);
    feedRow(g[0]);
    for (int x = 0; x < COLS; x++)
        if (g[ROWS - 1][x]) { gameOver(); return; }
}

static void startField() {                      // a fresh field at the current level
    memset(g, 0, sizeof g);
    sc = COLS / 2; shotOn = false; formOn = false; gapLeft = 2; yoff = 0;
    memset(parts, 0, sizeof parts);
    dropT = dropEvery;
    for (int i = 0; i < 6; i++) { for (int y = ROWS - 1; y > 0; y--) memcpy(g[y], g[y - 1], COLS); feedRow(g[0]); }
    state = PLAY;
    bannerT = 60;
}

static void newGame() {
    level = 1; score = 0; cleared = 0; dropEvery = 12;
    startField();
    audio::sfx(S_LEVEL);
}

static void continueGame() {
    score = sv.score; level = sv.level; cleared = sv.cleared; dropEvery = sv.dropEvery;
    startField();
    audio::sfx(S_LEVEL);
}

static void toTitle() { audio::melody(PSY); state = TITLE; titleT = 0; titleSel = 0; memset(g, 0, sizeof g); memset(parts, 0, sizeof parts); }

// The shape the new block belongs to (4-connected solid blocks) clears only when it has been
// completed: its bounding box must be completely full, 2x2 or bigger. Its cells then flash as an
// explosion (value 8 counting down to 2) before they go; a shot landing meanwhile still sticks, as
// a new stray block. Nothing above falls.
static void clearShape(int cx, int cy) {
    static uint8_t q[COLS * ROWS], seen[ROWS][COLS];
    memset(seen, 0, sizeof seen);
    int h = 0, t = 0, x0 = cx, x1 = cx, y0 = cy, y1 = cy;
    q[t++] = cy * COLS + cx; seen[cy][cx] = 1;
    while (h < t) {
        int c = q[h++], x = c % COLS, y = c / COLS;
        x0 = min(x0, x); x1 = max(x1, x); y0 = min(y0, y); y1 = max(y1, y);
        static const int8_t D[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
        for (int k = 0; k < 4; k++) {
            int nx = x + D[k][0], ny = y + D[k][1];
            if (nx >= 0 && ny >= 0 && nx < COLS && ny < ROWS && g[ny][nx] == 1 && !seen[ny][nx]) {
                seen[ny][nx] = 1; q[t++] = ny * COLS + nx;
            }
        }
    }
    int w = x1 - x0 + 1, hh = y1 - y0 + 1;
    if (w < 2 || hh < 2 || t != w * hh) return;          // a gap is left: the shape is not complete
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) g[y][x] = 8;
    waveT = 9; wx0 = OX + x0 * CELL; wx1 = OX + (x1 + 1) * CELL; wy0 = OY + y0 * CELL + yoff; wy1 = OY + (y1 + 1) * CELL + yoff;
    if (t >= 9) shakeT = 8;
    score += t * t * 5 * level;
    cleared++;
    audio::sfx(t >= 9 ? S_BIG : S_CLEAR);
    if (cleared % 4 == 0) {
        level++;
        dropEvery = max(3, dropEvery - 1);
        audio::sfx(S_LEVEL);
        bannerT = 60;
        saveProgress(true);                               // a checkpoint at each new level
    }
}

static void land() {
    shotOn = false;
    g[shotRow][shotX] = 1;
    audio::sfx(S_LAND);
    clearShape(shotX, shotRow);
    for (int x = 0; x < COLS; x++)
        if (g[ROWS - 1][x]) { gameOver(); return; }
}

#ifdef CHARTH_DEMO
// An autoplayer for recording the README's GIF: take the lowest shape, find its shortest columns and
// fill them down to its tallest, one shot at a time.
static void demoPlay(int &dx, bool &fire) {
    int ly = -1, lx = 0;
    for (int y = ROWS - 1; y >= 0 && ly < 0; y--) for (int x = 0; x < COLS; x++) if (g[y][x] == 1) { ly = y; lx = x; break; }
    if (ly < 0) return;
    static uint8_t seen[ROWS][COLS], q[COLS * ROWS];
    memset(seen, 0, sizeof seen);
    int h = 0, t = 0, x0 = lx, x1 = lx, y1 = ly;
    int bot[COLS]; for (int x = 0; x < COLS; x++) bot[x] = -1;
    q[t++] = ly * COLS + lx; seen[ly][lx] = 1;
    while (h < t) {
        int c = q[h++], x = c % COLS, y = c / COLS;
        x0 = min(x0, x); x1 = max(x1, x); y1 = max(y1, y); bot[x] = max(bot[x], y);
        static const int8_t D[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
        for (int k = 0; k < 4; k++) {
            int nx = x + D[k][0], ny = y + D[k][1];
            if (nx >= 0 && ny >= 0 && nx < COLS && ny < ROWS && g[ny][nx] == 1 && !seen[ny][nx]) { seen[ny][nx] = 1; q[t++] = ny * COLS + nx; }
        }
    }
    int best = -1;
    for (int x = x0; x <= x1; x++)
        if (bot[x] >= 0 && bot[x] < y1 && (best < 0 || abs(x - sc) < abs(best - sc))) best = x;
    if (best < 0) return;
    dx = best > sc ? 1 : best < sc ? -1 : 0;
    fire = (best == sc);
}
#endif

static void update() {
    frameNo++;
    if (muzzT) muzzT--;
    if (shakeT) shakeT--;
    if (bannerT && state == PLAY) bannerT--;
    if (waveT) waveT--;
    uint8_t cur = (chgame.pressed(UP_BUTTON) ? 1 : 0) | (chgame.pressed(DOWN_BUTTON) ? 2 : 0) |
                  (chgame.pressed(A_BUTTON) ? 4 : 0) | (chgame.pressed(B_BUTTON) ? 8 : 0);
    uint8_t edge = cur & ~prevBtn;
    prevBtn = cur;
    if (state == PAUSED) {
        if (edge & 1) pauseSel = (pauseSel + 2) % 3;
        if (edge & 2) pauseSel = (pauseSel + 1) % 3;
        if (edge & 12) {
            if (pauseSel == 0) state = PLAY;
            else if (pauseSel == 1) {
                saveProgress(true);
                pauseNote = save::available() ? "GAME SAVED" : "SAVING IS OFF";
            } else toTitle();
        }
        return;
    }
    if (state == TITLE) titleT++;
    for (int i = 0; i < 24; i++)
        if (parts[i].life) { parts[i].x += parts[i].vx; parts[i].y += parts[i].vy; if (frameNo & 1) parts[i].vy++; parts[i].life--; }
    if (score > hiScore) hiScore = score;
    if (state == TITLE) {
        if (hasSave && (edge & 3)) titleSel ^= 1;
        if (edge & 12) { if (hasSave && titleSel == 0) continueGame(); else newGame(); }
        return;
    }
    if (state == OVER) {
        if (overT > 0) overT--;
        else if (edge & 12) toTitle();
        return;
    }
    if (!(frameNo & 1))
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                if (g[y][x] > 1 && --g[y][x] < 2) {
                    g[y][x] = 0;
                    burst(OX + x * CELL + 3, OY + y * CELL + 3 + yoff, (x + y) & 1 ? C_L2 : C_C2);   // green slime
                }
    static int rep;
    int dx = (int)chgame.pressed(RIGHT_BUTTON) - (int)chgame.pressed(LEFT_BUTTON);
    bool fireNow = edge & 12;
#ifdef CHARTH_DEMO
    demoPlay(dx, fireNow);
#endif
    if (!dx) rep = 0;
    else if (rep++ % 3 == 0) sc = constrain(sc + dx, 0, COLS - 1);
    if (shotOn) {
        shotY -= 10;
        int r = -1;                                    // the lowest solid block in the column, now (the formation creeps)
        for (int y = ROWS - 1; y >= 0; y--) if (g[y][shotX] == 1) { r = y; break; }
        if (r < 0) { if (shotY < OY - CELL) shotOn = false; }          // nothing there: the shot flies off
        else if (shotY <= OY + (r + 1) * CELL + yoff) {
            shotRow = r + 1; shotY = OY + shotRow * CELL + yoff;
            if (shotRow >= ROWS) gameOver();
            else land();
        }
    } else if (fireNow) { shotX = sc; shotY = SHIPY; shotOn = true; muzzT = 3; audio::sfx(S_FIRE); }
    if (!(frameNo & 1)) thrust();
    if (!(frameNo & 3))
        for (int i = 0; i < 14; i++) if (++stars[i][1] >= ROWS * CELL) { stars[i][1] = 0; stars[i][0] = rnd(COLS * CELL); }
    if (state != PLAY) return;
    dropT -= chgame.pressed(DOWN_BUTTON) ? 8 : 1;          // hold DOWN to hurry the formation: eight times as fast
    while (dropT <= 0 && state == PLAY) { dropT += dropEvery; if (++yoff >= CELL) { yoff = 0; drop(); } }
}

// Sprites (Sprites.h) go straight into the 4 bit framebuffer: 255 is transparent.
static void spr(int px, int py, const uint8_t *d) {
    for (int y = 0; y < CELL; y++) {
        int yy = py + y;
        if (yy < 0 || yy >= GFX_H) continue;
        for (int x = 0; x < CELL; x++) {
            uint8_t c = d[y * CELL + x];
            int xx = px + x;
            if (c == 255 || xx < 0 || xx >= GFX_W) continue;
            uint8_t *q = gfx_fb + yy * GFX_FB_STRIDE + (xx >> 1);
            *q = (xx & 1) ? (uint8_t)((*q & 0x0F) | (c << 4)) : (uint8_t)((*q & 0xF0) | c);
        }
    }
}

// A stroke block: the bevel runs only along the outside of a shape, so touching blocks join into one stroke.
static bool occ(int x, int y) { return x >= 0 && x < COLS && y >= 0 && y < ROWS && g[y][x] == 1; }
static void bevel(int px, int py, uint8_t base, uint8_t lt, uint8_t dk, bool up, bool dn, bool lf, bool rt) {
    gfx_fillRect(px, py, CELL, CELL, base);
    if (!up) gfx_fillRect(px, py, CELL, 1, lt);
    if (!lf) gfx_fillRect(px, py, 1, CELL, lt);
    gfx_fillRect(px, py + CELL - 1, CELL, 1, dk);                // the bottom and right edges stay dark, so a joined
    gfx_fillRect(px + CELL - 1, py, 1, CELL, dk);                // stroke still shows its blocks
    (void)dn; (void)rt;
}
static void block(int px, int py) { bevel(px, py, C_C4, C_L4, C_D4, false, false, false, false); }   // the red shot block

static void head(int x0, int y0, uint8_t c, uint8_t hi) {
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 12; x++) {
            char ch = HEAD[y][x];
            if (ch == '.') continue;
            uint8_t col = ch == '+' ? hi : (y & 1 ? C_D2 : c);
            gfx_pixel(x0 + x, y0 + y, col); gfx_pixel(x0 + 23 - x, y0 + y, col);
        }
}

static void headMini(int x0, int y0, uint8_t c, uint8_t hi) {         // the head at half size, for the panel
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 12; x++) {
            int hx = 2 * (x < 6 ? x : 11 - x) + 1;                    // mirrored, so the icon is symmetric
            char ch = HEAD[2 * y][hx];
            if (ch != '.') gfx_pixel(x0 + x, y0 + y, ch == '+' ? hi : c);
        }
}

static void bay(int x, int y) {                       // a bracketed tile bay
    gfx_fillRect(x, y, 14, 14, C_SCAN);
    gfx_fillRect(x + 1, y + 1, 12, 12, C_BG);
    uint8_t f = C_FRAME;
    gfx_fillRect(x, y, 4, 1, f); gfx_fillRect(x, y, 1, 4, f);
    gfx_fillRect(x + 10, y, 4, 1, f); gfx_fillRect(x + 13, y, 1, 4, f);
    gfx_fillRect(x, y + 13, 4, 1, f); gfx_fillRect(x, y + 10, 1, 4, f);
    gfx_fillRect(x + 10, y + 13, 4, 1, f); gfx_fillRect(x + 13, y + 10, 1, 4, f);
    block(x + 4, y + 4);
}

static void hazard(int y, uint8_t a, uint8_t b) {                // chevron stripes, one cell row high
    for (int r = 0; r < CELL; r++)
        for (int x = 0; x < COLS * CELL; x += 4)
            gfx_fillRect(OX + x, y + r, 4, 1, (((x + r * 2) >> 2) & 1) ? a : b);
}

static void rivet(int x, int y) { gfx_pixel(x, y, C_TEXT); gfx_pixel(x + 1, y + 1, C_SCAN); }

static void typed(int x, int y, const char *t, int n, uint8_t c) {
    char b[28]; int l = (int)strlen(t);
    if (n > l) n = l;
    memcpy(b, t, n); b[n] = 0;
    gfx_text(x, y, b, c);
    if (n < l && (frameNo & 4)) gfx_fillRect(x + n * 6, y, 5, 7, c);   // the cursor
}

static void box(int x, int y, int w, int h, uint8_t edge) {
    gfx_fillRect(x, y, w, h, C_BG);
    gfx_fillRect(x, y, w, 1, edge); gfx_fillRect(x, y + h - 1, w, 1, edge);
    gfx_fillRect(x, y, 1, h, edge); gfx_fillRect(x + w - 1, y, 1, h, edge);
}

static void draw() {
    gfx_clear(C_BG);
    char s[40];
    if (state == TITLE) {
        for (int y = 0; y < 128; y += 2) gfx_fillRect(0, y, 128, 1, C_SCAN);        // scanlines
        gfx_text(4, 3, "CARGO BAY TERMINAL", C_C2);
        gfx_text(4, 11, "UNIT 7  //  BAY 4", C_D2);
        int sweep = (frameNo * 2) % 160 - 20;                                     // a bright band sweeps down
        head(52, 20, C_C2, C_L2);
        gfx_fillRect(0, sweep, 128, 2, C_D2);
        gfx_textScaled(16, 48, "CHARTH", C_D1, 3);
        gfx_textScaled(15, 47, "CHARTH", C_C1, 3);
        gfx_fillRect(8, 72, 112, 1, C_FRAME); gfx_fillRect(8, 74, 112, 1, C_FRAME);
        static const char *const L[3] = { "> SYSTEM ONLINE", "> A/B: FIRE BLOCK", "> COMPLETE SHAPES" };
        int n = titleT / 2;
        for (int i = 0; i < 3; i++) {
            int l = (int)strlen(L[i]);
            if (n > 0) typed(8, 80 + i * 9, L[i], n, i == 1 ? C_C4 : C_C2);
            n -= l + 6;
            if (n <= 0) break;
        }
        if (hiScore) { snprintf(s, sizeof s, "HI %d", hiScore); gfx_text(8, 108, s, C_C3); }
        if (titleT > 100) {
            if (hasSave) {
                gfx_text(72, 100, "CONTINUE", titleSel == 0 ? C_C1 : C_D1);
                gfx_text(72, 110, "NEW GAME", titleSel == 1 ? C_C1 : C_D1);
                gfx_text(64, titleSel ? 110 : 100, ">", C_C1);
            } else if (frameNo & 16) gfx_text(72, 110, "PRESS A", C_C1);
        }
        return;
    }
    // bezel
    gfx_fillRect(0, 0, PX - 1, 128, C_FRAME);
    gfx_fillRect(OX - 1, OY - 1, COLS * CELL + 2, ROWS * CELL + CELL + 4, C_SCAN);
    gfx_fillRect(OX, OY, COLS * CELL, ROWS * CELL + CELL + 2, C_BG);
    rivet(0, 0); rivet(PX - 3, 0); rivet(0, 124); rivet(PX - 3, 124); rivet(0, 62); rivet(PX - 3, 62);
    // field: scanlines, cell grid dots
    for (int y = OY; y < OY + ROWS * CELL; y += 2) gfx_fillRect(OX, y, COLS * CELL, 1, C_SCAN);
    for (int y = 1; y < ROWS; y++)
        for (int x = 1; x < COLS; x++) gfx_pixel(OX + x * CELL, OY + y * CELL, C_D2);
    for (int i = 0; i < 14; i++) gfx_pixel(OX + stars[i][0], OY + stars[i][1], (i & 1) ? C_D2 : C_D3);   // stars
    bool danger = false;
    for (int x = 0; x < COLS; x++) if (g[ROWS - 3][x]) danger = true;
    bool blink = danger && (frameNo & 8);
    if (blink) {                                                              // a red warning border
        gfx_fillRect(OX - 1, OY - 1, COLS * CELL + 2, 1, C_C4); gfx_fillRect(OX - 1, OY + ROWS * CELL + CELL, COLS * CELL + 2, 1, C_C4);
        gfx_fillRect(OX - 1, OY - 1, 1, ROWS * CELL + CELL + 2, C_C4); gfx_fillRect(OX + COLS * CELL, OY - 1, 1, ROWS * CELL + CELL + 2, C_C4);
    }
    hazard(OY + (ROWS - 1) * CELL, blink ? C_C4 : C_D1, blink ? C_D4 : C_SCAN);   // the line that must stay clear
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            if (g[y][x] > 1) {                                                    // clearing: an explosion
                static const uint8_t *const EX[3] = { EXP0, EXP1, EXP2 };
                spr(OX + x * CELL, OY + y * CELL + yoff, EX[constrain((8 - g[y][x]) / 2, 0, 2)]);
            }
            else if (g[y][x]) bevel(OX + x * CELL, OY + y * CELL + yoff, C_C2, ((frameNo >> 3) + x + y) % 7 == 0 ? C_L3 : C_L2,   // a slow shimmer: the shapes look alive
                                    C_D2, occ(x, y - 1), occ(x, y + 1), occ(x - 1, y), occ(x + 1, y));
    if (shotOn) {
        for (int y = shotY + CELL; y < SHIPY; y += 2) gfx_pixel(OX + shotX * CELL + 2 + ((y >> 1) & 1) * 2, y, (y & 4) ? C_L1 : C_D1);   // trail
        block(OX + shotX * CELL, shotY);
    } else if (state == PLAY) {
        for (int y = SHIPY - 4; y > OY; y -= 6) gfx_pixel(OX + sc * CELL + 3, y, C_D1);
    }
    spr(OX + sc * CELL, SHIPY, (frameNo >> 2) & 1 ? SHIP1 : SHIP0);
    if (muzzT) { static const uint8_t *const MZ[3] = { MUZ2, MUZ1, MUZ0 }; spr(OX + sc * CELL, SHIPY - CELL + 1, MZ[muzzT - 1]); }
    if (waveT) {                                                              // the shockwave round a cleared shape
        int g2 = (9 - waveT) * 2;
        uint8_t c = waveT > 4 ? C_TEXT : C_L1;
        gfx_fillRect(wx0 - g2, wy0 - g2, wx1 - wx0 + 2 * g2, 1, c); gfx_fillRect(wx0 - g2, wy1 + g2, wx1 - wx0 + 2 * g2, 1, c);
        gfx_fillRect(wx0 - g2, wy0 - g2, 1, wy1 - wy0 + 2 * g2, c); gfx_fillRect(wx1 + g2, wy0 - g2, 1, wy1 - wy0 + 2 * g2 + 1, c);
    }
    for (int i = 0; i < 24; i++)
        if (parts[i].life) gfx_pixel(parts[i].x >> 3, parts[i].y >> 3, parts[i].col);
    // side panel
    gfx_fillRect(PX, 0, 128 - PX, 128, C_BG);
    gfx_fillRect(PX, 0, 1, 128, C_FRAME);
    gfx_text(PX + 3, 3, "SCORE", C_D1);
    snprintf(s, sizeof s, "%d", score);
    gfx_text(PX + 3, 11, s, C_C1);
    gfx_fillRect(PX + 3, 20, 44, 1, C_SCAN);
    gfx_text(PX + 3, 24, "SECT", C_D1);
    headMini(PX + 32, 25, blink ? C_C4 : ((frameNo >> 4) & 1 ? C_C2 : C_D2), blink ? C_L4 : C_L2);   // the watcher: red when they close in
    snprintf(s, sizeof s, "%02d", level);
    gfx_text(PX + 3, 32, s, C_C2);
    gfx_text(PX + 3, 44, "THREAT", C_D1);                                          // how close the formation is
    int low = 0;
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) if (g[y][x]) low = y + 1;
    int fill = 44 * low / ROWS;
    gfx_fillRect(PX + 3, 52, 44, 5, C_SCAN);
    for (int i = 0; i < 44; i += 4) if (i < fill) gfx_fillRect(PX + 4 + i, 53, 3, 3, i < 24 ? C_C2 : (i < 36 ? C_C1 : C_C4));
    gfx_text(PX + 3, 62, "SHAPES", C_D1);
    snprintf(s, sizeof s, "%d", cleared);
    gfx_text(PX + 3, 70, s, C_C3);
    gfx_fillRect(PX + 3, 80, 44, 1, C_SCAN);
    gfx_text(PX + 3, 84, "FIRE", C_D1);
    if (state == PLAY && chgame.pressed(DOWN_BUTTON)) gfx_text(PX + 30, 84, "FAST", C_C4);
    bay(PX + 18, 94);
    for (int i = 0; i < 7; i++) {                                              // an equaliser, for the music
        int hgt = 1 + (int)(((frameNo / 3) * (i * 7 + 3) + i * 5) % 9);
        if (state == PAUSED) hgt = 1;
        gfx_fillRect(PX + 4 + i * 6, 117 - hgt, 4, hgt, hgt > 6 ? C_C4 : (hgt > 3 ? C_C1 : C_C2));
    }
    if (blink) gfx_text(PX + 3, 120, "ALERT", C_C4);
    if (bannerT && state == PLAY) {                                             // the sector banner
        int w = OX + 2;
        box(w, 38, COLS * CELL - 4, 22, C_C2);
        snprintf(s, sizeof s, "SECTOR %02d", level);
        gfx_text(w + 8, 43, s, C_C2);
        gfx_text(w + 8, 51, level == 1 ? "HIVE AHEAD" : "FASTER", C_D2);
    }
    if (state == PAUSED) {
        static const char *const ITEMS[3] = { "RESUME", "SAVE GAME", "RETURN TO TITLE" };
        box(6, 36, 108, 56, C_C1);
        gfx_text(18, 41, "PAUSED", C_C1);
        for (int i = 0; i < 3; i++) gfx_text(18, 53 + i * 9, ITEMS[i], i == pauseSel ? C_C1 : C_D1);
        gfx_text(10, 53 + pauseSel * 9, ">", C_C1);
        if (pauseNote) gfx_text(18, 82, pauseNote, C_C2);
    }
    if (state == OVER) {
        head(OX + 12 * CELL / 2 - 12, 12, C_C4, C_L4);                          // it got through
        box(4, 40, 70, 44, C_C4);
        gfx_text(6, 44, "SYSTEM LOST", (frameNo & 8) ? C_C4 : C_L4);
        snprintf(s, sizeof s, "FINAL %d", score);
        gfx_text(6, 54, s, C_C1);
        snprintf(s, sizeof s, "BEST  %d", hiScore);
        gfx_text(6, 63, s, C_C2);
        if (overT == 0 && (frameNo & 16)) gfx_text(6, 73, "PRESS A", C_TEXT);
    }
    if (shakeT) {                                                               // a big clear shakes the screen
        int d = (shakeT & 1) ? 2 : -2;
        if (d > 0) memmove(gfx_fb + d * GFX_FB_STRIDE, gfx_fb, (128 - d) * GFX_FB_STRIDE);
        else memmove(gfx_fb, gfx_fb - d * GFX_FB_STRIDE, (128 + d) * GFX_FB_STRIDE);
    }
}

#ifdef CHSIM
void sim_sync();
void sim_advance(uint32_t us);
void sim_waitInput();
#endif

void setup() {
    chgame.boot();
#if CHGAME_DEBUG
    dbg::begin("CHAR " CHARTH_VERSION);
#endif
    gfx_begin(GFX_DIV2, GFX_12BPP);
    const uint16_t pal[16] = {                         // 1979 terminal: amber, phosphor, cyan, alarm red
        gfx_rgb(6, 10, 6), gfx_rgb(255, 176, 0), gfx_rgb(53, 255, 112), gfx_rgb(64, 216, 224), gfx_rgb(255, 75, 43),
        gfx_rgb(255, 217, 138), gfx_rgb(180, 255, 200), gfx_rgb(184, 244, 248), gfx_rgb(255, 176, 154),
        gfx_rgb(122, 82, 0), gfx_rgb(15, 122, 48), gfx_rgb(18, 106, 112), gfx_rgb(138, 30, 10),
        gfx_rgb(255, 200, 87), gfx_rgb(22, 34, 22), gfx_rgb(184, 168, 120) };
    gfx_setPalette(pal, 16);
    audio::begin(SOUNDS, S_COUNT);
    audio::setMusic(audio::LEAD);
    audio::melody(PSY);
    chgame.setFrameRate(30);
    SaveData d;
    uint8_t flag = 0;
    if (save::load(MAGIC, 1, d, &flag)) {
        hiScore = d.hi;
        if (flag == 1) { hasSave = true; sv = d; }
    }
    rng ^= millis();
    for (int i = 0; i < 14; i++) { stars[i][0] = rnd(COLS * CELL); stars[i][1] = rnd(ROWS * CELL); }
    state = TITLE;
}

void loop() {
    static bool startPrev;
    bool st = chgame.pressed(START_BUTTON);                     // START pressed in play: the pause menu
    if (st && !startPrev) {
        if (state == PLAY) { state = PAUSED; pauseSel = 0; pauseNote = nullptr; }
        else if (state == PAUSED) state = PLAY;
    }
    startPrev = st;
    dbg::markRenderStart();
    update();
    audio::update();
    gfx_wait();
    if (saveDue) {
        saveDue = false;
        save::store(MAGIC, 1, sv, saveFlag ? 1 : 0);
        hasSave = saveFlag && save::available();
        if (hasSave) titleSel = 0;
    }
    draw();
    dbg::markRenderEnd();
    gfx_flushAsync();
#ifdef CHSIM
    uint8_t idle = 0;                                           // the simulator feeds its script between frames
    for (dbg::poll(); !chgame.nextFrame(); dbg::poll()) {
        sim_sync();
        sim_advance(1000);
        if (++idle > 2) { sim_waitInput(); idle = 0; }
    }
#else
    do { dbg::poll(); } while (!chgame.nextFrame());
#endif
    chgame.pollButtons();
    dbg::markUpdateStart();
    static uint32_t startDown;
    if (!chgame.pressed(START_BUTTON)) startDown = 0;
    else if (!startDown) startDown = millis() | 1;
    else if (millis() - startDown >= 3000) chgame.exitToMenu();
}
