/* h1 - graphics primitives. See gfx.h. */
#include "gfx.h"

#ifdef H1_DEVICE
uint32_t g_fb_w[FB_W * FB_H / 4] __attribute__((section(".pool")));
#else
uint32_t g_fb_w[FB_W * FB_H / 4];
#endif

static int gm = G_MAX;
static int glevel = 255;      /* the ink level of the primitive being drawn, 0..255 */
static f32 gdim = 1.0f;
static f32 gox, goy;
void (*g_poll_fn)(void);

void g_mode(int mode) { gm = mode; }
void g_dim(f32 k) { gdim = sat(k); }
void g_origin(f32 ox, f32 oy) { gox = ox; goy = oy; }

void g_clear(void)
{
    uint32_t *p = g_fb_w;
    uint32_t i;
    for (i = 0; i < FB_W * FB_H / 4u; i++)
        p[i] = 0;
}

void g_fade(int mul256)
{
    uint32_t i;
    for (i = 0; i < FB_W * FB_H; i++)
        g_fb[i] = (uint8_t)((g_fb[i] * mul256) >> 8);
}

static inline void set_level(f32 a)
{
    a *= gdim;
    glevel = a <= 0.0f ? 0 : (a >= 1.0f ? 255 : (int)(a * 255.0f + 0.5f));
}

/* one pixel, coverage 0..256 */
static inline void put(int x, int y, int cov)
{
    uint8_t *p;
    int v;
    if ((unsigned)x >= FB_W || (unsigned)y >= FB_H || cov <= 0)
        return;
    if (cov > 256)
        cov = 256;
    p = &g_fb[y * FB_W + x];
    if (gm == G_MAX) {
        v = (cov * glevel) >> 8;
        if (v > *p)
            *p = (uint8_t)v;
    } else if (gm == G_ADD) {
        v = *p + ((cov * glevel) >> 8);
        *p = (uint8_t)(v > 255 ? 255 : v);
    } else {
        v = glevel - *p;
        *p = (uint8_t)(*p + (v * cov) / 256);
    }
}

static inline int cov_of(f32 c) { return c <= 0.0f ? 0 : (c >= 1.0f ? 256 : (int)(c * 256.0f)); }

/* a horizontal run from xa to xb (edges, in pixel units), rows weighted by cy (0..256) */
static void hspan(int y, f32 xa, f32 xb, int cy)
{
    int x0, x1, x;
    if ((unsigned)y >= FB_H || xb <= xa || cy <= 0)
        return;
    if (xa < -0.5f) xa = -0.5f;
    if (xb > (f32)FB_W - 0.5f) xb = (f32)FB_W - 0.5f;
    if (xb <= xa)
        return;
    x0 = ifloor(xa + 0.5f);
    x1 = ifloor(xb + 0.5f);
    if (x0 == x1) {
        put(x0, y, (cov_of(xb - xa) * cy) >> 8);
        return;
    }
    put(x0, y, (cov_of((f32)x0 + 0.5f - xa) * cy) >> 8);
    for (x = x0 + 1; x < x1; x++)
        put(x, y, cy);
    put(x1, y, (cov_of(xb - ((f32)x1 - 0.5f)) * cy) >> 8);
}

void g_rect(f32 x, f32 y, f32 w, f32 h, f32 a)
{
    f32 ya, yb;
    int y0, y1, yy;
    G_POLL();
    if (w <= 0.0f || h <= 0.0f)
        return;
    set_level(a);
    x += gox; y += goy;
    ya = y - 0.5f; yb = y + h - 0.5f;          /* x,y name the top-left pixel's centre edge */
    x -= 0.5f;
    if (ya < -0.5f) ya = -0.5f;
    if (yb > (f32)FB_H - 0.5f) yb = (f32)FB_H - 0.5f;
    if (yb <= ya)
        return;
    y0 = ifloor(ya + 0.5f);
    y1 = ifloor(yb + 0.5f);
    if (y0 == y1) {
        hspan(y0, x, x + w, cov_of(yb - ya));
        return;
    }
    hspan(y0, x, x + w, cov_of((f32)y0 + 0.5f - ya));
    for (yy = y0 + 1; yy < y1; yy++)
        hspan(yy, x, x + w, 256);
    hspan(y1, x, x + w, cov_of(yb - ((f32)y1 - 0.5f)));
}

void g_disc(f32 cx, f32 cy, f32 r, f32 a)
{
    int y0, y1, y, x, xa, xb;
    f32 ro, ri2, ro2;
    G_POLL();
    if (r <= 0.0f)
        return;
    set_level(a);
    cx += gox; cy += goy;
    ro = r + 0.5f;
    ro2 = ro * ro;
    ri2 = r > 0.5f ? (r - 0.5f) * (r - 0.5f) : 0.0f;
    y0 = imax(0, ifloor(cy - ro));
    y1 = imin(FB_H - 1, ifloor(cy + ro) + 1);
    for (y = y0; y <= y1; y++) {
        f32 dy = (f32)y - cy, dy2 = dy * dy, hw;
        if (dy2 >= ro2)
            continue;
        hw = sqrtfx(ro2 - dy2);
        xa = imax(0, ifloor(cx - hw));
        xb = imin(FB_W - 1, ifloor(cx + hw) + 1);
        for (x = xa; x <= xb; x++) {
            f32 dx = (f32)x - cx, d2 = dx * dx + dy2;
            if (d2 <= ri2)
                put(x, y, 256);
            else if (d2 < ro2)
                put(x, y, cov_of(ro - sqrtfx(d2)));
        }
    }
}

