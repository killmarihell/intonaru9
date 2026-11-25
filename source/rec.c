#include "rec.h"
#include <stdio.h>
#include <string.h>

static void w32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void w16(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; }

static void header(uint8_t *h, uint32_t rate, uint32_t frames) {
    uint32_t ds = frames * 4;
    memcpy(h, "RIFF", 4); w32(h + 4, 36 + ds); memcpy(h + 8, "WAVEfmt ", 8); w32(h + 16, 16);
    w16(h + 20, 1); w16(h + 22, 2); w32(h + 24, rate); w32(h + 28, rate * 4); w16(h + 32, 4); w16(h + 34, 16);
    memcpy(h + 36, "data", 4); w32(h + 40, ds);
}

int rec_open(Rec *r, const char *dir, uint32_t rate, int16_t *ring_mem) {
    memset(r, 0, sizeof *r);
    r->ring = ring_mem; r->rate = rate;
    for (int i = 1; i < 10000; i++) {
        snprintf(r->path, sizeof r->path, "%s/REC_%04d.wav", dir, i);
        FILE *t = fopen(r->path, "rb");
        if (t) { fclose(t); continue; }
        break;
    }
    FILE *f = fopen(r->path, "wb");
    if (!f) { r->error = 1; return -1; }
    uint8_t h[44]; header(h, rate, 0);
    if (fwrite(h, 1, 44, f) != 44) { fclose(f); r->error = 1; return -1; }
    r->fp = f; r->active = 1;
    return 0;
}

void rec_push(Rec *r, const int16_t *st, int frames) {
    if (!r->active) return;
    uint32_t w = r->wr, used = w - r->rd;
    if (used + (uint32_t)frames > REC_RING_FRAMES) { r->dropped += (uint32_t)frames; return; }
    for (int i = 0; i < frames; i++) {
        uint32_t k = (w + (uint32_t)i) & (REC_RING_FRAMES - 1);
        r->ring[k * 2] = st[i * 2]; r->ring[k * 2 + 1] = st[i * 2 + 1];
    }
    __sync_synchronize();
    r->wr = w + (uint32_t)frames;
}

int rec_pump(Rec *r) {
    if (!r->fp) return 0;
    int total = 0;
    for (;;) {
        __sync_synchronize();
        uint32_t avail = r->wr - r->rd;
        if (!avail) break;
        uint32_t k = r->rd & (REC_RING_FRAMES - 1), n = avail;
        if (n > REC_RING_FRAMES - k) n = REC_RING_FRAMES - k;      /* trecho contiguo */
        if (n > 4096) n = 4096;
        if (fwrite(r->ring + k * 2, 4, n, (FILE *)r->fp) != n) { r->error = 1; r->active = 0; return total; }
        r->rd += n; r->frames += n; total += (int)n;
    }
    return total;
}

int rec_close(Rec *r) {
    r->active = 0;
    if (!r->fp) return -1;
    rec_pump(r);
    FILE *f = (FILE *)r->fp; uint8_t h[44]; header(h, r->rate, r->frames);
    int ok = (fseek(f, 0, SEEK_SET) == 0 && fwrite(h, 1, 44, f) == 44);
    fclose(f); r->fp = NULL;
    return (ok && !r->error) ? 0 : -1;
}

float rec_seconds(const Rec *r) { return r->rate ? (float)(r->frames + (r->wr - r->rd)) / (float)r->rate : 0.0f; }

int rec_wav_info(const char *path, uint32_t *rate, float *secs) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    uint8_t h[44]; size_t n = fread(h, 1, 44, f);
    long sz = 0; if (n == 44 && fseek(f, 0, SEEK_END) == 0) sz = ftell(f);
    fclose(f);
    if (n != 44 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return -1;
    uint32_t sr = h[24] | h[25] << 8 | h[26] << 16 | (uint32_t)h[27] << 24;
    if (!sr) return -1;
    *rate = sr; *secs = (float)((sz - 44) / 4) / (float)sr;
    return 0;
}
