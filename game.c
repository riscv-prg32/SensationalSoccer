/*
 * Sensational Soccer - an original six-a-side football cartridge for PRG32.
 *
 * Portable, integer-only, deterministic. All art is procedural or authored
 * here as character maps; all sound comes from scripts/generate_audio.py.
 *
 * Colour model (identical on QEMU RGB565 and the ESP32-C6 indexed panel):
 *  - sprites and text8 use exact colours of the firmware's 6x6x6 colour cube
 *    (palette 16..231), which the RGB565 -> index mapper reproduces exactly;
 *  - backgrounds use custom palette slots 8..15 and 232..255, which the
 *    mapper never produces, so they cannot corrupt RGB565 drawing.
 *
 * Audio policy: music (tracks 0 and 1) only on title and result screens.
 * Every transition to another screen stops the tracker AND every voice;
 * match updates keep the tracker stopped and trigger event effects only.
 */
#include "prg32.h"
#include <stdint.h>
#include <stddef.h>

#define W 320
#define H 200
#define FP 16
#define SIDE 6
#define N (SIDE * 2)
#define PW 960
#define PH 136
#define FW (PW * FP)
#define FH (PH * FP)
#define TOP 50
#define FPS 30
#define CAM_MIN (-40)
#define CAM_MAX (PW - W + 40)
#define GOAL_HALF 24
#define GOAL_HIGH 22
#define NET_DEPTH 16
#define BOX_DEPTH 84
#define BOX_HALF 50
#define SIX_DEPTH 28
#define SIX_HALF 30
#define SPOT_X 60
#define CIRCLE_R 34
#define MATCH_SECONDS 90
#define DEMO_SECONDS 60
#define ROOM "sensational-soccer-v2"

/* ------------------------------------------------------------ freestanding */
void *memset(void *d, int v, size_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)v; return d; }
void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }

static int absi(int v) { return v < 0 ? -v : v; }
static int sgn(int v) { return (v > 0) - (v < 0); }
static int clamp(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int vlen(int x, int y) { x = absi(x); y = absi(y); return x > y ? x + y / 2 : y + x / 2; }
static uint32_t seed = 0x2545F491u;
static uint32_t rnd(void) { seed = seed * 1664525u + 1013904223u; return seed >> 16; }
static uint32_t hash(uint32_t a, uint32_t b) { uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u; h ^= h >> 15; h *= 0x2C1B3C6Du; return h ^ (h >> 12); }

/* ---------------------------------------------------------------- colours */
#define CI(r, g, b) ((uint8_t)(16 + 36 * (r) + 6 * (g) + (b)))
static const uint8_t R5[6] = {0, 7, 13, 19, 25, 31}, G6[6] = {0, 13, 26, 38, 51, 63};
static uint16_t c565(uint8_t ci) { int v = ci - 16; if (v < 0 || v >= 216) return 0; return (uint16_t)((R5[v / 36] << 11) | (G6[(v / 6) % 6] << 5) | R5[v % 6]); }
static uint16_t rgb(int r, int g, int b) { return (uint16_t)(((r & 248) << 8) | ((g & 252) << 3) | (b >> 3)); }
enum {
  P_DARK = 8, P_PANEL, P_PANEL2, P_EDGE, P_RADAR, P_RADAR2, P_GLOW, P_POLE,
  P_SKY0 = 232, P_SKY1, P_SKY2, P_SKY3, P_ROOF, P_STAND0, P_STAND1, P_STAND2,
  P_G0, P_G1, P_G2, P_G3, P_LINE, P_SHADOW, P_APRON, P_APRON2,
  P_HUD, P_HUD2, P_NET, P_LOGO0, P_LOGO1, P_LOGO2, P_LOGO3, P_FLASH
};
#define HUDC CI(0, 0, 1)
#define WHITE CI(5, 5, 5)
#define YELLOW CI(5, 5, 0)
#define GREY CI(3, 3, 3)
#define BLACK CI(0, 0, 0)

static void make_palette(void) {
  static const struct { uint8_t i; uint8_t r, g, b; } pal[] = {
    {P_DARK, 6, 10, 22}, {P_PANEL, 14, 24, 48}, {P_PANEL2, 26, 42, 80}, {P_EDGE, 120, 170, 230},
    {P_RADAR, 12, 70, 34}, {P_RADAR2, 30, 110, 54}, {P_GLOW, 255, 250, 210}, {P_POLE, 40, 44, 56},
    {P_SKY0, 8, 12, 38}, {P_SKY1, 18, 22, 64}, {P_SKY2, 40, 32, 92}, {P_SKY3, 78, 48, 112},
    {P_ROOF, 22, 26, 36}, {P_STAND0, 34, 38, 52}, {P_STAND1, 52, 58, 74}, {P_STAND2, 74, 80, 98},
    {P_G0, 38, 150, 64}, {P_G1, 44, 162, 70}, {P_G2, 30, 128, 54}, {P_G3, 34, 138, 58},
    {P_LINE, 232, 248, 228}, {P_SHADOW, 18, 84, 36}, {P_APRON, 26, 112, 48}, {P_APRON2, 70, 64, 60},
    {P_HUD, 6, 14, 30}, {P_HUD2, 20, 36, 70}, {P_NET, 196, 214, 222}, {P_LOGO0, 255, 244, 120},
    {P_LOGO1, 255, 206, 40}, {P_LOGO2, 255, 150, 20}, {P_LOGO3, 226, 70, 24}, {P_FLASH, 255, 247, 151},
  };
  for (unsigned i = 0; i < sizeof pal / sizeof pal[0]; i++) prg32_palette_set(pal[i].i, rgb(pal[i].r, pal[i].g, pal[i].b));
}
static void box(int x, int y, int w, int h, uint8_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w > 0 && h > 0) prg32_gfx_rect_indexed(x, y, w, h, c);
}
static void text(int x, int y, const char *s, uint8_t fg, uint8_t bg) { prg32_gfx_text8(x, y, s, c565(fg), c565(bg)); }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void text_c(int y, const char *s, uint8_t fg, uint8_t bg) { text((W - slen(s) * 8) / 2, y, s, fg, bg); }