void g_ring(f32 cx, f32 cy, f32 r, f32 w, f32 a)
{
    int y0, y1, y, x;
    f32 h = w * 0.5f + 0.5f, ro, ri, ro2, ri2;
    G_POLL();
    if (r <= 0.0f || w <= 0.0f)
        return;
    if (r <= w * 0.5f) {
        g_disc(cx, cy, r + w * 0.5f, a);
        return;
    }
    set_level(a);
    cx += gox; cy += goy;
    ro = r + h;
    ri = r - h;
    ro2 = ro * ro;
    ri2 = ri > 0.0f ? ri * ri : 0.0f;
    y0 = imax(0, ifloor(cy - ro));
    y1 = imin(FB_H - 1, ifloor(cy + ro) + 1);
    for (y = y0; y <= y1; y++) {
        f32 dy = (f32)y - cy, dy2 = dy * dy, hwo, hwi;
        int xa, xb, xc, xd;
        if (dy2 >= ro2)
            continue;
        hwo = sqrtfx(ro2 - dy2);
        hwi = dy2 < ri2 ? sqrtfx(ri2 - dy2) : 0.0f;
        xa = imax(0, ifloor(cx - hwo));
        xb = imin(FB_W - 1, ifloor(cx - hwi) + 1);
        xc = imax(0, ifloor(cx + hwi));
        xd = imin(FB_W - 1, ifloor(cx + hwo) + 1);
        if (xc <= xb) {                        /* the two arcs meet: one run */
            xb = xd;
            xc = xd + 1;
        }
        for (x = xa; x <= xb; x++) {
            f32 dx = (f32)x - cx, d = sqrtfx(dx * dx + dy2);
            put(x, y, cov_of(h - absf(d - r)));
        }
        for (x = xc; x <= xd; x++) {
            f32 dx = (f32)x - cx, d = sqrtfx(dx * dx + dy2);
            put(x, y, cov_of(h - absf(d - r)));
        }
    }
}

void g_ellipse(f32 cx, f32 cy, f32 rx, f32 ry, f32 w, f32 a)
{
    int y0, y1, x0, x1, x, y;
    f32 pad, irx2, iry2, h;
    G_POLL();
    if (rx <= 0.05f || ry <= 0.05f) {
        if (rx > 0.05f)
            g_line(cx - rx, cy, cx + rx, cy, w > 0.0f ? w : 1.5f, a);
        else if (ry > 0.05f)
            g_line(cx, cy - ry, cx, cy + ry, w > 0.0f ? w : 1.5f, a);
        return;
    }
    set_level(a);
    cx += gox; cy += goy;
    h = w > 0.0f ? w * 0.5f : 0.0f;
    pad = h + 1.0f;
    irx2 = 1.0f / (rx * rx);
    iry2 = 1.0f / (ry * ry);
    y0 = imax(0, ifloor(cy - ry - pad));
    y1 = imin(FB_H - 1, ifloor(cy + ry + pad) + 1);
    x0 = imax(0, ifloor(cx - rx - pad));
    x1 = imin(FB_W - 1, ifloor(cx + rx + pad) + 1);
    for (y = y0; y <= y1; y++) {
        f32 dy = (f32)y - cy;
        for (x = x0; x <= x1; x++) {
            f32 dx = (f32)x - cx;
            f32 f2 = dx * dx * irx2 + dy * dy * iry2, f, g2, d;
            if (w <= 0.0f && f2 < 0.5f) {      /* well inside a filled one */
                put(x, y, 256);
                continue;
            }
            if (f2 < 1e-6f)
                continue;
            f = sqrtfx(f2);
            g2 = dx * dx * irx2 * irx2 + dy * dy * iry2 * iry2;
            d = (f - 1.0f) * f * rsqrtf(g2);   /* signed distance to the edge, first order */
            if (w <= 0.0f)
                put(x, y, cov_of(0.5f - d));
            else
                put(x, y, cov_of(h + 0.5f - absf(d)));
        }
    }
}

/* one pixel that is on the screen, coverage 1 and up */
static inline void ink(uint8_t *p, int cov, int mode, int lev)
{
    int v;
    if (cov > 256)
        cov = 256;
    if (mode == G_MAX) {
        v = (cov * lev) >> 8;
        if (v > *p)
            *p = (uint8_t)v;
    } else if (mode == G_ADD) {
        v = *p + ((cov * lev) >> 8);
        *p = (uint8_t)(v > 255 ? 255 : v);
    } else {
        v = lev - *p;
        *p = (uint8_t)(*p + (v * cov) / 256);
    }
}

/* A line as the pixels within R of it. It goes along its major axis m a column at a time (a column
 * is a row when the line is steep: the strides say which), and down each column over the pixels the
 * line can reach. How far a pixel is from the line (S) and how far along it (T) are straight in m
 * and in n, so they are set once in floats and then stepped as integers, S with 20 bits of fraction
 * and T with 16. No float per pixel or per column, but in the round ends, and none there either
 * where the end is solid or empty: on the FM-1 a line costs about what its pixels do. Against a
 * distance worked out in floats at every pixel, no pixel is more than one ink level of 255 off
 * (tests/lines.c). line_seg hands over only what the integers hold: a line shorter than 2048 that
 * starts within 2048 of the corner, R under 16.
 * skip: 1 leaves out the round end at (m0, n0), 2 the one at (m1, n1), for a path whose
 * neighbouring stroke covers it. */
