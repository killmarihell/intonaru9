#include "synth.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define BLK  64
#define MASK (DLY_N - 1)
#define INV_R (1.0f / (float)SYN_RATE)

/* ------------------------------------------------------------------ */
/* utilidades                                                          */
/* ------------------------------------------------------------------ */
static float sintab[2050];
static int   tab_ready = 0;

static inline float wrap01(float p) {
    p -= (float)(int)p;
    if (p < 0.0f) p += 1.0f;
    if (p >= 1.0f) p = 0.0f;
    return p;
}
static inline float tabsin(float p) {
    p = wrap01(p);
    float x = p * 2048.0f;
    int i = (int)x;
    float f = x - (float)i;
    return sintab[i] + (sintab[i + 1] - sintab[i]) * f;
}
static inline float rnd_s(uint32_t *s) {            /* -1 .. 1 */
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}
static inline float rnd_u(uint32_t *s) {            /* 0 .. 1 */
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}
static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

float param_hz(float pitch) { return 20.0f * exp2f(pitch * 8.0f); }   /* 20..5120 Hz */

/* ------------------------------------------------------------------ */
/* sintese                                                             */
/* ------------------------------------------------------------------ */
void synth_init(Synth *s, uint32_t seed) {
    if (!tab_ready) {
        for (int i = 0; i < 2049; i++)
            sintab[i] = sinf(6.28318530718f * (float)i / 2048.0f);
        sintab[2049] = sintab[1];
        tab_ready = 1;
    }
    memset(s, 0, sizeof(*s));
    s->rng = seed ? seed : 0x1234567u;
    s->ncnt = 1;
}

int wave_index(const float *v) {
    int i = (int)(v[P_WAVE] * 4.0f);
    return i < 0 ? 0 : (i > 3 ? 3 : i);
}

