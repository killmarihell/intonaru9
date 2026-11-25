/* INTONARUMORI 0.9 - arte vetorial (ver art.h).
 * Usa o motor grafico do h1 (gfx.c, MIT, (c) Jake Handy): framebuffer de intensidade 8 bits com
 * primitivas anti-aliased. Cada elemento e desenhado em 3 passadas (fantasma / halo / nucleo) como
 * os segmentos de um LCD/VFD: os apagados continuam visiveis, os acesos brilham.
 * A cor vem de uma paleta aplicada na hora de apresentar (4 tintas por quadro). */
#include "art.h"
#include "gfx.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11) | (((g)>>2)<<5) | ((b)>>3)))
typedef struct { uint8_t r, g, b; } Col;
#define CG {100,255,170}
#define CL {180,255,92}
#define CY {246,232,84}
#define CA {255,140,32}
#define CW {228,250,236}
static const Col acc[ART_SCENES]  = { CG,CW,CA,CY,CG,CL,CA,CG, CA,CG,CY,CG,CY,CA,CL,CA, CW, CG,CW,CA,CL };
static const Col acc2[ART_SCENES] = { CY,CG,CY,CA,CL,CA,CY,CY, CY,CA,CA,CY,CA,CY,CA,CY, CG, CY,CG,CY,CG };
static const Col BG_ART = {4, 9, 8};
static const Col K_AMB = CA;

/* ---------------- composicao: 4 tintas por quadro ---------------- */
static uint8_t cmp_i[FB_W * FB_H], cmp_k[FB_W * FB_H];     /* intensidade e tinta de cada pixel */
static uint8_t gh_i[FB_W * FB_H], gh_k[FB_W * FB_H];       /* fantasma em cache (por cena) */
static int gh_id = -1;
static int reg_w = ART_W, reg_h = ART_W, cur_ink = 0, pend = 0;

static void commit(void) {
    if (!pend) return;
    for (int y = 0; y < reg_h; y++) {
        uint8_t *f = g_fb + y * FB_W, *ci = cmp_i + y * FB_W, *ck = cmp_k + y * FB_W;
        for (int x = 0; x < reg_w; x++) {
            uint8_t a = f[x];
            if (a) { if (a > ci[x]) { ci[x] = a; ck[x] = (uint8_t)cur_ink; } f[x] = 0; }
        }
    }
    pend = 0;
}
static void ink_sel(int i) { if (i != cur_ink) { commit(); cur_ink = i; } }
static void begin_frame(int w, int h) {
    reg_w = w; reg_h = h; cur_ink = 0; pend = 0;
    g_clear(); g_mode(G_MAX); g_dim(1.0f); g_origin(0, 0);
}

typedef uint16_t Lut[4][256];
static void build_lut(Lut lut, const Col *ink) {
    for (int k = 0; k < 4; k++)
        for (int v = 0; v < 256; v++) {
            float f = (float)v / 255.0f, c = f * (2.0f - f) * 0.2f + f * 0.8f;     /* curva suave do fosforo */
            float hot = v > 215 ? (float)(v - 215) / 40.0f * 0.30f : 0.0f;        /* nucleo quase branco */
            int r = BG_ART.r + (int)((ink[k].r - BG_ART.r) * c), g = BG_ART.g + (int)((ink[k].g - BG_ART.g) * c), b = BG_ART.b + (int)((ink[k].b - BG_ART.b) * c);
            r += (int)((255 - r) * hot); g += (int)((255 - g) * hot); b += (int)((255 - b) * hot);
            lut[k][v] = RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
        }
}
/* apresenta a regiao composta; pixels vazios mostram a malha do vidro */
static void present(Surf *s, int dx, int dy, int w, int h, const Lut lut) {
    const uint16_t bg = RGB(BG_ART.r, BG_ART.g, BG_ART.b), mesh = RGB(11, 24, 20);
    for (int x = 0; x < w; x++) {
        int sx = dx + x;
        if ((unsigned)sx >= (unsigned)s->w) continue;
        uint16_t *col = s->px + sx * s->h;
        for (int y = 0; y < h; y++) {
            int sy = dy + y;
            if ((unsigned)sy >= (unsigned)s->h) continue;
            int i = y * FB_W + x; uint8_t v = cmp_i[i];
            col[s->h - 1 - sy] = v ? lut[cmp_k[i]][v] : (((x & 3) == 1 && (y & 3) == 1) ? mesh : bg);
        }
    }
}

/* ---------------- motor de segmentos (mesma API das cenas da v0.6) ---------------- */
typedef struct { int pass, ox, oy; float z; } Vw;
static const float *ve;                       /* envelopes das vozes (cena ambient) */
static uint32_t hsh(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 73856093u ^ b * 19349663u ^ c * 83492791u;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;
    return h;
}

/* a = brilho, g = quanto engorda (halo). devolve 0 se nao desenha nesta passada */
static int mode_of(const Vw *v, int lit, float *a, float *g) {
    if (v->pass == 0) { *a = 0.17f; *g = 0.0f; return 1; }
    if (!lit) return 0;
    if (v->pass == 1) { *a = 0.30f; *g = 2.6f; return 1; }
    *a = 1.0f; *g = 0.0f; return 1;
}
#define TX(x) (66.0f + ((float)(x) - 66.0f) * v->z)          /* zoom da cena em torno do centro da janela */
#define TY(y) (70.0f + ((float)(y) - 70.0f) * v->z)
#define TS(s) ((float)(s) * v->z)
#define OFX (v->pass ? (float)v->ox : 0.0f)
#define OFY (v->pass ? (float)v->oy : 0.0f)

static void sR(Vw *v, int lit, int x, int y, int w, int h) {
    float a, g; if (!mode_of(v, lit, &a, &g)) return; pend = 1;
    float fx = TX(x) + OFX, fy = TY(y) + OFY, fw = TS(w), fh = TS(h), mn = fw < fh ? fw : fh;
    if (mn < 10 && g == 0.0f) {                           /* segmento pequeno: pilula de pontas redondas */
        float r = (fh - 1.0f) * 0.5f;
        if (fabsf(fw - fh) < 0.5f) g_disc(fx + (fw - 1.0f) * 0.5f, fy + (fh - 1.0f) * 0.5f, fw * 0.5f, a);
        else if (fw > fh) g_line(fx + r, fy + r, fx + fw - 1.0f - r, fy + r, fh, a);
        else { float rr = (fw - 1.0f) * 0.5f; g_line(fx + rr, fy + rr, fx + rr, fy + fh - 1.0f - rr, fw, a); }
        return;
    }
    if (mn < 10) {                                        /* halo da pilula */
        float gw = mn + g;
        if (fw >= fh) g_line(fx + (fh - 1.0f) * 0.5f, fy + (fh - 1.0f) * 0.5f, fx + fw - 1.0f - (fh - 1.0f) * 0.5f, fy + (fh - 1.0f) * 0.5f, gw, a);
        else g_line(fx + (fw - 1.0f) * 0.5f, fy + (fw - 1.0f) * 0.5f, fx + (fw - 1.0f) * 0.5f, fy + fh - 1.0f - (fw - 1.0f) * 0.5f, gw, a);
        return;
    }
    g_rect(fx - g * 0.5f, fy - g * 0.5f, fw + g, fh + g, a);
}
static void sFR(Vw *v, int lit, int x, int y, int w, int h, int t) {
    float a, g; if (!mode_of(v, lit, &a, &g)) return; pend = 1;
    float fx = TX(x) + OFX, fy = TY(y) + OFY, fw = TS(w), fh = TS(h), ht = (float)(t - 1) * 0.5f;
    float p[10] = { fx + ht, fy + ht, fx + fw - 1.0f - ht, fy + ht, fx + fw - 1.0f - ht, fy + fh - 1.0f - ht, fx + ht, fy + fh - 1.0f - ht, fx + ht, fy + ht };
    g_poly(p, 5, 0, (float)t + g, a);
}
static void sD(Vw *v, int lit, int cx, int cy, int r) {
    float a, g; if (!mode_of(v, lit, &a, &g)) return; pend = 1;
    g_disc(TX(cx) + OFX, TY(cy) + OFY, TS(r) + g * 0.5f, a);
}
static void sO(Vw *v, int lit, int cx, int cy, int r, int th) {
    float a, g; if (!mode_of(v, lit, &a, &g)) return; pend = 1;
    g_ring(TX(cx) + OFX, TY(cy) + OFY, TS(r) - (float)th * 0.5f, (float)th + g, a);
}
static void sL(Vw *v, int lit, int x0, int y0, int x1, int y1, int th) {
    float a, g; if (!mode_of(v, lit, &a, &g)) return; pend = 1;
    g_line(TX(x0) + OFX, TY(y0) + OFY, TX(x1) + OFX, TY(y1) + OFY, (float)th + g, a);
}
static void sEA(Vw *v, int lit, int cx, int cy, int rx, int ry, int a0, int a1, int th) {
    float a, g; if (!mode_of(v, lit, &a, &g)) return; pend = 1;
    float ox = TX(cx) + OFX, oy = TY(cy) + OFY, frx = TS(rx), fry = TS(ry);
    float rm = frx > fry ? frx : fry; int n = (int)(rm * (float)(a1 - a0) * 0.01745f / 3.0f) + 2;
    if (n > 40) n = 40;
    float p[2 * 42];
    for (int i = 0; i <= n; i++) {
        float ang = ((float)a0 + (float)(a1 - a0) * (float)i / (float)n) * 0.0174533f;
        p[2 * i] = ox + cosf(ang) * frx; p[2 * i + 1] = oy + sinf(ang) * fry;
    }
    g_poly(p, n + 1, 0, (float)th + g, a);
}
/* curva suave (Catmull-Rom) por pontos de controle c[2n]; smth = 0 liga os pontos em reta */
static void spoly(Vw *v, int lit, const float *c, int n, int closed, int smth, int th) {
    float a, g; if (!mode_of(v, lit, &a, &g)) return; pend = 1;
    float q[2 * 200]; int m = 0, per = smth ? (190 / n > 9 ? 9 : 190 / n) : 1, segs = closed ? n : n - 1;
    if (per < 1) per = 1;
    for (int i = 0; i < segs; i++) {
        int i0 = closed ? (i + n - 1) % n : (i > 0 ? i - 1 : 0), i1 = i, i2 = (i + 1) % n, i3 = closed ? (i + 2) % n : (i + 2 < n ? i + 2 : n - 1);
        for (int k = 0; k < per; k++) {
            float u = (float)k / (float)per, u2 = u * u, u3 = u2 * u, px, py;
            if (smth) {
                px = 0.5f * (2.0f * c[2*i1] + (-c[2*i0] + c[2*i2]) * u + (2.0f * c[2*i0] - 5.0f * c[2*i1] + 4.0f * c[2*i2] - c[2*i3]) * u2 + (-c[2*i0] + 3.0f * c[2*i1] - 3.0f * c[2*i2] + c[2*i3]) * u3);
                py = 0.5f * (2.0f * c[2*i1+1] + (-c[2*i0+1] + c[2*i2+1]) * u + (2.0f * c[2*i0+1] - 5.0f * c[2*i1+1] + 4.0f * c[2*i2+1] - c[2*i3+1]) * u2 + (-c[2*i0+1] + 3.0f * c[2*i1+1] - 3.0f * c[2*i2+1] + c[2*i3+1]) * u3);
            } else { px = c[2*i1]; py = c[2*i1+1]; }
            q[m++] = TX(px) + OFX; q[m++] = TY(py) + OFY;
        }
    }
    if (!closed) { q[m++] = TX(c[2*(n-1)]) + OFX; q[m++] = TY(c[2*(n-1)+1]) + OFY; }
    g_poly(q, m / 2, closed, (float)th + g, a);
}
/* 7 segmentos com pontas redondas */
static void sS7(Vw *v, int x, int y, int w, int h, int t, int d) {
    static const uint8_t seg[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
    int m = d >= 0 ? seg[d % 10] : 0;
    float fx = TX(x), fy = TY(y), fw = TS(w), fh = TS(h), hh = fh * 0.5f, g = (float)(t / 2 + 1);
    float cx0 = fx + (float)t * 0.5f, cx1 = fx + fw - (float)t * 0.5f;
    float cy0 = fy + (float)t * 0.5f, cy1 = fy + hh, cy2 = fy + fh - (float)t * 0.5f;
#define SEG(bit, xa, ya, xb, yb) do { float a_, g_; if (mode_of(v, m & (bit), &a_, &g_)) { pend = 1; \
        float ox_ = OFX, oy_ = OFY; \
        g_line((xa) + ox_, (ya) + oy_, (xb) + ox_, (yb) + oy_, (float)t * 0.9f + g_, a_); } } while (0)
    SEG(1,  cx0 + g, cy0, cx1 - g, cy0);
    SEG(2,  cx1, cy0 + g, cx1, cy1 - g);
    SEG(4,  cx1, cy1 + g, cx1, cy2 - g);
    SEG(8,  cx0 + g, cy2, cx1 - g, cy2);
    SEG(16, cx0, cy1 + g, cx0, cy2 - g);
    SEG(32, cx0, cy0 + g, cx0, cy1 - g);
    SEG(64, cx0 + g, cy1, cx1 - g, cy1);