#define Q18(v) ((int32_t)((v) * 262144.0f + ((v) < 0.0f ? -0.5f : 0.5f)))
static void line_scan(f32 m0, f32 n0, f32 m1, f32 n1, f32 um, f32 un, f32 len, f32 R,
                      int mlim, int nlim, int mstride, int nstride, int skip)
{
    const int mode = gm, lev = glevel;
    const f32 slope = (n1 - n0) / (m1 - m0), reach = R / um + 0.01f;
    const f32 RR = R * R, solid = R > 1.0f ? (R - 1.0f) * (R - 1.0f) : -1.0f;
    const int32_t R20 = (int32_t)(R * 1048576.0f), LEN = (int32_t)(len * 65536.0f);
    const int32_t UM20 = (int32_t)(um * 1048576.0f + 0.5f), UN20 = (int32_t)(un * 1048576.0f + (un < 0.0f ? -0.5f : 0.5f));
    const int32_t UM16 = (UM20 + 8) >> 4, UN16 = (UN20 + 8) >> 4;
    const int32_t REACH = (int32_t)(reach * 262144.0f), SLOPE = Q18(slope), N0 = Q18(n0), N1 = Q18(n1);
    const int ma = imax(0, ifloor(m0 - R - 0.01f) + 1), mb = imin(mlim - 1, ifloor(m1 + R + 0.01f));
    const int mi = ifloor(m0) + 1, mj = -ifloor(-m1) - 1;          /* mi..mj: the columns between the ends */
    int32_t NC, S, T;
    int m = ma, nr;
    if (ma > mb)
        return;
    if (m < mi)
        NC = N0;
    else if (m > mj)
        NC = N1;
    else
        NC = Q18(n0 + ((f32)m - m0) * slope);
    nr = ((NC - REACH) >> 18) + 1;                                   /* the first row the line can reach: S and T are kept there */
    {
        f32 pm = (f32)m - m0, pn = (f32)nr - n0;
        S = (int32_t)((pm * un - pn * um) * 1048576.0f);
        T = (int32_t)((pm * um + pn * un) * 65536.0f);
    }
    for (;;) {
        int n, na = nr < 0 ? 0 : nr, nb = (NC + REACH) >> 18;
        if (nb > nlim - 1)
            nb = nlim - 1;
        if (na <= nb) {
            int32_t s = S - (na - nr) * UM20, t = T + (na - nr) * UN16, tb = t + (nb - na) * UN16;
            uint8_t *p = g_fb + m * mstride + na * nstride;
            if ((uint32_t)t <= (uint32_t)LEN && (uint32_t)tb <= (uint32_t)LEN) {
                for (n = na; n <= nb; n++, p += nstride, s -= UM20) {   /* all of it beside the line */
                    int c = (R20 - (s < 0 ? -s : s)) >> 12;
                    if (c > 0)
                        ink(p, c, mode, lev);
                }
            } else {
                for (n = na; n <= nb; n++, p += nstride, s -= UM20, t += UN16) {
                    int c;
                    if ((uint32_t)t <= (uint32_t)LEN)
                        c = (R20 - (s < 0 ? -s : s)) >> 12;
                    else {                                             /* past an end: how far from it */
                        f32 qm, qn, d2;
                        if (t < 0) {
                            if (skip & 1)
                                continue;
                            qm = (f32)m - m0;
                            qn = (f32)n - n0;
                        } else {
                            if (skip & 2)
                                continue;
                            qm = (f32)m - m1;
                            qn = (f32)n - n1;
                        }
                        d2 = qm * qm + qn * qn;
                        if (d2 >= RR)
                            continue;
                        c = d2 <= solid ? 256 : cov_of(R - sqrtfx(d2));
                    }
                    if (c > 0)
                        ink(p, c, mode, lev);
                }
            }
        }
        if (++m > mb)
            break;
        {
            int n2;
            if (m < mi)
                NC = N0;
            else if (m > mj)
                NC = N1;
            else if (m == mi)
                NC = Q18(n0 + ((f32)m - m0) * slope);
            else
                NC += SLOPE;
            n2 = ((NC - REACH) >> 18) + 1;
            S += UN20 - (n2 - nr) * UM20;
            T += UM16 + (n2 - nr) * UN16;
            nr = n2;
        }
    }
}

/* The same in floats at every pixel, for a line too long, too far off or too wide for line_scan's
 * integers. */
static void line_far(f32 x0, f32 y0, f32 x1, f32 y1, f32 ux, f32 uy, f32 len, f32 R)
{
    f32 reach;
    int i, ia, ib, j, ja, jb;
    if (absf(x1 - x0) >= absf(y1 - y0)) {
        f32 slope;
        if (x0 > x1) {
            f32 t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            ux = -ux; uy = -uy;
        }
        slope = (y1 - y0) / (x1 - x0);
        reach = R / absf(ux) + 0.01f;            /* only the pixels the line can reach: the rest have no cover */
        ia = imax(0, ifloor(x0 - R - 0.01f) + 1);
        ib = imin(FB_W - 1, ifloor(x1 + R + 0.01f));
        for (i = ia; i <= ib; i++) {
            f32 xc = clampf((f32)i, x0, x1);
            f32 yc = y0 + (xc - x0) * slope;
            ja = imax(0, ifloor(yc - reach) + 1);
            jb = imin(FB_H - 1, ifloor(yc + reach));
            for (j = ja; j <= jb; j++) {
                f32 px = (f32)i - x0, py = (f32)j - y0, t = px * ux + py * uy, d;
                if (t < 0.0f)
                    d = sqrtfx(px * px + py * py);
                else if (t > len) {
                    px = (f32)i - x1; py = (f32)j - y1;
                    d = sqrtfx(px * px + py * py);
                } else
                    d = absf(px * uy - py * ux);
                put(i, j, cov_of(R - d));
            }
        }
    } else {
        f32 slope;
        if (y0 > y1) {
            f32 t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            ux = -ux; uy = -uy;
        }
        slope = (x1 - x0) / (y1 - y0);
        reach = R / absf(uy) + 0.01f;
        ja = imax(0, ifloor(y0 - R - 0.01f) + 1);
        jb = imin(FB_H - 1, ifloor(y1 + R + 0.01f));
        for (j = ja; j <= jb; j++) {
            f32 yc = clampf((f32)j, y0, y1);
            f32 xc = x0 + (yc - y0) * slope;
            ia = imax(0, ifloor(xc - reach) + 1);
            ib = imin(FB_W - 1, ifloor(xc + reach));
            for (i = ia; i <= ib; i++) {
                f32 px = (f32)i - x0, py = (f32)j - y0, t = px * ux + py * uy, d;
                if (t < 0.0f)
                    d = sqrtfx(px * px + py * py);
                else if (t > len) {
                    px = (f32)i - x1; py = (f32)j - y1;
                    d = sqrtfx(px * px + py * py);
                } else
                    d = absf(px * uy - py * ux);
                put(i, j, cov_of(R - d));
            }
        }
    }
}