void synth_render(Synth *s, const float *pv, float bend, float xdrive,
                  int burst, int mute, int16_t *out, int frames)
{
    if (!s->primed) { memcpy(s->sm, pv, sizeof(s->sm)); s->primed = 1; }
    float peak = 0.0f;
    int sw = s->scope_w;

    for (int b0 = 0; b0 + BLK <= frames; b0 += BLK) {
        /* ---- taxa de controle: suaviza parametros (evita cliques) ---- */
        for (int i = 0; i < P_COUNT; i++)
            s->sm[i] += (pv[i] - s->sm[i]) * 0.22f;
        s->bend_s   += (bend   - s->bend_s)   * 0.22f;
        s->xdrive_s += (xdrive - s->xdrive_s) * 0.22f;
        const float *m = s->sm;

        int wave  = wave_index(pv);
        int ntype = (int)(pv[P_NTYPE] * 5.0f); if (ntype < 0) ntype = 0; if (ntype > 4) ntype = 4;
        int lfosh = (int)(pv[P_LFOS] * 4.0f);  if (lfosh < 0) lfosh = 0; if (lfosh > 3) lfosh = 3;
        int lfot  = (int)(pv[P_LFOT] * 4.0f);  if (lfot  < 0) lfot  = 0; if (lfot  > 3) lfot  = 3;

        float hz     = param_hz(m[P_PITCH]) * exp2f(s->bend_s);
        s->hz_dbg    = hz;
        float ob     = m[P_OSCB];
        float ratioB = exp2f((m[P_BDET] - 0.5f) * 2.0f);
        float norm   = 1.0f / (1.0f + ob);
        float oscl   = m[P_OSCLVL];
        float fmamt  = m[P_FMAMT] * 4.0f;
        float fmrat  = 0.25f * exp2f(m[P_FMRAT] * 5.0f);

        float namt   = m[P_NAMT];
        float ncol   = m[P_NCOL];
        float na     = 0.02f + 0.98f * ncol * ncol;          /* passa-baixa do ruido */
        int   nhold  = 1 + (int)((1.0f - ncol) * 60.0f);     /* ruido digital */
        float dprob  = 0.0005f + ncol * ncol * 0.3f;         /* poeira */

        float lfo_inc = 0.05f * exp2f(m[P_LFOR] * 14.0f) * INV_R;   /* 0.05..819 Hz */
        float depth   = m[P_LFOD];

        float ra      = m[P_RING];
        float ring_inc = 20.0f * exp2f(m[P_RINGF] * 8.0f) * INV_R;
        float thr     = 1.0f - m[P_CLIP] * 0.95f;
        float ithr    = 1.0f / thr;
        int   crush_on = m[P_CRUSH] > 0.01f;
        float lv      = exp2f((16.0f - m[P_CRUSH] * 14.5f) - 1.0f);
        float ilv     = 1.0f / lv;
        int   hold    = 1 + (int)(m[P_DOWN] * m[P_DOWN] * 60.0f);

        float dl = exp2f(m[P_FBTIME] * 11.0f);
        if (dl < 2.0f) dl = 2.0f;
        if (dl > (float)(DLY_N - 4)) dl = (float)(DLY_N - 4);
        float fb_base = m[P_FB] * 1.2f;

        float gt = mute ? 0.0f : m[P_VOL] * 0.9f;

        for (int n = 0; n < BLK; n++) {
            /* ---- LFO ---- */
            s->ph_lfo += lfo_inc;
            if (s->ph_lfo >= 1.0f) { s->ph_lfo -= 1.0f; s->lfo_sh = rnd_s(&s->rng); }
            float lfo;
            switch (lfosh) {
                case 0:  lfo = tabsin(s->ph_lfo); break;
                case 1:  lfo = 2.0f * s->ph_lfo - 1.0f; break;
                case 2:  lfo = (s->ph_lfo < 0.5f) ? 1.0f : -1.0f; break;
                default: lfo = s->lfo_sh; break;
            }
            float l = lfo * depth;

            /* ---- osciladores ---- */
            float f = hz;
            if (lfot == 0) { f = hz * (1.0f + l * depth * 3.0f); if (f < 1.0f) f = 1.0f; }
            float inc = f * INV_R;
            s->ph_a = wrap01(s->ph_a + inc);
            s->ph_b = wrap01(s->ph_b + inc * ratioB);
            float a, b;
            if (wave == 0) {
                a = tabsin(s->ph_a); b = tabsin(s->ph_b);
            } else if (wave == 1) {
                a = 2.0f * s->ph_a - 1.0f; b = 2.0f * s->ph_b - 1.0f;
            } else if (wave == 2) {
                a = (s->ph_a < 0.5f) ? 1.0f : -1.0f; b = (s->ph_b < 0.5f) ? 1.0f : -1.0f;
            } else {
                s->ph_fm = wrap01(s->ph_fm + inc * fmrat);
                float mv = tabsin(s->ph_fm) * fmamt;
                a = tabsin(s->ph_a + mv); b = tabsin(s->ph_b + mv);
            }
            float osc = (a + b * ob) * norm * oscl;

            /* ---- ruido ---- */
            float w = rnd_s(&s->rng), nz;
            switch (ntype) {
                case 0:
                    nz = w; break;
                case 1:
                    s->pb0 = 0.99765f * s->pb0 + w * 0.0990460f;
                    s->pb1 = 0.96300f * s->pb1 + w * 0.2965164f;
                    s->pb2 = 0.57000f * s->pb2 + w * 1.0526913f;
                    nz = (s->pb0 + s->pb1 + s->pb2 + w * 0.1848f) * 0.4f; break;
                case 2:
                    s->brown = s->brown * 0.985f + w * 0.06f;
                    nz = s->brown * 3.0f; break;
                case 3:
                    if (--s->ncnt <= 0) { s->nsh = w; s->ncnt = nhold; }
                    nz = s->nsh; break;
                default:
                    nz = (rnd_u(&s->rng) < dprob) ? w : 0.0f; break;
            }
            if (ntype <= 2) { s->nlp += na * (nz - s->nlp); nz = s->nlp; }

            float x = (osc + nz * namt) * 0.8f;

            /* ---- feedback (entra antes da cadeia de dano = auto-oscilacao) ---- */
            float fbg = fb_base + (lfot == 2 ? l * 0.6f : 0.0f);
            fbg = clampf(fbg, 0.0f, 1.3f);
            if (burst) fbg = 1.15f;
            float rp = (float)s->dw - dl;
            if (rp < 0.0f) rp += (float)DLY_N;
            int i0 = (int)rp;
            float fr = rp - (float)i0;
            float d0 = s->dly[i0 & MASK], d1 = s->dly[(i0 + 1) & MASK];
            float fd = d0 + (d1 - d0) * fr;
            x += fbg * fd + 0.0002f * rnd_s(&s->rng);   /* semente p/ auto-oscilar */

            /* ---- DAMAGE UNIT ---- */
            if (ra > 0.001f) {
                s->ph_ring = wrap01(s->ph_ring + ring_inc);
                x += (x * tabsin(s->ph_ring) - x) * ra;
            }
            float fe = m[P_FOLD] + (lfot == 3 ? l * 0.5f : 0.0f);
            if (fe > 0.01f) {
                if (fe > 1.0f) fe = 1.0f;
                x *= 1.0f + fe * 8.0f;
                float t = x * 0.25f + 0.25f;
                t -= floorf(t);
                x = 1.0f - fabsf(t * 4.0f - 2.0f);          /* dobra triangular */
            }
            float de = m[P_DRIVE] + (lfot == 1 ? l * 0.5f : 0.0f) + s->xdrive_s * 0.5f;
            de = clampf(de, 0.0f, 1.0f);
            x *= 1.0f + de * de * de * 60.0f;
            x = clampf(x, -thr, thr) * ithr;                /* hard clip */
            if (crush_on) x = floorf(x * lv + 0.5f) * ilv;  /* bitcrush */
            if (hold > 1) {                                  /* reducao de taxa */
                if (s->cr_cnt <= 0) { s->cr_held = x; s->cr_cnt = hold; }
                s->cr_cnt--; x = s->cr_held;
            }

            /* ---- bloqueio de DC + protecao ---- */
            float y = x - s->dc_x + 0.995f * s->dc_y;
            s->dc_x = x; s->dc_y = y;
            if (!(y > -4.0f && y < 4.0f)) {                  /* NaN/inf: reseta */
                y = 0.0f; s->dc_x = 0.0f; s->dc_y = 0.0f;
                memset(s->dly, 0, sizeof(s->dly));
            }
            y = clampf(y, -1.0f, 1.0f);
            s->dly[s->dw] = y;
            s->dw = (s->dw + 1) & MASK;

            /* ---- master ---- */
            s->gain += (gt - s->gain) * 0.003f;
            float o = y * s->gain;
            int16_t q = (int16_t)(o * 32767.0f);
            out[(b0 + n) * 2]     = q;
            out[(b0 + n) * 2 + 1] = q;
            float ao = o < 0 ? -o : o;
            if (ao > peak) peak = ao;
            s->scope[sw & (SCOPE_N - 1)] = y;
            sw++;
        }
    }
    s->scope_w = sw;
    s->peak = peak;
}