#undef SEG
}

#define K(i)                  ink_sel(i)
#define gR(l,x,y,w,h)         sR(v,(l),x,y,w,h)
#define gF(l,x,y,w,h,th)      sFR(v,(l),x,y,w,h,th)
#define gD(l,x,y,r)           sD(v,(l),x,y,r)
#define gO(l,x,y,r,th)        sO(v,(l),x,y,r,th)
#define gL(l,x0,y0,x1,y1,th)  sL(v,(l),x0,y0,x1,y1,th)
#define gA(l,x,y,r,a0,a1,th)  sEA(v,(l),x,y,r,r,a0,a1,th)
#define gE(l,x,y,rx,ry,a0,a1,th) sEA(v,(l),x,y,rx,ry,a0,a1,th)
#define LIVE (v->pass != 0)

typedef void (*SceneFn)(Vw *v, int t, float lv);

/* ------------------------------------------------------------------ */
/* CENAS: 0..15 presets (7 VOID refeita na 0.8), 16 fita, 17..20 ambient */
/* (mar, chuva, deserto, espaco): tudo em segmentos fixos com fantasma,  */
/* halo e nucleo, como um LCD Game&Watch.                                */
/* ------------------------------------------------------------------ */

/* 0 DRONE: quadricoptero pairando, helices girando em 3 fases */
static void sc_drone(Vw *v, int t, float lv) {
    int sp = lv > 0.30f ? 2 : 4, ph = (t / sp) % 3, tk = t / 8;
    K(0);
    gR(1, 50,56,32,12); gR(1, 58,50,16,6); gD(1, 66,75,4);        /* corpo, cupula, camera */
    gL(1, 50,60, 25,53, 3); gL(1, 82,60, 107,53, 3);               /* bracos */
    gR(1, 21,52,8,8); gR(1, 103,52,8,8);                           /* motores */
    gL(1, 54,68, 46,86, 3); gL(1, 78,68, 86,86, 3);                /* trem de pouso */
    gR(1, 40,86,14,3); gR(1, 78,86,14,3);
    gL(1, 66,50, 66,38, 2); gD(1, 66,36, 2);                       /* antena */
    gR(1, 4,118,124,2);                                            /* chao */
    K(1);
    gR(ph == 0, 4,46,40,3);   gR(ph == 0, 88,46,40,3);             /* helices */
    gR(ph == 1, 11,46,26,3);  gR(ph == 1, 95,46,26,3);
    gR(ph == 2, 18,46,12,3);  gR(ph == 2, 102,46,12,3);
    K(2);
    for (int i = 0; i < 3; i++)                                    /* sinal de radio */
        gA(i <= tk % 3 && i <= (int)(lv * 3.0f), 66,36, 8 + i * 7, 225, 315, 2);
    for (int i = 0; i < 3; i++) {                                  /* vento das helices */
        gR(((i + tk) & 1) == 0, 23,66 + i * 9, 2,5);
        gR(((i + tk) & 1) == 0, 107,66 + i * 9, 2,5);
    }
    gR(tk % 3 == 0, 46,110,40,3); gR(tk % 3 == 1, 36,110,60,3); gR(tk % 3 == 2, 26,110,80,3);   /* sombra */
}

/* 1 STATIC: televisao de tubo com chuvisco e antena balancando */
static void sc_static(Vw *v, int t, float lv) {
    int ea = (t / 10) & 1, tk = t / 3, rb = (t / 4) % 8;
    K(2);
    gF(1, 12,34,108,84,3); gF(1, 20,42,70,68,2);
    gO(1, 104,60,8,2); gO(1, 104,84,8,2);
    gL(ea == 0, 104,60, 104,54, 2); gL(ea == 1, 104,60, 109,63, 2);
    gL(ea == 1, 104,84, 104,78, 2); gL(ea == 0, 104,84, 99,87, 2);
    gR(1, 97,98,14,2); gR(1, 97,102,14,2); gR(1, 97,106,14,2);
    gL(1, 28,118, 26,127, 3); gL(1, 104,118, 106,127, 3);
    K(0);
    gD(1, 66,32, 4);
    gL(ea == 0, 66,30, 40,8, 2); gL(ea == 0, 66,30, 94,10, 2);
    gL(ea == 1, 66,30, 44,6, 2); gL(ea == 1, 66,30, 90,6, 2);
    for (int cy = 0; cy < 7; cy++) for (int cx = 0; cx < 9; cx++) {
        int snow = (int)(hsh(cx, cy, tk) & 255) < 62 + (int)(lv * 110.0f);
        gR(snow || (cy * 8 / 7 == rb), 24 + cx * 7, 46 + cy * 9, 5, 7);
    }
}

/* 2 HARSH: pilha de amplificador socando, raios dos dois lados */
static void sc_harsh(Vw *v, int t, float lv) {
    int th = (t / 3) & 1, tk = t / 4, hit = lv > 0.12f;
    int bl = (tk % 3 == 0) || lv > 0.5f, br = (tk % 3 == 1) || lv > 0.5f;
    v->ox = hit ? (th ? 2 : -2) : 0;
    K(0);
    gF(1, 30,14,72,22,3);
    for (int i = 0; i < 5; i++) gD(1, 42 + i * 12, 25, 3);
    gF(1, 30,40,72,76,3);
    for (int sp = 0; sp < 2; sp++) {
        int cx = sp ? 84 : 48, cy = 80;
        gO(1, cx,cy,15,3);
        K(1);
        gO(th == 0, cx,cy,10,2); gO(th == 1, cx,cy,8,2);
        gD(th == 0, cx,cy,5); gD(th == 1, cx,cy,3);
        K(0);
    }
    v->ox = 0;
    gR(1, 36,118,12,4); gR(1, 84,118,12,4);
    K(2);
    gL(bl, 24,20, 14,42, 3); gL(bl, 14,42, 22,42, 3); gL(bl, 22,42, 10,70, 3);
    gL(br, 108,20, 118,42, 3); gL(br, 118,42, 110,42, 3); gL(br, 110,42, 122,70, 3);
}

