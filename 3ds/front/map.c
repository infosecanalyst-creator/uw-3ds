/* map.c: the dashboard's live map, drawn from the game's own record of what the player has
   seen (PlayersMap: AUTOMAP.C draws a square only when its low nibble is 1 to 9). Low nibble:
   1 open, 2..5 diagonal (SE, SW, NE, NW: the open half), 6..9 slope (N, S, E, W). Bits 4-5:
   floor terrain (1 water, 2 lava). Bits 6-7: 1 a door, 2 a special object, 3 stairs. The map's
   y grows north, so north is up. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "plat3ds.h"
#include "ui.h"
#include "uwsnap.h"
#include "front.h"

static int zoom = 1;                    /* 0..3: 4, 6, 9, 13 pixels a square */
static const float zooms[4] = { 4, 6, 9, 13 };
static int view_level;                  /* 0: your own; else another level from the save */
static uint8_t other[64][64];
static int other_ok, other_level;
static unsigned other_at;
static struct uwnote notes[100];
static int nnotes, notes_level = -1;
static unsigned notes_at;

void map_zoom(int d) { zoom += d; if (zoom < 0) zoom = 0; if (zoom > 3) zoom = 3; }
int map_zoom_level(void) { return zoom; }
int map_viewing(void) { return view_level; }
void map_follow(void) { view_level = 0; }

void map_level(int d)
{
    const struct uwsnap *s = uw_snap();
    int l = view_level ? view_level : s->level;
    l += d;
    if (l < 1) l = 1;
#ifdef GAME_UW2
    if (l > 80) l = 80;
#else
    if (l > 9) l = 9;
#endif
    view_level = l == s->level ? 0 : l;
    other_ok = 0;
}

#define C_FLOOR  C2D_Color32(168, 146, 108, 255)
#define C_SLOPE  C2D_Color32(150, 128, 92, 255)
#define C_WATER  C2D_Color32(64, 96, 168, 255)
#define C_LAVA   C2D_Color32(196, 70, 34, 255)
#define C_WALL   C2D_Color32(52, 38, 24, 255)
#define C_DOOR   C2D_Color32(110, 64, 28, 255)
#define C_THING  C2D_Color32(232, 190, 92, 255)
#define C_STAIRS C2D_Color32(30, 24, 18, 255)
#define C_YOU    C2D_Color32(220, 40, 30, 255)

static int seen(uint8_t (*m)[64], int x, int y)
{
    int c;
    if (x < 0 || y < 0 || x > 63 || y > 63) return 0;
    c = m[y][x] & 0xF;
    return c >= 1 && c <= 9;
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c)
{
    C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, 0.5f);
}

static void line(float x0, float y0, float x1, float y1, float w, u32 c)
{
    C2D_DrawLine(x0, y0, c, x1, y1, c, w, 0.5f);
}