static void line_seg(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, f32 a, int skip)
{
    f32 dx, dy, len2, inv, len, ux, uy, R;
    G_POLL();
    x0 += gox; x1 += gox; y0 += goy; y1 += goy;
    dx = x1 - x0;
    dy = y1 - y0;
    len2 = dx * dx + dy * dy;
    if (len2 < 1e-4f) {
        g_disc(x0 - gox, y0 - goy, w * 0.5f, a);
        return;
    }
    set_level(a);
    if (!glevel && gm != G_PAINT)
        return;                                  /* no ink: nothing to do */
    inv = rsqrtf(len2);
    len = len2 * inv;
    ux = dx * inv;
    uy = dy * inv;
    R = w * 0.5f + 0.5f;
    if (!(len < 2048.0f && R < 16.0f && absf(x0) < 2048.0f && absf(y0) < 2048.0f)) {
        line_far(x0, y0, x1, y1, ux, uy, len, R);
        return;
    }
    if (absf(dx) >= absf(dy)) {
        if (x0 > x1)
            line_scan(x1, y1, x0, y0, -ux, -uy, len, R, FB_W, FB_H, 1, FB_W, ((skip & 1) << 1) | (skip >> 1));
        else
            line_scan(x0, y0, x1, y1, ux, uy, len, R, FB_W, FB_H, 1, FB_W, skip);
    } else {
        if (y0 > y1)
            line_scan(y1, x1, y0, x0, -uy, -ux, len, R, FB_H, FB_W, FB_W, 1, ((skip & 1) << 1) | (skip >> 1));
        else
            line_scan(y0, x0, y1, x1, uy, ux, len, R, FB_H, FB_W, FB_W, 1, skip);
    }
}

void g_line(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, f32 a) { line_seg(x0, y0, x1, y1, w, a, 0); }

/* ---- a bed: what a wide stroke with no ink clears -------------------------------------------
 * A path is stamped into a mask of one bit a pixel, and the mask is taken out of the picture in one
 * go. However often the path crosses itself, each pixel is cleared once: a 9-wide g_line with no ink
 * went over every pixel of a tangled cord five and six times, and that was most of its frame. */
#define BED_WORDS ((FB_W + 31) / 32)
#ifdef H1_DEVICE
static uint32_t bed[FB_H * BED_WORDS] __attribute__((section(".pool")));
#else
static uint32_t bed[FB_H * BED_WORDS];
#endif
static int bed_y0 = FB_H, bed_y1 = -1;           /* the rows that hold anything */
static int32_t bed_x, bed_y;                     /* where the path is, 16.16 */
static int bed_px, bed_py;

static void bed_dot(int x, int y)
{
    static const int8_t HW[9] = {2, 3, 4, 4, 4, 4, 4, 3, 2};   /* a disc 9 across */
    int j;
    if (x == bed_px && y == bed_py)
        return;
    bed_px = x;
    bed_py = y;
    if (x < -4 || x > FB_W + 3 || y < -4 || y > FB_H + 3)
        return;
    if (y - 4 < bed_y0) bed_y0 = imax(0, y - 4);
    if (y + 4 > bed_y1) bed_y1 = imin(FB_H - 1, y + 4);
    for (j = 0; j < 9; j++) {
        int yy = y + j - 4, a = x - HW[j], b = x + HW[j];
        uint32_t *row, ma, mb;
        if ((unsigned)yy >= FB_H)
            continue;
        if (a < 0) a = 0;
        if (b > FB_W - 1) b = FB_W - 1;
        if (a > b)
            continue;
        row = &bed[yy * BED_WORDS];
        ma = 0xFFFFFFFFu << (a & 31);
        mb = 0xFFFFFFFFu >> (31 - (b & 31));
        if (a >> 5 == b >> 5)
            row[a >> 5] |= ma & mb;
        else {
            row[a >> 5] |= ma;
            row[b >> 5] |= mb;
        }
    }
}

static int32_t bed_fix(f32 v)                    /* 16.16, and far enough off the screen is all the same */
{
    if (!(v > -64.0f)) v = -64.0f;               /* (whatever v is, a number comes out) */
    if (v > (f32)FB_W + 64.0f) v = (f32)FB_W + 64.0f;
    return (int32_t)(v * 65536.0f);
}

void g_bed_move(f32 x, f32 y)
{
    bed_x = bed_fix(x + gox);
    bed_y = bed_fix(y + goy);
    bed_px = bed_py = -99;
    bed_dot((bed_x + 32768) >> 16, (bed_y + 32768) >> 16);
}

void g_bed_to(f32 x, f32 y)
{
    int32_t x1 = bed_fix(x + gox), y1 = bed_fix(y + goy), dx = x1 - bed_x, dy = y1 - bed_y;
    int32_t l = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    int n = (int)(l / (3 * 65536)) + 1, i;       /* a stamp every three pixels or less */
    dx /= n;
    dy /= n;
    for (i = 1; i < n; i++) {
        bed_x += dx;
        bed_y += dy;
        bed_dot((bed_x + 32768) >> 16, (bed_y + 32768) >> 16);
    }
    bed_x = x1;
    bed_y = y1;
    bed_dot((bed_x + 32768) >> 16, (bed_y + 32768) >> 16);
}

void g_bed_clear(void)
{
    int y, w, n;
    for (y = bed_y0; y <= bed_y1; y++) {
        uint32_t *row = &bed[y * BED_WORDS];
        for (w = 0; w < BED_WORDS; w++) {
            uint32_t m = row[w];
            uint32_t *fw;
            if (!m)
                continue;
            row[w] = 0;
            fw = &g_fb_w[(y * FB_W + w * 32) / 4];   /* (a row starts on a word, and so does every 32 pixels of it) */
            for (n = 0; m; n++, m >>= 4) {
                uint8_t *p = (uint8_t *)&fw[n];
                if ((m & 15u) == 15u) {
                    fw[n] = 0;
                    continue;
                }
                if (m & 1u) p[0] = 0;
                if (m & 2u) p[1] = 0;
                if (m & 4u) p[2] = 0;
                if (m & 8u) p[3] = 0;
            }
        }
    }
    bed_y0 = FB_H;
    bed_y1 = -1;
}

/* whether a stroke from a to b is a line (g_line draws anything shorter as a dot) */
static int stroke_real(const f32 *a, const f32 *b)
{
    f32 dx = (b[0] + gox) - (a[0] + gox), dy = (b[1] + goy) - (a[1] + goy);
    return dx * dx + dy * dy >= 1e-4f;
}

/* In G_MAX a stroke's round start lies wholly inside the stroke before it, which is at least as
 * strong at every pixel of it: it is left out, and the picture is the same. */