/* 3 SHREDDER: trituradora de papel sobre lixeira; folha entra, tiras caem */
static void sc_shredder(Vw *v, int t, float lv) {
    int sp = lv > 0.30f ? 4 : 7, ph = (t / sp) % 3, tk = t / sp;
    static const float bin[8] = { 30,72, 102,72, 96,112, 36,112 };
    K(1);
    gF(1, 46,4,40,46,2);                                              /* folha */
    for (int r = 0; r < 5; r++) gR(((r + tk) & 1) == 0, 53, 11 + r * 8, 26 - (r % 3) * 5, 2);
    K(0);
    gF(1, 22,50,88,22,3); gR(1, 32,58,60,4);                          /* cabeca com a boca */
    gD(1, 98,61,3); gL(1, 28,56, 28,66, 2);
    spoly(v, 1, bin, 4, 1, 0, 3);                                     /* lixeira */
    gR(1, 38,113,14,4); gR(1, 80,113,14,4);                          /* pes */
    K(3);
    gD(lv > 0.05f || (tk & 1), 98,61,3);
    K(2);
    for (int r = 0; r < 3; r++) for (int i = 0; i < 8; i++)           /* tiras caindo */
        gR(((tk + i * 2 + r) % 4) < 2, 42 + i * 7, 78 + r * 11, 3, 9);
    for (int i = 0; i < 8; i++) gR(i + 1 <= 2 + ph * 2, 41 + i * 7, 106, 5, 3);   /* monte no fundo */
}

/* 4 DIGITAL: chip com pinos, bits correndo nas trilhas */
static void sc_digital(Vw *v, int t, float lv) {
    int tk = t / 3, dg = t / 7;
    (void)lv;
    K(0);
    gF(1, 36,32,60,60,3); gA(1, 66,33, 6, 0, 180, 2);
    for (int i = 0; i < 5; i++) {
        gR(1, 20,41 + i * 10, 16,4); gR(1, 96,41 + i * 10, 16,4);
    }
    K(2);
    for (int i = 0; i < 5; i++) {
        gR(1, 4,42 + i * 10, 16,2); gR(1, 112,42 + i * 10, 16,2);
        for (int k = 0; k < 3; k++) {
            gR((tk + i) % 3 == k, 4 + k * 5, 40 + i * 10, 4,6);
            gR((tk + i) % 3 == 2 - k, 114 + k * 5, 40 + i * 10, 4,6);
        }
    }
    K(1);
    sS7(v, 47,44, 16,30, 3, (int)(hsh(dg, 1, 1) % 10));
    sS7(v, 69,44, 16,30, 3, (int)(hsh(dg, 2, 2) % 10));
    for (int i = 0; i < 8; i++) gR(((hsh(i, tk, 4) >> 3) & 1), 45 + i * 6, 80, 4,4);
}

/* 5 FEEDBACK: caixa de som -> microfone -> cabo -> caixa (laco de realimentacao) */
static void fb_path(int d, int *x, int *y) {         /* cabo: 24 posicoes */
    if (d < 4) { *x = 114; *y = 104 + d * 4; }
    else if (d < 20) { *x = 114 - (d - 4) * 5 - 2; *y = 120; }
    else { *x = 26; *y = 118 - (d - 20) * 5; }
}
static void sc_feedback(Vw *v, int t, float lv) {
    int tk = t / 4, wv = tk % 5;
    K(0);
    gF(1, 6,32,42,68,3); gO(1, 27,48,6,2); gO(1, 27,76,15,3); gD(1, 27,76,5);
    K(2);
    gO(1, 104,46,12,3); gL(1, 96,46, 112,46, 2); gL(1, 104,38, 104,54, 2);
    gL(1, 107,62, 115,100, 7);
    K(1);
    for (int i = 0; i < 4; i++)
        gA(wv == i || (lv > 0.3f && wv == i + 1), 27,76, 24 + i * 13, -28, 28, 3);
    K(2);
    for (int d = 0; d < 24; d++) {
        int x, y; fb_path(d, &x, &y);
        gR(((d + tk) & 3) == 0, x - 2, y - 1, 5, 3);
    }
    K(1);
    if (LIVE && lv > 0.45f) { gL(1, 12,22, 8,14, 2); gL(1, 27,18, 27,10, 2); gL(1, 42,22, 46,14, 2); }
}

/* 6 INDUSTRIAL: fabrica com engrenagem, chamine e fumaca subindo */
static void sc_industrial(Vw *v, int t, float lv) {
    int tk = t / 5, ge = (t / (lv > 0.30f ? 3 : 6)) & 1;
    K(0);
    gR(1, 10,72,3,48); gR(1, 84,72,3,48); gR(1, 10,118,77,3);
    for (int i = 0; i < 3; i++) { gL(1, 11 + i * 25,72, 11 + i * 25,58, 3); gL(1, 11 + i * 25,58, 36 + i * 25,72, 3); }
    gO(1, 48,97,9,4); gD(1, 48,97,3);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 0.7854f, b = a + 0.3927f;
        gL(ge == 0, 48 + (int)(cosf(a) * 12), 97 + (int)(sinf(a) * 12), 48 + (int)(cosf(a) * 18), 97 + (int)(sinf(a) * 18), 4);
        gL(ge == 1, 48 + (int)(cosf(b) * 12), 97 + (int)(sinf(b) * 12), 48 + (int)(cosf(b) * 18), 97 + (int)(sinf(b) * 18), 4);
    }
    gF(1, 94,42,14,78,3);
    K(2);
    gR(1, 97,54,8,4); gR(1, 97,68,8,4); gR(1, 97,82,8,4);
    K(1);
    static const int8_t pf[5][3] = { {100,34,4}, {106,25,5}, {99,15,6}, {107,6,6}, {100,-2,6} };
    for (int i = 0; i < 5; i++) gD(i < tk % 6, pf[i][0], pf[i][1], pf[i][2]);
}


/* 8 WALL: caixa de som martelando uma parede de tijolos */
static void sc_wall(Vw *v, int t, float lv) {
    int tk = t / 5, ph = (t / 3) % 3;
    K(1);
    gF(1, 3,40,34,64,3); gO(1, 20,66,10,2); gD(1, 20,66,3);
    gA(ph == 0, 20,72, 24, -34, 34, 3); gA(ph == 1, 20,72, 36, -34, 34, 3); gA(ph == 2, 20,72, 48, -34, 34, 3);
    for (int r = 0; r < 7; r++) for (int c = 0; c < 4; c++) {
        int odd = r & 1, x = odd ? 44 + (c - 1) * 28 + 14 : 44 + c * 28, w = 26, y = 18 + r * 14;
        if (odd && c == 0) { x = 44; w = 12; } else if (odd) x = 44 + (c - 1) * 28 + 14;
        if (!odd && c == 3) continue;
        if (odd && c == 3) { x = 114; w = 12; }
        int gone = (hsh(r, c, tk / 2) & 7) == 0;
        K(0);
        v->ox = (LIVE && lv > 0.1f) ? (int)(hsh(r, c, t / 2) & 1) * 2 : 0;
        gR(!gone, x, y, w, 12);
        v->ox = 0;
        K(2);
        if (LIVE && w > 20) { gR(gone, x + 6, y + 14, 6, 4); gR(gone, x + 17, y + 16, 4, 4); }
    }
}

/* 9 GLITCH: busto em linhas de varredura; as linhas escorregam e deixam eco ambar */
static void sc_glitch(Vw *v, int t, float lv) {
    static const float sh[12] = { 10,126, 28,115, 52,110, 80,110, 104,115, 122,126 };
    const int cx = 66, cy = 62;
    K(0);
    for (int r = 0; r < 20; r++) {
        int y = 26 + r * 4;
        float d = (float)(y - cy) / 38.0f, hw = d * d >= 1.0f ? 0.0f : 31.0f * sqrtf(1.0f - d * d);
        uint32_t g = hsh(r, t / 3, 7);
        int sx = 0;
        if (LIVE && (g % 8 == 0 || (lv > 0.5f && g % 5 == 0))) sx = (int)((g >> 8) % 17) - 8;
        if (sx) { K(2); v->ox = -sx / 2 - (sx > 0 ? 3 : -3); gL(1, cx - (int)hw, y, cx + (int)hw, y, 2); }
        K(0); v->ox = sx;
        gL(1, cx - (int)hw, y, cx + (int)hw, y, 2);
        v->ox = 0;
    }
    K(2);
    gL(1, 55,98, 53,110, 2); gL(1, 77,98, 79,110, 2);
    spoly(v, 1, sh, 6, 0, 1, 2);
    K(1);
    gR(!(t % 120 > 112), 49,52,14,7); gR(!(t % 120 > 112), 71,52,14,7);   /* olhos (piscam) */
    K(3);
    for (int i = 0; i < 7; i++) {                                         /* boca: equalizador de 3 segmentos */
        int h = (int)(lv * 4.0f * (float)(1 + (int)(hsh(i, t / 3, 5) & 3)) * 0.5f);
        for (int k = 0; k < 3; k++) gR(k == 1 || k < h, 52 + i * 4, 82 + k * 4, 3, 3);
    }
    K(1);
    if (LIVE) {
        gR(1, 0, 20 + (int)(hsh(t / 2, 2, 2) % 100), 132, 2);
        for (int i = 0; i < 3; i++) if (hsh(i, t / 4, 9) & 1) gR(1, (int)(hsh(i, t, 1) % 110), 20 + (int)(hsh(t, i, 3) % 100), 14, 4);
    }
}

