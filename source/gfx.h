/* h1 - graphics: an 8-bit intensity framebuffer (0 = the bezel's black, 255 = full ink) and
 * anti-aliased vector primitives. One ink colour per instrument is applied by a palette when
 * the frame is presented, so the art is drawn once, in shades.
 *
 * Pixel centres are at integer coordinates. Everything clips to the screen, and shapes that run
 * off the edge are fine: the black around the panel is part of the picture. */
#pragma once
#include "mathx.h"

#ifndef FB_W
#define FB_W 232   /* INTONARUMORI: janela 232x132 (arte 132x132 ou painel do osciloscopio) */
#endif
#ifndef FB_H
#define FB_H 132
#endif

enum { G_MAX, G_ADD, G_PAINT };   /* MAX: line art (crossings don't blow out); PAINT: alpha over (a = 0 erases) */

/* The framebuffer is kept as words, so it sits on a word boundary whatever the linker puts in front of
 * it: g_clear writes it a word at a time, and the FM-1's CPU stops on a word written to an odd address. */
extern uint32_t g_fb_w[FB_W * FB_H / 4];
#define g_fb ((uint8_t *)g_fb_w)

/* Called at the start of every primitive when set: the port's chance to do a little of something else
 * (send the last frame's pixels) while a frame is drawn. A long loop that draws nothing can call G_POLL() too. */
extern void (*g_poll_fn)(void);
#define G_POLL() do { if (g_poll_fn) g_poll_fn(); } while (0)

void g_clear(void);
void g_fade(int mul256);                                   /* every pixel * mul/256: trails */
void g_mode(int mode);
void g_dim(f32 k);                                         /* global multiplier on every later alpha (0..1) */
void g_origin(f32 ox, f32 oy);                             /* added to every coordinate */

void g_line(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, f32 a); /* round caps; w = stroke width */
void g_disc(f32 cx, f32 cy, f32 r, f32 a);
void g_ring(f32 cx, f32 cy, f32 r, f32 w, f32 a);
void g_ellipse(f32 cx, f32 cy, f32 rx, f32 ry, f32 w, f32 a);   /* w <= 0: filled */
void g_rect(f32 x, f32 y, f32 w, f32 h, f32 a);            /* filled, x,y = top-left edge */
void g_poly(const f32 *xy, int n, int closed, f32 w, f32 a);
void g_fill(const f32 *xy, int n, f32 a);                  /* a filled convex polygon, either winding */
void g_vspan(int x, f32 y0, f32 y1, f32 a);                /* one column of pixels from y0 to y1 (edges), soft ends */
void g_fillv(const f32 *xy, int n, f32 ya, f32 a, f32 yb, f32 b);   /* g_fill, the ink running from a at row ya to b at row yb */
/* Soft things, dithered. In PAINT mode they darken what is under them instead (a = how much). */
void g_glow(f32 cx, f32 cy, f32 r, f32 a);                 /* a round light: a at the middle, nothing at r */
void g_beam(f32 x0, f32 y0, f32 x1, f32 y1, f32 w0, f32 w1, f32 a0, f32 a1, f32 soft);
                                                           /* a stroke whose width and ink run from w0, a0 to w1, a1.
                                                            * soft 0: a crisp edge. 1: brightest on its axis, nothing at its edge */
void g_arc(f32 cx, f32 cy, f32 r, f32 t0, f32 t1, f32 w, f32 a);   /* angles in turns, 0 = east, clockwise on screen */

void g_comet(f32 x, f32 y, f32 r, f32 dx, f32 dy, f32 len, f32 curl, f32 a, f32 t, f32 seed);
                                                           /* head at x, y, r across; tails along the unit vector dx, dy */

/* A bed: the path of a stroke 9 wide with no ink, cleared out of the picture in one go (g_bed_clear).
 * For a cord that passes in front of what is already drawn. */
void g_bed_move(f32 x, f32 y);
void g_bed_to(f32 x, f32 y);
void g_bed_clear(void);

/* stroke font: capitals, digits, a few signs. size = cap height in pixels. */
f32 g_text_w(const char *s, f32 size);
f32 g_text(f32 x, f32 y, const char *s, f32 size, f32 w, f32 a, int align);   /* y = top of caps; align 0 left, 1 centre, 2 right */