/* ------------------------------------------------------- 5x7 display font */
static const uint8_t FONT[][7] = {
  {0, 0, 0, 0, 0, 0, 0},                                          /* space */
  {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
  {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F}, {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
  {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
  {0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
  {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
  {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
  {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
  {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
  {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
  {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
  {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
  {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
  {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
  {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
  {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
  {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
  {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
  {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
  {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
  {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04},
  {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}, {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10},
  {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08},
};
static int glyph(char c) {
  if (c >= 'A' && c <= 'Z') return 1 + c - 'A';
  if (c >= '0' && c <= '9') return 27 + c - '0';
  switch (c) { case '!': return 37; case '-': return 38; case ':': return 39; case '.': return 40;
    case '\'': return 41; case '/': return 42; case '<': return 43; case '>': return 44; }
  return 0;
}
static int ftext_w(const char *s, int sc) { return slen(s) * 6 * sc - sc; }
/* Draw transparent display text; rows[] gives a per-row colour gradient. */
static void ftext(int x, int y, const char *s, int sc, const uint8_t *rows, uint8_t col) {
  for (; *s; s++, x += 6 * sc) {
    const uint8_t *g = FONT[glyph(*s)];
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5;) {
        if (!(g[r] & (16 >> c))) { c++; continue; }
        int c0 = c;
        while (c < 5 && (g[r] & (16 >> c))) c++;
        box(x + c0 * sc, y + r * sc, (c - c0) * sc, sc, rows ? rows[r] : col);
      }
  }
}
static void ftext_shadow(int x, int y, const char *s, int sc, const uint8_t *rows, uint8_t col, uint8_t edge) {
  ftext(x + 1, y + sc, s, sc, 0, edge);
  ftext(x - 1, y, s, sc, 0, edge);
  ftext(x + 1, y - 1, s, sc, 0, edge);
  ftext(x, y, s, sc, rows, col);
}

/* ---------------------------------------------------------------- sprites */
#define SPR_W 16
#define SPR_H 24
#define SPR_BYTES (SPR_W * SPR_H / 2)
enum { F_STAND, F_RUN0, F_RUN1, F_RUN2, F_RUN3, F_KICK, F_CHEER, F_SLIDE, NFR };
/* Template colour keys (see kit palettes):
 * . clear k outline s skin S skin shade h hair J shirt j shirt shade
 * L shirt light P shorts p shorts shade O socks b boots w white */
static const char HEAD[8][17] = {
  "......kkkk......", ".....khhhhk.....", "....khhhhhhk....", "....khhhsssk....",
  "....khhsskssk...", ".....khsssssk...", "......kssSkk....", ".....kkSSkk.....",
};
static const char UP_IDLE[7][17] = {
  "....kJJJJJJk....", "...kJLLJJJJjk...", "...kJLLJJJJjk...", "...kJkLJJJJjk...",
  "...kJksJJJjjk...", "....kksJJjjk....", "....kSskkkkk....",
};
static const char UP_FWD[7][17] = {
  "....kJJJJJJk....", "...kJJJLLJJjk...", "...kJJJJLLLk....", "...kJJJJkksssk..",
  "...kJJJJJjkkk...", "....kJJJjjjk....", "....kkkkkkkk....",
};
static const char UP_BACK[7][17] = {
  "....kJJJJJJk....", "...kLLJJJJJjk...", "..kLLkJJJJJjk...", ".ksskJJJJJJjk...",
  ".kkkkJJJJJjjk...", "....kJJJJjjk....", "....kkkkkkkk....",
};
static const char UP_KICK[7][17] = {
  "....kJJJJJJk....", ".kkkJLLJJJJjk...", "ksssLLJJJJJjkk..", ".kkkJJJJJJJjssk.",
  "....kJJJJJjjkk..", "....kJJJJjjk....", "....kkkkkkkk....",
};
static const char LO_STAND[9][17] = {
  ".....kPPPPpk....", ".....kPPPPpk....", ".....kPPkPpk....", ".....ksSkksk....",
  ".....kOOkkOk....", ".....kOOkkOk....", ".....kOOkkOk....", ".....kbbbkbbbk..",
  ".....kkkkkkkkk..",
};
static const char LO_STRIDE[9][17] = {
  ".....kPPPPpk....", "....kPPPPPPpk...", "...kPPpkkkPPk...", "...kSSk...kSSk..",
  "..kOOk.....kOOk.", "..kOk.......kOk.", ".kOOk.......kOk.", ".kbbk.......kbbk",
  ".kkk........kkkk",
};
static const char LO_PASS[9][17] = {
  ".....kPPPPpk....", ".....kPPPPpk....", ".....kPPPPkk....", "......kSSkSk....",
  "......kOOkOOk...", "......kOOkkOOk..", "......kOOk.kbbk.", ".....kbbbk.kkk..",
  ".....kkkkk......",
};
static const char LO_KICK[9][17] = {
  ".....kPPPPpk....", ".....kPPPPPPkk..", "....kPPpkkPPSSk.", "....kSSk..kkSOk.",
  "....kOOk....kbbk", "....kOOk.....kk.", "....kOOk........", "...kbbbk........",
  "...kkkkk........",
};
static const char LO_JUMP[9][17] = {
  ".....kPPPPpk....", "....kPPPPPPpk...", "....kPPkkkPPk...", "...kSSk...kSSk..",
  "...kOOk...kOOk..", "...kOOk...kOOk..", "...kOOk...kOOk..", "..kbbbk..kbbbk..",
  "..kkkkk..kkkkk..",
};
static const char CHEER_TOP[15][17] = {
  ".kk...kkkk...kk.", "kssk.khhhhk.kssk", "kssk.khhhhhkkssk", ".kLkkhhhsssk.kLk",
  ".kLkkhhsskssk.Lk", ".kLk.khssssskkLk", ".kLkk.kssSkk.kLk", "..kLkkkSSkk.kLk.",
  "..kLJJJJJJJkLLk.", "...kJLLJJJJjjk..", "...kJLLJJJJjk...", "...kJJJJJJJjk...",
  "....kJJJJJjjk...", "....kJJJJjjk....", "....kkkkkkkk....",
};
static const char SLIDE[24][17] = {
  "................", "................", "................", "................",
  "................", "................", "................", "................",
  "................", "................", "......kkkk......", ".....khhhhk.....",
  "....khhhsssk....", "....khhsskssk...", ".....khsssssk...", ".....kkJJJJk....",
  "...kkJJJJJJjk...", "..ksLJJJJJJJjk..", "..kkkPPPPPPPpkk.", "....kPPPPkSSOOOk",
  "....kSSOOkkkOObk", "....kkOOOOOOkkbk", ".....kkkkkkkkkk.", "................",
};
static uint8_t spr_pix[2][NFR][SPR_BYTES];
enum { KIT_HOME, KIT_AWAY, KIT_HKEEP, KIT_AKEEP, NKIT };
#define NSKIN 4
static uint16_t kit_pal[NKIT][NSKIN][16];
static uint8_t kit_idx[NKIT][NSKIN][16];
static prg32_indexed_sprite_t spr[NKIT][NSKIN][2];
typedef struct { uint8_t shirt, light, dark, shorts, shorts_dark, socks; } Kit;
static const Kit team_kit[4] = {
  {CI(0, 2, 5), CI(2, 4, 5), CI(0, 1, 3), CI(5, 5, 5), CI(3, 3, 4), CI(0, 2, 5)},
  {CI(4, 0, 0), CI(5, 2, 1), CI(2, 0, 0), CI(5, 5, 5), CI(3, 3, 4), CI(4, 0, 0)},
  {CI(5, 4, 0), CI(5, 5, 2), CI(4, 2, 0), CI(0, 0, 2), CI(0, 0, 1), CI(0, 0, 2)},
  {CI(0, 3, 1), CI(1, 4, 2), CI(0, 2, 0), CI(5, 4, 0), CI(4, 3, 0), CI(5, 5, 5)},
};
static const Kit keeper_kit[2] = {
  {CI(4, 0, 4), CI(5, 2, 5), CI(2, 0, 2), CI(1, 1, 1), CI(0, 0, 0), CI(4, 0, 4)},
  {CI(2, 2, 2), CI(3, 3, 3), CI(1, 1, 1), CI(0, 0, 0), CI(0, 0, 0), CI(2, 2, 2)},
};
static const uint8_t complexion[NSKIN][3] = {
  {CI(5, 4, 3), CI(4, 3, 2), CI(5, 4, 1)}, {CI(5, 4, 3), CI(4, 3, 2), CI(2, 1, 0)},
  {CI(4, 3, 2), CI(3, 2, 1), CI(1, 0, 0)}, {CI(3, 2, 1), CI(2, 1, 0), CI(0, 0, 0)},
};
static const char team_name[4][8] = {"NAPOLI", "LONDON", "TOKYO", "RIO"};
static int chosen[2] = {0, 1};

static int key_of(char c) {
  switch (c) { case 'k': return 1; case 's': return 2; case 'S': return 3; case 'h': return 4;
    case 'J': return 5; case 'j': return 6; case 'L': return 7; case 'P': return 8; case 'p': return 9;
    case 'O': return 10; case 'b': return 11; case 'w': return 12; }
  return 0;
}
static void put4(uint8_t *buf, int pos, int v) {
  if (pos & 1) buf[pos >> 1] = (uint8_t)((buf[pos >> 1] & 0xF0) | v);
  else buf[pos >> 1] = (uint8_t)((buf[pos >> 1] & 0x0F) | (v << 4));
}
static int get4(const uint8_t *buf, int pos) { return (pos & 1) ? buf[pos >> 1] & 15 : buf[pos >> 1] >> 4; }
typedef const char Row[17];
static void build_frame(int f, Row *head, Row *up, Row *lo, Row *full) {
  for (int r = 0; r < SPR_H; r++) {
    const char *row = full ? full[r] : (r < 8 && head ? head[r] : (r < 15 ? up[r - (head ? 8 : 0)] : lo[r - 15]));
    for (int c = 0; c < SPR_W; c++) {
      int v = key_of(row[c]);
      put4(spr_pix[0][f], r * SPR_W + c, v);
      put4(spr_pix[1][f], r * SPR_W + (SPR_W - 1 - c), v);
    }
  }
}
static void set_kit(int k, const Kit *kit) {
  for (int s = 0; s < NSKIN; s++) {
    uint8_t *ix = kit_idx[k][s];
    ix[0] = BLACK; ix[1] = BLACK; ix[2] = complexion[s][0]; ix[3] = complexion[s][1]; ix[4] = complexion[s][2];
    ix[5] = kit->shirt; ix[6] = kit->dark; ix[7] = kit->light; ix[8] = kit->shorts; ix[9] = kit->shorts_dark;
    ix[10] = kit->socks; ix[11] = CI(1, 1, 1); ix[12] = WHITE; ix[13] = ix[14] = ix[15] = BLACK;
    for (int i = 0; i < 16; i++) kit_pal[k][s][i] = c565(ix[i]);
  }
}

/* Ball: 6x6, four rolling frames. */
static const char BALL[4][6][7] = {
  {".kkkk.", "kwwbwk", "kwbbwk", "kwwwgk", "kgwwgk", ".kkkk."},
  {".kkkk.", "kwwwwk", "kbwwwk", "kbbwgk", "kgwwgk", ".kkkk."},
  {".kkkk.", "kwwwwk", "kwwwbk", "kwwbbk", "kgwwgk", ".kkkk."},
  {".kkkk.", "kwbwwk", "kwwwwk", "kwwwgk", "kgbbgk", ".kkkk."},
};
static uint8_t ball_pix[4][18];
static uint16_t ball_pal[5];
static prg32_indexed_sprite_t ball_spr;

/* Crowd: a 320x28 opaque tile with three animation frames. */
#define CROWD_W 320
#define CROWD_H 28
static uint8_t crowd_pix[3][CROWD_W * CROWD_H / 2];
static uint16_t crowd_pal[16];
static prg32_indexed_sprite_t crowd_spr;
enum { CR_ROOF, CR_CONC, CR_STEP, CR_SKIN, CR_SKIN2, CR_HAIR, CR_HAIR2, CR_HOME, CR_HOMEL, CR_AWAY, CR_AWAYL,
       CR_WHITE, CR_YELLOW, CR_GREY, CR_BLACK, CR_GLINT };
static void cput(int f, int x, int y, int v) { if (x >= 0 && x < CROWD_W && y >= 0 && y < CROWD_H) put4(crowd_pix[f], y * CROWD_W + x, v); }
static void build_crowd(void) {
  static const uint8_t base[16] = {CI(0, 0, 1), CI(1, 1, 1), CI(2, 2, 2), CI(5, 4, 3), CI(3, 2, 1), CI(1, 0, 0),
    CI(5, 4, 1), 0, 0, 0, 0, WHITE, YELLOW, GREY, BLACK, CI(5, 5, 4)};
  for (int i = 0; i < 16; i++) crowd_pal[i] = c565(base[i]);
  for (int f = 0; f < 3; f++) {
    for (int y = 0; y < CROWD_H; y++)
      for (int x = 0; x < CROWD_W; x++) {
        int v = CR_CONC;
        if (y < 3) v = (y == 1 && x % 40 == 6) || (y == 1 && x % 40 == 8) ? CR_GLINT : CR_ROOF;
        else if ((y - 3) % 8 == 7) v = CR_STEP;
        cput(f, x, y, v);
      }
    for (int t = 0; t < 3; t++)
      for (int x0 = (t * 2) % 5; x0 < CROWD_W; x0 += 5) {
        uint32_t h = hash((uint32_t)x0, (uint32_t)t);
        if ((h & 15) == 0) continue;               /* empty seat */
        int y0 = 3 + t * 8, shirt, light;
        int sel = (h >> 4) % 10;
        if (sel < 4) { shirt = CR_HOME; light = CR_HOMEL; }
        else if (sel < 8) { shirt = CR_AWAY; light = CR_AWAYL; }
        else { shirt = sel == 8 ? CR_WHITE : CR_GREY; light = CR_WHITE; }
        int skin = (h >> 9) & 3 ? CR_SKIN : CR_SKIN2, hair = (h >> 11) % 5 ? CR_HAIR : CR_HAIR2;
        int up = f == 2 || (f == 1 && (h >> 14) % 5 == 0), dy = f == 2 && ((h >> 17) & 1) ? -1 : 0;
        int y = y0 + dy;
        for (int c = 1; c <= 3; c++) { cput(f, x0 + c, y, hair); cput(f, x0 + c, y + 1, skin); cput(f, x0 + c, y + 2, skin); }
        cput(f, x0 + 2, y + 2, CR_SKIN2);
        for (int r = 3; r <= 6; r++) for (int c = 0; c <= 4; c++) cput(f, x0 + c, y + r, c == 1 && r < 5 ? light : shirt);
        if (dy) for (int c = 0; c <= 4; c++) cput(f, x0 + c, y0 + 6, CR_CONC);
        if (up) {
          for (int r = -1; r <= 2; r++) { cput(f, x0, y + r, r < 0 ? skin : shirt); cput(f, x0 + 4, y + r, r < 0 ? skin : shirt); }
          if (f == 2 && (h >> 19) % 6 == 0) for (int c = -1; c <= 5; c++) cput(f, x0 + c, y - 2, c & 1 ? CR_WHITE : shirt);
        }
      }
  }
}
static void crowd_colours(void) {
  crowd_pal[CR_HOME] = c565(team_kit[chosen[0]].shirt); crowd_pal[CR_HOMEL] = c565(team_kit[chosen[0]].light);
  crowd_pal[CR_AWAY] = c565(team_kit[chosen[1]].shirt); crowd_pal[CR_AWAYL] = c565(team_kit[chosen[1]].light);
}

/* Advertising boards along the far touchline, world x -48..1008. */
#define BOARD_W 1056
#define BOARD_H 8
static uint8_t board_pix[BOARD_W * BOARD_H / 2];
static uint16_t board_pal[12];
static prg32_indexed_sprite_t board_spr;
static void build_boards(void) {
  static const uint8_t pal[12] = {BLACK, CI(0, 0, 2), YELLOW, WHITE, CI(0, 0, 3), CI(4, 0, 0), CI(0, 3, 5),
    CI(0, 5, 1), CI(0, 3, 1), CI(5, 2, 0), CI(0, 1, 4), CI(1, 1, 1)};
  static const struct { char t[16]; uint8_t bg, fg; } ads[11] = {
    {"PRG32", 1, 2}, {"RISC-V", 3, 4}, {"GOAL TV", 5, 3}, {"UNIPARTHENOPE", 6, 3}, {"8-BIT FC", 0, 7},
    {"PRG32", 2, 0}, {"OPEN HW", 8, 3}, {"RISC-V", 4, 3}, {"SUPER SPORT", 9, 3}, {"NAPOLI 26", 3, 10}, {"PRG32", 5, 2},
  };
  for (int i = 0; i < 12; i++) board_pal[i] = c565(pal[i]);
  for (int s = 0; s < 11; s++) {
    int x0 = s * 96;
    for (int y = 0; y < BOARD_H; y++)
      for (int x = 0; x < 96; x++)
        put4(board_pix, y * BOARD_W + x0 + x, (y == 0 || x == 0) ? 11 : ads[s].bg);
    const char *t = ads[s].t;
    int tx = x0 + (96 - (slen(t) * 6 - 1)) / 2;
    for (; *t; t++, tx += 6) {
      const uint8_t *g = FONT[glyph(*t)];
      for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (g[r] & (16 >> c)) put4(board_pix, (r + 1) * BOARD_W + tx + c, ads[s].fg);
    }
  }
}

static void sprite_init(prg32_indexed_sprite_t *s, const uint8_t *pix, const uint16_t *pal, int w, int h, int frames, int pc, int tr) {
  s->pixels = pix; s->palette = pal; s->width = (uint16_t)w; s->height = (uint16_t)h; s->frame_count = (uint16_t)frames;
  s->palette_count = (uint16_t)pc; s->bits_per_pixel = PRG32_SPRITE_BPP_4; s->transparent_index = (int16_t)tr;
}
static void apply_kits(void) {
  set_kit(KIT_HOME, &team_kit[chosen[0]]); set_kit(KIT_AWAY, &team_kit[chosen[1]]);
  set_kit(KIT_HKEEP, &keeper_kit[0]); set_kit(KIT_AKEEP, &keeper_kit[1]);
  crowd_colours();
}
static void build_graphics(void) {
  build_frame(F_STAND, HEAD, UP_IDLE, LO_STAND, 0);
  build_frame(F_RUN0, HEAD, UP_FWD, LO_STRIDE, 0);
  build_frame(F_RUN1, HEAD, UP_IDLE, LO_PASS, 0);
  build_frame(F_RUN2, HEAD, UP_BACK, LO_STRIDE, 0);
  build_frame(F_RUN3, HEAD, UP_IDLE, LO_STAND, 0);
  build_frame(F_KICK, HEAD, UP_KICK, LO_KICK, 0);
  build_frame(F_CHEER, 0, CHEER_TOP, LO_JUMP, 0);
  build_frame(F_SLIDE, 0, 0, 0, SLIDE);
  apply_kits();
  for (int k = 0; k < NKIT; k++)
    for (int s = 0; s < NSKIN; s++)
      for (int f = 0; f < 2; f++) sprite_init(&spr[k][s][f], &spr_pix[f][0][0], kit_pal[k][s], SPR_W, SPR_H, NFR, 16, 0);
  static const uint8_t bp[5] = {0, CI(1, 1, 1), WHITE, CI(3, 3, 3), BLACK};
  for (int i = 0; i < 5; i++) ball_pal[i] = c565(bp[i]);
  for (int f = 0; f < 4; f++)
    for (int r = 0; r < 6; r++)
      for (int c = 0; c < 6; c++) {
        char ch = BALL[f][r][c];
        put4(ball_pix[f], r * 6 + c, ch == 'k' ? 1 : ch == 'w' ? 2 : ch == 'g' ? 3 : ch == 'b' ? 4 : 0);
      }
  sprite_init(&ball_spr, &ball_pix[0][0], ball_pal, 6, 6, 4, 5, 0);
  build_crowd();
  sprite_init(&crowd_spr, &crowd_pix[0][0], crowd_pal, CROWD_W, CROWD_H, 3, 16, -1);
  build_boards();
  sprite_init(&board_spr, board_pix, board_pal, BOARD_W, BOARD_H, 1, 12, -1);
}
/* Large (menu) rendering of a player frame via indexed rectangles. */
static void draw_big(int x, int y, int f, int kit, int skin, int sc, int face) {
  const uint8_t *pix = spr_pix[face ? 0 : 1][f];
  for (int r = 0; r < SPR_H; r++)
    for (int c = 0; c < SPR_W;) {
      int v = get4(pix, r * SPR_W + c);
      if (!v) { c++; continue; }
      int c0 = c;
      while (c < SPR_W && get4(pix, r * SPR_W + c) == v) c++;
      box(x + c0 * sc, y + r * sc, (c - c0) * sc, sc, kit_idx[kit][skin][v]);
    }
}

/* ------------------------------------------------------------------ audio */
enum { I_KICK = 8, I_WHISTLE, I_ROAR, I_OOH, I_CLANG, I_BOUNCE, I_NET, I_BLIP };
enum { CH_BALL, CH_BOUNCE, CH_WHISTLE, CH_POST, CH_CROWD_L, CH_CROWD_R, CH_NET, CH_UI };
static int camera_x, whistle_left, whistle_wait;
static int8_t pan_at(int wx) { return (int8_t)clamp((wx / FP - camera_x - W / 2) * 56 / (W / 2), -56, 56); }
static void music_off(void) {
  prg32_audio_stop_track(); prg32_audio_stop_all();
  for (int c = 0; c < 8; c++) prg32_audio_set_channel_volume((uint8_t)c, 255);   /* tracks set lower mix levels */
}
static void music_on(int track) { prg32_audio_stop_all(); prg32_audio_play_track((uint16_t)track); }
static void sfx_kick(int power, int wx) {
  prg32_audio_note_on_pan(CH_BALL, I_KICK, (uint8_t)(power > 140 ? 57 : power > 100 ? 60 : 63), (uint8_t)clamp(power + 110, 170, 255), pan_at(wx));
}
static void sfx_touch(int wx) { prg32_audio_note_on_pan(CH_BALL, I_KICK, 67, 120, pan_at(wx)); }
static void sfx_tackle(int wx) { prg32_audio_note_on_pan(CH_POST, I_KICK, 52, 240, pan_at(wx)); }
static void sfx_catch(int wx) { prg32_audio_note_on_pan(CH_BALL, I_KICK, 50, 255, pan_at(wx)); }
static void sfx_bounce(int vz, int wx) {
  prg32_audio_set_channel_pan(CH_BOUNCE, pan_at(wx));
  prg32_audio_note(CH_BOUNCE, I_BOUNCE, 43, (uint8_t)clamp(vz * 5, 60, 220), 70);
}
static void sfx_whistle(int blasts) { whistle_left = blasts; whistle_wait = 0; }
static void sfx_roar(void) {
  prg32_audio_note_on_pan(CH_CROWD_L, I_ROAR, 60, 255, -44);
  prg32_audio_note_on_pan(CH_CROWD_R, I_ROAR, 61, 240, 44);
}
static void sfx_ooh(void) {
  prg32_audio_note_on_pan(CH_CROWD_L, I_OOH, 60, 240, -30);
  prg32_audio_note_on_pan(CH_CROWD_R, I_OOH, 59, 220, 30);
}
static void sfx_post(int wx) { prg32_audio_note_on_pan(CH_POST, I_CLANG, 60, 255, pan_at(wx)); }
static void sfx_net(int wx) { prg32_audio_set_channel_pan(CH_NET, pan_at(wx)); prg32_audio_note(CH_NET, I_NET, 76, 170, 260); }
static void sfx_blip(int ok) { prg32_audio_note(CH_UI, I_BLIP, (uint8_t)(ok ? 84 : 77), 130, ok ? 120 : 45); }
static void sfx_update(void) {
  if (!whistle_left) return;
  if (whistle_wait) { whistle_wait--; return; }
  int last = whistle_left == 1;
  prg32_audio_note_on_pan(CH_WHISTLE, I_WHISTLE, (uint8_t)(last ? 59 : 60), 255, 0);
  whistle_left--;
  whistle_wait = 9;
}

/* ------------------------------------------------------------- simulation */
enum { ACT_NONE, ACT_KICK, ACT_SLIDE, ACT_DIVE, ACT_CHEER, ACT_HOLD };
typedef struct {
  int x, y, vx, vy, stride;
  uint8_t team, role, face, skin, action, timer, cooldown, stun;
} Player;
typedef struct { int x, y, z, vx, vy, vz, spin, owner, last_team, last_player, roll, shot; } Ball;
static Player p[N];
static Ball ball;
enum { SCREEN_TITLE, SCREEN_TEAM, SCREEN_MODE, SCREEN_MATCH, SCREEN_RESULT };
enum { PH_KICKOFF, PH_PLAY, PH_GOAL, PH_RESTART, PH_FULLTIME };
enum { RS_GOALKICK, RS_CORNER, RS_THROW };
static uint32_t frame, last_input, last_remote;
static int score[2], clock_frames, controlled[2], attract, networked, peer_seen, screen, menu_cursor, menu_lock;
static int mode, round_no, wins, celebrate, net_shake, phase, phase_timer, restart_kind, restart_team, restart_taker;
static int kickoff_team, aftertouch, banner_timer, hold_timer[2], screen_timer;
static const char *banner;

static const int16_t FORM_X[SIDE] = {14, 160, 160, 300, 300, 420};
static const int16_t FORM_Y[SIDE] = {68, 38, 98, 28, 108, 68};
static int goal_x(int team) { return team ? FW : 0; }          /* the goal a team defends */
static int attack_dir(int team) { return team ? -1 : 1; }
static int dist(int x0, int y0, int x1, int y1) { return vlen(x1 - x0, y1 - y0); }

static void reset_ball(int x, int y) {
  ball.x = x; ball.y = y; ball.z = ball.vx = ball.vy = ball.vz = ball.spin = 0; ball.owner = -1; ball.shot = 0;
}
static void setup_kickoff(int team) {
  for (int i = 0; i < N; i++) {
    Player *q = &p[i];
    int t = i >= SIDE, r = i % SIDE, x = FORM_X[r];
    q->team = (uint8_t)t; q->role = (uint8_t)r; q->skin = (uint8_t)((i * 7 + 3) % NSKIN);
    if (r == 5 && t == team) x = 473;
    q->x = (t ? PW - x : x) * FP; q->y = FORM_Y[r] * FP;
    q->vx = q->vy = 0; q->face = (uint8_t)!t; q->action = q->timer = q->cooldown = q->stun = 0;
  }
  reset_ball(FW / 2, FH / 2);
  kickoff_team = team; phase = PH_KICKOFF; phase_timer = 0;
  controlled[0] = 5; controlled[1] = SIDE + 5;
  hold_timer[0] = hold_timer[1] = 0;
  banner = "KICK OFF"; banner_timer = 30;
}
static void new_match(int demo) {
  score[0] = score[1] = clock_frames = 0; attract = demo; celebrate = net_shake = 0;
  apply_kits(); setup_kickoff((int)(frame & 1));
  camera_x = (PW - W) / 2;
}

static void take_ball(int i) {
  ball.owner = i; ball.z = ball.vz = ball.spin = 0; ball.vx = p[i].vx; ball.vy = p[i].vy; ball.last_team = p[i].team; ball.last_player = i; ball.shot = 0;
  if (!attract && p[i].role) controlled[p[i].team] = i;
}
static void release(int i, int vx, int vy, int vz, int spin) {
  Player *q = &p[i];
  ball.owner = -1; ball.vx = vx; ball.vy = vy; ball.vz = vz; ball.spin = spin; ball.z = 2 * FP;
  ball.x = q->x + sgn(vx) * 5 * FP; ball.y = q->y + FP;
  ball.last_team = q->team; ball.last_player = i;
  q->action = ACT_KICK; q->timer = 8; q->cooldown = 10;
  sfx_kick(vlen(vx, vy), q->x);
}
static void shoot(int i, int aim_y, int power, int vz) {
  Player *q = &p[i];
  int gx = goal_x(!q->team), dx = gx - q->x, vx = sgn(dx) * power;
  if (!vx) vx = attack_dir(q->team) * power;
  int frames = imax(1, absi(dx) / power);
  int vy = clamp((aim_y - q->y) / frames, -90, 90);
  release(i, vx, vy, vz, 0);
  ball.shot = 1;
}
static int best_pass(int i, int px, int py) {
  Player *q = &p[i];
  int t = q->team, dir = attack_dir(t), best = -1, bs = -(1 << 30);
  for (int j = t * SIDE; j < t * SIDE + SIDE; j++) {
    if (j == i) continue;
    int dx = p[j].x - q->x, dy = p[j].y - q->y, d = vlen(dx, dy);
    if (d < 30 * FP || d > 340 * FP) continue;
    int sc = dx * dir * 2 - d / 2;
    if (p[j].role == 0) sc -= 200 * FP;
    if (px || py) sc += (px * dx + py * dy) * 3 / imax(d / FP, 1) * FP / 8;
    for (int o = (!t) * SIDE; o < (!t) * SIDE + SIDE; o++) {
      int mx = q->x + dx / 2, my = q->y + dy / 2;
      if (dist(p[o].x, p[o].y, mx, my) < 22 * FP) sc -= 120 * FP;
      if (dist(p[o].x, p[o].y, p[j].x, p[j].y) < 16 * FP) sc -= 80 * FP;
    }
    if (sc > bs) { bs = sc; best = j; }
  }
  return best;
}
static void pass_to(int i, int j) {
  Player *q = &p[i];
  int tx = p[j].x + p[j].vx * 8, ty = p[j].y + p[j].vy * 8;
  int dx = tx - q->x, dy = ty - q->y, d = imax(vlen(dx, dy), FP);
  int lofted = d > 230 * FP, s = lofted ? clamp(d / 24, 60, 150) : clamp(d / 17, 55, 150);
  release(i, dx * s / d, dy * s / d, lofted ? 34 : 0, 0);
  if (!attract && p[j].role) controlled[p[j].team] = j;
}
static void accelerate(Player *q, int dvx, int dvy, int acc) {
  q->vx += clamp(dvx - q->vx, -acc, acc);
  q->vy += clamp(dvy - q->vy, -acc, acc);
}
static void steer(int i, int tx, int ty, int spd) {
  Player *q = &p[i];
  int dx = tx - q->x, dy = ty - q->y, d = vlen(dx, dy), dvx = 0, dvy = 0;
  if (d > 2 * FP) { dvx = dx * spd / d; dvy = dy * spd / d; }
  if (d < 16 * FP) { dvx = dvx * d / (16 * FP); dvy = dvy * d / (16 * FP); }
  accelerate(q, dvx, dvy, 7);
}
static int nearest(int team, int x, int y, int outfield, int except) {
  int best = -1, bd = 1 << 30;
  for (int i = team * SIDE; i < team * SIDE + SIDE; i++) {
    if (i == except || (outfield && p[i].role == 0)) continue;
    int d = dist(p[i].x, p[i].y, x, y);
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}
static void anchor(int i, int *tx, int *ty) {
  Player *q = &p[i];
  int t = q->team, r = q->role;
  int own = ball.owner >= 0 && p[ball.owner].team == t;
  int bpx = t ? PW - ball.x / FP : ball.x / FP;                   /* ball, in this team's frame */
  int x = FORM_X[r] + 30 + (bpx - 480) * 11 / 20 + (own ? 55 : 0);
  if (r == 5) x += 25;
  x = clamp(x, 40, 900);
  *tx = (t ? PW - x : x) * FP;
  *ty = clamp(FORM_Y[r] * FP + (ball.y - FH / 2) * 3 / 10, 8 * FP, FH - 8 * FP);
}
static void slide(int i, int ay) {
  Player *q = &p[i];
  q->action = ACT_SLIDE; q->timer = 14;
  q->vx = (q->face ? 1 : -1) * 82; q->vy = ay * 18;
}
static void steal(int i) {
  int o = ball.owner;
  if (o >= 0) { p[o].stun = 18; p[o].vx /= 3; p[o].vy /= 3; }
  take_ball(i);
  p[i].cooldown = 6;
  sfx_tackle(p[i].x);
}
static void ai_keeper(int i) {
  Player *q = &p[i];
  int t = q->team, gx = goal_x(t), out = attack_dir(t);
  if (ball.owner == i) {
    q->action = ACT_HOLD;
    accelerate(q, 0, 0, 6);
    if (++hold_timer[t] > 40) {
      hold_timer[t] = 0; q->action = ACT_NONE;
      int j = best_pass(i, out, 0);
      if (j >= 0) pass_to(i, j); else release(i, out * 140, (int)(rnd() % 61) - 30, 40, 0);
    }
    return;
  }
  if (q->action == ACT_DIVE) { q->vx = q->vx * 7 / 8; q->vy = q->vy * 7 / 8; return; }
  int bx = ball.x, by = ball.y, d = absi(bx - gx);
  int depth = clamp(d / 9, 6 * FP, 34 * FP);
  int tx = gx + out * depth, ty = FH / 2 + clamp((by - FH / 2) / 3, -(GOAL_HALF + 6) * FP, (GOAL_HALF + 6) * FP);
  if (ball.owner < 0 && ball.vx * out < -24 && d < 230 * FP) {
    int frames = imax(1, (q->x - ball.x) / (ball.vx ? ball.vx : 1));
    int py = ball.y + ball.vy * frames;
    if (frames > 0 && frames < 40) ty = clamp(py, FH / 2 - (GOAL_HALF + 10) * FP, FH / 2 + (GOAL_HALF + 10) * FP);
    if (frames > 0 && frames < 9 && absi(py - q->y) > 7 * FP && absi(py - q->y) < 34 * FP) {
      q->action = ACT_DIVE; q->timer = 18; q->vy = clamp((py - q->y) / imax(frames, 3), -70, 70); q->vx = 0;
      q->face = (uint8_t)(t == 0);
      return;
    }
  }
  int in_box = absi(bx - gx) < BOX_DEPTH * FP && absi(by - FH / 2) < BOX_HALF * FP;
  if (ball.owner < 0 && in_box && nearest(t, bx, by, 0, -1) == i && dist(q->x, q->y, bx, by) < 60 * FP) { tx = bx; ty = by; }
  steer(i, tx, ty, 46);
}
static void ai_carrier(int i) {
  Player *q = &p[i];
  int t = q->team, dir = attack_dir(t), gx = goal_x(!t), dgoal = absi(gx - q->x);
  int threat = -1, td = 1 << 30;
  for (int o = (!t) * SIDE; o < (!t) * SIDE + SIDE; o++) {
    int dx = (p[o].x - q->x) * dir, dy = p[o].y - q->y;
    if (dx > -6 * FP && dx < 60 * FP && absi(dy) < 26 * FP && dx + absi(dy) < td) { td = dx + absi(dy); threat = o; }
  }
  int ty = FH / 2 + (q->y - FH / 2) * 3 / 4;
  if (threat >= 0) ty = q->y + (p[threat].y > q->y ? -44 : 44) * FP;
  if (q->cooldown == 0) {
    int central = absi(q->y - FH / 2) < 64 * FP;
    if (central && ((dgoal < 150 * FP && rnd() % 4 == 0) || (dgoal < 210 * FP && rnd() % 14 == 0))) {
      int side = (rnd() & 1) ? 1 : -1;                       /* aim for a corner */
      int aim = FH / 2 + side * (GOAL_HALF - 3 - (int)(rnd() % 8)) * FP;
      shoot(i, aim, 152 + (int)(rnd() % 26), 8 + (int)(rnd() % 30));
      return;
    }
    if ((threat >= 0 && td < 18 * FP && rnd() % 10 == 0) || rnd() % 160 == 0) {
      int j = best_pass(i, 0, 0);
      if (j >= 0) { pass_to(i, j); return; }
    }
  }
  steer(i, q->x + dir * 90 * FP, ty, 50);
}
static int chaser[2], support[2];
static void ai_player(int i) {
  Player *q = &p[i];
  int t = q->team, tx, ty;
  if (q->role == 0) { ai_keeper(i); return; }
  if (ball.owner == i) { ai_carrier(i); return; }
  int own = ball.owner >= 0 && p[ball.owner].team == t;
  if (!own && i == chaser[t]) {
    tx = ball.x + ball.vx * 5; ty = ball.y + ball.vy * 5;
    if (ball.owner >= 0 && q->cooldown == 0 && dist(q->x, q->y, ball.x, ball.y) < 8 * FP) {
      int human = !attract && ball.owner == controlled[p[ball.owner].team] && (p[ball.owner].team == 0 || peer_seen);
      if (rnd() % (human ? 12 : 10) == 0) { steal(i); return; }
      q->cooldown = 20;
    }
    if (ball.owner >= 0 && q->cooldown == 0 && dist(q->x, q->y, ball.x, ball.y) < 22 * FP && rnd() % 45 == 0 &&
        sgn(ball.x - q->x) == (q->face ? 1 : -1)) { slide(i, sgn(ball.y - q->y)); return; }
    steer(i, tx, ty, ball.owner >= 0 ? 49 : 54);          /* a dribbler can outpace a chaser from behind */
    return;
  }
  if (!own && i == support[t]) {
    int gx = goal_x(t);
    tx = ball.x + (gx - ball.x) / 4; ty = ball.y + (FH / 2 - ball.y) / 4;
    steer(i, tx, ty, 50);
    return;
  }
  anchor(i, &tx, &ty);
  steer(i, tx, ty, own ? 50 : 44);
}

static int human_team(int t) { return !attract && (t == 0 || peer_seen); }
static void human(int t, uint32_t in, uint32_t pressed) {
  int i = controlled[t];
  if (ball.owner != i) {
    int tx = ball.x + ball.vx * 4, ty = ball.y + ball.vy * 4;
    int best = nearest(t, tx, ty, 1, -1);
    int own_flight = ball.owner < 0 && ball.last_team == t && ball.last_player != i && p[i].role;
    if (best >= 0 && best != i && !own_flight &&
        ((pressed & PRG32_BTN_B) || dist(p[best].x, p[best].y, tx, ty) + 14 * FP < dist(p[i].x, p[i].y, tx, ty)))
      controlled[t] = i = best;
  }
  Player *q = &p[i];
  int ax = ((in & PRG32_BTN_RIGHT) != 0) - ((in & PRG32_BTN_LEFT) != 0);
  int ay = ((in & PRG32_BTN_DOWN) != 0) - ((in & PRG32_BTN_UP) != 0);
  if (aftertouch && ball.owner < 0 && ball.last_player == i) { ball.spin += ay * 7; ball.vz += (ax * attack_dir(t) < 0) ? 1 : 0; aftertouch--; }
  if (q->action == ACT_SLIDE || q->stun) return;
  int spd = ball.owner == i ? 50 : 58, k = ax && ay ? 181 : 256;
  accelerate(q, ax * spd * k / 256, ay * spd * k / 256, 10);
  if (ball.owner == i) {
    if (pressed & PRG32_BTN_A) {
      int dir = ax ? ax : (q->face ? 1 : -1);
      if (dir == attack_dir(t)) {
        int aim = FH / 2 + (ay ? ay * (GOAL_HALF - 7) : (q->y < FH / 2 ? 9 : -9)) * FP;
        shoot(i, aim, 168, 24);
      } else release(i, dir * 150, ay * 50, 40, 0);
      aftertouch = 14;
    } else if (pressed & PRG32_BTN_B) {
      int j = best_pass(i, ax ? ax : (q->face ? 1 : -1), ay);
      if (j >= 0) pass_to(i, j); else release(i, (ax ? ax : (q->face ? 1 : -1)) * 110, ay * 60, 0, 0);
    }
  } else if ((pressed & PRG32_BTN_A) && q->cooldown == 0) slide(i, ay);
}

static void move_players(void) {
  for (int i = 0; i < N; i++) {
    Player *q = &p[i];
    if (q->stun) { q->stun--; q->vx = q->vx * 3 / 4; q->vy = q->vy * 3 / 4; }
    if (q->action == ACT_SLIDE) { q->vx = q->vx * 15 / 16; q->vy = q->vy * 15 / 16; }
    q->x = clamp(q->x + q->vx, -14 * FP, FW + 14 * FP);
    q->y = clamp(q->y + q->vy, -6 * FP, FH + 6 * FP);
    if (q->action != ACT_SLIDE && q->action != ACT_DIVE && absi(q->vx) > 5) q->face = q->vx > 0;
    q->stride += vlen(q->vx, q->vy);
    if (q->cooldown) q->cooldown--;
    if (q->timer && !--q->timer && q->action != ACT_HOLD && q->action != ACT_CHEER) q->action = ACT_NONE;
  }
  for (int i = 0; i < N; i++)
    for (int j = i + 1; j < N; j++) {
      int dx = p[j].x - p[i].x, dy = p[j].y - p[i].y;
      if (absi(dx) < 7 * FP && absi(dy) < 4 * FP) {
        int px = (dx >= 0 ? 1 : -1) * 10, py = (dy >= 0 ? 1 : -1) * 6;
        p[i].x -= px; p[j].x += px; p[i].y -= py; p[j].y += py;
      }
    }
}

static void start_restart(int kind, int team, int bx, int by) {
  phase = PH_RESTART; phase_timer = 0; restart_kind = kind; restart_team = team;
  reset_ball(bx, by);
  restart_taker = kind == RS_GOALKICK ? team * SIDE : nearest(team, bx, by, 1, -1);
  if (!attract && kind != RS_GOALKICK) controlled[team] = restart_taker;
  banner = kind == RS_GOALKICK ? "GOAL KICK" : kind == RS_CORNER ? "CORNER" : "THROW IN";
  banner_timer = 40;
}
static void goal_scored(int scorer) {
  score[scorer]++;
  phase = PH_GOAL; phase_timer = 0; celebrate = 80; net_shake = 40;
  banner = "GOAL!"; banner_timer = 80;
  sfx_roar(); sfx_net(ball.x); sfx_whistle(1); whistle_wait = 20;
  for (int i = scorer * SIDE; i < scorer * SIDE + SIDE; i++) { p[i].action = ACT_CHEER; p[i].timer = 0; }
}
static void pickups(void) {
  if (ball.owner >= 0) {
    Player *q = &p[ball.owner];
    int off = 6 + ((q->stride >> 6) & 3);
    if (q->action == ACT_HOLD) { ball.x = q->x + (q->face ? 3 : -3) * FP; ball.y = q->y; ball.z = 12 * FP; }
    else {
      ball.x = q->x + (q->face ? off : -off) * FP; ball.y = q->y + FP; ball.z = 0;
      int before = q->stride - vlen(q->vx, q->vy);
      if (vlen(q->vx, q->vy) > 20 && (before >> 8) != (q->stride >> 8)) sfx_touch(q->x);
    }
    ball.vx = q->vx; ball.vy = q->vy;                      /* carried ball moves with its owner */
    ball.roll += vlen(q->vx, q->vy);
    return;
  }
  int speed = vlen(ball.vx, ball.vy), best = -1, bd = 1 << 30;
  for (int i = 0; i < N; i++) {
    Player *q = &p[i];
    if (q->stun || q->cooldown || q->action == ACT_CHEER) continue;
    int keeper = q->role == 0, d = dist(q->x, q->y, ball.x, ball.y);
    int reach = keeper ? (q->action == ACT_DIVE ? 15 : 9) : 8, zmax = keeper ? 24 : 12;
    if (keeper && absi(q->x - goal_x(q->team)) > (BOX_DEPTH + 4) * FP) { reach = 8; zmax = 12; }
    if (d < reach * FP && ball.z < zmax * FP && d < bd) { bd = d; best = i; }
  }
  if (best < 0) return;
  Player *q = &p[best];
  if (q->action == ACT_SLIDE) {                          /* slide tackles knock the ball on */
    ball.vx = q->vx * 3 / 2; ball.vy = q->vy + (int)(rnd() % 21) - 10; ball.vz = 8;
    ball.last_team = q->team; ball.last_player = best; q->cooldown = 10;
    sfx_tackle(q->x);
    return;
  }
  if (q->role == 0) {
    int shot = ball.shot && ball.last_team != q->team;
    /* A hard, well placed shot can beat the keeper outright. */
    if (shot && speed > 100 && (int)(rnd() % 100) < 42 + absi(ball.y - q->y) * 5 / FP) { q->cooldown = 10; return; }
    if (shot && speed > 150 && rnd() % 3 == 0) {         /* parry */
      ball.vx = -ball.vx / 3; ball.vy = (int)(rnd() % 81) - 40; ball.vz = 22; ball.shot = 0;
      ball.last_team = q->team; ball.last_player = best; q->cooldown = 12;
      sfx_catch(q->x); sfx_ooh();
      return;
    }
    take_ball(best); hold_timer[q->team] = 0; q->action = ACT_HOLD; q->timer = 0;
    sfx_catch(q->x);
    if (shot) sfx_ooh();
    return;
  }
  if (ball.shot && ball.last_team != q->team && rnd() % 4) { q->cooldown = 8; return; }  /* block missed */
  if (speed > 135 && ball.z > 5 * FP) {                  /* too hot to trap: deflect */
    ball.vx = -ball.vx / 4; ball.vy /= 2; ball.vz = 12; ball.last_team = q->team; ball.last_player = best;
    q->cooldown = 8; sfx_bounce(20, ball.x);
    return;
  }
  if (ball.owner < 0 && ball.last_team != q->team && ball.last_player >= 0) q->cooldown = 0;
  take_ball(best);
}
static void ball_physics(void) {
  if (ball.owner >= 0) return;
  ball.x += ball.vx; ball.y += ball.vy; ball.z += ball.vz;
  ball.vy += ball.spin / 16; ball.spin = ball.spin * 15 / 16;
  if (ball.z > 0) { ball.vz -= 4; ball.vx = ball.vx * 255 / 256; ball.vy = ball.vy * 255 / 256; }
  else {
    ball.z = 0;
    if (ball.vz < -12) { sfx_bounce(-ball.vz, ball.x); ball.vz = -ball.vz / 2; ball.vx = ball.vx * 7 / 8; ball.vy = ball.vy * 7 / 8; }
    else { ball.vz = 0; ball.vx = ball.vx * 61 / 64; ball.vy = ball.vy * 61 / 64; ball.shot = ball.shot && vlen(ball.vx, ball.vy) > 60; }
  }
  ball.roll += vlen(ball.vx, ball.vy);
}
static void ball_rules(void) {
  if (phase == PH_GOAL) {                                 /* ball rests inside the net */
    int side = ball.x > FW / 2, back = side ? FW + (NET_DEPTH - 3) * FP : -(NET_DEPTH - 3) * FP;
    if ((side && ball.x > back) || (!side && ball.x < back)) { ball.x = back; ball.vx = -ball.vx / 5; }
    ball.y = clamp(ball.y, FH / 2 - (GOAL_HALF - 3) * FP, FH / 2 + (GOAL_HALF - 3) * FP);
    if (ball.z > (GOAL_HIGH - 4) * FP) { ball.z = (GOAL_HIGH - 4) * FP; ball.vz = -2; }
    return;
  }
  if (ball.owner >= 0 || phase != PH_PLAY) return;
  if (ball.x < 0 || ball.x > FW) {
    int side = ball.x > FW, line = side ? FW : 0, gy = absi(ball.y - FH / 2);
    int scorer = side ? 0 : 1;
    if (gy < GOAL_HALF * FP && ball.z < GOAL_HIGH * FP) { goal_scored(scorer); return; }
    if (absi(gy - GOAL_HALF * FP) < 3 * FP && ball.z < (GOAL_HIGH + 2) * FP) {
      ball.x = line + (side ? -FP : FP); ball.vx = -ball.vx * 2 / 5; ball.vy += sgn(ball.y - FH / 2) * 24;
      sfx_post(ball.x); sfx_ooh(); ball.shot = 0; return;
    }
    if (gy < GOAL_HALF * FP && ball.z < (GOAL_HIGH + 4) * FP) {
      ball.x = line + (side ? -FP : FP); ball.vx = -ball.vx / 3; ball.vz = -absi(ball.vz) / 2 - 4;
      sfx_post(ball.x); sfx_ooh(); ball.shot = 0; return;
    }
    if (ball.shot && gy < (GOAL_HALF + 30) * FP) sfx_ooh();
    int defenders = side ? 1 : 0;
    if (ball.last_team == scorer)
      start_restart(RS_GOALKICK, defenders, side ? FW - SIX_DEPTH * FP : SIX_DEPTH * FP, FH / 2 + sgn(ball.y - FH / 2) * 18 * FP);
    else
      start_restart(RS_CORNER, scorer, side ? FW - 2 * FP : 2 * FP, ball.y < FH / 2 ? 2 * FP : FH - 2 * FP);
    return;
  }
  if (ball.y < 0 || ball.y > FH) {
    int team = !ball.last_team;
    start_restart(RS_THROW, team, clamp(ball.x, 4 * FP, FW - 4 * FP), ball.y < 0 ? FP : FH - FP);
  }
}

static void team_roles(void) {
  for (int t = 0; t < 2; t++) {
    int tx = ball.x + ball.vx * 4, ty = ball.y + ball.vy * 4;
    chaser[t] = nearest(t, tx, ty, 1, -1);
    support[t] = nearest(t, tx, ty, 1, chaser[t]);
    if (human_team(t) && chaser[t] == controlled[t]) chaser[t] = -1;
  }
}
static uint32_t peer_input(void) {
  peer_seen = 0;
  if (!networked) return 0;
  prg32_player_state_t peer;
  int count = prg32_multiplayer_get_peer_count();
  for (int i = 0; i < count; i++)
    if (prg32_multiplayer_get_peer(i, &peer) == 0 && peer.player_id != prg32_multiplayer_local_player_id()) {
      peer_seen = 1;
      return peer.input;
    }
  return 0;
}
static void go_screen(int s) {
  screen = s; menu_lock = 10; screen_timer = 0;
  if (s == SCREEN_TITLE) { frame = 0; music_on(0); }
  else if (s == SCREEN_RESULT) music_on(1);
  else music_off();
}

static void match_update(uint32_t local, uint32_t pressed, uint32_t remote) {
  uint32_t rpressed = remote & ~last_remote;
  last_remote = remote;
  if (attract && (pressed || local)) { go_screen(SCREEN_TITLE); return; }
  if (pressed & PRG32_BTN_SELECT) { go_screen(SCREEN_TITLE); return; }
  phase_timer++;
  if (banner_timer) banner_timer--;
  if (celebrate) celebrate--;
  if (net_shake) net_shake--;
  team_roles();
  switch (phase) {
    case PH_KICKOFF:
      if (phase_timer == 30) {
        sfx_whistle(1);
        int taker = kickoff_team * SIDE + 5;
        take_ball(taker);
        controlled[kickoff_team] = taker;
        phase = PH_PLAY; phase_timer = 0;
      }
      break;
    case PH_GOAL:
      for (int i = 0; i < N; i++) if (p[i].action != ACT_CHEER) steer(i, p[i].x, p[i].y, 0);
      if (phase_timer > 85) {
        int conceded = ball.x > FW / 2 ? 1 : 0;
        for (int i = 0; i < N; i++) p[i].action = ACT_NONE;
        setup_kickoff(conceded);
      }
      break;
    case PH_FULLTIME:
      for (int i = 0; i < N; i++) steer(i, p[i].x, p[i].y, 0);
      if (phase_timer > 80) {
        if (attract) { go_screen(SCREEN_TITLE); return; }
        if (score[0] > score[1]) wins++;
        go_screen(SCREEN_RESULT);
        return;
      }
      break;
    case PH_RESTART: {
      Player *tk = &p[restart_taker];
      for (int i = 0; i < N; i++) {
        if (i == restart_taker) continue;
        if (human_team(p[i].team) && i == controlled[p[i].team]) { human(p[i].team, p[i].team ? remote : local, 0); continue; }
        ai_player(i);
        if (p[i].team != restart_team && dist(p[i].x, p[i].y, ball.x, ball.y) < 30 * FP)
          steer(i, p[i].x + sgn(p[i].x - ball.x) * 40 * FP, p[i].y + sgn(p[i].y - ball.y) * 40 * FP, 50);
      }
      int behind = restart_kind == RS_THROW ? 0 : -attack_dir(restart_team) * 2 * FP;
      steer(restart_taker, ball.x + behind, ball.y, 56);
      if ((dist(tk->x, tk->y, ball.x, ball.y) < 8 * FP && phase_timer > 25) || phase_timer > 90) {
        take_ball(restart_taker);
        if (restart_taker % SIDE == 0) { tk->action = ACT_HOLD; hold_timer[restart_team] = 20; }
        phase = PH_PLAY; phase_timer = 0;
        if (!human_team(restart_team) || restart_kind == RS_GOALKICK) tk->cooldown = 0;
      }
      clock_frames++;
      break;
    }
    case PH_PLAY:
      if (!attract) human(0, local, pressed);
      if (human_team(1)) human(1, remote, rpressed);
      for (int i = 0; i < N; i++) {
        if (human_team(p[i].team) && i == controlled[p[i].team] && p[i].role) continue;
        if (p[i].action == ACT_CHEER) continue;
        ai_player(i);
      }
      if (++clock_frames >= (attract ? DEMO_SECONDS : MATCH_SECONDS) * FPS) {
        phase = PH_FULLTIME; phase_timer = 0; sfx_whistle(3);
        banner = "FULL TIME"; banner_timer = 80;
      }
      break;
  }
  move_players();
  if (phase == PH_RESTART || phase == PH_KICKOFF) {
    if (ball.owner < 0) { ball.vx = ball.vy = ball.vz = ball.spin = 0; }   /* dead ball waits for its taker */
  } else {
    ball_physics();
    pickups();
  }
  ball_rules();
  int lead = ball.owner >= 0 ? p[ball.owner].vx * 10 : ball.vx * 6;
  int target = clamp((ball.x + lead) / FP - W / 2, CAM_MIN, CAM_MAX), dc = target - camera_x;
  camera_x += dc / 6 + (dc > 0 && dc < 6) - (dc < 0 && dc > -6);
}

void sensational_soccer_init(void) {
  make_palette();
  build_graphics();
  frame = last_input = last_remote = 0;
  score[0] = score[1] = clock_frames = 0;
  attract = mode = round_no = wins = celebrate = 0;
  whistle_left = 0;
  setup_kickoff(0);
  camera_x = (PW - W) / 2;
  /* Regular publication volume; use 0 for Central European night tests. */
  prg32_audio_set_master_volume(160);
  prg32_multiplayer_init();
  networked = prg32_multiplayer_available() && prg32_multiplayer_join(ROOM, PRG32_MP_FLAG_ENABLE) == 0;
  prg32_band_set_game_info("SENSATIONAL SOCCER  A SHOOT/SLIDE  B PASS/SWITCH");
  go_screen(SCREEN_TITLE);
}

void sensational_soccer_update(void) {
  uint32_t local = prg32_input_read(), pressed = local & ~last_input, remote = peer_input();
  last_input = local;
  frame++; screen_timer++;
  sfx_update();
  if (networked) {
    prg32_multiplayer_set_input(local);
    prg32_multiplayer_set_local_state((int16_t)(p[controlled[0]].x / FP), (int16_t)(p[controlled[0]].y / FP), 0, (uint16_t)score[0]);
    prg32_multiplayer_tick();
  }
  if (screen == SCREEN_MATCH) {
    /* Gameplay is effects-only: keep the tracker stopped every frame. */
    prg32_audio_stop_track();
    match_update(local, pressed, remote);
    return;
  }
  if (menu_lock) { menu_lock--; return; }
  switch (screen) {
    case SCREEN_TITLE:
      if (pressed & (PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_SELECT)) { go_screen(SCREEN_TEAM); sfx_blip(1); }
      else if (screen_timer > FPS * 10) { go_screen(SCREEN_MATCH); new_match(1); }
      break;
    case SCREEN_TEAM:
      if (pressed & (PRG32_BTN_LEFT | PRG32_BTN_RIGHT)) {
        int d = (pressed & PRG32_BTN_RIGHT) ? 1 : 3;
        do chosen[0] = (chosen[0] + d) % 4; while (chosen[0] == chosen[1]);
        sfx_blip(0); apply_kits();
      }
      if (pressed & (PRG32_BTN_UP | PRG32_BTN_DOWN)) {
        int d = (pressed & PRG32_BTN_DOWN) ? 1 : 3;
        do chosen[1] = (chosen[1] + d) % 4; while (chosen[1] == chosen[0]);
        sfx_blip(0); apply_kits();
      }
      if (pressed & PRG32_BTN_A) { go_screen(SCREEN_MODE); sfx_blip(1); }
      else if (pressed & PRG32_BTN_B) go_screen(SCREEN_TITLE);
      break;
    case SCREEN_MODE:
      if (pressed & (PRG32_BTN_UP | PRG32_BTN_DOWN)) { mode = !mode; sfx_blip(0); }
      if (pressed & PRG32_BTN_A) { round_no = wins = 0; go_screen(SCREEN_MATCH); new_match(0); sfx_blip(1); }
      else if (pressed & PRG32_BTN_B) { go_screen(SCREEN_TEAM); sfx_blip(0); }
      break;
    case SCREEN_RESULT:
      if (pressed & PRG32_BTN_A) {
        sfx_blip(1);
        if (mode && round_no < 2) {
          round_no++;
          do chosen[1] = (chosen[1] + 1) % 4; while (chosen[1] == chosen[0]);
          go_screen(SCREEN_MATCH); new_match(0);
        } else go_screen(SCREEN_TITLE);
      }
      break;
  }
}

/* -------------------------------------------------------------- rendering */
static int sx(int wx) { return wx / FP - camera_x; }
static int sy(int wy) { return TOP + wy / FP; }
static const int8_t SINE[32] = {0, 1, 2, 2, 3, 3, 3, 3, 3, 3, 3, 2, 2, 1, 0, 0, 0, -1, -2, -2, -3, -3, -3, -3, -3, -3, -3, -2, -2, -1, 0, 0};

static void draw_stands(int scroll, int y, int fr) {
  int off = ((scroll % CROWD_W) + CROWD_W) % CROWD_W;
  prg32_sprite_draw_indexed(-off, y, &crowd_spr, (uint32_t)fr);
  prg32_sprite_draw_indexed(CROWD_W - off, y, &crowd_spr, (uint32_t)fr);
}
static void draw_grass(int cam, int y0, int y1) {
  int first = (cam + 48) / 64 - 1;
  for (int s = first; s < first + 7; s++) {
    int x = s * 64 - 48 - cam;
    for (int b = 0, y = y0; y < y1; b++, y += 36) {
      int alt = (s + 16) & 1;
      box(x, y, 64, imin(36, y1 - y), alt ? ((b & 1) ? P_G0 : P_G1) : ((b & 1) ? P_G2 : P_G3));
    }
  }
}
static void hline(int x0, int x1, int wy, uint8_t c) { int a = sx(x0 * FP), b = sx(x1 * FP); box(imin(a, b), sy(wy * FP), absi(b - a) + 1, 1, c); }
static void vline(int wx, int y0, int y1, uint8_t c) { box(sx(wx * FP) - 1, sy(y0 * FP), 2, y1 - y0 + 1, c); }
static void arc(int cx, int cy, int r, int xmin, int xmax, uint8_t c) {
  int prev = -1;
  for (int dy = -r; dy <= r; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
    int lo = prev < 0 ? dx : imin(dx, prev), hi = prev < 0 ? dx : imax(dx, prev);
    for (int s = -1; s <= 1; s += 2) {
      int a = cx + s * hi, b = cx + s * lo;
      int x0 = imin(a, b), x1 = imax(a, b);
      if (x1 < xmin || x0 > xmax) continue;
      x0 = imax(x0, xmin); x1 = imin(x1, xmax);
      box(sx(x0 * FP), sy((cy + dy) * FP), x1 - x0 + 1, 1, c);
    }
    prev = dx;
  }
}
static void draw_markings(void) {
  uint8_t c = P_LINE;
  hline(0, PW, 0, c); hline(0, PW, PH, c);
  vline(0, 0, PH, c); vline(PW, 0, PH, c); vline(PW / 2, 0, PH, c);
  arc(PW / 2, PH / 2, CIRCLE_R, -9999, 9999, c);
  box(sx(FW / 2) - 1, sy(FH / 2) - 1, 3, 3, c);
  for (int s = 0; s < 2; s++) {
    int g = s ? PW : 0, d = s ? -1 : 1;
    hline(g, g + d * BOX_DEPTH, PH / 2 - BOX_HALF, c); hline(g, g + d * BOX_DEPTH, PH / 2 + BOX_HALF, c);
    vline(g + d * BOX_DEPTH, PH / 2 - BOX_HALF, PH / 2 + BOX_HALF, c);
    hline(g, g + d * SIX_DEPTH, PH / 2 - SIX_HALF, c); hline(g, g + d * SIX_DEPTH, PH / 2 + SIX_HALF, c);
    vline(g + d * SIX_DEPTH, PH / 2 - SIX_HALF, PH / 2 + SIX_HALF, c);
    box(sx((g + d * SPOT_X) * FP) - 1, sy(FH / 2), 2, 2, c);
    if (s) arc(PW - SPOT_X, PH / 2, 28, -9999, PW - BOX_DEPTH - 1, c);
    else arc(SPOT_X, PH / 2, 28, BOX_DEPTH + 1, 9999, c);
  }
}
/* Goal frame with oblique depth: tops of posts lean 4 px outward. */
static void draw_goal_back(int side) {
  int x0 = sx(side ? FW : 0), out = side ? 1 : -1, yf = sy(FH / 2 - GOAL_HALF * FP), yn = sy(FH / 2 + GOAL_HALF * FP);
  if (x0 < -40 || x0 > W + 40) return;
  int shake = net_shake && ((frame >> 1) & 1) && (ball.x > FW / 2) == side ? out : 0;
  for (int d = 0; d <= NET_DEPTH; d += 3) {                 /* vertical strands */
    int top = yf - GOAL_HIGH + 4 + d * 8 / NET_DEPTH;
    box(x0 + out * (4 + d) + shake * (d > 6), top, 1, yn - top, P_NET);
  }
  for (int y = yf - GOAL_HIGH + 4; y <= yn; y += 4) {       /* horizontal strands */
    int dmax = NET_DEPTH;
    while (dmax > 0 && yf - GOAL_HIGH + 4 + dmax * 8 / NET_DEPTH > y) dmax--;
    int a = x0 + out * 4, b = x0 + out * (4 + dmax);
    box(imin(a, b), y, absi(b - a) + 1, 1, P_NET);
  }
  for (int k = 0; k < GOAL_HIGH; k++) box(x0 + out * (k * 4 / GOAL_HIGH), yf - k, 2, 1, WHITE);  /* far post */
  box(x0 + out * 4 - (side ? 0 : 1), yf - GOAL_HIGH, 2, yn - yf + 1, WHITE);                    /* crossbar */
}
static void draw_goal_front(int side) {
  int x0 = sx(side ? FW : 0), out = side ? 1 : -1, yn = sy(FH / 2 + GOAL_HALF * FP);
  if (x0 < -40 || x0 > W + 40) return;
  for (int k = 0; k < GOAL_HIGH; k++) box(x0 + out * (k * 4 / GOAL_HIGH), yn - k, 2, 1, WHITE);
  box(x0 - 1, yn, 3, 1, GREY);
}
static void shadow(int x, int y, int w) { box(x - w / 2 + 1, y - 1, w - 2, 1, P_SHADOW); box(x - w / 2, y, w, 2, P_SHADOW); }
static void draw_player(int i) {
  Player *q = &p[i];
  int x = sx(q->x), y = sy(q->y), dx = 0, dz = 0, f = F_STAND;
  if (x < -20 || x > W + 20) return;
  int moving = vlen(q->vx, q->vy) > 8;
  if (q->action == ACT_SLIDE) f = F_SLIDE;
  else if (q->action == ACT_KICK) f = F_KICK;
  else if (q->action == ACT_CHEER) { f = F_CHEER; dz = ((frame + i * 3) >> 2) & 1 ? 3 : 0; }
  else if (q->action == ACT_DIVE) { f = F_CHEER; dz = 5; dx = q->vy > 0 ? 1 : -1; }
  else if (moving && !q->stun) f = F_RUN0 + ((q->stride >> 7) & 3);
  shadow(x, y, q->action == ACT_SLIDE ? 16 : 12);
  int kit = q->role == 0 ? (q->team ? KIT_AKEEP : KIT_HKEEP) : (q->team ? KIT_AWAY : KIT_HOME);
  if (q->stun && (frame & 2)) return;
  prg32_sprite_draw_indexed(x - 8 + dx, y - 23 - dz, &spr[kit][q->skin][q->face ? 0 : 1], (uint32_t)f);
  if (!attract && human_team(q->team) && i == controlled[q->team]) {
    uint8_t c = q->team ? CI(5, 2, 1) : CI(2, 4, 5);
    int top = y - 31 - dz;
    box(x - 3, top, 7, 1, c); box(x - 2, top + 1, 5, 1, c); box(x - 1, top + 2, 3, 1, c); box(x, top + 3, 1, 1, c);
  }
}
static void draw_ball(void) {
  int x = sx(ball.x), g = sy(ball.y), h = ball.z / FP;
  if (x < -8 || x > W + 8) return;
  int w = clamp(7 - h / 6, 3, 7);
  box(x - w / 2, g + 1, w, 2, P_SHADOW);
  prg32_sprite_draw_indexed(x - 3, g - 5 - h, &ball_spr, (uint32_t)((ball.roll >> 8) & 3));
}
static void itoa_s(int v, char *out) {
  char t[8]; int n = 0;
  if (v <= 0) t[n++] = '0';
  while (v > 0) { t[n++] = (char)('0' + v % 10); v /= 10; }
  for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
  out[n] = 0;
}
static void draw_hud(void) {
  box(0, 0, W, 12, HUDC);
  box(3, 2, 8, 8, team_kit[chosen[0]].shirt); box(3, 6, 8, 4, team_kit[chosen[0]].shorts);
  text(14, 2, team_name[chosen[0]], WHITE, HUDC);
  char s[12]; itoa_s(score[0], s);
  text(132, 2, s, YELLOW, HUDC); text(148, 2, "-", WHITE, HUDC); itoa_s(score[1], s); text(164, 2, s, YELLOW, HUDC);
  int nl = slen(team_name[chosen[1]]);
  text(204 - nl * 8 + 48, 2, team_name[chosen[1]], WHITE, HUDC);
  box(256, 2, 8, 8, team_kit[chosen[1]].shirt); box(256, 6, 8, 4, team_kit[chosen[1]].shorts);
  int limit = attract ? DEMO_SECONDS : MATCH_SECONDS, minute = clock_frames * 90 / (limit * FPS);
  itoa_s(imin(minute, 90), s); int l = slen(s); s[l] = '\''; s[l + 1] = 0;
  text(316 - (l + 1) * 8, 2, s, WHITE, HUDC);
  if (peer_seen) text(270, 2, "NET", CI(2, 5, 2), HUDC);
}
static void draw_bottom(void) {
  box(0, 192, W, 8, HUDC);
  if (attract) text(4, 192, (frame >> 4) & 1 ? "DEMO  PRESS ANY BUTTON" : "DEMO", YELLOW, HUDC);
  else text(4, 192, ball.owner == controlled[0] ? "A SHOOT  B PASS" : "A SLIDE  B SWITCH", GREY, HUDC);
  int rx = 252, ry = 192;
  box(rx - 1, ry, 62, 8, P_RADAR);
  box(rx - 1 + 30, ry, 1, 8, P_RADAR2);
  box(rx - 1 + (camera_x + 48) * 62 / 1056, ry, 20, 1, P_RADAR2);
  for (int i = 0; i < N; i++) box(rx + clamp(p[i].x / FP / 16, 0, 59), ry + clamp(p[i].y / FP / 17, 0, 7), 2, 1, team_kit[chosen[p[i].team]].shirt);
  box(rx + clamp(ball.x / FP / 16, 0, 59), ry + clamp(ball.y / FP / 17, 0, 7), 1, 1, WHITE);
}
static void draw_banner(void) {
  if (!banner_timer || !banner) return;
  int goal = banner[0] == 'G' && banner[4] == '!';
  int sc = goal ? 3 : 2, w = ftext_w(banner, sc), x = (W - w) / 2, y = goal ? 74 : 80;
  static const uint8_t grad[7] = {P_LOGO0, P_LOGO0, P_LOGO1, P_LOGO1, P_LOGO2, P_LOGO2, P_LOGO3};
  box(0, y - 4, W, 7 * sc + 8, P_DARK);
  box(0, y - 5, W, 1, P_EDGE); box(0, y + 7 * sc + 4, W, 1, P_EDGE);
  if (goal && (frame & 4)) ftext_shadow(x, y, banner, sc, 0, P_FLASH, BLACK);
  else ftext_shadow(x, y, banner, sc, grad, 0, BLACK);
}
static void draw_match(void) {
  int fr = celebrate ? (((frame >> 2) & 1) ? 2 : 1) : (((frame >> 5) % 7) == 3 ? 1 : 0);
  draw_stands(camera_x / 2, 12, fr);
  prg32_sprite_draw_indexed(-48 - camera_x, 40, &board_spr, 0);
  draw_grass(camera_x, 48, 192);
  draw_markings();
  /* depth-sorted scene: players, ball, goal frames */
  int key[N + 5], id[N + 5], n = 0;
  for (int i = 0; i < N; i++) { key[n] = p[i].y; id[n++] = i; }
  key[n] = ball.y + 1; id[n++] = 100;
  key[n] = FH / 2 - GOAL_HALF * FP - 1; id[n++] = 200;
  key[n] = FH / 2 - GOAL_HALF * FP - 1; id[n++] = 201;
  key[n] = FH / 2 + GOAL_HALF * FP; id[n++] = 300;
  key[n] = FH / 2 + GOAL_HALF * FP; id[n++] = 301;
  for (int a = 1; a < n; a++)
    for (int b = a; b > 0 && key[b - 1] > key[b]; b--) {
      int t = key[b]; key[b] = key[b - 1]; key[b - 1] = t; t = id[b]; id[b] = id[b - 1]; id[b - 1] = t;
    }
  for (int k = 0; k < n; k++) {
    int v = id[k];
    if (v < N) draw_player(v);
    else if (v == 100) draw_ball();
    else if (v < 300) draw_goal_back(v - 200);
    else draw_goal_front(v - 300);
  }
  draw_hud();
  draw_bottom();
  draw_banner();
}

/* Title / menu backdrop: night sky, floodlights, crowd, boards and pitch. */
static int title_runner;
static void draw_backdrop(int dim) {
  static const uint8_t sky[4] = {P_SKY0, P_SKY1, P_SKY2, P_SKY3};
  for (int b = 0; b < 4; b++) box(0, b * 14, W, 14, sky[b]);
  for (int s = 0; s < 24; s++) {
    uint32_t h = hash((uint32_t)s, 77);
    if (((frame >> 3) + s) % 11) box((int)(h % W), (int)((h >> 9) % 50), 1, 1, P_GLOW);
  }
  for (int l = 0; l < 2; l++) {
    int x = l ? 278 : 22;
    box(x + 7, 18, 3, 40, P_POLE);
    box(x, 8, 17, 11, P_POLE);
    for (int r = 0; r < 2; r++) for (int c = 0; c < 4; c++) box(x + 1 + c * 4, 9 + r * 5, 3, 4, P_GLOW);
  }
  draw_stands(frame / 2, 56, (frame >> 4) % 5 == 2 ? 1 : 0);
  prg32_sprite_draw_indexed(-(int)((frame / 2) % 700), 84, &board_spr, 0);
  draw_grass((int)(frame / 2) % 128, 92, 200);
  box(0, 92, W, 1, P_LINE);
  if (!dim) {
    title_runner = (int)((frame * 3) % 520) - 60;
    int rx = title_runner, ry = 168, ph = (int)(frame >> 2) & 3;
    shadow(rx + 8, ry + 24, 12);
    prg32_sprite_draw_indexed(rx, ry, &spr[KIT_HOME][1][0], (uint32_t)(F_RUN0 + ph));
    int bx = rx + 15 + (ph & 1), bz = absi(SINE[(frame * 2) & 31]);
    box(bx - 1, ry + 25, 6, 2, P_SHADOW);
    prg32_sprite_draw_indexed(bx, ry + 19 - bz, &ball_spr, (uint32_t)(frame & 3));
    int cx = rx - 46, cp = (int)((frame + 2) >> 2) & 3;
    shadow(cx + 8, ry + 24, 12);
    prg32_sprite_draw_indexed(cx, ry, &spr[KIT_AWAY][2][0], (uint32_t)(F_RUN0 + cp));
  }
}
static void panel(int x, int y, int w, int h) {
  box(x, y, w, h, CI(0, 0, 1)); box(x, y, w, 1, P_EDGE); box(x, y + h - 1, w, 1, P_EDGE);
  box(x, y, 1, h, P_EDGE); box(x + w - 1, y, 1, h, P_EDGE);
}
static void draw_logo(int y) {
  static const uint8_t grad[7] = {P_LOGO0, P_LOGO0, P_LOGO1, P_LOGO1, P_LOGO2, P_LOGO2, P_LOGO3};
  const char *a = "SENSATIONAL", *b = "SOCCER";
  int x = (W - ftext_w(a, 3)) / 2;
  for (int i = 0; a[i]; i++) {
    char one[2] = {a[i], 0};
    ftext_shadow(x + i * 18, y + SINE[(frame + i * 3) & 31], one, 3, 0, WHITE, P_DARK);
  }
  x = (W - ftext_w(b, 5)) / 2;
  for (int i = 0; b[i]; i++) {
    char one[2] = {b[i], 0};
    int yy = y + 26 + SINE[(frame + i * 4 + 9) & 31];
    ftext(x + i * 30 + 3, yy + 3, one, 5, 0, P_DARK);
    ftext_shadow(x + i * 30, yy, one, 5, grad, 0, BLACK);
  }
}
static void draw_title(void) {
  draw_backdrop(0);
  draw_logo(14);
  ftext_shadow((W - ftext_w("WORLD TOUR", 1)) / 2, 100, "WORLD TOUR", 1, 0, P_LOGO0, BLACK);
  if ((frame >> 4) & 1) { panel(84, 120, 152, 16); text_c(124, "PRESS A TO PLAY", WHITE, CI(0, 0, 1)); }
  box(0, 189, W, 11, HUDC);
  text_c(191, "(C) 2026 R. MONTELLA - PRG32", GREY, HUDC);
}
static void draw_team(void) {
  draw_backdrop(1);
  ftext_shadow((W - ftext_w("CHOOSE TEAMS", 2)) / 2, 10, "CHOOSE TEAMS", 2, 0, P_LOGO1, BLACK);
  for (int s = 0; s < 2; s++) {
    int x = s ? 168 : 20;
    panel(x, 34, 132, 128);
    text(x + 6, 40, s ? "AWAY" : "HOME", s ? CI(5, 2, 1) : CI(2, 4, 5), CI(0, 0, 1));
    text(x + 70, 40, s ? "UP/DN" : "LT/RT", GREY, CI(0, 0, 1));
    int ph = (int)(frame >> 3) & 3;
    draw_big(x + 42, 54, F_RUN0 + ph, s ? KIT_AWAY : KIT_HOME, s ? 2 : 1, 3, !s);
    const char *nm = team_name[chosen[s]];
    ftext_shadow(x + (132 - ftext_w(nm, 2)) / 2, 134, nm, 2, 0, WHITE, BLACK);
    ftext(x + 4, 136, "<", 2, 0, P_EDGE); ftext(x + 118, 136, ">", 2, 0, P_EDGE);
  }
  box(0, 176, W, 24, HUDC);
  text_c(180, "A CONFIRM   B BACK", WHITE, HUDC);
}
static void draw_mode(void) {
  draw_backdrop(1);
  ftext_shadow((W - ftext_w("MATCH MODE", 2)) / 2, 16, "MATCH MODE", 2, 0, P_LOGO1, BLACK);
  for (int m = 0; m < 2; m++) {
    int y = 56 + m * 46, sel = mode == m;
    panel(56, y, 208, 36);
    if (sel) { box(57, y + 1, 206, 34, CI(0, 1, 2)); box(60, y + 4, 4, 28, (frame & 8) ? P_LOGO1 : P_LOGO2); }
    const char *t = m ? "CHAMPIONSHIP" : "SINGLE MATCH";
    ftext_shadow(76, y + 6, t, 2, 0, sel ? P_LOGO0 : WHITE, BLACK);
    text(76, y + 24, m ? "3 ROUNDS VS NEW RIVALS" : "ONE 90 MINUTE FIXTURE", GREY, sel ? CI(0, 1, 2) : CI(0, 0, 1));
  }
  box(0, 176, W, 24, HUDC);
  text_c(180, "UP/DOWN SELECT  A KICK OFF", WHITE, HUDC);
}
static void draw_result(void) {
  draw_backdrop(1);
  int win = score[0] > score[1], draw = score[0] == score[1];
  int final = !mode || round_no >= 2;
  const char *head = !final ? "FULL TIME" : mode ? (wins >= 2 ? "CHAMPIONS!" : "TOUR OVER") : (win ? "YOU WIN!" : draw ? "DRAW" : "YOU LOSE");
  ftext_shadow((W - ftext_w(head, 3)) / 2, 14, head, 3, 0, (frame & 8) && (win || wins >= 2) ? P_FLASH : P_LOGO1, BLACK);
  panel(30, 54, 260, 84);
  for (int s = 0; s < 2; s++) {
    const char *nm = team_name[chosen[s]];
    int x = s ? 214 : 46;
    draw_big(x + 4, 62, win == !s && !draw ? F_CHEER : F_STAND, s ? KIT_AWAY : KIT_HOME, s ? 2 : 1, 2, !s);
    text(x + 32 - slen(nm) * 4, 120, nm, WHITE, CI(0, 0, 1));
  }
  char sc[8]; itoa_s(score[0], sc);
  ftext_shadow(124, 76, sc, 4, 0, P_LOGO0, BLACK);
  ftext_shadow(148, 76, "-", 4, 0, WHITE, BLACK);
  itoa_s(score[1], sc);
  ftext_shadow(172, 76, sc, 4, 0, P_LOGO0, BLACK);
  if (mode) {
    char line[32] = "ROUND X OF 3  WINS Y";
    line[6] = (char)('1' + round_no); line[19] = (char)('0' + wins);
    text_c(146, line, YELLOW, CI(0, 0, 1));
  }
  box(0, 176, W, 24, HUDC);
  text_c(180, final ? "PRESS A FOR TITLE" : "PRESS A FOR NEXT ROUND", WHITE, HUDC);
}

void sensational_soccer_draw(void) {
  switch (screen) {
    case SCREEN_TITLE: draw_title(); break;
    case SCREEN_TEAM: draw_team(); break;
    case SCREEN_MODE: draw_mode(); break;
    case SCREEN_RESULT: draw_result(); break;
    default: draw_match(); break;
  }
}