void g_poly(const f32 *xy, int n, int closed, f32 w, f32 a)
{
    int i, lap = closed && n > 2, joined = gm == G_MAX;
    for (i = 0; i + 1 < n; i++) {
        int before = i ? i - 1 : (lap ? n - 1 : -1);
        line_seg(xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], w, a,
                 joined && before >= 0 && stroke_real(xy + before * 2, xy + i * 2));
    }
    if (lap)
        line_seg(xy[(n - 1) * 2], xy[(n - 1) * 2 + 1], xy[0], xy[1], w, a,
                 joined && stroke_real(xy + (n - 2) * 2, xy + (n - 1) * 2));
}

/* a filled convex polygon whose ink runs from a at the row ya to b at the row yb (and holds outside them) */
static void fill_poly(const f32 *xy, int n, f32 a, f32 ya, f32 b, f32 yb)
{
    f32 ex[8], ey[8], ec[8], ea[8], ei[8], minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, area = 0.0f, sgn;
    f32 grad = yb != ya ? 1.0f / (yb - ya) : 0.0f;
    int i, x, y, x0, x1, y0, y1, shade = a != b && grad != 0.0f;
    G_POLL();
    if (n < 3 || n > 8)
        return;
    set_level(a);
    ya += goy;
    for (i = 0; i < n; i++) {
        f32 ax = xy[2 * i] + gox, ay = xy[2 * i + 1] + goy;
        int j = i + 1 < n ? i + 1 : 0;
        f32 bx = xy[2 * j] + gox, by = xy[2 * j + 1] + goy, dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, inv;
        area += ax * by - bx * ay;
        minx = minf(minx, ax); maxx = maxf(maxx, ax);
        miny = minf(miny, ay); maxy = maxf(maxy, ay);
        inv = l2 > 1e-8f ? rsqrtf(l2) : 0.0f;
        ex[i] = -dy * inv;                       /* the edge's normal, and its offset */
        ey[i] = dx * inv;
        ec[i] = -(ex[i] * ax + ey[i] * ay);
    }
    sgn = area >= 0.0f ? 1.0f : -1.0f;
    x0 = imax(0, ifloor(minx - 1.0f));
    x1 = imin(FB_W - 1, ifloor(maxx + 2.0f));
    y0 = imax(0, ifloor(miny - 1.0f));
    y1 = imin(FB_H - 1, ifloor(maxy + 2.0f));
    for (i = 0; i < n; i++) {                    /* e = ea * x + (eb * y + ec), and 1 / ea for the row's ends */
        ea[i] = sgn * ex[i];
        ei[i] = ea[i] > 1e-6f || ea[i] < -1e-6f ? 1.0f / ea[i] : 0.0f;
    }
    for (y = y0; y <= y1; y++) {
        /* where this row can have any cover (xa..xb), and where it is all inside (ia..ib): only the
         * pixels between the two are worked out edge by edge */
        f32 xa = (f32)x0, xb = (f32)x1, ia = (f32)x0, ib = (f32)x1;
        int a0, a1, i0, i1, none = 0;
        for (i = 0; i < n; i++) {
            f32 c = sgn * (ey[i] * (f32)y + ec[i]);
            if (ei[i] == 0.0f) {
                if (c <= -0.5f) none = 1;
                if (c < 0.51f) ib = ia - 1.0f;
            } else if (ea[i] > 0.0f) {
                xa = maxf(xa, (-0.51f - c) * ei[i]);
                ia = maxf(ia, (0.51f - c) * ei[i]);
            } else {
                xb = minf(xb, (-0.51f - c) * ei[i]);
                ib = minf(ib, (0.51f - c) * ei[i]);
            }
        }
        if (none || xb < xa)
            continue;
        if (shade)
            set_level(lerpf(a, b, sat(((f32)y - ya) * grad)));
        a0 = imax(x0, ifloor(xa));
        a1 = imin(x1, ifloor(xb) + 1);
        i0 = ifloor(ia) + 1;
        i1 = ifloor(ib);
        if (i1 < i0)
            i0 = a1 + 1, i1 = a1;                /* no inside on this row */
        for (x = a0; x <= a1; x++) {
            f32 d = 1e9f;
            if (x >= i0 && x <= i1) {            /* the inside of the row in one run */
                uint8_t *p = &g_fb[y * FB_W + x], *e = p + (i1 - x);
                if (gm == G_MAX) {
                    for (; p <= e; p++)
                        if (glevel > *p) *p = (uint8_t)glevel;
                } else if (gm == G_ADD) {
                    for (; p <= e; p++) {
                        int v = *p + glevel;
                        *p = (uint8_t)(v > 255 ? 255 : v);
                    }
                } else {
                    for (; p <= e; p++)
                        *p = (uint8_t)glevel;
                }
                x = i1;
                continue;
            }
            for (i = 0; i < n && d > -0.5f; i++) {
                f32 e = sgn * (ex[i] * (f32)x + ey[i] * (f32)y + ec[i]);
                if (e < d)
                    d = e;
            }
            if (d > -0.5f)
                put(x, y, cov_of(d + 0.5f));
        }
    }
}

void g_fill(const f32 *xy, int n, f32 a) { fill_poly(xy, n, a, 0.0f, a, 0.0f); }
void g_fillv(const f32 *xy, int n, f32 ya, f32 a, f32 yb, f32 b) { fill_poly(xy, n, a, ya, b, yb); }

/* ---- soft things: light, not line --------------------------------------------------------
 * Their ink changes from pixel to pixel, so it is worked out to 16 bits and brought down to the
 * framebuffer's 8 through a 4 x 4 ordered dither: a slow gradient on a 16-bit panel would band. */
static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

/* one pixel of soft ink, v = 0..65535. MAX and ADD as for lines; PAINT darkens what is there. */
static inline void put_soft(int x, int y, int v)
{
    uint8_t *p = &g_fb[y * FB_W + x];
    int q;
    if (v <= 0)
        return;
    if (gm == G_PAINT) {
        *p = (uint8_t)((*p * (65536 - v)) >> 16);
        return;
    }
    q = ((v + (BAYER[(y & 3) * 4 + (x & 3)] << 5)) >> 9) << 1;        /* to every other level: about what the panel can show */
    if (q > 255) q = 255;
    if (!q)
        return;
    if (gm == G_MAX) {
        if (q > *p) *p = (uint8_t)q;
    } else {
        q += *p;
        *p = (uint8_t)(q > 255 ? 255 : q);
    }
}