/* 10 GRAIN: ampulheta de vidro curvo, areia descendo */
static void sc_grain(Vw *v, int t, float lv) {
    static const float gl[40] = { 36,22, 36,22, 96,22, 96,22, 92,36, 80,54, 70,66, 70,70, 80,84, 92,102,
                                  96,116, 96,116, 36,116, 36,116, 40,102, 52,84, 62,70, 62,66, 52,54, 40,36 };
    int p = (t / (lv > 0.3f ? 8 : 13)) % 7, tk = t / 3;
    K(1);
    gR(1, 28,12,76,8); gR(1, 28,118,76,8); gR(1, 30,20,3,98); gR(1, 99,20,3,98);
    spoly(v, 1, gl, 20, 1, 1, 3);
    K(0);
    for (int i = 0; i < 5; i++) {
        int y = 28 + i * 8, hw = 2 + 28 * (68 - (y + 3)) / 46 - 5; if (hw < 1) hw = 1;
        gR(i >= p, 66 - hw, y, 2 * hw, 6);
    }
    for (int j = 0; j < 5; j++) {
        int y = 106 - j * 8, hw = 2 + 28 * ((y + 3) - 70) / 46 - 5; if (hw < 1) hw = 1;
        gR(j < p, 66 - hw, y, 2 * hw, 6);
    }
    for (int k = 0; k < 6; k++) gR(p < 6 && ((k + tk) % 3 == 0), 65, 66 + k * 7, 3, 3);
    K(2);
    if (LIVE && lv > 0.1f) for (int i = 0; i < 4; i++) gR(1, 8 + (int)(hsh(i, tk, 1) % 14), 30 + (int)(hsh(i, tk, 2) % 80), 2, 2);
}

/* 11 TURBINE: turbina eolica, velocidade segue o volume */
static void sc_turbine(Vw *v, int t, float lv) {
    int sp = lv > 0.30f ? 2 : (lv > 0.08f ? 4 : 8), st = (t / sp) % 4, tk = t / 4;
    K(0);
    gD(1, 66,50, 6); gL(1, 63,56, 59,116, 2); gL(1, 69,56, 73,116, 2);
    gR(1, 62,76,10,2); gR(1, 61,92,12,2); gR(1, 60,108,14,2);
    gR(1, 46,116,40,4); gR(1, 0,122,132,2);
    K(1);
    for (int s = 0; s < 4; s++) for (int k = 0; k < 3; k++) {
        float a = (float)(s * 30 + k * 120) * 0.0174533f - 1.5708f;
        gL(st == s, 66 + (int)(cosf(a) * 8.0f), 50 + (int)(sinf(a) * 8.0f), 66 + (int)(cosf(a) * 42.0f), 50 + (int)(sinf(a) * 42.0f), 4);
    }
    K(2);
    gO(1, 108,30,7,2);
    for (int i = 0; i < 16; i++) {                                  /* raios do sol: 16 posicoes fixas, alternam */
        float a = (float)i * 0.3927f;
        gL((i & 1) == (tk & 1), 108 + (int)(cosf(a) * 10), 30 + (int)(sinf(a) * 10), 108 + (int)(cosf(a) * 14), 30 + (int)(sinf(a) * 14), 2);
    }
    K(0);
    for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) gR((tk + r) % 3 == k, 4 + k * 10, 70 + r * 14, 8,2);
}

/* 12 SCREECH: passaro cantando num galho - silhueta em curvas, bico aberto, ondas de som */
static void sc_screech(Vw *v, int t, float lv) {
    static const float body[] = { 40,48, 44,38, 54,35, 63,42, 76,50, 94,58, 112,70, 121,79, 121,79, 99,81, 84,90, 70,95, 56,93, 46,83, 40,70, 38,58 };
    static const float wing[] = { 56,58, 72,55, 90,64, 100,77, 84,79, 68,73, 58,66 };
    static const float leafA[] = { 100,103, 108,93, 120,92, 114,102, 104,106 };
    int tk = t / 5, open = (tk & 1) || lv > 0.25f, wv = (t / 3) % 4, fl = (t / 4) % 3;
    K(0);
    spoly(v, 1, body, 16, 1, 1, 3);                                   /* corpo */
    spoly(v, 1, wing, 7, 1, 1, 2);                                    /* asa */
    gL(1, 66,96, 64,108, 3); gL(1, 76,93, 78,106, 3);                 /* pernas */
    gL(1, 64,108, 58,111, 2); gL(1, 64,108, 66,112, 2); gL(1, 78,106, 72,110, 2); gL(1, 78,106, 82,110, 2);
    K(2);
    gL(1, 14,106, 56,109, 4); gL(1, 56,109, 112,104, 4);              /* galho */
    spoly(v, 1, leafA, 5, 1, 1, 2);
    gL(fl == 0, 62,61, 84,69, 2); gL(fl == 1, 62,66, 86,73, 2); gL(fl == 2, 62,71, 84,76, 2);   /* penas da asa */
    K(3);
    gL(!open, 40,48, 28,52, 3); gL(!open, 28,52, 40,56, 3);           /* bico fechado */
    gL(open, 40,46, 25,42, 3); gL(open, 25,42, 39,51, 3);             /* bico aberto: cima */
    gL(open, 39,53, 26,62, 3); gL(open, 26,62, 40,57, 3);             /* e baixo */
    K(1);
    gO(1, 49,47, 4, 2); gD(1, 50,47, 1);                              /* olho */
    for (int i = 0; i < 4; i++) gA(open && i <= wv, 28,52, 9 + i * 8, 150, 210, 3);
}

/* labareda em gota: base redonda, lados que afinam ate a ponta (inclinada por `curl`); lit liga/desliga o contorno */
static void flame(Vw *v, int lit, float cx, float base, float h, float w, float curl, int th) {
    const int N = 9;
    float r = w, by = base - r, px[2] = { cx - w, cx + w }, py = by;
    gA(lit, (int)(cx + 0.5f), (int)(by + 0.5f), (int)(r + 0.5f), 0, 180, th);                    /* base arredondada */
    for (int side = 0; side < 2; side++) {
        float lx = px[side], ly = py;
        for (int j = 1; j <= N; j++) {
            float u = (float)j / (float)N, sg = side ? 1.0f : -1.0f;
            float hw = w * powf(1.0f - u, 1.25f), x = cx + sg * hw + curl * sinf(u * 1.5708f) * u, y = by - u * (h - r);
            gL(lit, (int)(lx + 0.5f), (int)(ly + 0.5f), (int)(x + 0.5f), (int)(y + 0.5f), th);
            lx = x; ly = y;
        }
    }
}

/* 13 EMBERS: fogueira crepitando - labaredas, brasas e fagulhas subindo */
static void sc_rust(Vw *v, int t, float lv) {
    int tk = t / 5, hot = lv > 0.15f, sw = ((tk / 2) & 1) ? 1 : -1;
    K(2);
    gL(1, 26, 116, 100, 106, 6); gL(1, 30, 106, 104, 116, 6);       /* toras cruzadas */
    gD(1, 26, 116, 3); gD(1, 104, 116, 3);
    gR(1, 0, 124, 132, 2);
    K(0);
    v->ox = sw;                                                     /* so o aceso balanca; o fantasma fica parado */
    flame(v, 1, 66.0f, 106.0f, 66.0f, 22.0f, 5.0f, 3);
    v->ox = -sw;
    flame(v, tk % 3 != 0, 34.0f, 106.0f, 34.0f, 9.0f, -6.0f, 3);
    flame(v, tk % 3 != 1, 98.0f, 106.0f, 42.0f, 10.0f, 7.0f, 3);
    v->ox = sw;
    K(1);
    flame(v, hot || (tk & 1), 66.0f, 106.0f, 40.0f, 12.0f, 3.0f, 2);
    K(3);
    flame(v, hot && (tk % 3 != 2), 66.0f, 106.0f, 20.0f, 6.0f, 1.0f, 2);
    v->ox = 0;
    K(3);
    for (int k = 0; k < 3; k++) for (int i = 0; i < 6; i++)          /* fagulhas subindo */
        gD(((t / 4) + i * 3 + (2 - k) * 2) % 9 == 0 || (lv > 0.5f && ((t / 4) + i + k) % 7 == 0), 32 + i * 12 + (k & 1) * 5, 10 + k * 8, 2);
    K(2);
    for (int i = 0; i < 8; i++) gD(((int)(hsh(i, t / 6, 11) & 3)) == 0, 30 + i * 10, 119, 1);   /* brasas estalando */
}

/* 14 STUTTER: disco de vinil, agulha pulando para tras */
static void sc_stutter(Vw *v, int t, float lv) {
    int st = (t / (lv > 0.3f ? 3 : 5)) % 6, pos = (t / 5) & 1, tk = t / 4;
    K(0);
    gO(1, 52,78,42,3); gO(1, 52,78,31,1); gO(1, 52,78,22,1);
    gD(1, 52,78,12);
    K(1);
    gD(1, 52,78,2);
    for (int k = 0; k < 6; k++) { gA(k == st, 52,78, 37, k * 60, k * 60 + 36, 4); gA(k == (st + 3) % 6, 52,78, 27, k * 60 + 20, k * 60 + 52, 3); }
    gD(1, 118,24,6); gL(1, 118,28, 118,46, 3);
    gL(pos == 0, 118,46, 90,60, 3); gR(pos == 0, 85,57,7,7);
    gL(pos == 1, 118,46, 86,68, 3); gR(pos == 1, 81,65,7,7);
    K(2);
    for (int i = 0; i < 3; i++) gR((tk % 4) > i, 98 + i * 10, 96, 6,16);
    for (int i = 0; i < 2; i++) { gL(pos == 1, 108 + i * 10,118, 102 + i * 10,121, 2); gL(pos == 1, 102 + i * 10,121, 108 + i * 10,124, 2); }
}