/* ------------------------------------------------------------------ */
/* patches, presets, randomizador                                      */
/* ------------------------------------------------------------------ */
static uint32_t prng = 0xC0FFEE11u;
void patch_seed(uint32_t seed) { prng = seed ? seed : 0xC0FFEE11u; }
static float R01(void) { return rnd_u(&prng); }

/* valores centrais das opcoes discretas */
#define W_SIN .125f
#define W_SAW .375f
#define W_SQR .625f
#define W_FM  .875f
#define N_WHT .1f
#define N_PNK .3f
#define N_BRN .5f
#define N_DIG .7f
#define N_DST .9f
#define T_PIT .125f
#define T_DRV .375f
#define T_FB  .625f
#define T_FLD .875f

void patch_default(float *v) {
    static const float d[P_COUNT] = {
        [P_WAVE]=W_SAW, [P_PITCH]=.31f, [P_OSCLVL]=.7f, [P_OSCB]=0, [P_BDET]=.5f,
        [P_FMAMT]=.3f, [P_FMRAT]=.4f,
        [P_NTYPE]=N_WHT, [P_NAMT]=.2f, [P_NCOL]=.8f,
        [P_DRIVE]=.35f, [P_CLIP]=0, [P_FOLD]=0, [P_CRUSH]=0, [P_DOWN]=0, [P_RING]=0, [P_RINGF]=.5f,
        [P_FB]=.15f, [P_FBTIME]=.5f, [P_LFOR]=.35f, [P_LFOD]=.1f, [P_LFOS]=W_SIN, [P_LFOT]=T_PIT,
        [P_MUT]=.3f, [P_VOL]=.3f
    };
    memcpy(v, d, sizeof(d));
}

