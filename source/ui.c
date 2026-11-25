/* INTONARUMORI 0.9 - interface.
 * Tela de cima: cena vetorial (art.c) + painel do osciloscopio + cartoes + VU.
 * Tela de baixo: abas + PAUSE/REC, e uma pagina por aba. Desenho e toque usam as MESMAS funcoes
 * de retangulo (r_*), entao o que se ve e o que responde ao toque nao saem de sincronia.
 * Regra dos controles: valor continuo = slider; 2..4 opcoes = botoes (pills); 5+ opcoes = < valor >.
 * Todo texto passa por textfit(): se nao cabe na largura, e cortado (nunca vaza). */
#include "ui.h"
#include "art.h"
#include "synth.h"
#include "amb.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ---------------- paleta (VFD: uma familia verde-agua + ambar) ---------------- */
#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11) | (((g)>>2)<<5) | ((b)>>3)))
#define K_BG     RGB(5,10,9)
#define K_PANEL  RGB(14,26,22)
#define K_PANEL2 RGB(20,38,32)
#define K_GRID   RGB(30,54,45)
#define K_DOT    RGB(14,28,23)
#define K_WHITE  RGB(228,250,236)
#define K_GREY   RGB(104,146,124)
#define K_GREEN  RGB(100,255,170)
#define K_LIME   RGB(180,255,92)
#define K_YELL   RGB(246,232,84)
#define K_AMBER  RGB(255,140,32)
#define K_RED    RGB(255,70,40)

static const uint16_t mcolor[N_MODULES] = { RGB(100,255,170), RGB(228,250,236), RGB(255,140,32), RGB(180,255,92), RGB(246,232,84) };
static const uint16_t gcolor[A_NGROUPS] = { RGB(100,255,170), RGB(246,232,84), RGB(180,255,92), RGB(255,140,32) };

static uint16_t mulc(uint16_t c, int f) {        /* escurece uma cor RGB565 (f = 0..255) */
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (uint16_t)(((r * f / 255) << 11) | ((g * f / 255) << 5) | (b * f / 255));
}
static uint16_t rgb3(const uint8_t c[3]) { return RGB(c[0], c[1], c[2]); }

/* ---------------- primitivas ---------------- */
typedef struct { int x, y, w, h; } R;
static int inr(R r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

static void fillrect(Surf *s, int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; i++) {              /* colunas sao contiguas no 3DS */
        uint16_t *p = s->px + (x + i) * s->h + (s->h - y - h);
        for (int j = 0; j < h; j++) p[j] = c;
    }
}
static void rrect(Surf *s, R r, uint16_t c) {   /* retangulo com cantos cortados */
    if (r.w < 4 || r.h < 4) { fillrect(s, r.x, r.y, r.w, r.h, c); return; }
    fillrect(s, r.x + 1, r.y, r.w - 2, r.h, c);
    fillrect(s, r.x, r.y + 1, r.w, r.h - 2, c);
}
static void rframe(Surf *s, R r, uint16_t c) {  /* contorno de 1 px com cantos cortados */
    fillrect(s, r.x + 1, r.y, r.w - 2, 1, c); fillrect(s, r.x + 1, r.y + r.h - 1, r.w - 2, 1, c);
    fillrect(s, r.x, r.y + 1, 1, r.h - 2, c); fillrect(s, r.x + r.w - 1, r.y + 1, 1, r.h - 2, c);
}
static void disc(Surf *s, int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; dy++) {
        int w = (int)sqrtf((float)(r * r - dy * dy) + 0.5f);
        fillrect(s, cx - w, cy + dy, 2 * w + 1, 1, c);
    }
}
static void tri(Surf *s, int cx, int cy, int r, int dir, uint16_t c) {   /* seta: dir -1 esquerda, +1 direita */
    for (int i = 0; i <= r; i++) {
        int x = dir > 0 ? cx - r / 2 + i : cx + r / 2 - i;
        fillrect(s, x, cy - (r - i), 1, 2 * (r - i) + 1, c);
    }
}