/* 15 SUBQUAKE: casa tremendo, rachadura abrindo */
static void sc_quake(Vw *v, int t, float lv) {
    int tk = t / 5, sev = 1 + (int)(lv * 4.0f);
    K(1);
    gR(1, 0,100,132,3);
    gL((tk % 4) >= 0, 66,104, 58,114, 3); gL((tk % 4) >= 1, 58,114, 70,122, 3); gL((tk % 4) >= 2, 70,122, 62,130, 3);
    gL((tk % 4) >= 3, 70,122, 82,126, 2);
    v->ox = (hsh(t, 3, 3) % 3 == 0) ? 0 : ((t & 1) ? sev : -sev);
    v->oy = ((t / 2) & 1) && lv > 0.2f ? -1 : 0;
    K(0);
    gF(1, 40,60,52,40,3); gL(1, 34,62, 66,34, 3); gL(1, 98,62, 66,34, 3);
    gR(1, 82,38,8,14); gR(1, 60,76,12,24);
    K(2);
    gF((tk & 1) == 0, 46,68,10,10,2); gF((tk & 1) == 1, 76,68,10,10,2);
    v->ox = v->oy = 0;
    K(0);
    gR(tk % 3 == 0, 96,60,5,5); gR(tk % 3 == 1, 100,74,5,5); gR(tk % 3 == 2, 104,90,5,5);
    K(1);
    for (int i = 0; i < 12; i++) {
        int lvl = (int)((float)(hsh(i, t / 2, 1) & 3) * (0.4f + lv * 1.2f));
        for (int k = 1; k <= 3; k++) { gR(lvl >= k, 6 + i * 6, 22 - 4 * k, 4,3); gR(lvl >= k, 6 + i * 6, 22 + 4 * k, 4,3); }
        gR(1, 6 + i * 6, 22, 4,3);
    }
}


/* 16 sem preset: fita cassete */
static void sc_blank(Vw *v, int t, float lv) {
    int sp = (t / 6) & 1;
    (void)lv;
    K(0);
    gF(1, 12,34,108,74,3); gF(1, 28,42,76,22,2);
    gO(1, 44,86,12,3); gO(1, 88,86,12,3);
    gD(1, 44,86,3); gD(1, 88,86,3);
    for (int i = 0; i < 2; i++) {
        int cx = i ? 88 : 44;
        gL(sp == 0, cx,76, cx,96, 2); gL(sp == 0, cx - 8,86, cx + 8,86, 2);
        gL(sp == 1, cx - 6,80, cx + 6,92, 2); gL(sp == 1, cx + 6,80, cx - 6,92, 2);
    }
    gL(1, 44,98, 52,108, 2); gL(1, 88,98, 80,108, 2); gR(1, 52,106,28,2);
    K(1);
    gR(1, 36,50,60,2); gR(1, 36,56,40,2);
    K(2);
    gR(1, 12,114,108,2);
}

/* ---- cenas novas da 0.8: tudo em slots fixos (fantasma sempre visivel), so o "aceso" anima ---- */
static float vvz(int i) { return ve ? ve[((i % AMB_V) + AMB_V) % AMB_V] : 0.0f; }
static int pol(float a) { return (int)(a * 100.0f + (a < 0 ? -0.5f : 0.5f)); }   /* arredonda para int */

/* 7 VOID: disco de acrecao em 3 aneis de arcos, horizonte de eventos, jatos, particula e estrelas */
static void sc_void(Vw *v, int t, float lv) {
    static const int RX[3] = { 56, 42, 28 }, RY[3] = { 16, 12, 8 };
    static const uint8_t st[10][2] = { {12,24},{32,12},{58,20},{96,14},{118,30},{14,112},{40,122},{84,118},{112,106},{124,72} };
    const int cx = 66, cy = 70, tk = t / 4, tail = 3 + (int)(lv * 4.0f);
    K(0);
    for (int r = 0; r < 3; r++) {
        int head = (r & 1) ? 11 - (tk + r * 3) % 12 : (tk + r * 3) % 12;
        for (int sg = 0; sg < 12; sg++) {
            int d = (r & 1) ? (sg - head + 12) % 12 : (head - sg + 12) % 12;
            gE(d < tail, cx, cy, RX[r], RY[r], sg * 30 + 3, sg * 30 + 27, 3);
        }
    }
    K(1);
    gO(1, cx, cy, 13, 3);
    gO(lv > 0.28f, cx, cy, 8, 2);
    gD(lv > 0.10f && ((t / 6) & 1), cx, cy, 3);
    for (int i = 0; i < 10; i++) gD(((int)(hsh(i, t / 14, 7) & 7)) < 2, st[i][0], st[i][1], 1);
    K(2);
    for (int k = 0; k < 3; k++) {                                   /* jatos polares */
        int on = (int)(lv * 4.0f) > k;
        gR(on, 64, 44 - k * 10, 4, 6); gR(on, 64, 82 + k * 10, 4, 6);
    }
    K(3);
    for (int i = 0; i < 24; i++) {                                  /* particula orbitando com cauda */
        int d = ((t / 3) % 24 - i + 24) % 24;
        float a = (float)i * 0.2617994f;
        gD(d < 3, cx + pol(62.0f * cosf(a)), cy + pol(20.0f * sinf(a)), 2);
    }
}

/* 17 MAR: lua com raios, estrelas (uma por voz), barco balancando e fileiras de ondas */
static void sc_sea(Vw *v, int t, float lv) {
    static const uint8_t st[12][2] = { {8,14},{22,34},{38,12},{52,28},{14,52},{66,18},{30,46},{58,46},{70,40},{44,6},{6,30},{68,8} };
    int tk = t / 6, on = 1 + (lv > 0.3f) + (lv > 0.65f);
    K(1);
    gO(1, 98, 30, 12, 3); gA(1, 98, 30, 7, 100, 260, 4);            /* lua (aro + meia-lua) */
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 0.785398f;
        gL(((i + tk / 2) & 1) == 0, 98 + pol(17.0f * cosf(a)), 30 + pol(17.0f * sinf(a)), 98 + pol(21.0f * cosf(a)), 30 + pol(21.0f * sinf(a)), 2);
    }
    for (int i = 0; i < 12; i++)
        gD(vvz(i) > 0.04f || ((int)(hsh(i, t / 16, 3) & 7)) == 0, st[i][0], st[i][1], 1);
    if (LIVE) for (int i = 0; i < 12; i++) gD(vvz(i) > 0.25f, st[i][0], st[i][1], 2);
    K(2);
    gR(1, 4, 62, 124, 2);                                           /* horizonte */
    for (int i = 0; i < 3; i++) gR(((tk + i) % 3) != 0, 98 - (14 - i * 4) / 2, 66 + i * 4, 14 - i * 4, 2);   /* reflexo */
    v->oy = (tk & 3) == 1 ? 1 : ((tk & 3) == 3 ? -1 : 0);          /* barco: so o aceso se mexe */
    gL(1, 30, 66, 54, 66, 3); gL(1, 33, 66, 36, 72, 2); gL(1, 51, 66, 48, 72, 2); gL(1, 36, 72, 48, 72, 2);
    gL(1, 42, 66, 42, 46, 2); gL(1, 42, 48, 56, 62, 2); gL(1, 42, 48, 32, 62, 2);
    v->oy = 0;
    for (int k = 0; k < 5; k++) {
        K(k == 4 ? 2 : 0);
        int y0 = 74 + k * 10, dir = (k & 1) ? 1 : -1;
        for (int i = 0; i < 11; i++) {
            int ph = (((i + dir * tk + k * 3) % 5) + 5) % 5;
            gR(ph < on || vvz(i) * 6.0f > (float)k, 4 + i * 12, y0 + (((i + k) & 1) ? 3 : 0), 9, 3);
        }
    }
}

/* 18 CHUVA: nuvem, raio nos picos, uma coluna de pingos por voz e aneis na poca */
static void sc_rain(Vw *v, int t, float lv) {
    int tk = t / (lv > 0.4f ? 2 : 4), fl = lv > 0.5f && (t / 5) % 7 < 2;
    K(1);
    gA(1, 38, 38, 8, 90, 303, 3); gA(1, 56, 28, 14, 166, 370, 3); gA(1, 76, 37, 9, 226, 450, 3);   /* nuvem: 3 lobulos emendados */
    gL(1, 38, 46, 76, 46, 3);
    K(3);
    gL(fl, 62, 50, 54, 62, 3); gL(fl, 54, 62, 66, 64, 3); gL(fl, 66, 64, 56, 82, 3);
    K(0);
    for (int i = 0; i < 12; i++) {
        int x = 8 + i * 10;
        for (int k = 0; k < 4; k++) {
            int lit = ((tk + i * 5 + k * 3) % 4 == 0) || (vvz(i) > 0.10f && (tk + i + k) % 3 == 0);
            gL(lit, x, 56 + k * 13, x - 3, 63 + k * 13, 2);
        }
    }
    K(2);
    gR(1, 4, 112, 124, 2);
    for (int i = 0; i < 4; i++) {
        int x = 22 + i * 30, stg = (tk / 2 + i * 3) % 6;
        gE(stg == 0 || stg == 1, x, 122, 5, 2, 0, 360, 1);
        gE(stg == 1 || stg == 2, x, 122, 11, 4, 0, 360, 1);
    }
}

/* 19 DESERTO: sol com raios, dunas, cacto, vento e miragem (uma barra por voz) */
static void sc_desert(Vw *v, int t, float lv) {
    int tk = t / 6;
    K(3);
    gD(1, 34, 34, 8);
    for (int i = 0; i < 10; i++) {
        float a = (float)i * 0.628319f;
        gL(((i + tk / 2) & 1) == 0, 34 + pol(13.0f * cosf(a)), 34 + pol(13.0f * sinf(a)), 34 + pol(20.0f * cosf(a)), 34 + pol(20.0f * sinf(a)), 2);
    }
    K(0);
    gE(1, 96, 118, 60, 34, 180, 360, 3); gE(1, 20, 124, 56, 26, 180, 360, 3); gE(1, 72, 142, 90, 22, 180, 360, 3);
    K(2);
    gR(1, 66, 68, 6, 24); gR(1, 56, 76, 10, 4); gR(1, 56, 71, 4, 9); gR(1, 72, 72, 10, 4); gR(1, 78, 67, 4, 9);
    K(3);
    gD((tk & 1) == 0, 69, 64, 2);
    K(1);
    for (int r = 0; r < 2; r++) for (int i = 0; i < 4; i++)
        gR(((i * 3 + tk + r * 2) % 6) < 2 + (lv > 0.4f), 64 + i * 16, 44 + r * 8, 9, 2);
    K(0);
    for (int i = 0; i < 11; i++)
        gR(((i + tk) % 3 == 0) || vvz(i) > 0.08f, 4 + i * 12, 100 + ((i & 1) ? 3 : 0), 9, 3);
}