const char *const preset_names[N_PRESETS] = {
    "DRONE", "STATIC", "HARSH", "SHREDDER", "DIGITAL", "FEEDBACK", "INDUSTRIAL", "VOID",
    "WALL", "GLITCH", "GRAIN", "TURBINE", "SCREECH", "EMBERS", "STUTTER", "SUBQUAKE"
};

/* parametros controlados pelo pad XY de cada preset (X, Y) */
void patch_xy(int p, int *xid, int *yid) {
    static const int8_t xy[N_PRESETS][2] = {
        {P_PITCH,P_FB},{P_NCOL,P_DRIVE},{P_PITCH,P_DRIVE},{P_RINGF,P_FOLD},
        {P_DOWN,P_CRUSH},{P_FBTIME,P_FB},{P_RINGF,P_DRIVE},{P_PITCH,P_FB},
        {P_NCOL,P_DRIVE},{P_DOWN,P_LFOR},{P_NCOL,P_FB},{P_PITCH,P_FOLD},
        {P_PITCH,P_FB},{P_NCOL,P_RING},{P_LFOR,P_DOWN},{P_PITCH,P_DRIVE}};
    if (p < 0 || p >= N_PRESETS) p = 2;
    *xid = xy[p][0]; *yid = xy[p][1];
}