void g_glow(f32 cx, f32 cy, f32 r, f32 a)
{
    int x, y, x0, x1, y0, y1;
    f32 ir2, peak;
    G_POLL();
    a = sat(a * gdim);
    if (r < 1.0f || a < 0.004f)
        return;
    cx += gox; cy += goy;
    ir2 = 1.0f / (r * r);
    peak = a * 65535.0f;
    y0 = imax(0, ifloor(cy - r) + 1);
    y1 = imin(FB_H - 1, ifloor(cy + r));
    for (y = y0; y <= y1; y++) {
        f32 dy = (f32)y - cy, hw2 = r * r - dy * dy, hw;
        if (hw2 <= 0.0f)
            continue;
        hw = sqrtfx(hw2);
        x0 = imax(0, ifloor(cx - hw) + 1);
        x1 = imin(FB_W - 1, ifloor(cx + hw));
        for (x = x0; x <= x1; x++) {
            f32 dx = (f32)x - cx, f = 1.0f - (dx * dx + dy * dy) * ir2;
            if (f > 0.0f)
                put_soft(x, y, (int)(f * f * peak));
        }
    }
}

void g_beam(f32 x0, f32 y0, f32 x1, f32 y1, f32 w0, f32 w1, f32 a0, f32 a1, f32 soft)
{
    f32 dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy, len, il, ux, uy, h0 = w0 * 0.5f, h1 = w1 * 0.5f, hm = maxf(h0, h1) + 1.0f, bias;
    int x, y, ya, yb, bx0, bx1;
    G_POLL();
    a0 = sat(a0 * gdim); a1 = sat(a1 * gdim);
    if (l2 < 0.25f || (a0 < 0.004f && a1 < 0.004f))
        return;
    x0 += gox; y0 += goy; x1 += gox; y1 += goy;
    il = rsqrtf(l2);
    len = l2 * il;
    ux = dx * il; uy = dy * il;
    soft = sat(soft);
    bias = 0.5f * (1.0f - soft);
    ya = imax(0, ifloor(minf(y0, y1) - hm));
    yb = imin(FB_H - 1, ifloor(maxf(y0, y1) + hm) + 1);
    bx0 = imax(0, ifloor(minf(x0, x1) - hm));
    bx1 = imin(FB_W - 1, ifloor(maxf(x0, x1) + hm) + 1);
    for (y = ya; y <= yb; y++) {
        int xa = bx0, xb = bx1;
        f32 ry = (f32)y - y0, al, pe;
        if (uy > 0.05f || uy < -0.05f) {         /* the row's stretch of the beam: where the axis crosses, and its width along x */
            f32 xc = x0 + ry * ux / uy, hx = hm / absf(uy);
            xa = imax(xa, ifloor(xc - hx));
            xb = imin(xb, ifloor(xc + hx) + 1);
        }
        al = ((f32)xa - x0) * ux + ry * uy;      /* along the axis, and across it */
        pe = ((f32)xa - x0) * uy - ry * ux;
        for (x = xa; x <= xb; x++, al += ux, pe += uy) {
            f32 t, hw, e, c, d = pe < 0.0f ? -pe : pe;
            if (al <= -0.5f || al >= len + 0.5f)
                continue;
            t = sat(al * il);
            hw = h0 + (h1 - h0) * t;
            e = 1.0f + soft * (hw - 1.0f);
            if (e < 1.0f) e = 1.0f;
            c = (hw - d) / e + bias;
            if (c <= 0.0f)
                continue;
            if (c > 1.0f) c = 1.0f;
            if (al < 0.5f) c *= al + 0.5f;
            else if (al > len - 0.5f) c *= len + 0.5f - al;
            put_soft(x, y, (int)(c * (a0 + (a1 - a0) * t) * 65535.0f));
        }
    }
}

/* A comet with its head at (x, y), r across, its tails streaming along (dx, dy) (a unit vector), len long:
 * a coma, a broad dust tail that lags to one side (curl, in pixels at its end), a thin straight
 * gas tail, and a few grains coming off it. t moves the grains; seed tells one comet from another. */
void g_comet(f32 x, f32 y, f32 r, f32 dx, f32 dy, f32 len, f32 curl, f32 a, f32 t, f32 seed)
{
    f32 px = -dy, py = dx, qx = x, qy = y;
    int i;
    if (a < 0.02f || r < 0.6f)
        return;
    for (i = 1; i <= 5; i++) {                   /* the dust: five soft lengths, widening and thinning out */
        f32 u = (f32)i * 0.2f, v = u - 0.2f, nx = x + dx * len * u + px * curl * u * u, ny = y + dy * len * u + py * curl * u * u;
        g_beam(qx, qy, nx, ny, r * (1.7f + 2.4f * v), r * (1.7f + 2.4f * u), a * 0.85f * (1.0f - v) * (1.0f - 0.5f * v), a * 0.85f * (1.0f - u) * (1.0f - 0.5f * u), 1.0f);
        qx = nx - dx * 1.5f; qy = ny - dy * 1.5f;        /* (the next length starts inside this one: no seam) */
    }
    g_beam(x, y, x + dx * len * 1.2f - px * curl * 0.3f, y + dy * len * 1.2f - py * curl * 0.3f, maxf(1.3f, r * 0.42f), 0.8f, a * 0.8f, 0.0f, 0.0f);
    for (i = 0; i < 5; i++) {                    /* grains */
        f32 h = fractf(seed * 7.31f + (f32)i * 0.618f), u = fractf(h + t * (0.35f + 0.2f * h)), sp = (fractf(h * 13.7f) - 0.5f) * r * 3.2f * u;
        g_disc(x + dx * len * u + px * (curl * u * u + sp), y + dy * len * u + py * (curl * u * u + sp), 0.6f + 0.5f * (1.0f - u), a * (1.0f - u) * 0.9f);
    }
    g_glow(x, y, r * 2.6f, a * 0.7f);
    g_disc(x, y, r * 0.62f, a);
}