void map_draw(float rx, float ry, float rw, float rh)
{
    const struct uwsnap *s = uw_snap();
    uint8_t (*m)[64];
    float sc = zooms[zoom], cx = rx + rw / 2, cy = ry + rh / 2, px, py;
    int level, x, y, i;
    float wl = sc >= 9 ? 2.0f : 1.0f;
    ui_rect(rx, ry, rw, rh, COL_BG);
    if (!s->in_game) {
        ui_text(cx, cy - 8, 0.5f, COL_DIM, C2D_AlignCenter, 0, "The map appears once you are in the Abyss.");
        return;
    }
    level = view_level ? view_level : s->level;
    if (view_level) {
        if (!other_ok || other_level != level || p3_frame() - other_at > 300) {
            other_ok = uw_level_automap(level, other);
            other_level = level;
            other_at = p3_frame();
        }
        if (!other_ok) {
            ui_textf(cx - 110, cy - 8, 0.5f, COL_DIM, "Level %d: nothing mapped yet.", level);
            return;
        }
        m = other;
        px = 32; py = 32;
    } else {
        m = (uint8_t (*)[64])s->automap;
        px = s->px; py = s->py;
    }
    if (notes_level != level || p3_frame() - notes_at > 180) {
        nnotes = uw_notes(level, notes, 100);
        notes_level = level;
        notes_at = p3_frame();
    }
    /* the squares */
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            int c = m[y][x], t = c & 0xF, terr = (c >> 4) & 3, mark = c >> 6;
            float x0 = cx + (x - px) * sc, y0 = cy - (y + 1 - py) * sc, x1 = x0 + sc, y1 = y0 + sc;
            u32 col;
            if (t < 1 || t > 9) continue;
            if (x0 < rx || x1 > rx + rw || y0 < ry || y1 > ry + rh) continue;
            col = terr == 1 ? C_WATER : terr == 2 ? C_LAVA : t >= 6 ? C_SLOPE : C_FLOOR;
            switch (t) {
            case 2: tri(x1, y0, x1, y1, x0, y1, col); line(x0, y1, x1, y0, wl, C_WALL); break;   /* SE open */
            case 3: tri(x0, y0, x0, y1, x1, y1, col); line(x0, y0, x1, y1, wl, C_WALL); break;   /* SW */
            case 4: tri(x0, y0, x1, y0, x1, y1, col); line(x0, y0, x1, y1, wl, C_WALL); break;   /* NE */
            case 5: tri(x1, y0, x0, y0, x0, y1, col); line(x0, y1, x1, y0, wl, C_WALL); break;   /* NW */
            default:
                ui_rect(x0, y0, sc, sc, col);
                if (t >= 6 && sc >= 6) {
                    /* a slope: a small arrow uphill */
                    float mx = x0 + sc / 2, my = y0 + sc / 2, a = sc * 0.3f;
                    u32 k = C_WALL;
                    if (t == 6) tri(mx, my - a, mx - a, my + a, mx + a, my + a, k);
                    else if (t == 7) tri(mx, my + a, mx - a, my - a, mx + a, my - a, k);
                    else if (t == 8) tri(mx + a, my, mx - a, my - a, mx - a, my + a, k);
                    else tri(mx - a, my, mx + a, my - a, mx + a, my + a, k);
                }
            }
            /* the walls: the edges toward squares not seen as open */
            if (t == 1 || t >= 6) {
                if (!seen(m, x, y + 1)) line(x0, y0, x1, y0, wl, C_WALL);
                if (!seen(m, x, y - 1)) line(x0, y1, x1, y1, wl, C_WALL);
                if (!seen(m, x - 1, y)) line(x0, y0, x0, y1, wl, C_WALL);
                if (!seen(m, x + 1, y)) line(x1, y0, x1, y1, wl, C_WALL);
            }
            if (mark == 1) ui_rect(x0 + sc * 0.2f, y0 + sc * 0.2f, sc * 0.6f, sc * 0.6f, C_DOOR);
            else if (mark == 2) ui_rect(x0 + sc * 0.35f, y0 + sc * 0.35f, sc * 0.3f, sc * 0.3f, C_THING);
            else if (mark == 3) {
                for (i = 0; i < 3; i++) ui_rect(x0 + sc * 0.15f, y0 + sc * (0.2f + i * 0.25f), sc * 0.7f, sc * 0.12f, C_STAIRS);
            }
        }
    /* the notes written on the game's map */
    for (i = 0; i < nnotes; i++) {
        float nx = cx + (notes[i].x - px) * sc, ny = cy - (notes[i].y - py) * sc;
        if (nx < rx + 3 || nx > rx + rw - 3 || ny < ry + 3 || ny > ry + rh - 3) continue;
        C2D_DrawCircleSolid(nx, ny, 0.5f, 3, COL_GOLD);
        if (sc >= 9) ui_text(nx + 5, ny - 6, 0.36f, COL_GOLD, 0, 0, notes[i].text);
    }
    /* you: an arrow turned to your heading (0 north, clockwise) */
    if (!view_level) {
        float a = s->heading * (float)(2 * M_PI / 256), r = sc < 6 ? 6 : sc * 0.8f;
        float sx = sinf(a), cy0 = -cosf(a);
        float tx = cx + sx * r, ty = cy + cy0 * r;
        float lx = cx - sx * r * 0.6f - cy0 * r * 0.6f, ly = cy - cy0 * r * 0.6f + sx * r * 0.6f;
        float qx = cx - sx * r * 0.6f + cy0 * r * 0.6f, qy = cy - cy0 * r * 0.6f - sx * r * 0.6f;
        tri(tx, ty, lx, ly, qx, qy, C_YOU);
    }
    /* the level's name and the compass */
    ui_rect(rx, ry, rw, 15, COL_SHADE);
    ui_textf(rx + 4, ry + 1, 0.42f, view_level ? COL_GOLD : COL_TEXT, "Level %d%s", level, view_level ? "  (as you left it)" : "");
    ui_text(rx + rw - 10, ry + 1, 0.42f, COL_DIM, C2D_AlignRight, 0, "N ^");
}