void patch_preset(float *v, int n) {
    float vol = v[P_VOL], mut = v[P_MUT];
    patch_default(v);
    #define S(id, x) v[id] = (x)
    switch (n) {
    case 0: /* DRONE */
        S(P_WAVE,W_SAW); S(P_PITCH,.22f); S(P_OSCLVL,.8f); S(P_OSCB,.6f); S(P_BDET,.52f);
        S(P_NTYPE,N_BRN); S(P_NAMT,.15f); S(P_NCOL,.3f); S(P_DRIVE,.15f); S(P_FOLD,0);
        S(P_FB,.35f); S(P_FBTIME,.6f); S(P_LFOR,.15f); S(P_LFOD,.3f); S(P_LFOS,W_SIN); S(P_LFOT,T_PIT); break;
    case 1: /* STATIC */
        S(P_OSCLVL,0); S(P_NTYPE,N_WHT); S(P_NAMT,.9f); S(P_NCOL,.9f); S(P_DRIVE,.2f);
        S(P_CRUSH,.1f); S(P_FB,.1f); S(P_LFOD,0); break;
    case 2: /* HARSH */
        S(P_WAVE,W_SQR); S(P_PITCH,.45f); S(P_OSCLVL,.8f); S(P_OSCB,.7f); S(P_BDET,.62f);
        S(P_NTYPE,N_WHT); S(P_NAMT,.5f); S(P_NCOL,1); S(P_DRIVE,.85f); S(P_CLIP,.5f); S(P_FOLD,.4f);
        S(P_CRUSH,.2f); S(P_DOWN,.1f); S(P_FB,.5f); S(P_FBTIME,.3f);
        S(P_LFOR,.5f); S(P_LFOD,.3f); S(P_LFOS,W_SQR); S(P_LFOT,T_DRV); break;
    case 3: /* SHREDDER */
        S(P_WAVE,W_SAW); S(P_PITCH,.6f); S(P_OSCB,.8f); S(P_BDET,.7f);
        S(P_NTYPE,N_PNK); S(P_NAMT,.3f); S(P_DRIVE,.9f); S(P_CLIP,.6f); S(P_FOLD,.7f);
        S(P_RING,.6f); S(P_RINGF,.55f); S(P_FB,.3f); S(P_FBTIME,.2f);
        S(P_LFOR,.75f); S(P_LFOD,.6f); S(P_LFOS,W_SAW); S(P_LFOT,T_PIT); break;
    case 4: /* DIGITAL */
        S(P_WAVE,W_FM); S(P_PITCH,.35f); S(P_FMAMT,.8f); S(P_FMRAT,.6f); S(P_OSCB,.5f); S(P_BDET,.3f);
        S(P_NTYPE,N_DIG); S(P_NAMT,.5f); S(P_NCOL,.4f); S(P_CRUSH,.75f); S(P_DOWN,.5f);
        S(P_DRIVE,.3f); S(P_CLIP,.2f); S(P_FOLD,.3f); S(P_RING,.3f); S(P_RINGF,.35f); S(P_FB,.2f);
        S(P_LFOR,.6f); S(P_LFOD,.2f); S(P_LFOS,W_SQR + .25f); S(P_LFOT,T_FLD); break;
    case 5: /* FEEDBACK */
        S(P_WAVE,W_SIN); S(P_PITCH,.3f); S(P_OSCLVL,.3f); S(P_NAMT,.05f); S(P_DRIVE,.5f);
        S(P_CLIP,.1f); S(P_FOLD,.2f); S(P_FB,.9f); S(P_FBTIME,.45f);
        S(P_LFOR,.2f); S(P_LFOD,.2f); S(P_LFOT,T_FB); break;
    case 6: /* INDUSTRIAL */
        S(P_WAVE,W_SQR); S(P_PITCH,.15f); S(P_OSCB,.5f); S(P_BDET,.35f);
        S(P_NTYPE,N_PNK); S(P_NAMT,.35f); S(P_DRIVE,.6f); S(P_CLIP,.4f); S(P_FOLD,.3f);
        S(P_CRUSH,.35f); S(P_DOWN,.25f); S(P_RING,.5f); S(P_RINGF,.22f);
        S(P_FB,.55f); S(P_FBTIME,.7f); S(P_LFOR,.45f); S(P_LFOD,.4f); S(P_LFOS,W_SQR); S(P_LFOT,T_DRV); break;
    case 7: /* VOID */
        S(P_WAVE,W_SIN); S(P_PITCH,.05f); S(P_OSCLVL,.6f); S(P_NTYPE,N_BRN); S(P_NAMT,.5f); S(P_NCOL,.15f);
        S(P_DRIVE,.1f); S(P_FB,.7f); S(P_FBTIME,.9f); S(P_LFOR,.05f); S(P_LFOD,.2f); S(P_LFOT,T_PIT); break;
    case 8: /* WALL: parede de ruido */
        S(P_OSCLVL,.2f); S(P_NTYPE,N_PNK); S(P_NAMT,1.0f); S(P_NCOL,.95f); S(P_DRIVE,.95f); S(P_CLIP,.7f);
        S(P_FOLD,.55f); S(P_FB,.6f); S(P_FBTIME,.55f); S(P_LFOR,.12f); S(P_LFOD,.25f); S(P_LFOT,T_DRV); break;
    case 9: /* GLITCH */
        S(P_WAVE,W_FM); S(P_PITCH,.5f); S(P_FMAMT,.9f); S(P_FMRAT,.75f); S(P_NTYPE,N_DIG); S(P_NAMT,.3f);
        S(P_NCOL,.2f); S(P_CRUSH,.6f); S(P_DOWN,.7f); S(P_DRIVE,.4f); S(P_FB,.4f); S(P_FBTIME,.12f);
        S(P_LFOR,.8f); S(P_LFOD,.7f); S(P_LFOS,W_FM); S(P_LFOT,T_PIT); break;
    case 10: /* GRAIN: poeira granular com eco */
        S(P_OSCLVL,0); S(P_NTYPE,N_DST); S(P_NAMT,1.0f); S(P_NCOL,.55f); S(P_DRIVE,.5f); S(P_CRUSH,.3f);
        S(P_FB,.75f); S(P_FBTIME,.2f); S(P_LFOR,.3f); S(P_LFOD,.5f); S(P_LFOT,T_FB); break;
    case 11: /* TURBINE */
        S(P_WAVE,W_SAW); S(P_PITCH,.12f); S(P_OSCLVL,.5f); S(P_OSCB,.7f); S(P_BDET,.51f); S(P_NTYPE,N_BRN);
        S(P_NAMT,.6f); S(P_NCOL,.4f); S(P_DRIVE,.55f); S(P_FOLD,.35f); S(P_RING,.3f); S(P_RINGF,.2f);
        S(P_FB,.5f); S(P_FBTIME,.8f); S(P_LFOR,.2f); S(P_LFOD,.5f); S(P_LFOT,T_FLD); break;
    case 12: /* SCREECH */
        S(P_WAVE,W_SIN); S(P_PITCH,.75f); S(P_OSCLVL,.5f); S(P_OSCB,.5f); S(P_BDET,.6f); S(P_NAMT,.05f);
        S(P_DRIVE,.6f); S(P_FOLD,.4f); S(P_FB,.88f); S(P_FBTIME,.18f); S(P_LFOR,.4f); S(P_LFOD,.5f); S(P_LFOT,T_FB); break;
    case 13: /* EMBERS: crepitar de brasas (textura sutil) */
        S(P_OSCLVL,0); S(P_NTYPE,N_PNK); S(P_NAMT,.55f); S(P_NCOL,.45f); S(P_DRIVE,.3f); S(P_CRUSH,.4f);
        S(P_DOWN,.35f); S(P_RING,.35f); S(P_RINGF,.12f); S(P_FB,.3f); S(P_FBTIME,.65f);
        S(P_LFOR,.3f); S(P_LFOD,.4f); S(P_LFOS,W_SQR); S(P_LFOT,T_DRV); break;
    case 14: /* STUTTER */
        S(P_WAVE,W_SQR); S(P_PITCH,.4f); S(P_OSCB,.4f); S(P_BDET,.65f); S(P_NAMT,.1f); S(P_DOWN,.45f);
        S(P_CRUSH,.3f); S(P_DRIVE,.5f); S(P_CLIP,.3f); S(P_FB,.35f); S(P_FBTIME,.3f);
        S(P_LFOR,.6f); S(P_LFOD,.8f); S(P_LFOS,W_FM); S(P_LFOT,T_PIT); break;
    case 15: /* SUBQUAKE */
        S(P_WAVE,W_SIN); S(P_PITCH,.03f); S(P_OSCLVL,.9f); S(P_NTYPE,N_BRN); S(P_NAMT,.4f); S(P_NCOL,.1f);
        S(P_DRIVE,.7f); S(P_CLIP,.3f); S(P_FB,.5f); S(P_FBTIME,.95f); S(P_LFOR,.05f); S(P_LFOD,.3f); break;
    default: break;
    }
    #undef S
    v[P_VOL] = vol; v[P_MUT] = mut;
}