void g_vspan(int x, f32 y0, f32 y1, f32 a)
{
    int ya, yb, y;
    if (!(x & 15))
        G_POLL();                                /* (a picture made of columns: now and then) */
    x += (int)gox;
    y0 += goy; y1 += goy;
    if ((unsigned)x >= FB_W || y1 <= y0)
        return;
    set_level(a);
    y0 -= 0.5f; y1 -= 0.5f;                      /* to edges */
    if (y0 < -0.5f) y0 = -0.5f;
    if (y1 > (f32)FB_H - 0.5f) y1 = (f32)FB_H - 0.5f;
    if (y1 <= y0)
        return;
    ya = ifloor(y0 + 0.5f);
    yb = ifloor(y1 + 0.5f);
    if (ya == yb) {
        put(x, ya, cov_of(y1 - y0));
        return;
    }
    put(x, ya, cov_of((f32)ya + 0.5f - y0));
    for (y = ya + 1; y < yb; y++)
        put(x, y, 256);
    put(x, yb, cov_of(y1 - ((f32)yb - 0.5f)));
}

void g_arc(f32 cx, f32 cy, f32 r, f32 t0, f32 t1, f32 w, f32 a)
{
    f32 span = absf(t1 - t0), p[4];
    int n = iclamp((int)(span * r * 0.9f) + 2, 2, 96), i, joined = 0;
    p[2] = cx + r * cos_turns(t0);
    p[3] = cy + r * sin_turns(t0);
    for (i = 1; i <= n; i++) {
        f32 t = t0 + (t1 - t0) * (f32)i / (f32)n;
        p[0] = p[2]; p[1] = p[3];
        p[2] = cx + r * cos_turns(t);
        p[3] = cy + r * sin_turns(t);
        line_seg(p[0], p[1], p[2], p[3], w, a, joined);
        joined = gm == G_MAX && stroke_real(p, p + 2);   /* (the next stroke's round start is inside this one) */
    }
}

/* ---- the stroke font ------------------------------------------------------------------
 * Glyphs live on a grid 8 wide and 12 tall (half units), top-left origin; every corner is a
 * chamfer, so the letters stay crisp at any size and can be drawn with one line routine. */
#define P(x, y) (uint8_t)(((x) << 4) | (y))
#define UP 0xFF
#define END 0xFE

static const uint8_t F_A[] = {P(0,12),P(0,4),P(4,0),P(8,4),P(8,12),UP,P(0,8),P(8,8),END};
static const uint8_t F_B[] = {P(0,0),P(6,0),P(8,2),P(8,4),P(6,6),P(0,6),UP,P(6,6),P(8,8),P(8,10),P(6,12),P(0,12),P(0,0),END};
static const uint8_t F_C[] = {P(8,2),P(6,0),P(2,0),P(0,2),P(0,10),P(2,12),P(6,12),P(8,10),END};
static const uint8_t F_D[] = {P(0,0),P(5,0),P(8,3),P(8,9),P(5,12),P(0,12),P(0,0),END};
static const uint8_t F_E[] = {P(8,0),P(0,0),P(0,12),P(8,12),UP,P(0,6),P(6,6),END};
static const uint8_t F_F[] = {P(8,0),P(0,0),P(0,12),UP,P(0,6),P(6,6),END};
static const uint8_t F_G[] = {P(8,2),P(6,0),P(2,0),P(0,2),P(0,10),P(2,12),P(6,12),P(8,10),P(8,6),P(4,6),END};
static const uint8_t F_H[] = {P(0,0),P(0,12),UP,P(8,0),P(8,12),UP,P(0,6),P(8,6),END};
static const uint8_t F_I[] = {P(4,0),P(4,12),UP,P(1,0),P(7,0),UP,P(1,12),P(7,12),END};
static const uint8_t F_J[] = {P(8,0),P(8,10),P(6,12),P(2,12),P(0,10),P(0,8),END};
static const uint8_t F_K[] = {P(0,0),P(0,12),UP,P(8,0),P(0,7),UP,P(2,5),P(8,12),END};
static const uint8_t F_L[] = {P(0,0),P(0,12),P(8,12),END};
static const uint8_t F_M[] = {P(0,12),P(0,0),P(4,6),P(8,0),P(8,12),END};
static const uint8_t F_N[] = {P(0,12),P(0,0),P(8,12),P(8,0),END};
static const uint8_t F_O[] = {P(2,0),P(6,0),P(8,2),P(8,10),P(6,12),P(2,12),P(0,10),P(0,2),P(2,0),END};
static const uint8_t F_P[] = {P(0,12),P(0,0),P(6,0),P(8,2),P(8,5),P(6,7),P(0,7),END};
static const uint8_t F_Q[] = {P(2,0),P(6,0),P(8,2),P(8,10),P(6,12),P(2,12),P(0,10),P(0,2),P(2,0),UP,P(5,9),P(8,12),END};
static const uint8_t F_R[] = {P(0,12),P(0,0),P(6,0),P(8,2),P(8,5),P(6,7),P(0,7),UP,P(4,7),P(8,12),END};
static const uint8_t F_S[] = {P(8,2),P(6,0),P(2,0),P(0,2),P(0,4),P(2,6),P(6,6),P(8,8),P(8,10),P(6,12),P(2,12),P(0,10),END};
static const uint8_t F_T[] = {P(0,0),P(8,0),UP,P(4,0),P(4,12),END};
static const uint8_t F_U[] = {P(0,0),P(0,10),P(2,12),P(6,12),P(8,10),P(8,0),END};
static const uint8_t F_V[] = {P(0,0),P(4,12),P(8,0),END};
static const uint8_t F_W[] = {P(0,0),P(1,12),P(4,5),P(7,12),P(8,0),END};
static const uint8_t F_X[] = {P(0,0),P(8,12),UP,P(8,0),P(0,12),END};
static const uint8_t F_Y[] = {P(0,0),P(4,6),P(8,0),UP,P(4,6),P(4,12),END};
static const uint8_t F_Z[] = {P(0,0),P(8,0),P(0,12),P(8,12),END};
static const uint8_t F_0[] = {P(2,0),P(6,0),P(8,2),P(8,10),P(6,12),P(2,12),P(0,10),P(0,2),P(2,0),UP,P(6,3),P(2,9),END};
static const uint8_t F_1[] = {P(2,2),P(4,0),P(4,12),UP,P(1,12),P(7,12),END};
static const uint8_t F_2[] = {P(0,2),P(2,0),P(6,0),P(8,2),P(8,5),P(0,12),P(8,12),END};
static const uint8_t F_3[] = {P(0,2),P(2,0),P(6,0),P(8,2),P(8,4),P(6,6),P(3,6),UP,P(6,6),P(8,8),P(8,10),P(6,12),P(2,12),P(0,10),END};
static const uint8_t F_4[] = {P(6,12),P(6,0),P(0,8),P(8,8),END};
static const uint8_t F_5[] = {P(8,0),P(0,0),P(0,5),P(6,5),P(8,7),P(8,10),P(6,12),P(2,12),P(0,10),END};
static const uint8_t F_6[] = {P(8,2),P(6,0),P(2,0),P(0,2),P(0,10),P(2,12),P(6,12),P(8,10),P(8,8),P(6,6),P(0,6),END};
static const uint8_t F_7[] = {P(0,0),P(8,0),P(3,12),END};
static const uint8_t F_8[] = {P(2,0),P(6,0),P(8,2),P(8,4),P(6,6),P(2,6),P(0,4),P(0,2),P(2,0),UP,P(2,6),P(0,8),P(0,10),P(2,12),P(6,12),P(8,10),P(8,8),P(6,6),END};
static const uint8_t F_9[] = {P(0,10),P(2,12),P(6,12),P(8,10),P(8,2),P(6,0),P(2,0),P(0,2),P(0,4),P(2,6),P(8,6),END};
static const uint8_t F_MINUS[] = {P(1,6),P(7,6),END};
static const uint8_t F_PLUS[] = {P(1,6),P(7,6),UP,P(4,3),P(4,9),END};
static const uint8_t F_DOT[] = {P(4,11),P(4,12),END};
static const uint8_t F_SLASH[] = {P(8,0),P(0,12),END};
static const uint8_t F_COLON[] = {P(4,3),P(4,4),UP,P(4,9),P(4,10),END};
static const uint8_t F_PCT[] = {P(8,0),P(0,12),UP,P(0,0),P(2,0),P(2,2),P(0,2),P(0,0),UP,P(6,10),P(8,10),P(8,12),P(6,12),P(6,10),END};
static const uint8_t F_BANG[] = {P(4,0),P(4,8),UP,P(4,11),P(4,12),END};
static const uint8_t F_QM[] = {P(0,2),P(2,0),P(6,0),P(8,2),P(8,4),P(4,7),P(4,8),UP,P(4,11),P(4,12),END};
static const uint8_t F_LT[] = {P(7,2),P(1,6),P(7,10),END};
static const uint8_t F_GT[] = {P(1,2),P(7,6),P(1,10),END};
static const uint8_t F_USC[] = {P(0,12),P(8,12),END};
static const uint8_t F_EQ[] = {P(1,4),P(7,4),UP,P(1,8),P(7,8),END};
static const uint8_t F_HASH[] = {P(3,1),P(2,11),UP,P(6,1),P(5,11),UP,P(0,4),P(8,4),UP,P(0,8),P(8,8),END};
static const uint8_t F_NONE[] = {END};