/* fonte 5x7 (colunas, bit0 = topo), ASCII 32..90 */
static const uint8_t font[59][5] = {
 {0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},
 {0x23,0x13,8,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0,5,3,0,0},{0,0x1C,0x22,0x41,0},{0,0x41,0x22,0x1C,0},
 {0x14,8,0x3E,8,0x14},{8,8,0x3E,8,8},{0,0x50,0x30,0,0},{8,8,8,8,8},{0,0x60,0x60,0,0},{0x20,0x10,8,4,2},
 {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
 {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},
 {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},
 {8,0x14,0x22,0x41,0},{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,8},{2,1,0x51,9,6},{0x32,0x49,0x79,0x41,0x3E},
 {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
 {0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,9,1},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,8,8,8,0x7F},{0,0x41,0x7F,0x41,0},
 {0x20,0x40,0x41,0x3F,1},{0x7F,8,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,2,0x0C,2,0x7F},
 {0x7F,4,8,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,9,9,9,6},{0x3E,0x41,0x51,0x21,0x5E},
 {0x7F,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{1,1,0x7F,1,1},{0x3F,0x40,0x40,0x40,0x3F},
 {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}
};
static void glyph(Surf *s, int x, int y, int sc, uint16_t c, char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch == '_') { fillrect(s, x, y + 7 * sc, 5 * sc, sc, c); return; }
    if (ch < 32 || ch > 90) ch = '?';
    const uint8_t *g = font[ch - 32];
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            if (g[col] & (1 << row)) fillrect(s, x + col * sc, y + row * sc, sc, sc, c);
}

/* texto cortado pela largura maxima maxw (px). Se cortou, o ultimo caractere vira '.'. Devolve a largura usada. */
static int textfit(Surf *s, int x, int y, int sc, uint16_t c, const char *t, int maxw) {
    int cap = maxw / (6 * sc), n = (int)strlen(t);
    if (cap <= 0) return 0;
    int cut = n > cap;
    int m = cut ? cap : n;
    for (int i = 0; i < m; i++) glyph(s, x + i * 6 * sc, y, sc, c, (cut && i == m - 1) ? '.' : t[i]);
    return m * 6 * sc - sc;
}
static int fitw(int sc, const char *t, int maxw) {         /* largura que textfit usaria */
    int cap = maxw / (6 * sc), n = (int)strlen(t);
    if (cap <= 0) return 0;
    return (n > cap ? cap : n) * 6 * sc - sc;
}
static void textc(Surf *s, int cx, int y, int sc, uint16_t c, const char *t, int maxw) {   /* centralizado em cx */
    int w = fitw(sc, t, maxw);
    textfit(s, cx - w / 2, y, sc, c, t, maxw);
}
static void textr(Surf *s, int rx, int y, int sc, uint16_t c, const char *t, int maxw) {   /* alinhado a direita em rx */
    int w = fitw(sc, t, maxw);
    textfit(s, rx - w, y, sc, c, t, maxw);
}
static void textbox(Surf *s, R r, int sc, uint16_t c, const char *t) {   /* centro do retangulo */
    textc(s, r.x + r.w / 2, r.y + (r.h - 7 * sc) / 2, sc, c, t, r.w - 4);
}

static void button(Surf *s, R r, const char *t, uint16_t bg, uint16_t fg, uint16_t border) {
    rrect(s, r, bg); rframe(s, r, border); textbox(s, r, 1, fg, t);
}

static void vumeter(Surf *s, int x, int y, int nseg, int pitch, int segw, int h, float v) {
    int lit = (int)(v * (float)nseg + 0.5f);
    for (int i = 0; i < nseg; i++) {
        uint16_t on = i < nseg * 2 / 3 ? K_GREEN : (i < nseg * 6 / 7 ? K_YELL : K_RED);
        fillrect(s, x + i * pitch, y, segw, h, i < lit ? on : mulc(on, 34));
    }
}
static void segbar(Surf *s, int x, int y, int w, int h, float v, uint16_t on) {
    int n = w / 4, lit = (int)(v * (float)n + 0.5f);
    for (int i = 0; i < n; i++) fillrect(s, x + i * 4, y, 3, h, i < lit ? on : mulc(on, 34));
}
static void mmss(char *b, size_t n, float secs) {
    int t = (int)secs; snprintf(b, n, "%02d:%02d", t / 60 % 100, t % 60);
}
static float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

/* ---------------- layout da tela de baixo (compartilhado por desenho e toque) ---------------- */
#define TAB_W 40
#define TAB_H 24
static R r_tab(int i)  { R r = { i * TAB_W, 0, TAB_W - 1, TAB_H }; return r; }
static R r_pause(void) { R r = { 242, 2, 37, 20 }; return r; }
static R r_rec(void)   { R r = { 281, 2, 38, 20 }; return r; }

#define ROW0 70
#define ROW_H 22
static R r_row(int i)  { R r = { 4, ROW0 + i * ROW_H, 312, 20 }; return r; }
static R r_mtile(int i, int n) { int w = (312 - (n - 1) * 3) / n; R r = { 4 + i * (w + 3), 30, w, 26 }; return r; }

/* harsh, tela de tocar */
static R r_pad(void)   { R r = { UI_PAD_X, UI_PAD_Y, UI_PAD_W, UI_PAD_H }; return r; }
static R r_prev(void)  { R r = { 212, 30, 48, 30 }; return r; }
static R r_next(void)  { R r = { 266, 30, 48, 30 }; return r; }
static R r_pname(void) { R r = { 212, 64, 102, 22 }; return r; }
static R r_mutate(void){ R r = { 212, 92, 102, 24 }; return r; }
static R r_random(void){ R r = { 212, 120, 102, 24 }; return r; }
static R r_edit(void)  { R r = { 212, 148, 102, 22 }; return r; }
static R r_vol(void)   { R r = { UI_VOL_X - 6, 176, UI_VOL_W + 12, 22 }; return r; }

/* ambient, tela de tocar: duas "camadas" (DRONE = acordes, LEAD = melodia), cada uma com seu timbre SEMPRE visivel */
static R r_pane(int i) { R r = { 6 + i * 104, 30, 100, 22 }; return r; }
static R r_xb1(void)   { R r = { 214, 30, 48, 22 }; return r; }
static R r_xb2(void)   { R r = { 266, 30, 48, 22 }; return r; }
static R r_chip(int i) { R r = { 6 + i * 39, 56, 37, 18 }; return r; }
/* DRONE */
static R r_cpad(int i, int nd) { int w = 308 / nd; R r = { 6 + i * w, 78, w - 2, 50 }; return r; }
static R r_cmod(int i) { R r = { 6 + i * 62, 132, 60, 20 }; return r; }
static R r_cslot(int i){ R r = { 6 + i * 39, 158, 37, 34 }; return r; }
/* LEAD */
static R r_lmode(int i){ R r = { 6 + i * 62, 78, 60, 20 }; return r; }
static R r_ldir(int i) { int w = (308 - 4 * 3) / 5; R r = { 6 + i * (w + 3), 102, w, 20 }; return r; }
static R r_key(int i)  { R r = { 6 + i * 308 / 12, 176, 308 / 12 - 2, 26 }; return r; }
static R r_econt(int i){ int w = (UI_ES_W - 4 * 3) / 5; R r = { UI_ES_X + i * (w + 3), 180, w, 20 }; return r; }
static R r_esl(int i)  { R r = { UI_ES_X - 4, 104 + i * 24, UI_ES_W + 8, 20 }; return r; }
#define SEQ_X0 6
#define SEQ_Y0 102
#define SEQ_ROWH 9
#define SEQ_ROWS 11
static R r_seqcol(int i) { R r = { SEQ_X0 + i * 39, SEQ_Y0, 37, SEQ_ROWH * SEQ_ROWS }; return r; }

static int lmode_idx(const UiState *u) { return u->ap ? amb_ival(u->ap, A_LMODE) : L_DRIFT; }
static int lmode_eff(const UiState *u) { return (u->ap && u->ap->lead_on) ? amb_ival(u->ap, A_LMODE) : L_OFF; }
int ui_apad_rect(const UiState *u, int *x, int *y, int *w, int *h) {
    int lm = lmode_idx(u);
    if (u->pane != 1 || lm == L_SEQ || lm == L_EUCL) return 0;
    *x = 6; *w = 308;
    if (lm == L_ARP) { *y = 126; *h = 46; } else { *y = 102; *h = 68; }
    return 1;
}
static R r_apad(const UiState *u) { R r = { 0, 0, 0, 0 }; ui_apad_rect(u, &r.x, &r.y, &r.w, &r.h); return r; }
int ui_seq_cell(int x, int y, int *step, int *val) {
    if (x < SEQ_X0 || x >= SEQ_X0 + 8 * 39 || y < SEQ_Y0 || y >= SEQ_Y0 + SEQ_ROWH * SEQ_ROWS) return 0;
    int st = (x - SEQ_X0) / 39, row = SEQ_ROWS - 1 - (y - SEQ_Y0) / SEQ_ROWH;   /* row 0 = pausa, 1..10 = grau 0..9 */
    *step = st > 7 ? 7 : st; *val = row - 1;
    return 1;
}

/* presets (noise e ambient usam a mesma grade) */
static R r_ptile(int i) { R r = { 4 + (i % 4) * 78, 32 + (i / 4) * 50, 76, 46 }; return r; }

/* arquivos */
static R r_frow(int i)  { R r = { 4, 46 + i * 20, 312, 18 }; return r; }
static R r_fup(void)    { R r = { 6, 210, 52, 26 }; return r; }
static R r_fdn(void)    { R r = { 62, 210, 52, 26 }; return r; }
static R r_fren(void)   { R r = { 118, 210, 100, 26 }; return r; }
static R r_fdel(void)   { R r = { 222, 210, 92, 26 }; return r; }

/* ajustes */
#define SET_Y0 34
#define SET_DY 28
static R r_srow(int i)  { R r = { 4, SET_Y0 + i * SET_DY, 312, 22 }; return r; }
static const int set_n[SET_N] = { 0, 2, 2, 4, 3, 4 };     /* 0 = slider; senao numero de botoes */

/* controle de uma linha: slider / pills / stepper (a mesma geometria serve para desenho e toque) */
static R c_pill(R row, int n, int i)  { int pw = (222 - (n - 1) * 3) / n; R r = { 92 + i * (pw + 3), row.y, pw, row.h }; return r; }
static R c_stepl(R row) { R r = { 92, row.y, 26, row.h }; return r; }
static R c_stepv(R row) { R r = { 120, row.y, 170, row.h }; return r; }
static R c_stepr(R row) { R r = { 292, row.y, 22, row.h }; return r; }
static R c_slider(R row){ R r = { UI_SLD_X - 6, row.y, UI_SLD_W + 12, row.h }; return r; }

static int ctrl_kind(int n) { return n == 0 ? 0 : (n <= 4 ? 1 : 2); }   /* 0 slider, 1 pills, 2 stepper */

/* numero de linhas e id do parametro da linha, por pagina */
static int edit_rows(const UiState *u) {
    if (u->page == PG_AMB) return amb_groups[u->module % A_NGROUPS].count;
    return modules[u->module % N_MODULES].count;
}
static int edit_id(const UiState *u, int row) {
    if (u->page == PG_AMB) return amb_groups[u->module % A_NGROUPS].first + row;
    return modules[u->module % N_MODULES].first + row;
}
static int edit_n(const UiState *u, int id) { return u->page == PG_AMB ? amb_discrete(id) : param_discrete(id); }

/* ---------------- toque ---------------- */
static UiHit hit(int kind, int arg) { UiHit h = { kind, arg, 0, 0, 0.0f, 0.0f }; return h; }
static UiHit hit_pos(int kind, R r, int x, int y) {
    UiHit h = hit(kind, 0);
    h.fx = clamp01((float)(x - r.x) / (float)(r.w > 1 ? r.w - 1 : 1));
    h.fy = clamp01(1.0f - (float)(y - r.y) / (float)(r.h > 1 ? r.h - 1 : 1));
    return h;
}

/* um controle de linha: devolve o hit pelo tipo do controle */
static UiHit hit_ctrl(R row, int id, int n, int rowi, int x, int y) {
    UiHit h = hit(H_NONE, 0);
    h.row = rowi;
    int k = ctrl_kind(n);
    if (k == 0) {
        R t = c_slider(row);
        if (inr(t, x, y)) { h.kind = H_SLIDER; h.arg = id; h.fx = clamp01((float)(x - UI_SLD_X) / (float)UI_SLD_W); }
    } else if (k == 1) {
        for (int i = 0; i < n; i++) if (inr(c_pill(row, n, i), x, y)) { h.kind = H_PILL; h.arg = id; h.arg2 = i; }
    } else {
        if (inr(c_stepl(row), x, y)) { h.kind = H_STEPL; h.arg = id; }
        else if (inr(c_stepr(row), x, y)) { h.kind = H_STEPR; h.arg = id; }
    }
    return h;
}

UiHit ui_hit(const UiState *u, int x, int y) {
    for (int i = 0; i < PG_COUNT; i++) if (inr(r_tab(i), x, y)) return hit(H_TAB, i);
    if (inr(r_pause(), x, y)) return hit(H_PAUSE, 0);
    if (inr(r_rec(), x, y)) return hit(H_REC, 0);

    switch (u->page) {
    case PG_HARSH:
        if (u->sub == 0) {
            if (inr(r_pad(), x, y)) return hit_pos(H_PAD, r_pad(), x, y);
            if (inr(r_prev(), x, y)) return hit(H_PREV, 0);
            if (inr(r_next(), x, y)) return hit(H_NEXT, 0);
            if (inr(r_mutate(), x, y)) return hit(H_MUTATE, 0);
            if (inr(r_random(), x, y)) return hit(H_RANDOM, 0);
            if (inr(r_edit(), x, y)) return hit(H_EDIT, 0);
            if (inr(r_vol(), x, y)) { UiHit h = hit(H_VOL, 0); h.fx = clamp01((float)(x - UI_VOL_X) / (float)UI_VOL_W); return h; }
            return hit(H_NONE, 0);
        }
        /* fallthrough */
    case PG_AMB:
        if (u->page == PG_AMB && u->sub == 0) {
            int lm = lmode_idx(u), nd = amb_nd(u->ap);
            if (inr(r_pane(0), x, y)) return hit(H_DRONE, 0);
            if (inr(r_pane(1), x, y)) return hit(H_LEAD, 0);
            for (int i = 0; i < A_NSND; i++) if (inr(r_chip(i), x, y)) return hit(H_TIMB, i);
            if (u->pane == 0) {
                if (inr(r_xb1(), x, y)) return hit(H_CWRITE, 0);
                if (inr(r_xb2(), x, y)) return hit(H_CDICE, 0);
                for (int i = 0; i < nd; i++) if (inr(r_cpad(i, nd), x, y)) return hit(H_CPAD, i);
                for (int i = 0; i < 5; i++) if (inr(r_cmod(i), x, y)) return hit(H_CMOD, 1 << i);
                for (int i = 0; i < AMB_PSLOTS; i++) if (inr(r_cslot(i), x, y)) return hit(H_CSLOT, i);
                return hit(H_NONE, 0);
            }
            if (inr(r_xb1(), x, y)) return hit(H_LDICE, 0);
            if (inr(r_xb2(), x, y)) return hit(H_LXTRA, 0);
            for (int i = 0; i < A_NLMODE; i++) if (inr(r_lmode(i), x, y)) return hit(H_LMODE, i);
            if (lm == L_ARP) for (int i = 0; i < 5; i++) if (inr(r_ldir(i), x, y)) { UiHit h = hit(H_PILL, A_LDIR); h.arg2 = i; h.row = -1; return h; }
            if (lm == L_SEQ) {
                int st, v; if (ui_seq_cell(x, y, &st, &v)) { UiHit h = hit(H_SEQCELL, st); h.arg2 = v; return h; }
            }
            if (lm == L_EUCL) {
                static const int ids[3] = { A_EHITS, A_ESTEPS, A_EROT };
                for (int i = 0; i < 3; i++) if (inr(r_esl(i), x, y)) {
                    UiHit h = hit_pos(H_EPAR, r_esl(i), x, y); h.arg = ids[i]; h.fx = clamp01((float)(x - UI_ES_X) / (float)UI_ES_W); return h;
                }
                for (int i = 0; i < 5; i++) if (inr(r_econt(i), x, y)) { UiHit h = hit(H_PILL, A_ECONT); h.arg2 = i; h.row = -1; return h; }
            }
            if (inr(r_apad(u), x, y)) return hit_pos(H_APAD, r_apad(u), x, y);
            if (lm == L_DRIFT || lm == L_KEYS) for (int i = 0; i < 12; i++) if (inr(r_key(i), x, y)) return hit(H_KEY, i);
            return hit(H_NONE, 0);
        }
        {   /* editor (harsh ou ambient) */
            int nt = u->page == PG_AMB ? A_NGROUPS : N_MODULES;
            for (int i = 0; i < nt; i++) if (inr(r_mtile(i, nt), x, y)) return hit(H_MODTILE, i);
            int nr = edit_rows(u);
            for (int i = 0; i < nr; i++) {
                R row = r_row(i);
                if (y >= row.y && y < row.y + ROW_H) { int id = edit_id(u, i); return hit_ctrl(row, id, edit_n(u, id), i, x, y); }
            }
            return hit(H_NONE, 0);
        }
    case PG_HPRE:
        for (int i = 0; i < N_PRESETS; i++) if (inr(r_ptile(i), x, y)) return hit(H_PRESET, i);
        return hit(H_NONE, 0);
    case PG_APRE:
        for (int i = 0; i < A_NPRESETS; i++) if (inr(r_ptile(i), x, y)) return hit(H_APRE, i);
        return hit(H_NONE, 0);
    case PG_FILES:
        if (inr(r_fup(), x, y)) return hit(H_FUP, 0);
        if (inr(r_fdn(), x, y)) return hit(H_FDN, 0);
        if (inr(r_fren(), x, y)) return hit(H_REN, 0);
        if (inr(r_fdel(), x, y)) return hit(H_DEL, 0);
        for (int i = 0; i < MAX_FILES; i++)
            if (u->fscroll + i < u->nfiles && inr(r_frow(i), x, y)) return hit(H_FILE, u->fscroll + i);
        return hit(H_NONE, 0);
    case PG_SET:
        for (int i = 0; i < SET_N; i++) {
            R row = r_srow(i);
            if (y >= row.y && y < row.y + row.h) return hit_ctrl(row, i, set_n[i], i, x, y);
        }
        return hit(H_NONE, 0);
    }
    return hit(H_NONE, 0);
}

/* ---------------- desenho de um controle de linha ---------------- */
/* opts: textos das opcoes (so para pills); valtxt: valor atual em texto */
static void draw_ctrl(Surf *s, R row, int n, int cur, float v01, const char *valtxt, char opts[][14], uint16_t col, int sel) {
    int k = ctrl_kind(n);
    if (k == 0) {
        R t = { UI_SLD_X, row.y + 7, UI_SLD_W, 6 };
        rrect(s, t, K_DOT);
        int fw = (int)(v01 * (float)UI_SLD_W + 0.5f);
        if (fw > 0) rrect(s, (R){ t.x, t.y, fw < 2 ? 2 : fw, t.h }, col);
        int hx = UI_SLD_X + fw; if (hx < UI_SLD_X + 2) hx = UI_SLD_X + 2; if (hx > UI_SLD_X + UI_SLD_W - 2) hx = UI_SLD_X + UI_SLD_W - 2;
        rrect(s, (R){ hx - 2, row.y + 3, 5, 14 }, sel ? K_WHITE : col);
        textr(s, 313, row.y + 6, 1, sel ? K_WHITE : K_GREY, valtxt, 48);
    } else if (k == 1) {
        for (int i = 0; i < n; i++) {
            R p = c_pill(row, n, i); int on = i == cur;
            rrect(s, p, on ? col : K_PANEL);
            rframe(s, p, on ? col : K_GRID);
            textbox(s, p, 1, on ? K_BG : K_GREY, opts[i]);
        }
    } else {
        R l = c_stepl(row), vb = c_stepv(row), rr = c_stepr(row);
        rrect(s, l, K_PANEL2); rframe(s, l, K_GRID); tri(s, l.x + l.w / 2, l.y + l.h / 2, 5, -1, col);
        rrect(s, rr, K_PANEL2); rframe(s, rr, K_GRID); tri(s, rr.x + rr.w / 2, rr.y + rr.h / 2, 5, 1, col);
        rrect(s, vb, K_PANEL); rframe(s, vb, K_GRID);
        textbox(s, vb, 1, K_WHITE, valtxt);
    }
}

static void draw_row_frame(Surf *s, R row, const char *label, int sel, uint16_t col) {
    if (sel) { rrect(s, row, K_PANEL); fillrect(s, row.x, row.y + 2, 2, row.h - 4, col); }
    textfit(s, row.x + 6, row.y + (row.h - 7) / 2, 1, sel ? K_WHITE : K_GREY, label, 82);
}

static void harsh_opt(const float *v, int id, int i, char *b, size_t n) {
    if (id == P_WAVE) { snprintf(b, n, "%s", wave_name(i)); return; }
    if (id == P_NTYPE) { snprintf(b, n, "%s", ntype_name(i)); return; }
    float t[P_COUNT]; memcpy(t, v, sizeof t);
    int cnt = param_discrete(id); t[id] = ((float)i + 0.5f) / (float)cnt;
    param_text(t, id, b, n);
}

/* ---------------- paginas da tela de baixo ---------------- */
static void tabs(Surf *s, const UiState *u) {
    static const char *const nm[PG_COUNT] = { "HARSH", "H-PRE", "AMB", "A-PRE", "FILES", "SET" };
    for (int i = 0; i < PG_COUNT; i++) {
        R r = r_tab(i); int on = i == u->page;
        uint16_t col = (i == PG_AMB || i == PG_APRE) ? K_YELL : K_GREEN;
        rrect(s, r, on ? col : K_PANEL);
        if (!on) rframe(s, r, K_GRID);
        textbox(s, r, 1, on ? K_BG : K_GREY, nm[i]);
    }
    R p = r_pause(), rc = r_rec();                              /* PAUSE e REC viram icones (cabem as 6 abas) */
    rrect(s, p, u->paused ? K_AMBER : K_PANEL2); rframe(s, p, K_AMBER);
    if (u->paused) tri(s, p.x + p.w / 2, p.y + p.h / 2, 6, 1, K_BG);
    else { fillrect(s, p.x + 13, p.y + 5, 4, 10, K_AMBER); fillrect(s, p.x + 20, p.y + 5, 4, 10, K_AMBER); }
    rrect(s, rc, u->rec_on ? K_RED : K_PANEL2); rframe(s, rc, K_RED);
    if (u->rec_on) fillrect(s, rc.x + 14, rc.y + 6, 9, 9, K_BG); else disc(s, rc.x + rc.w / 2, rc.y + rc.h / 2, 5, K_RED);
}

static void page_harsh_play(Surf *s, const UiState *u) {
    char b[48];
    uint8_t rgb[3]; art_accent(u->preset < 0 ? ART_BLANK : u->preset, rgb);
    uint16_t ac = rgb3(rgb);
    /* pad XY */
    R pad = r_pad();
    rrect(s, pad, K_PANEL); rframe(s, pad, u->touching ? ac : K_GRID);
    for (int i = 1; i < 4; i++) {
        fillrect(s, pad.x + pad.w * i / 4, pad.y + 2, 1, pad.h - 4, K_DOT);
        fillrect(s, pad.x + 2, pad.y + pad.h * i / 4, pad.w - 4, 1, K_DOT);
    }
    float fx = u->touching ? u->tx : u->v[u->xid], fy = u->touching ? u->ty : u->v[u->yid];
    int cx = pad.x + (int)(fx * (float)(pad.w - 1)), cy = pad.y + (int)((1.0f - fy) * (float)(pad.h - 1));
    fillrect(s, cx, pad.y + 2, 1, pad.h - 4, mulc(ac, 110)); fillrect(s, pad.x + 2, cy, pad.w - 4, 1, mulc(ac, 110));
    disc(s, cx, cy, 6, mulc(ac, 90)); disc(s, cx, cy, 4, u->touching ? K_WHITE : ac);
    textfit(s, pad.x + 5, pad.y + 5, 1, K_GREY, param_name(u->yid), 90);
    textc(s, pad.x + pad.w / 2, pad.y + pad.h - 11, 1, K_GREY, param_name(u->xid), 150);
    /* coluna da direita */
    R pv = r_prev(), nx = r_next();
    rrect(s, pv, K_PANEL2); rframe(s, pv, K_GRID); tri(s, pv.x + pv.w / 2, pv.y + pv.h / 2, 7, -1, ac);
    rrect(s, nx, K_PANEL2); rframe(s, nx, K_GRID); tri(s, nx.x + nx.w / 2, nx.y + nx.h / 2, 7, 1, ac);
    R pn = r_pname();
    rrect(s, pn, K_PANEL); rframe(s, pn, ac);
    snprintf(b, sizeof b, "%s%s", u->preset >= 0 ? preset_names[u->preset] : "RANDOM", u->modified ? "*" : "");
    textbox(s, pn, 1, K_WHITE, b);
    button(s, r_mutate(), "MUTATE", K_PANEL2, K_LIME, K_GRID);
    button(s, r_random(), "RANDOM", K_PANEL2, K_YELL, K_GRID);
    button(s, r_edit(), "EDIT >", K_PANEL2, K_GREEN, K_GRID);
    /* volume */
    textfit(s, 8, 182, 1, K_GREY, "VOLUME", 80);
    R vr = r_vol();
    draw_ctrl(s, (R){ 4, vr.y, 312, 20 }, 0, 0, u->v[P_VOL], (snprintf(b, sizeof b, "%d%%", (int)(u->v[P_VOL] * 100.0f + 0.5f)), b), NULL, K_GREEN, 0);
    snprintf(b, sizeof b, "X: %s   Y: %s", param_name(u->xid), param_name(u->yid));
    textfit(s, 8, 208, 1, K_GREY, b, 304);
    textfit(s, 8, 220, 1, K_GREY, "L/R PAGE  SELECT EDITOR  X BURST  Y MUTE", 304);
    textfit(s, 8, 230, 1, K_GREY, "D-PAD: PRESET / VOLUME", 304);
}

static void page_edit(Surf *s, const UiState *u) {
    int amb = u->page == PG_AMB;
    int nt = amb ? A_NGROUPS : N_MODULES;
    for (int i = 0; i < nt; i++) {
        R r = r_mtile(i, nt); int on = i == u->module;
        uint16_t col = amb ? gcolor[i] : mcolor[i];
        rrect(s, r, on ? col : K_PANEL); if (!on) rframe(s, r, K_GRID);
        textbox(s, r, 1, on ? K_BG : col, amb ? amb_groups[i].name : modules[i].name);
    }
    uint16_t mc = amb ? gcolor[u->module % A_NGROUPS] : mcolor[u->module % N_MODULES];
    if (!amb) textfit(s, 6, 60, 1, K_GREY, modules[u->module % N_MODULES].desc, 308);
    else { static const char *const gd[A_NGROUPS] = { "DRONE: SOUND, KEY/SCALE, CHORD TYPE, SEQUENCE, VOICING", "LEAD: SOUND, PLAY MODE, REGISTER, SPEED, DENSITY",
                                                       "PLAY: ARP DIRECTION, EUCLID, STRUM, KEYS HOLD", "MIX: LEVELS, ECHO, REVERB, MODULATE, MASTER" };
           textfit(s, 6, 60, 1, K_GREY, gd[u->module % A_NGROUPS], 308); }
    int nr = edit_rows(u);
    for (int i = 0; i < nr; i++) {
        R row = r_row(i); int id = edit_id(u, i), n = edit_n(u, id), sel = i == u->row;
        char val[24], opts[6][14]; float v01;
        if (amb) { amb_ptext(u->ap, id, val, sizeof val); v01 = u->ap->p[id]; }
        else { param_text(u->v, id, val, sizeof val); v01 = u->v[id]; }
        int cur = 0;
        if (n) {
            if (amb) cur = amb_ival(u->ap, id);
            else { cur = (int)(u->v[id] * (float)n); if (cur >= n) cur = n - 1; }
            if (ctrl_kind(n) == 1)
                for (int k = 0; k < n && k < 6; k++) {
                    if (amb) amb_opt_text(u->ap, id, k, opts[k], 14); else harsh_opt(u->v, id, k, opts[k], 14);
                }
        }
        draw_row_frame(s, row, amb ? amb_pname(id) : param_name(id), sel, mc);
        draw_ctrl(s, row, n, cur, v01, val, opts, mc, sel);
    }
}

static const char *vcn_s(const AmbPatch *a) { static const char *const n[3] = { "SMOOTH", "CLOSE", "OPEN" }; return n[amb_ival(a, A_VOICING)]; }
static const char *locs(int i) { static const char *const n[3] = { "LOW", "MID", "HIGH" }; return n[i % 3]; }
static int popcount12(unsigned m) { int c = 0; for (int i = 0; i < 12; i++) c += (m >> i) & 1; return c; }

static void chip_row(Surf *s, int cur, uint16_t col) {          /* timbres sempre visiveis: tocar = trocar na hora */
    for (int i = 0; i < A_NSND; i++) {
        R c = r_chip(i); int on = i == cur;
        rrect(s, c, on ? col : K_PANEL); rframe(s, c, on ? col : K_GRID);
        textbox(s, c, 1, on ? K_BG : K_GREY, amb_snd_short(i));
    }
}

static void page_amb_play(Surf *s, const UiState *u) {
    char b[56], t[24], t2[16];
    const AmbPatch *a = u->ap;
    int lm = lmode_eff(u), lmi = lmode_idx(u), nd = amb_nd(a), root = amb_ival(a, A_ROOT), pane = u->pane;
    /* camadas: toque na camada fora de foco = seleciona; na camada em foco = liga/desliga */
    R pd = r_pane(0), pl = r_pane(1);
    rrect(s, pd, pane == 0 && a->drone_on ? K_GREEN : (pane == 0 ? K_PANEL2 : K_PANEL)); rframe(s, pd, pane == 0 ? K_GREEN : K_GRID);
    textbox(s, pd, 1, pane == 0 && a->drone_on ? K_BG : (a->drone_on ? K_GREEN : K_GREY), a->drone_on ? "DRONE" : "DRONE OFF");
    rrect(s, pl, pane == 1 && a->lead_on ? K_YELL : (pane == 1 ? K_PANEL2 : K_PANEL)); rframe(s, pl, pane == 1 ? K_YELL : K_GRID);
    textbox(s, pl, 1, pane == 1 && a->lead_on ? K_BG : (a->lead_on ? K_YELL : K_GREY), a->lead_on ? "LEAD" : "LEAD OFF");
    chip_row(s, amb_ival(a, pane == 0 ? A_DSND : A_LSND), pane == 0 ? K_GREEN : K_YELL);

    if (pane == 0) {
        button(s, r_xb1(), "WRITE", u->write_on ? K_RED : K_PANEL2, u->write_on ? K_BG : K_RED, K_RED);
        button(s, r_xb2(), "DICE", K_PANEL2, K_GREEN, K_GRID);
        for (int i = 0; i < nd; i++) {                               /* pads de acorde: o grau da escala, em tempo real */
            R p = r_cpad(i, nd); int on = i == u->cur_deg;
            char rn[8], lb[12]; amb_roman(a, i, rn, sizeof rn); amb_chord_label(a, i, lb, sizeof lb);
            rrect(s, p, on ? K_GREEN : K_PANEL2); rframe(s, p, on ? K_WHITE : K_GRID);
            textc(s, p.x + p.w / 2, p.y + 8, 2, on ? K_BG : K_WHITE, rn, p.w - 2);
            fillrect(s, p.x + 4, p.y + 28, p.w - 8, 1, on ? K_BG : K_GRID);
            textc(s, p.x + p.w / 2, p.y + 35, 1, on ? K_BG : K_GREEN, lb, p.w - 2);
        }
        static const char *const mn[5] = { "7TH", "9TH", "SUS", "FLIP", "INV" };
        for (int i = 0; i < 5; i++) {
            R m = r_cmod(i); int on = (a->cmod >> i) & 1;
            rrect(s, m, on ? K_YELL : K_PANEL); rframe(s, m, on ? K_YELL : K_GRID);
            textbox(s, m, 1, on ? K_BG : K_GREY, mn[i]);
        }
        int plen = amb_plen(a), pm = amb_ival(a, A_PROG);
        for (int i = 0; i < AMB_PSLOTS; i++) {                        /* sequencia de acordes (8 slots): tocar = mudar o grau */
            R c = r_cslot(i); int d = a->prog[i], used = i < plen && d >= 0, cur = pm != P_MANUAL && i == u->cur_slot;
            rrect(s, c, used ? K_PANEL2 : K_PANEL); rframe(s, c, cur ? K_WHITE : (used ? K_GREEN : K_GRID));
            snprintf(t, sizeof t, "%d", i + 1); textfit(s, c.x + 3, c.y + 3, 1, K_GREY, t, 12);
            if (used) {
                char rn[8], lb[12]; amb_roman(a, d, rn, sizeof rn); amb_chord_label(a, d, lb, sizeof lb);
                textc(s, c.x + c.w / 2, c.y + 13, 1, cur ? K_WHITE : K_GREEN, rn, c.w - 2);
                textc(s, c.x + c.w / 2, c.y + 24, 1, K_GREY, lb, c.w - 2);
            } else textc(s, c.x + c.w / 2, c.y + 15, 1, K_GRID, "-", c.w);
        }
        amb_ptext(a, A_PROG, t, sizeof t); amb_ptext(a, A_DRATE, t2, sizeof t2);
        snprintf(b, sizeof b, "CHORDS: %s  %s", t, pm == P_MANUAL ? "MANUAL: YOU PLAY" : t2);
        textfit(s, 8, 198, 1, K_GREEN, b, 304);
        snprintf(b, sizeof b, "%s %s  %s  %s", note_name(root), amb_scale_name(amb_ival(a, A_SCALE)), amb_qual_name(amb_ival(a, A_QUAL)), vcn_s(a));
        textfit(s, 8, 209, 1, K_GREEN, b, 304);
        textfit(s, 8, 220, 1, K_GREY, "TAP PAD = CHORD NOW   TAP SLOT = EDIT", 304);
        textfit(s, 8, 230, 1, K_GREY, "</>: CHORD  A NEXT  B DICE  X LEAD  Y MUTE", 304);
        return;
    }

    /* ---- LEAD ---- */
    static const char *const xl[5] = { "OCT", "OCT", "SHIFT", "ROT", "HOLD" };
    button(s, r_xb1(), "DICE", K_PANEL2, K_YELL, K_GRID);
    button(s, r_xb2(), xl[lmi], K_PANEL2, K_YELL, K_GRID);
    for (int i = 0; i < A_NLMODE; i++) {                              /* o jeito de tocar: abas fixas, nao "preset" */
        R m = r_lmode(i); int on = i == lmi;
        rrect(s, m, on ? K_YELL : K_PANEL); rframe(s, m, on ? K_YELL : K_GRID);
        textbox(s, m, 1, on ? K_BG : K_GREY, i == L_SEQ ? "SEQ" : (i == L_EUCL ? "EUCL" : (i == L_ARP ? "ARP" : amb_lmode_name(i))));
    }
    if (lmi == L_ARP) {
        for (int i = 0; i < 5; i++) {
            R d = r_ldir(i); int on = i == amb_ival(a, A_LDIR);
            static const char *const dn[5] = { "UP", "DOWN", "PEND", "RAND", "WALK" };
            rrect(s, d, on ? K_GREEN : K_PANEL); rframe(s, d, on ? K_GREEN : K_GRID); textbox(s, d, 1, on ? K_BG : K_GREY, dn[i]);
        }
    }
    int has_pad = 0, px, py, pw, ph; has_pad = ui_apad_rect(u, &px, &py, &pw, &ph);
    R pad = { px, py, pw, ph };
    if (lmi == L_SEQ) {
        for (int i = 0; i < 8; i++) {
            R c = r_seqcol(i); int d = a->seq[i], play = lm == L_SEQ && i == u->step_ui;
            rrect(s, c, K_PANEL); rframe(s, c, play ? K_WHITE : K_GRID);
            for (int r = 0; r < SEQ_ROWS; r++) {
                int y = c.y + (SEQ_ROWS - 1 - r) * SEQ_ROWH;
                int on = r == d + 1;
                if (r > 0 && r <= d + 1) fillrect(s, c.x + 3, y + 3, c.w - 6, 3, on ? K_YELL : mulc(K_YELL, 60));
                else if (r > 0 && !(r & 1)) fillrect(s, c.x + 3, y + 4, c.w - 6, 1, K_DOT);
            }
            if (d < 0) textc(s, c.x + c.w / 2, c.y + c.h - 9, 1, K_GREY, "-", c.w);
        }
    } else if (lmi == L_EUCL) {
        int steps = 4 + amb_ival(a, A_ESTEPS), hits = amb_ival(a, A_EHITS) + 1, rot = amb_ival(a, A_EROT) % steps;
        if (hits > steps) hits = steps;
        const int cx = 76, cy = 152, rr = 40;
        disc(s, cx, cy, rr + 9, K_PANEL); disc(s, cx, cy, rr - 9, K_BG);
        for (int p = 0; p < steps; p++) {
            float ang = -1.5708f + 6.28318f * (float)p / (float)steps;
            int x = cx + (int)(cosf(ang) * (float)rr), y = cy + (int)(sinf(ang) * (float)rr);
            int hit = amb_euclid_hit(hits, steps, rot, p), cur = lm == L_EUCL && p == u->step_ui;
            if (hit) disc(s, x, y, cur ? 6 : 4, cur ? K_WHITE : K_YELL); else disc(s, x, y, cur ? 3 : 1, cur ? K_WHITE : K_GREY);
        }
        snprintf(t, sizeof t, "%d/%d", hits, steps); textc(s, cx, cy - 8, 2, K_YELL, t, 60);
        snprintf(t, sizeof t, "ROT %d", rot); textc(s, cx, cy + 10, 1, K_GREY, t, 60);
        static const int ids[3] = { A_EHITS, A_ESTEPS, A_EROT };
        static const char *const ln[3] = { "HITS", "STEPS", "ROTATE" };
        for (int i = 0; i < 3; i++) {
            R r = r_esl(i); int id = ids[i], n = amb_discrete(id); float v = ((float)amb_ival(a, id) + 0.5f) / (float)n;
            textfit(s, UI_ES_X, r.y + 1, 1, K_GREY, ln[i], 60);
            amb_ptext(a, id, t, sizeof t); textr(s, UI_ES_X + UI_ES_W, r.y + 1, 1, K_WHITE, t, 40);
            R bar = { UI_ES_X, r.y + 11, UI_ES_W, 6 }; rrect(s, bar, K_DOT);
            int fw = (int)(v * (float)UI_ES_W); rrect(s, (R){ bar.x, bar.y, fw < 2 ? 2 : fw, 6 }, K_YELL);
        }
        textfit(s, UI_ES_X, 170, 1, K_GREY, "CONTOUR", 60);
        static const char *const cn[5] = { "WLK", "UP", "DN", "ARC", "RND" };
        for (int i = 0; i < 5; i++) {
            R c = r_econt(i); int on = i == amb_ival(a, A_ECONT);
            rrect(s, c, on ? K_YELL : K_PANEL); rframe(s, c, on ? K_YELL : K_GRID); textbox(s, c, 1, on ? K_BG : K_GREY, cn[i]);
        }
    } else if (has_pad) {
        rrect(s, pad, K_PANEL); rframe(s, pad, u->touching ? K_YELL : K_GRID);
        if (lmi == L_KEYS) {                                           /* teclado: uma faixa por nota permitida */
            int loct = amb_ival(a, A_LOCT), lspan = loct == 0 ? 18 : (loct == 1 ? 14 : 12), n = 0, notes[48];
            for (int k = 0; k <= lspan && n < 48; k++) if ((a->mask >> (k % 12)) & 1) notes[n++] = k;
            if (n == 0) notes[n++] = 0;
            for (int i = 0; i < n; i++) {
                int x0 = pad.x + i * pad.w / n, x1 = pad.x + (i + 1) * pad.w / n, pc = (notes[i] % 12 + root) % 12;
                fillrect(s, x0 + 1, pad.y + 2, x1 - x0 - 1, pad.h - 4, (i & 1) ? K_PANEL2 : K_PANEL);
                if (x1 - x0 >= 14) textc(s, (x0 + x1) / 2, pad.y + pad.h - 12, 1, notes[i] % 12 == 0 ? K_YELL : K_GREY, note_name(pc), x1 - x0);
            }
            if (u->touching) {
                int cx = pad.x + (int)(u->tx * (float)(pad.w - 1)), cy = pad.y + (int)((1.0f - u->ty) * (float)(pad.h - 1));
                fillrect(s, cx - 1, pad.y + 2, 3, pad.h - 4, mulc(K_YELL, 120)); disc(s, cx, cy, 5, K_WHITE);
            }
            textfit(s, pad.x + 5, pad.y + 5, 1, K_GREY, amb_ival(a, A_KHOLD) ? "TOUCH: PLAY (HOLD ON)  Y: BRIGHT" : "TOUCH: PLAY  Y: BRIGHT", pad.w - 10);
        } else {
            for (int i = 1; i < 4; i++) {
                fillrect(s, pad.x + pad.w * i / 4, pad.y + 2, 1, pad.h - 4, K_DOT);
                fillrect(s, pad.x + 2, pad.y + pad.h * i / 4, pad.w - 4, 1, K_DOT);
            }
            int xi = A_SPEED, yi = A_DENS;
            float fx = u->touching ? u->tx : a->p[xi], fy = u->touching ? u->ty : a->p[yi];
            int cx = pad.x + (int)(fx * (float)(pad.w - 1)), cy = pad.y + (int)((1.0f - fy) * (float)(pad.h - 1));
            fillrect(s, cx, pad.y + 2, 1, pad.h - 4, mulc(K_YELL, 100)); fillrect(s, pad.x + 2, cy, pad.w - 4, 1, mulc(K_YELL, 100));
            disc(s, cx, cy, 5, u->touching ? K_WHITE : K_YELL);
            textfit(s, pad.x + 5, pad.y + 5, 1, K_GREY, amb_pname(yi), 120);
            textr(s, pad.x + pad.w - 5, pad.y + pad.h - 11, 1, K_GREY, amb_pname(xi), 120);
        }
    }
    if (lmi == L_DRIFT || lmi == L_KEYS) {                              /* notas permitidas (12 teclas): tocar liga/desliga */
        for (int i = 0; i < 12; i++) {
            R k = r_key(i); int on = (a->mask >> i) & 1, blk = (i == 1 || i == 3 || i == 6 || i == 8 || i == 10);
            rrect(s, k, on ? K_YELL : (blk ? K_DOT : K_PANEL)); rframe(s, k, on ? K_YELL : K_GRID);
            textbox(s, k, 1, on ? K_BG : K_GREY, note_name((root + i) % 12));
        }
    }
    if (lmi == L_ARP) {
        char lb[12]; amb_chord_label(a, u->cur_deg, lb, sizeof lb);
        snprintf(b, sizeof b, "ARP FOLLOWS THE DRONE CHORD: %s", lb); textfit(s, 8, 176, 1, K_GREEN, b, 304);
        for (int i = 0; i < 8; i++) {
            int cur = lm == L_ARP && i == u->step_ui % 8;
            fillrect(s, 8 + i * 38, 188, 34, 5, cur ? K_WHITE : K_PANEL2);
        }
    }
    snprintf(b, sizeof b, "LEAD %s  %s  %s", amb_snd_name(amb_ival(a, A_LSND)), amb_lmode_name(lmi), locs(amb_ival(a, A_LOCT)));
    textfit(s, 8, 206, 1, K_YELL, b, 304);
    static const char *const hint[5] = { "DRIFT: WANDERS ON THE ALLOWED NOTES (BOTTOM KEYS)", "ARP: ARPEGGIATES THE CURRENT CHORD",
        "STEP SEQ: TAP/DRAG THE COLUMNS", "EUCLID: HITS SPREAD EVENLY IN THE RING", "KEYS: PLAY THE LEAD TIMBRE WITH TOUCH" };
    textfit(s, 8, 217, 1, K_GREY, hint[lmi], 304);
    snprintf(b, sizeof b, "A CHORD  B DICE  X DRONE  Y MUTE  %d NOTES", popcount12(a->mask));
    textfit(s, 8, 228, 1, K_GREY, b, 304);
}

static void page_presets(Surf *s, const UiState *u, int amb) {
    int n = amb ? A_NPRESETS : N_PRESETS;
    for (int i = 0; i < n; i++) {
        R r = r_ptile(i); int on = i == (amb ? u->apreset : u->preset);
        uint8_t rgb[3]; art_accent(amb ? ART_AMB0 + amb_preset_scene(i) : i, rgb); uint16_t ac = rgb3(rgb);
        rrect(s, r, on ? ac : K_PANEL); rframe(s, r, on ? ac : K_GRID);
        char nb[12]; snprintf(nb, sizeof nb, "%02d", i + 1);
        textfit(s, r.x + 5, r.y + 5, 1, on ? K_BG : mulc(ac, 160), nb, 20);
        fillrect(s, r.x + 5, r.y + 16, r.w - 10, 1, on ? K_BG : K_GRID);
        textc(s, r.x + r.w / 2, r.y + 22, 1, on ? K_BG : K_WHITE, amb ? amb_preset_name(i) : preset_names[i], r.w - 6);
        if (amb) {
            AmbPatch t; amb_preset(&t, i);
            char sc[24]; snprintf(sc, sizeof sc, "%s", amb_scale_name(amb_ival(&t, A_SCALE)));
            textc(s, r.x + r.w / 2, r.y + 33, 1, on ? K_BG : K_GREY, sc, r.w - 6);
        }
    }
}

static void page_files(Surf *s, const UiState *u) {
    char b[48];
    snprintf(b, sizeof b, "RECORDINGS: %d", u->nfiles);
    textfit(s, 8, 32, 1, K_GREEN, b, 200);
    textr(s, 313, 32, 1, K_GREY, "SD:/3DS/INTONARUMORI/REC", 150);
    if (u->nfiles == 0) textfit(s, 8, 60, 1, K_GREY, "NO RECORDINGS YET. TAP REC TO START.", 304);
    for (int i = 0; i < MAX_FILES; i++) {
        int k = u->fscroll + i; if (k >= u->nfiles) break;
        R r = r_frow(i); int sel = k == u->fsel;
        rrect(s, r, sel ? K_GREEN : (i & 1 ? K_PANEL : K_BG));
        uint16_t fg = sel ? K_BG : K_WHITE;
        textfit(s, r.x + 6, r.y + 5, 1, fg, u->files[k].name, 206);
        mmss(b, sizeof b, u->files[k].secs); textr(s, r.x + r.w - 6, r.y + 5, 1, sel ? K_BG : K_GREY, b, 44);
    }
    if (u->nfiles > MAX_FILES) {                       /* barra de rolagem */
        int th = 160 * MAX_FILES / u->nfiles; if (th < 8) th = 8;
        int ty = 46 + (160 - th) * u->fscroll / (u->nfiles - MAX_FILES);
        fillrect(s, 316, 46, 2, 160, K_DOT); fillrect(s, 316, ty, 2, th, K_GREEN);
    }
    R up = r_fup(), dn = r_fdn();
    rrect(s, up, K_PANEL2); rframe(s, up, K_GRID); tri(s, up.x + up.w / 2, up.y + up.h / 2, 6, -1, K_GREEN);
    rrect(s, dn, K_PANEL2); rframe(s, dn, K_GRID); tri(s, dn.x + dn.w / 2, dn.y + dn.h / 2, 6, 1, K_GREEN);
    int has = u->nfiles > 0;
    button(s, r_fren(), "RENAME", K_PANEL2, has ? K_YELL : K_DOT, K_GRID);
    button(s, r_fdel(), u->del_arm ? "SURE? TAP" : "DELETE", u->del_arm ? K_RED : K_PANEL2, u->del_arm ? K_BG : (has ? K_RED : K_DOT), K_RED);
}

static void page_set(Surf *s, const UiState *u) {
    static const char *const nm[SET_N] = { "SMOOTH", "SPRING", "LITE", "STICKS", "HOLD Y", "AUTO MUT" };
    static const char *const op[SET_N][4] = { {0,0,0,0}, {"OFF","ON",0,0}, {"OFF","ON",0,0}, {"OFF","CIRCLE","C-STK","BOTH"}, {"OFF","2 S","3 S",0}, {"OFF","10 S","20 S","40 S"} };
    char val[32], opts[4][14];
    for (int i = 0; i < SET_N; i++) {
        R row = r_srow(i); int sel = i == u->setrow;
        draw_row_frame(s, row, nm[i], sel, K_GREEN);
        if (i == 0) { snprintf(val, sizeof val, "%d%%", (int)(u->set_v[0] * 100.0f + 0.5f)); draw_ctrl(s, row, 0, 0, u->set_v[0], val, NULL, K_GREEN, sel); }
        else {
            for (int k = 0; k < set_n[i]; k++) snprintf(opts[k], 14, "%s", op[i][k]);
            draw_ctrl(s, row, set_n[i], (int)(u->set_v[i] + 0.5f), 0.0f, "", opts, K_GREEN, sel);
        }
    }
    int y = SET_Y0 + SET_N * SET_DY + 2;
    textfit(s, 8, y, 1, K_GREY, "STICKS: CIRCLE PAD / C-STICK = BEND + DRIVE", 304); y += 10;
    textfit(s, 8, y, 1, K_GREY, "HOLD Y: HOLD IT TO PAUSE.  AUTO MUT: HARSH", 304); y += 10;
    textfit(s, 8, y, 1, K_GREY, "MUTATES / AMBIENT ROLLS A NEW LEAD PATTERN", 304); y += 11;
    textfit(s, 8, y, 1, K_WHITE, "INTONARUMORI 0.9", 150);
    if (u->rec_err) textfit(s, 150, y, 1, K_RED, "REC ERROR: CHECK SD", 164);
    else if (u->rec_drop) { snprintf(val, sizeof val, "DROPPED: %u", (unsigned)u->rec_drop); textfit(s, 150, y, 1, K_RED, val, 164); }
}

void ui_draw_bottom(Surf *s, const UiState *u) {
    fillrect(s, 0, 0, s->w, s->h, K_BG);
    fillrect(s, 0, TAB_H + 1, s->w, 1, K_GRID);
    tabs(s, u);
    switch (u->page) {
    case PG_HARSH: if (u->sub == 0) page_harsh_play(s, u); else page_edit(s, u); break;
    case PG_AMB:   if (u->sub == 0) page_amb_play(s, u);   else page_edit(s, u); break;
    case PG_HPRE:  page_presets(s, u, 0); break;
    case PG_APRE:  page_presets(s, u, 1); break;
    case PG_FILES: page_files(s, u); break;
    case PG_SET:   page_set(s, u); break;
    }
}

/* ---------------- tela de cima ---------------- */
static float midi_hz_f(int m) { return 440.0f * exp2f((float)(m - 69) / 12.0f); }

static void card(Surf *s, R r, const char *label, const char *val, float v01, uint16_t col) {
    rrect(s, r, K_PANEL); rframe(s, r, K_GRID);
    textfit(s, r.x + 5, r.y + 5, 1, K_GREY, label, r.w - 10);
    int sc = (int)strlen(val) * 12 <= r.w - 10 ? 2 : 1;
    textfit(s, r.x + 5, r.y + 15 + (sc == 1 ? 3 : 0), sc, K_WHITE, val, r.w - 10);
    segbar(s, r.x + 5, r.y + r.h - 9, r.w - 10, 4, v01, col);
}

/* anunciadores de LCD (estilo Game&Watch): placas escuras; o icone aceso brilha, o apagado fica como fantasma */
static void annun(Surf *s, int i, int kind, int lit, uint16_t col) {
    R r = { 238 + i * 26, 3, 22, 15 };
    rrect(s, r, lit ? mulc(col, 52) : RGB(8, 16, 14)); rframe(s, r, lit ? col : K_GRID);
    uint16_t c = lit ? col : mulc(col, 46); int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    switch (kind) {
    case 0: tri(s, cx, cy, 4, 1, c); break;                                           /* PL: tocando */
    case 1: disc(s, cx, cy, 3, c); break;                                             /* RC: gravando */
    case 2: fillrect(s, cx - 4, cy - 4, 3, 8, c); fillrect(s, cx + 1, cy - 4, 3, 8, c); break;   /* PA: pausa */
    case 3: textc(s, cx, cy - 3, 1, c, "MU", 20); break;                              /* mute */
    case 4: textc(s, cx, cy - 3, 1, c, "BU", 20); break;                              /* burst */
    default: textc(s, cx, cy - 3, 1, c, lit ? "AM" : "HR", 20); break;               /* motor: AM = ambient, HR = harsh */
    }
}

void ui_draw_top(Surf *s, const UiState *u) {
    char b[48], t[24];
    int amb = u->engine == 1;
    fillrect(s, 0, 0, s->w, s->h, K_BG);
    /* cabecalho: o nome do app so aparece na abertura (art_intro); aqui ficam so os anunciadores */
    fillrect(s, 0, 20, s->w, 1, K_GRID);
    int blink = (u->frame >> 2) & 1;
    annun(s, 0, 0, !u->paused && !u->mute, K_GREEN);
    annun(s, 1, 1, u->rec_on, K_RED);
    annun(s, 2, 2, u->paused || (u->holdy > 0 && blink), K_AMBER);
    annun(s, 3, 3, u->mute, K_RED);
    annun(s, 4, 4, u->burst, K_AMBER);
    annun(s, 5, 5, amb, K_YELL);
    if (u->holdy > 0) fillrect(s, 238 + 2 * 26, 18, 22 * u->holdy / 100, 2, K_AMBER);
    if (u->rec_on) { mmss(t, sizeof t, u->rec_secs); snprintf(b, sizeof b, "REC %s", t); textfit(s, 8, 5, 2, K_RED, b, 150); }
    else if (u->holdy > 0) textfit(s, 8, 5, 2, K_AMBER, "HOLD Y", 150);

    /* cena + painel */
    int scene = amb ? ART_AMB0 + (u->ap ? u->ap->scene : 0) : (u->preset < 0 ? ART_BLANK : u->preset);
    uint8_t rgb[3]; art_accent(scene, rgb); uint16_t ac = rgb3(rgb);
    fillrect(s, 6, 24, 136, 136, mulc(ac, 90)); fillrect(s, 7, 25, 134, 134, K_BG);
    art_scene(s, 8, 26, scene, u->frame, u->paused ? 0.0f : u->level, u->venv);
    fillrect(s, 155, 25, 236, 136, mulc(ac, 90)); fillrect(s, 156, 26, 234, 134, K_BG);
    ArtDisp d; memset(&d, 0, sizeof d);
    d.scope = u->scope; d.spp = u->spp < 1 ? 1 : u->spp; d.level = u->level; d.scene = scene; d.frame = u->frame; d.paused = u->paused;
    char lb[12];
    if (amb) {
        int n = u->cur_note > 0 ? u->cur_note : 57;
        d.hz = (int)(midi_hz_f(n) + 0.5f);
        amb_chord_label(u->ap, u->cur_deg, lb, sizeof lb);
        snprintf(b, sizeof b, "%s %s%d", lb, note_name(n % 12), n / 12 - 1); d.sub = b;
        d.title = u->apreset >= 0 ? amb_preset_name(u->apreset) : "CUSTOM"; d.idx = u->apreset >= 0 ? u->apreset + 1 : 0; d.total = A_NPRESETS;
    } else {
        d.hz = (int)(param_hz(u->v[P_PITCH]) + 0.5f);
        d.sub = wave_name(wave_index(u->v));
        d.title = u->preset >= 0 ? preset_names[u->preset] : "RANDOM";
        d.idx = u->preset >= 0 ? u->preset + 1 : 0; d.total = N_PRESETS;
    }
    art_display(s, 156, 27, &d);

    /* cartoes de parametros */
    R c[4]; for (int i = 0; i < 4; i++) { R r = { 8 + i * 96, 168, 92, 40 }; c[i] = r; }
    if (amb) {
        const AmbPatch *a = u->ap; int nd = amb_nd(a);
        char rn[8]; amb_roman(a, u->cur_deg, rn, sizeof rn);
        snprintf(t, sizeof t, "%s", lb);
        card(s, c[0], rn, t, (float)(u->cur_deg + 1) / (float)nd, K_GREEN);
        amb_ptext(a, A_DSND, t, sizeof t);  card(s, c[1], "DRONE", a->drone_on ? t : "OFF", a->drone_on ? a->p[A_DVOL] : 0.0f, K_GREEN);
        amb_ptext(a, A_LMODE, t, sizeof t); card(s, c[2], "LEAD", a->lead_on ? t : "OFF", a->lead_on ? a->p[A_LVOL] : 0.0f, K_YELL);
        amb_ptext(a, A_VOL, t, sizeof t);   card(s, c[3], "MASTER", t, a->p[A_VOL], K_AMBER);
    } else {
        int ids[4] = { u->xid, u->yid, P_FB, P_VOL };
        static const int alt[4] = { P_DRIVE, P_FB, P_FOLD, P_NAMT };
        for (int k = 0; k < 4; k++) if (ids[2] == u->xid || ids[2] == u->yid) ids[2] = alt[k];
        static const uint16_t cc[4] = { RGB(100,255,170), RGB(246,232,84), RGB(255,140,32), RGB(180,255,92) };
        for (int i = 0; i < 4; i++) { param_text(u->v, ids[i], t, sizeof t); card(s, c[i], param_name(ids[i]), t, u->v[ids[i]], cc[i]); }
    }

    /* rodape: VU + dica (o analogico mostra a leitura quando se mexe) */
    vumeter(s, 8, 216, 32, 5, 4, 9, u->paused ? 0.0f : u->level);
    if (fabsf(u->stick_x) > 0.05f || fabsf(u->stick_y) > 0.05f) {
        snprintf(b, sizeof b, "STICK X %+d%%  Y %+d%%", (int)(u->stick_x * 100.0f), (int)(u->stick_y * 100.0f));
        textfit(s, 176, 217, 1, K_AMBER, b, 216);
    } else textfit(s, 176, 217, 1, K_GREY, amb ? "PAD=CHORD  A NEXT  B DICE  X PANE" : "X BURST  Y MUTE  A MUTATE  B RANDOM", 216);
    textfit(s, 8, 229, 1, K_GREY, u->paused ? "PAUSED: TAP PLAY, ZL/ZR OR HOLD Y" : "TAP PAUSE / ZL ZR / HOLD Y 2S      START EXIT", 384);
}

void ui_draw_error(Surf *s, int kind) {
    (void)kind;
    fillrect(s, 0, 0, s->w, s->h, K_BG);
    int y = 10;
    textfit(s, 8, y, 2, K_AMBER, "INTONARUMORI", 304); y += 24;
    textfit(s, 8, y, 1, K_RED, "ERRO: NAO FOI POSSIVEL INICIAR O AUDIO (DSP).", 304); y += 16;
    textfit(s, 8, y, 1, K_AMBER, "FALTA O ARQUIVO  SD:/3DS/DSPFIRM.CDC", 304); y += 20;
    textfit(s, 8, y, 1, K_GREEN, "COMO CRIAR:", 304); y += 12;
    textfit(s, 8, y, 1, K_WHITE, "1. SAIA DESTE PROGRAMA (START).", 304); y += 11;
    textfit(s, 8, y, 1, K_WHITE, "2. APERTE L + SETA PRA BAIXO + SELECT", 304); y += 11;
    textfit(s, 8, y, 1, K_WHITE, "   (MENU ROSALINA DO LUMA3DS).", 304); y += 11;
    textfit(s, 8, y, 1, K_WHITE, "3. MISCELLANEOUS OPTIONS", 304); y += 11;
    textfit(s, 8, y, 1, K_WHITE, "4. DUMP DSP FIRMWARE", 304); y += 11;
    textfit(s, 8, y, 1, K_WHITE, "5. ABRA O INTONARUMORI DE NOVO.", 304); y += 20;
    textfit(s, 8, y, 1, K_GREEN, "START = SAIR", 304);
}

/* so para teste no PC: a cena do preset `id` na janela 132x132 em (8,26) */
void ui_vis_test(Surf *s, int id, int t, float lv) {
    static const float ve[AMB_V] = { .3f, .2f, .5f, 0, .1f, 0, 0, 0, .4f, 0 };
    art_scene(s, 8, 26, id, t, lv, ve);
}