int param_discrete(int id) {
    switch (id) {
        case P_WAVE: return 4;
        case P_NTYPE: return 5;
        case P_LFOS: case P_LFOT: return 4;
        default: return 0;
    }
}

void param_adjust(float *v, int id, int dir, float step) {
    int n = param_discrete(id);
    if (n) {
        int i = (int)(v[id] * (float)n);
        i += dir;
        if (i < 0) i = n - 1;
        if (i >= n) i = 0;
        v[id] = ((float)i + 0.5f) / (float)n;
    } else {
        v[id] = clampf(v[id] + dir * step, 0.0f, 1.0f);
    }
}

void patch_randomize(float *v) {
    for (int i = P_WAVE; i <= P_LFOT; i++) v[i] = R01();
    if (v[P_OSCLVL] < 0.3f && v[P_NAMT] < 0.3f) v[P_OSCLVL] = 0.6f;   /* evita silencio */
    if (v[P_FB] > 0.92f) v[P_FB] = 0.92f;
}

void patch_mutate(float *v) {
    float amt = 0.05f + 0.35f * v[P_MUT];
    for (int i = P_WAVE; i <= P_LFOT; i++) {
        if (R01() < 0.5f) continue;
        int n = param_discrete(i);
        if (n) { if (R01() < 0.25f * amt * 4.0f) v[i] = R01(); }
        else v[i] = clampf(v[i] + (R01() * 2.0f - 1.0f) * amt, 0.0f, 1.0f);
    }
    if (v[P_OSCLVL] < 0.3f && v[P_NAMT] < 0.3f) v[P_OSCLVL] = 0.6f;
}