/* 20 ESPACO: planeta com aneis, lua em orbita, estrelas (uma por voz) e cometa */
static void sc_space(Vw *v, int t, float lv) {
    static const uint8_t st[12][2] = { {10,14},{30,28},{52,10},{78,24},{104,14},{120,36},{12,60},{116,66},{18,100},{44,118},{96,112},{122,100} };
    int tk = t / 6;
    K(0);
    gO(1, 62, 72, 20, 3);
    for (int k = 0; k < 3; k++) gR(((tk / 2 + k) & 1) == 0, 50, 62 + k * 9, 24, 3);
    for (int sg = 0; sg < 10; sg++) gE(((sg + tk) % 3) != 0, 62, 72, 46, 9, sg * 36 + 3, sg * 36 + 33, 2);
    K(1);
    for (int i = 0; i < 12; i++) {
        int lit = vvz(i) > 0.04f || ((int)(hsh(i, t / 12, 5) & 7)) < 2, x = st[i][0], y = st[i][1], r = 2 + (vvz(i) > 0.25f);
        (void)r; gD(lit, x, y, 1); gL(lit, x - 4, y, x + 4, y, 1); gL(lit, x, y - 4, x, y + 4, 1);
        if (LIVE) { gL(vvz(i) > 0.25f, x - 6, y, x + 6, y, 1); gL(vvz(i) > 0.25f, x, y - 6, x, y + 6, 1); }
    }
    K(3);
    for (int i = 0; i < 16; i++) {
        int d = (((t / 5) % 16) - i + 16) % 16;
        float a = (float)i * 0.392699f;
        gD(d < 3, 62 + pol(58.0f * cosf(a)), 72 + pol(26.0f * sinf(a)), 2);
    }
    K(2);
    for (int j = 0; j < 9; j++) {
        int d = (((t / 4) % 20) - j + 20) % 20;
        gD(d < 3, 100 - j * 11, 34 + j * 3 / 2, 2);
    }
}


static const SceneFn scenes[ART_SCENES] = {
    sc_drone, sc_static, sc_harsh, sc_shredder, sc_digital, sc_feedback, sc_industrial, sc_void,
    sc_wall, sc_glitch, sc_grain, sc_turbine, sc_screech, sc_rust, sc_stutter, sc_quake, sc_blank,
    sc_sea, sc_rain, sc_desert, sc_space
};

/* escala de cada cena dentro do quadro 132x132 (centro em 66,70) */
static const float zoom[ART_SCENES] = {
    0.92f, 0.82f, 0.86f, 0.92f, 0.84f, 0.90f, 0.90f, 1.00f, 0.95f, 0.86f, 0.88f, 0.86f, 0.95f, 0.95f, 0.86f, 0.90f,
    0.82f, 1.00f, 1.00f, 1.00f, 1.00f
};

static void run_pass(Vw *v, int pass, int id, int t, float lv) {
    v->pass = pass; v->ox = v->oy = 0; v->z = zoom[id]; cur_ink = 0; pend = 0;
    scenes[id](v, t, lv);
    v->z = 1.0f;
    K(0);
    int sc = (int)(lv * 99.0f + 0.5f);                    /* placar de 2 digitos: nivel do som */
    sS7(v, 100, 4, 9, 15, 2, sc / 10); sS7(v, 113, 4, 9, 15, 2, sc % 10);
    commit(); v->ox = v->oy = 0;
}

void art_accent(int id, uint8_t rgb[3]) {
    if (id < 0 || id >= ART_SCENES) id = ART_BLANK;
    rgb[0] = acc[id].r; rgb[1] = acc[id].g; rgb[2] = acc[id].b;
}

void art_scene(Surf *s, int x, int y, int id, int t, float lv, const float *venv) {
    if (id < 0 || id >= ART_SCENES) id = ART_BLANK;
    lv = lv < 0.0f ? 0.0f : (lv > 1.0f ? 1.0f : lv);
    ve = venv;
    Col ink[4] = { acc[id], {228,250,236}, acc2[id], K_AMB };
    Lut lut; build_lut(lut, ink);
    begin_frame(ART_W, ART_W);
    Vw v = { 0, 0, 0, 1.0f };
    if (gh_id != id) {
        memset(cmp_i, 0, sizeof cmp_i); memset(cmp_k, 0, sizeof cmp_k);
        run_pass(&v, 0, id, t, lv);
        memcpy(gh_i, cmp_i, sizeof gh_i); memcpy(gh_k, cmp_k, sizeof gh_k); gh_id = id;
    } else { memcpy(cmp_i, gh_i, sizeof cmp_i); memcpy(cmp_k, gh_k, sizeof cmp_k); }
    run_pass(&v, 1, id, t, lv);
    run_pass(&v, 2, id, t, lv);
    g_clear();
    present(s, x, y, ART_W, ART_W, lut);
}

