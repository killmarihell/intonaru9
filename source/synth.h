/* NOISE MACHINE 3DS - motor de sintese (C puro, sem dependencia do 3DS).
 * Roda igual no PC (para testes) e no console. */
#ifndef SYNTH_H
#define SYNTH_H
#include <stdint.h>
#include <stddef.h>

#define SYN_RATE   32728          /* taxa nativa do NDSP do 3DS */
#define SCOPE_N    2048           /* anel de amostras para o osciloscopio */
#define DLY_N      4096           /* linha de atraso do feedback */

/* Todos os parametros sao float 0..1 (facilita UI, randomizador e presets). */
enum {
    /* OSCILLATORS */
    P_WAVE, P_PITCH, P_OSCLVL, P_OSCB, P_BDET, P_FMAMT, P_FMRAT,
    /* NOISE LAB */
    P_NTYPE, P_NAMT, P_NCOL,
    /* DAMAGE UNIT */
    P_DRIVE, P_CLIP, P_FOLD, P_CRUSH, P_DOWN, P_RING, P_RINGF,
    /* FEEDBACK / MODULATION */
    P_FB, P_FBTIME, P_LFOR, P_LFOD, P_LFOS, P_LFOT,
    /* RANDOMIZER / MASTER */
    P_MUT, P_VOL,
    P_COUNT
};

#define N_MODULES 5
#define N_PRESETS 16

typedef struct {
    const char *name;   /* nome curto no painel */
    const char *desc;   /* descricao (<= 48 caracteres) */
    int first, count;   /* primeiro parametro e quantidade (-1 = presets) */
} Module;

extern const Module modules[N_MODULES];
extern const char *const preset_names[N_PRESETS];

typedef struct {
    uint32_t rng;
    int primed;
    float ph_a, ph_b, ph_fm, ph_lfo, ph_ring, lfo_sh;
    float pb0, pb1, pb2, brown, nlp, nsh; int ncnt;
    float cr_held; int cr_cnt;
    float dc_x, dc_y;
    float gain, hz_dbg;
    float bend_s, xdrive_s;
    float sm[P_COUNT];
    int dw;
    float dly[DLY_N];
    float scope[SCOPE_N];
    volatile int scope_w;
    volatile float peak;
} Synth;

void synth_init(Synth *s, uint32_t seed);
/* Gera `frames` quadros estereo (L=R) em out. frames deve ser multiplo de 64.
 * pv = parametros; bend -1..1 (oitavas); xdrive -1..1; burst/mute = 0/1. */
void synth_render(Synth *s, const float *pv, float bend, float xdrive,
                  int burst, int mute, int16_t *out, int frames);

/* Patches */
void  patch_seed(uint32_t seed);
void  patch_default(float *v);
void  patch_xy(int preset, int *xid, int *yid);   /* parametros do pad XY */
void  patch_preset(float *v, int n);      /* mantem VOL e MUT atuais */
void  patch_randomize(float *v);          /* texturas aleatorias */
void  patch_mutate(float *v);             /* deriva suave a partir do atual */
int   param_discrete(int id);             /* 0 = continuo, senao n de opcoes */
void  param_adjust(float *v, int id, int dir, float step);
const char *param_name(int id);
void  param_text(const float *v, int id, char *buf, size_t n);
float param_hz(float pitch);              /* mapeamento do PITCH em Hz */
int   wave_index(const float *v);
const char *wave_name(int idx);
const char *ntype_name(int idx);

#endif