/* ------------------------------------------------------------------ */
/* metadados para a interface                                          */
/* ------------------------------------------------------------------ */
const Module modules[N_MODULES] = {
    { "OSC",    "SINE SAW SQUARE FM + SECOND OSCILLATOR",   P_WAVE,  7  },
    { "NOISE",  "WHITE PINK BROWN DIGITAL DUST",            P_NTYPE, 3  },
    { "DAMAGE", "DRIVE CLIP FOLD CRUSH RING MOD",           P_DRIVE, 7  },
    { "FB/MOD", "FEEDBACK LOOP + LFO. SELF-OSCILLATION",    P_FB,    6  },
    { "RANDOM", "MUTATE OR SCRAMBLE THE WHOLE PATCH",       P_MUT,   2  },
};

static const char *const pnames[P_COUNT] = {
    [P_WAVE]="WAVE", [P_PITCH]="PITCH", [P_OSCLVL]="LEVEL", [P_OSCB]="OSC B", [P_BDET]="B TUNE",
    [P_FMAMT]="FM AMT", [P_FMRAT]="FM RATIO",
    [P_NTYPE]="TYPE", [P_NAMT]="AMOUNT", [P_NCOL]="COLOR",
    [P_DRIVE]="DRIVE", [P_CLIP]="HARD CLIP", [P_FOLD]="FOLD", [P_CRUSH]="BITCRUSH",
    [P_DOWN]="DOWNSMPL", [P_RING]="RING MOD", [P_RINGF]="RING HZ",
    [P_FB]="FEEDBACK", [P_FBTIME]="FB TIME", [P_LFOR]="LFO RATE", [P_LFOD]="LFO DEPTH",
    [P_LFOS]="LFO SHAPE", [P_LFOT]="LFO DEST",
    [P_MUT]="MUTATION", [P_VOL]="VOLUME"
};
const char *param_name(int id) { return (id >= 0 && id < P_COUNT) ? pnames[id] : "?"; }

const char *wave_name(int i)  { static const char *n[4] = {"SINE","SAW","SQUARE","FM"}; return n[i & 3]; }
const char *ntype_name(int i) { static const char *n[5] = {"WHITE","PINK","BROWN","DIGITAL","DUST"}; return n[i % 5]; }

void param_text(const float *v, int id, char *buf, size_t n) {
    float x = v[id];
    switch (id) {
        case P_WAVE:  snprintf(buf, n, "%s", wave_name(wave_index(v))); break;
        case P_NTYPE: { int i = (int)(x * 5.0f); if (i > 4) i = 4; snprintf(buf, n, "%s", ntype_name(i)); } break;
        case P_LFOS:  { static const char *t[4] = {"SINE","SAW","SQR","S&H"}; int i = (int)(x*4.0f); if (i>3) i=3; snprintf(buf, n, "%s", t[i]); } break;
        case P_LFOT:  { static const char *t[4] = {"PITCH","DRIVE","FEED","FOLD"}; int i = (int)(x*4.0f); if (i>3) i=3; snprintf(buf, n, "%s", t[i]); } break;
        case P_PITCH: snprintf(buf, n, "%d HZ", (int)(param_hz(x) + 0.5f)); break;
        case P_BDET:  snprintf(buf, n, "%+d ST", (int)((x - 0.5f) * 24.0f + (x >= 0.5f ? 0.5f : -0.5f))); break;
        case P_FMRAT: snprintf(buf, n, "X%.2f", 0.25f * exp2f(x * 5.0f)); break;
        case P_CRUSH: snprintf(buf, n, "%d BIT", (int)(16.0f - x * 14.5f + 0.5f)); break;
        case P_DOWN:  snprintf(buf, n, "1/%d", 1 + (int)(x * x * 60.0f)); break;
        case P_RINGF: snprintf(buf, n, "%d HZ", (int)(20.0f * exp2f(x * 8.0f))); break;
        case P_FBTIME:snprintf(buf, n, "%.1f MS", exp2f(x * 11.0f) * 1000.0f / (float)SYN_RATE); break;
        case P_LFOR:  snprintf(buf, n, "%.2f HZ", 0.05f * exp2f(x * 14.0f)); break;
        default:      snprintf(buf, n, "%d%%", (int)(x * 100.0f + 0.5f)); break;
    }
}