/* ---------------- painel do osciloscopio + leituras ---------------- */
static void seg7f(float x, float y, float w, float h, float t, int d) {
    static const uint8_t seg[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
    int m = d >= 0 ? seg[d % 10] : 0;
    const float sl = 0.14f, g = t * 0.75f;
    float yy[3] = { y + t * 0.5f, y + h * 0.5f, y + h - t * 0.5f };
    #define PX(px, py) (x + (px) + (h - ((py) - y)) * sl)
    for (int pass = 0; pass < 3; pass++)
        for (int s = 0; s < 7; s++) {
            int lit = (m >> s) & 1;
            float a = pass == 0 ? 0.13f : (pass == 1 ? 0.28f : 1.0f), wd = t * 0.9f + (pass == 1 ? 2.8f : 0.0f);
            if (pass && !lit) continue;
            if (pass == 0 && lit) continue;
            float x0, y0, x1, y1;
            switch (s) {
            case 0: y0 = y1 = yy[0]; x0 = t * 0.5f + g; x1 = w - t * 0.5f - g; break;
            case 1: x0 = x1 = w - t * 0.5f; y0 = yy[0] + g; y1 = yy[1] - g; break;
            case 2: x0 = x1 = w - t * 0.5f; y0 = yy[1] + g; y1 = yy[2] - g; break;
            case 3: y0 = y1 = yy[2]; x0 = t * 0.5f + g; x1 = w - t * 0.5f - g; break;
            case 4: x0 = x1 = t * 0.5f; y0 = yy[1] + g; y1 = yy[2] - g; break;
            case 5: x0 = x1 = t * 0.5f; y0 = yy[0] + g; y1 = yy[1] - g; break;
            default: y0 = y1 = yy[1]; x0 = t * 0.5f + g; x1 = w - t * 0.5f - g; break;
            }
            g_line(PX(x0, y0), y0, PX(x1, y1), y1, wd, a);
        }
    #undef PX
}

void art_display(Surf *s, int x, int y, const ArtDisp *d) {
    int id = d->scene < 0 || d->scene >= ART_SCENES ? ART_BLANK : d->scene;
    Col ink[4] = { acc[id], {228,250,236}, acc2[id], K_AMB };
    Lut lut; build_lut(lut, ink);
    begin_frame(232, 132);
    memset(cmp_i, 0, sizeof cmp_i); memset(cmp_k, 0, sizeof cmp_k);
    /* ---- osciloscopio: graticula ---- */
    K(1);
    for (int k = 0; k <= 8; k++) g_line(2.0f + (float)k * 28.5f, 2.0f, 2.0f + (float)k * 28.5f, 72.0f, 1.0f, k == 4 ? 0.20f : 0.08f);
    for (int k = 0; k <= 4; k++) g_line(2.0f, 2.0f + (float)k * 17.5f, 230.0f, 2.0f + (float)k * 17.5f, 1.0f, k == 2 ? 0.20f : 0.08f);
    for (int k = 0; k < 8; k++) for (int j = 0; j < 5; j++) g_disc(2.0f + (float)k * 28.5f + 14.25f, 2.0f + (float)j * 17.5f, 0.7f, 0.22f);
    pend = 1; commit();
    /* ---- forma de onda ---- */
    K(0);
    {
        float xy[2 * 116]; const int N = 114;
        for (int i = 0; i < N; i++) {
            float val = (d->scope && !d->paused) ? d->scope[i * 2 * d->spp] : 0.0f;
            if (val > 1.0f) val = 1.0f; else if (val < -1.0f) val = -1.0f;
            xy[2 * i] = 2.0f + (float)i * (228.0f / (float)(N - 1)); xy[2 * i + 1] = 37.0f - val * 33.0f;
        }
        g_poly(xy, N, 0, 4.2f, 0.20f);
        g_poly(xy, N, 0, 1.5f, 1.0f);
    }
    commit();
    K(2); g_line(2.0f, 77.0f, 230.0f, 77.0f, 1.0f, 0.35f); pend = 1; commit();
    /* ---- Hz em 7 segmentos (ambar) ---- */
    K(3);
    {
        int hz = d->hz < 0 ? 0 : (d->hz > 9999 ? 9999 : d->hz);
        int dg[4] = { hz / 1000, (hz / 100) % 10, (hz / 10) % 10, hz % 10 };
        for (int i = 0; i < 4; i++) {
            int lead = (i < 3) && (hz < (i == 0 ? 1000 : (i == 1 ? 100 : 10)));
            seg7f(6.0f + (float)i * 27.0f, 86.0f, 19.0f, 38.0f, 4.0f, lead ? -1 : dg[i]);
        }
        g_text(118.0f, 112.0f, "HZ", 9.0f, 1.2f, 0.95f, 0);
    }
    pend = 1; commit();
    K(1);
    if (d->sub) g_text(118.0f, 86.0f, d->sub, 8.0f, 1.1f, 0.85f, 0);
    commit();
    /* ---- nome do preset ---- */
    K(2);
    {
        char b[16]; snprintf(b, sizeof b, "%02d/%02d", d->idx, d->total);
        if (d->idx > 0) g_text(228.0f, 86.0f, b, 7.0f, 1.0f, 0.75f, 2);
    }
    commit();
    K(0);
    if (d->title) {
        float sz = 14.0f, w = g_text_w(d->title, sz);
        if (w > 96.0f) sz = sz * 96.0f / w;
        g_text(228.0f, 102.0f, d->title, sz, sz * 0.12f, 1.0f, 2);
        g_line(228.0f - 96.0f, 124.0f, 228.0f, 124.0f, 1.0f, 0.45f);
    }
    pend = 1; commit();
    g_clear();
    present(s, x, y, 232, 132, lut);
}

/* ------------------------------------------------------------------ */
/* ABERTURA: painel VFD de 14 segmentos que escreve o nome em tracos   */
/* (como a assinatura de boot do JIANT FM). 3 s a 30 quadros/s. E a    */
/* unica vez em que o nome aparece: o resto do app nao o repete.       */
/*   0-4 vidro apagado | 5-8 teste de lampadas | 12-51 escrita em     */
/*   tracos + barra | 52-69 legenda, versao, feixe de brilho |         */
/*   70-85 glitch de saida (celulas morrem, linhas rasgam) | 86-89 off */
/* ------------------------------------------------------------------ */
enum { S_A, S_B, S_C, S_D, S_E, S_F, S_G1, S_G2, S_H, S_I, S_J, S_K, S_L, S_M };
#define IN_CELLS 12
#define IN_W 28.0f
#define IN_H 56.0f
#define IN_T 5.2f
#define IN_PITCH 32
#define IN_X0 10
#define IN_Y0 84
#define IN_BAND_Y 52
#define IN_NBAR 48
#define IN_GL  130                 /* quadro em que comeca o glitch de saida (antes disso o nome fica inteiro na tela) */
#define IN_END (IN_GL + 16)        /* fim do glitch; os ultimos quadros ate ART_INTRO_FRAMES ficam apagados */

/* ordem dos tracos de cada letra: I N T O A R U M */
static const int8_t in_ord[8][9] = {
    { S_A, S_I, S_L, S_D, -1 },
    { S_F, S_E, S_H, S_M, S_C, S_B, -1 },
    { S_A, S_I, S_L, -1 },
    { S_A, S_B, S_C, S_D, S_E, S_F, -1 },
    { S_E, S_F, S_A, S_B, S_C, S_G1, S_G2, -1 },
    { S_F, S_E, S_A, S_B, S_G2, S_G1, S_M, -1 },
    { S_F, S_E, S_D, S_C, S_B, -1 },
    { S_E, S_F, S_H, S_J, S_B, S_C, -1 },
};
/* I N T O N A R U M O R I */
static const uint8_t in_txt[IN_CELLS] = { 0, 1, 2, 3, 1, 4, 5, 6, 7, 3, 5, 0 };

static void in_seg_xy(int s, float *x0, float *y0, float *x1, float *y1) {
    const float L = IN_T * 0.5f, R = IN_W - IN_T * 0.5f, T = IN_T * 0.5f, B = IN_H - IN_T * 0.5f;
    const float XM = IN_W * 0.5f, YM = IN_H * 0.5f, G = 3.0f;
    switch (s) {
    case S_A:  *x0 = L + G; *y0 = T; *x1 = R - G; *y1 = T; break;
    case S_B:  *x0 = R; *y0 = T + G; *x1 = R; *y1 = YM - G; break;
    case S_C:  *x0 = R; *y0 = YM + G; *x1 = R; *y1 = B - G; break;
    case S_D:  *x0 = L + G; *y0 = B; *x1 = R - G; *y1 = B; break;
    case S_E:  *x0 = L; *y0 = YM + G; *x1 = L; *y1 = B - G; break;
    case S_F:  *x0 = L; *y0 = T + G; *x1 = L; *y1 = YM - G; break;
    case S_G1: *x0 = L + G; *y0 = YM; *x1 = XM - 2.2f; *y1 = YM; break;
    case S_G2: *x0 = XM + 2.2f; *y0 = YM; *x1 = R - G; *y1 = YM; break;
    case S_H:  *x0 = L + 4.6f; *y0 = T + 5.8f; *x1 = XM - 3.4f; *y1 = YM - 4.2f; break;
    case S_I:  *x0 = XM; *y0 = T + G; *x1 = XM; *y1 = YM - G; break;
    case S_J:  *x0 = R - 4.6f; *y0 = T + 5.8f; *x1 = XM + 3.4f; *y1 = YM - 4.2f; break;
    case S_K:  *x0 = L + 4.6f; *y0 = B - 5.8f; *x1 = XM - 3.4f; *y1 = YM + 4.2f; break;
    case S_L:  *x0 = XM; *y0 = YM + G; *x1 = XM; *y1 = B - G; break;
    default:   *x0 = R - 4.6f; *y0 = B - 5.8f; *x1 = XM + 3.4f; *y1 = YM + 4.2f; break;
    }
}
static void in_seg(int s, float ox, float oy, float a, float extra) {
    float x0, y0, x1, y1; in_seg_xy(s, &x0, &y0, &x1, &y1);
    int diag = s == S_H || s == S_J || s == S_K || s == S_M;
    g_line(ox + x0, oy + y0, ox + x1, oy + y1, (diag ? IN_T * 0.78f : IN_T * 0.9f) + extra, a);
    pend = 1;
}

typedef struct {
    uint16_t lit[IN_CELLS], hot[IN_CELLS];      /* segmentos acesos / recem-acesos (brilho branco) por celula */
    float amp[IN_CELLS]; int dx[IN_CELLS];      /* brilho do nucleo e deslocamento (glitch) */
    int nbar, head;
} InSt;

static void in_state(int t, InSt *st) {
    memset(st, 0, sizeof *st); st->head = -1;
    for (int i = 0; i < IN_CELLS; i++) {
        int g = in_txt[i], full = 0;
        for (int k = 0; in_ord[g][k] >= 0; k++) full |= 1 << in_ord[g][k];
        st->amp[i] = 1.0f;
        if (t >= 5 && t < 9) { st->lit[i] = 0x3FFF; if (t == 5) st->hot[i] = 0x3FFF; }          /* teste de lampadas */
        else if (t >= 12 && t < IN_GL) {
            int s0 = 12 + i * 3, lit = 0, hot = 0;
            for (int k = 0; in_ord[g][k] >= 0; k++) {                                           /* um traco por quadro */
                int lf = s0 + k;
                if (t >= lf) { lit |= 1 << in_ord[g][k]; if (t - lf < 3) hot |= 1 << in_ord[g][k]; }
            }
            {                                                                                   /* feixe de brilho: ida (54-68) e volta (100-114) */
                int beam = -999, cx = IN_X0 + i * IN_PITCH + 14;
                if (t >= 54 && t <= 68) beam = IN_X0 + (t - 54) * 390 / 14;
                else if (t >= 100 && t <= 114) beam = IN_X0 + 390 - (t - 100) * 390 / 14;
                if (beam > -999 && abs(cx - beam) < 26) hot = lit;
            }
            if (t >= 50 && hsh((uint32_t)i, (uint32_t)(t >> 1), 7u) % 19u == 0u) st->amp[i] = 0.45f;   /* tremida do filamento */
            st->lit[i] = (uint16_t)lit; st->hot[i] = (uint16_t)hot;
        } else if (t >= IN_GL && t < IN_END) {                                                         /* celulas morrem em ordem aleatoria */
            int d = IN_GL + 2 + (int)(hsh((uint32_t)i, 3u, 9u) % 12u);
            if (t < d - 2) st->lit[i] = (uint16_t)full;
            else if (t <= d) {
                uint32_t h = hsh((uint32_t)i, (uint32_t)t, 5u);
                st->lit[i] = (uint16_t)((h ^ (h >> 14)) & (uint32_t)full);
                st->hot[i] = (uint16_t)(st->lit[i] & (h >> 7));
                st->dx[i] = (int)(hsh((uint32_t)i, (uint32_t)t, 6u) % 9u) - 4;
                st->amp[i] = 0.85f;
            }
        }
    }
    if (t >= 5 && t < 9) st->nbar = IN_NBAR;
    else if (t >= 12 && t < 54) { st->nbar = (t - 11) * IN_NBAR / 42; st->head = st->nbar - 1; }
    else if (t >= 54 && t < IN_GL) st->nbar = IN_NBAR;
    else if (t >= IN_GL && t < IN_END) st->nbar = IN_NBAR;          /* cada tick morre sozinho (ver in_band) */
}

/* o painel inteiro em coordenadas da tela; g_origin ja desloca para a regiao (rx) */
static void in_band(const InSt *st, int t, int rx) {
    /* tinta 0 (verde): regua, fantasmas, segmentos acesos */
    K(0);
    g_line(6.0f, 74.0f, 394.0f, 74.0f, 1.0f, 0.16f); g_line(6.0f, 148.0f, 394.0f, 148.0f, 1.0f, 0.16f);
    g_line(6.0f, 74.0f, 6.0f, 80.0f, 1.0f, 0.16f); g_line(394.0f, 74.0f, 394.0f, 80.0f, 1.0f, 0.16f);     /* cantos da moldura */
    g_line(6.0f, 148.0f, 6.0f, 142.0f, 1.0f, 0.16f); g_line(394.0f, 148.0f, 394.0f, 142.0f, 1.0f, 0.16f);
    pend = 1;
    for (int i = 0; i < IN_CELLS; i++) {
        float ox = (float)(IN_X0 + i * IN_PITCH);
        if (ox + IN_W < (float)rx || ox > (float)(rx + 232)) continue;
        for (int s = 0; s < 14; s++) in_seg(s, ox, (float)IN_Y0, 0.13f, 0.0f);                 /* fantasma */
        for (int s = 0; s < 14; s++)
            if ((st->lit[i] >> s & 1) && !(st->hot[i] >> s & 1)) {
                if (st->amp[i] > 0.9f) in_seg(s, ox + (float)st->dx[i], (float)IN_Y0, 0.28f, 2.8f);   /* halo */
                in_seg(s, ox + (float)st->dx[i], (float)IN_Y0, st->amp[i], 0.0f);                     /* nucleo */
            }
    }
    /* tinta 3 (ambar): barra de carga e versao */
    K(3);
    for (int k = 0; k < IN_NBAR; k++) {
        float x = (float)(IN_X0 + k * 8);
        int on = k < st->nbar && !(t >= IN_GL && t >= IN_GL + 2 + (int)(hsh((uint32_t)k, 1u, 4u) % 14u)) && k != st->head;
        g_rect(x, 168.0f, 5.0f, 7.0f, 0.13f);
        if (on) { g_rect(x - 1.0f, 167.0f, 7.0f, 9.0f, 0.28f); g_rect(x, 168.0f, 5.0f, 7.0f, 1.0f); }
        pend = 1;
    }
    float ta = (t < 58 && (t & 1)) ? 0.35f : 0.95f;
    if (t >= 52 && t < IN_GL + 12 && !(t >= IN_GL + 6 && (hsh((uint32_t)t, 1u, 1u) & 1u))) { g_text(390.0f, 60.0f, "V0.9", 9.0f, 1.1f, ta, 2); pend = 1; }
    /* tinta 1 (branco): tracos recem-acesos e a cabeca da barra */
    K(1);
    for (int i = 0; i < IN_CELLS; i++) {
        float ox = (float)(IN_X0 + i * IN_PITCH);
        if (ox + IN_W < (float)rx || ox > (float)(rx + 232)) continue;
        for (int s = 0; s < 14; s++)
            if (st->hot[i] >> s & 1 && st->lit[i] >> s & 1) {
                in_seg(s, ox + (float)st->dx[i], (float)IN_Y0, 0.30f, 2.8f);
                in_seg(s, ox + (float)st->dx[i], (float)IN_Y0, 1.0f, 0.0f);
            }
    }
    if (t >= 58 && t < IN_GL + 12 && !(t >= IN_GL + 6 && (hsh((uint32_t)t, 3u, 1u) & 1u))) {      /* credito: digita 1 letra a cada 2 quadros */
        int n = (t - 58) / 2 + 1; if (n > 13) n = 13;
        char cb[14]; memcpy(cb, "MADE BY MJCDN", (size_t)n); cb[n] = 0;
        g_text(10.0f, 60.0f, cb, 9.0f, 1.1f, 0.95f, 0); pend = 1;
    }
    if (st->head >= 0) { float x = (float)(IN_X0 + st->head * 8); g_rect(x - 1.0f, 167.0f, 7.0f, 9.0f, 0.30f); g_rect(x, 168.0f, 5.0f, 7.0f, 1.0f); pend = 1; }
    /* tinta 2 (amarelo): legenda */
    K(2);
    if (t >= 52 && t < IN_GL + 12 && !(t >= IN_GL + 6 && (hsh((uint32_t)t, 2u, 1u) & 1u))) { g_text(200.0f, 154.0f, "NOISE / AMBIENT INSTRUMENT", 8.0f, 1.0f, ta, 1); pend = 1; }
    commit();
}

/* nivel (0..20) da coluna c do medidor da tela de baixo */
static int in_lvl(int c, int t) {
    if (t >= 5 && t < 9) return 20;
    if (t < 12 || t >= IN_END) return 0;
    float env = t < 50 ? 0.25f + 0.65f * (float)(t - 12) / 38.0f : (t < IN_GL ? 0.90f - 0.35f * (float)(t - 50) / (float)(IN_GL - 50) : 0.55f);
    float sp = fabsf(sinf((float)c * 0.9f + (float)t * 0.35f) * sinf((float)c * 0.37f - (float)t * 0.21f));
    float v = env * (0.30f + 0.70f * sp) + 0.10f * (float)(hsh((uint32_t)c, (uint32_t)t, 3u) & 255u) / 255.0f;
    int n = (int)(v * 20.0f + 0.5f);
    if (t >= IN_GL && t >= IN_GL + 2 + (int)(hsh((uint32_t)c, 5u, 2u) % 13u)) return 0;
    return n > 20 ? 20 : n;
}

static void glass(Surf *s, int x0, int y0, int w, int h) {         /* vidro apagado: fundo + malha */
    const uint16_t bg = RGB(BG_ART.r, BG_ART.g, BG_ART.b), mesh = RGB(11, 24, 20);
    for (int x = x0; x < x0 + w && x < s->w; x++) {
        uint16_t *col = s->px + x * s->h;
        for (int y = y0; y < y0 + h && y < s->h; y++) col[s->h - 1 - y] = ((x & 3) == 1 && (y & 3) == 1) ? mesh : bg;
    }
}

/* apresenta so as colunas [xa, xb) da regiao; tear[y] = deslocamento horizontal da linha y (glitch) */
static void present_cols(Surf *s, int dx, int dy, int w, int h, int xa, int xb, const Lut lut, const int8_t *tear) {
    const uint16_t bg = RGB(BG_ART.r, BG_ART.g, BG_ART.b), mesh = RGB(11, 24, 20);
    for (int x = xa; x < xb; x++) {
        int sx = dx + x;
        if ((unsigned)sx >= (unsigned)s->w) continue;
        uint16_t *col = s->px + sx * s->h;
        for (int y = 0; y < h; y++) {
            int sy = dy + y;
            if ((unsigned)sy >= (unsigned)s->h) continue;
            int xs = tear ? x - tear[y] : x;
            uint8_t v = (xs >= 0 && xs < w) ? cmp_i[y * FB_W + xs] : 0;
            col[s->h - 1 - sy] = v ? lut[cmp_k[y * FB_W + xs]][v] : (((sx & 3) == 1 && (sy & 3) == 1) ? mesh : bg);
        }
    }
}

void art_intro(Surf *top, Surf *bot, int t) {
    static Lut lut; static int lut_ok, bot_ok;
    if (!lut_ok) { const Col ink[4] = { CG, CW, CY, CA }; build_lut(lut, ink); lut_ok = 1; }
    if (t < 0) t = 0;
    int on = t >= 4 && t < IN_END;
    glass(top, 0, 0, 400, 240);                          /* a tela de cima tem 2 buffers: repinta tudo */
    if (on) {
        InSt st; in_state(t, &st);
        for (int half = 0; half < 2; half++) {
            int rx = half ? 168 : 0;
            begin_frame(232, 132);
            memset(cmp_i, 0, sizeof cmp_i); memset(cmp_k, 0, sizeof cmp_k);
            g_origin((float)-rx, (float)-IN_BAND_Y);
            in_band(&st, t, rx);
            int8_t tear[132];
            for (int y = 0; y < 132; y++) {
                tear[y] = 0;
                if (t >= IN_GL + 4 && t < IN_END) {
                    uint32_t h = hsh((uint32_t)((IN_BAND_Y + y) / 5), (uint32_t)t, 11u);
                    if (h % 100u < (uint32_t)(20 + (t - IN_GL - 4) * 3)) tear[y] = (int8_t)((int)((h >> 9) % 29u) - 14);
                }
            }
            present_cols(top, rx, IN_BAND_Y, 232, 132, half ? 200 - rx : 0, half ? 232 : 200 - rx, lut, tear);
            g_origin(0, 0); g_clear();
        }
    }
    /* tela de baixo: medidor de 16 colunas x 20 segmentos que reage como um analisador */
    if (!bot_ok) { glass(bot, 0, 0, 320, 240); bot_ok = 1; }
    if (on) {
        begin_frame(224, 120);
        memset(cmp_i, 0, sizeof cmp_i); memset(cmp_k, 0, sizeof cmp_k);
        g_origin(-48.0f, -60.0f);
        static const int j0[3] = { 0, 14, 17 }, j1[3] = { 14, 17, 20 }, ik[3] = { 0, 2, 3 };
        for (int g = 0; g < 3; g++) {
            K(ik[g]);
            for (int c = 0; c < 16; c++) {
                int lv = in_lvl(c, t);
                float x = (float)(48 + c * 14);
                for (int j = j0[g]; j < j1[g]; j++) {
                    float y = (float)(60 + (19 - j) * 6);
                    if (j < lv) { g_rect(x - 1.2f, y - 1.2f, 14.4f, 6.4f, 0.28f); g_rect(x, y, 12.0f, 4.0f, 1.0f); }
                    else g_rect(x, y, 12.0f, 4.0f, 0.13f);
                    pend = 1;
                }
            }
        }
        commit();
        present_cols(bot, 48, 60, 224, 120, 0, 224, lut, NULL);
        g_origin(0, 0); g_clear();
    } else glass(bot, 48, 60, 224, 120);
}

#ifdef ART_TEST
/* hash da passada do fantasma de uma cena em (t, lv): o fantasma NAO pode depender do tempo */
uint32_t art_ghost_hash(int id, int t, float lv) {
    Vw v = { 0, 0, 0, 1.0f }; static float vt[AMB_V];
    for (int i = 0; i < AMB_V; i++) vt[i] = 0.5f + 0.5f * sinf((float)t * 0.07f + (float)i);
    ve = vt;
    memset(cmp_i, 0, sizeof cmp_i); memset(cmp_k, 0, sizeof cmp_k);
    begin_frame(ART_W, ART_W);
    run_pass(&v, 0, id, t, lv);
    uint32_t h = 2166136261u;
    for (int y = 0; y < ART_W; y++) for (int x = 0; x < ART_W; x++) { h ^= cmp_i[y * FB_W + x]; h *= 16777619u; h ^= cmp_k[y * FB_W + x]; h *= 16777619u; }
    return h;
}
#endif