static const uint8_t *glyph(char c)
{
    static const uint8_t *const AZ[26] = {F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H, F_I, F_J, F_K, F_L, F_M,
                                          F_N, F_O, F_P, F_Q, F_R, F_S, F_T, F_U, F_V, F_W, F_X, F_Y, F_Z};
    static const uint8_t *const D09[10] = {F_0, F_1, F_2, F_3, F_4, F_5, F_6, F_7, F_8, F_9};
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return AZ[c - 'A'];
    if (c >= '0' && c <= '9') return D09[c - '0'];
    switch (c) {
    case '-': return F_MINUS;
    case '+': return F_PLUS;
    case '.': return F_DOT;
    case '/': return F_SLASH;
    case ':': return F_COLON;
    case '%': return F_PCT;
    case '!': return F_BANG;
    case '?': return F_QM;
    case '<': return F_LT;
    case '>': return F_GT;
    case '_': return F_USC;
    case '=': return F_EQ;
    case '#': return F_HASH;
    default: return F_NONE;
    }
}

#define GLYPH_W (8.0f / 12.0f)       /* of the cap height */
#define GLYPH_GAP (3.4f / 12.0f)

static f32 advance_of(char c, f32 size)
{
    if (c == ' ') return size * 0.5f;
    if (c == '.' || c == ':' || c == '!') return size * (5.0f / 12.0f + GLYPH_GAP * 0.6f);
    if (c == '1' || c == 'I') return size * (7.0f / 12.0f + GLYPH_GAP);
    return size * (GLYPH_W + GLYPH_GAP);
}

f32 g_text_w(const char *s, f32 size)
{
    f32 w = 0.0f;
    if (!*s)
        return 0.0f;
    for (; *s; s++)
        w += advance_of(*s, size);
    return w - size * GLYPH_GAP;
}

f32 g_text(f32 x, f32 y, const char *s, f32 size, f32 w, f32 a, int align)
{
    f32 u = size / 12.0f;
    if (align == 1) x -= g_text_w(s, size) * 0.5f;
    else if (align == 2) x -= g_text_w(s, size);
    for (; *s; s++) {
        const uint8_t *g = glyph(*s);
        f32 ox = x, lx = 0.0f, ly = 0.0f;
        int pen = 0;
        if (*s == '.' || *s == ':' || *s == '!') ox -= u * 1.5f;
        if (*s == '1' || *s == 'I') ox -= u * 0.5f;
        for (; *g != END; g++) {
            f32 px, py;
            if (*g == UP) {
                pen = 0;
                continue;
            }
            px = ox + (f32)(*g >> 4) * u;
            py = y + (f32)(*g & 15) * u;
            if (pen)
                g_line(lx, ly, px, py, w, a);
            lx = px; ly = py;
            pen = 1;
        }
        x += advance_of(*s, size);
    }
    return x;
}
