/* INTONARUMORI 0.9 - motor Ambient/Drone em duas camadas (C puro, sem dependencia do 3DS).
 *
 *   DRONE (acordes): timbre + escala + ACORDES EM TEMPO REAL. Os pads tocam o grau da escala na hora,
 *     com modificadores (7a, 9a, SUS, maior<->menor, inversao). A sequencia de acordes pode ser
 *     MANUAL, 8 SLOTS editaveis, progressoes prontas ou GERADA (harmonia funcional).
 *     Troca de acorde = CROSSFADE entre vozes (nada de glide: era o que soava como sirene) e
 *     conducao de vozes (cada acorde fica o mais perto possivel do anterior).
 *   LEAD (melodia): timbre sempre visivel + 5 jeitos de tocar: DRIFT / ARP / STEP SEQ / EUCLID / KEYS.
 *     O lead usa o timbre escolhido e segue o acorde atual (ARP) ou a escala (os demais).
 * As duas camadas tem volume proprio e passam pelo mesmo filtro, eco e reverb.
 * Ideias adaptadas de: KARMA (CHORD+, VLEAD, STRUM, ACID GEN/DIR), ChoralRoot (LATCH),
 * JIANT (EUCLID, CONTOUR, MUTATE). */
#ifndef AMB_H
#define AMB_H
#include <stdint.h>
#include <stddef.h>
#include "synth.h"

#define AMB_DV 8              /* vozes do drone: 0..7 (4 do acorde atual + 4 se apagando) */
#define AMB_LV 4              /* vozes do lead: 8..11 */
#define AMB_V  (AMB_DV + AMB_LV)
#define AMB_PSLOTS 8          /* slots de acorde */

enum { A_DSND, A_ROOT, A_SCALE, A_QUAL, A_PROG, A_VOICING, A_DRATE,          /* grupo DRONE */
       A_LSND, A_LMODE, A_LOCT, A_SPEED, A_DENS, A_DUR,                       /* grupo LEAD  */
       A_LDIR, A_EHITS, A_ESTEPS, A_EROT, A_ECONT, A_STRUM, A_KHOLD,          /* grupo PLAY  */
       A_DVOL, A_LVOL, A_ECHO, A_REV, A_MOD, A_BRIGHT, A_VOL,                 /* grupo MIX   */
       A_COUNT };
#define A_NGROUPS 4
#define A_NSND 8
#define A_NLMODE 5
#define A_NPROG 10
#define A_NQUAL 8
#define A_NPRESETS 16
enum { L_DRIFT, L_ARP, L_SEQ, L_EUCL, L_KEYS, L_OFF };      /* L_OFF so existe na interface (indice 5) */
enum { P_MANUAL, P_SLOTS, P_FIRST_PRESET /* 2..8 */, P_GEN = 9 };
enum { CM_7 = 1, CM_9 = 2, CM_SUS = 4, CM_FLIP = 8, CM_INV = 16 };   /* modificadores do acorde (tempo real) */

typedef struct { const char *name; int first, count; } AGroup;
extern const AGroup amb_groups[A_NGROUPS];

typedef struct {
    float   p[A_COUNT];       /* 0..1 */
    uint16_t mask;            /* notas permitidas (bit i = i semitons acima da tonica): DRIFT e KEYS */
    int8_t  seq[8];           /* STEP SEQ: grau da escala 0..13 (-1 = pausa) */
    int8_t  prog[AMB_PSLOTS]; /* slots de acorde: grau 0..6 (-1 = fim da sequencia) */
    uint8_t drone_on, lead_on;
    uint8_t cdeg;             /* grau pedido pelos pads (modo manual e toque) */
    uint8_t cmod;             /* CM_* */
    uint8_t cgen;             /* sobe a cada toque num pad: reaciona o acorde e zera o relogio */
    uint8_t scene;            /* variante de cena (0..3) do preset - so a interface usa */
} AmbPatch;

/* touch/x/y: teclado do lead; cx/cy: analogico (-1..1): bend do lead (+-2 semitons) e brilho */
typedef struct { int touch; float x, y; int mute; float cx, cy; } AmbIn;

typedef struct {
    float ph1, ph2, freq, tfreq, env, envt, ak, rk, gk;
    int gate, hold, held, midi, pdly, pmidi, psnd; uint32_t age; int snd;
} AVoice;

#define ECHO_N 16384

typedef struct {
    uint32_t rng; int primed;
    AVoice v[AMB_V];
    float sm[A_COUNT];
    float lfo1, lfo2, lfo3, lfo4, lpl[2], lpr[2], gain, dcx[2], dcy[2];
    int clk, clk2, step, cyc, prog_i, last_idx, motif[8], sigd, sigl, chord_sig, cgen_seen;
    int touch_prev, touch_note, arp_i, cur_deg, euc_hk, prev_up[3], have_prev;
    float ed_s;
    uint32_t age;
    float comb[8][1040]; int ci[8]; float cf[8];
    float ap[4][440]; int ai[4];
    float ec[2][ECHO_N]; int ei;
    float scope[SCOPE_N];
    volatile int scope_w;
    volatile float peak;
    volatile int cur_note, notes_pc;     /* para a interface */
    volatile int cur_deg_ui, step_ui, nsteps_ui, hit_ui;
    volatile float venv[AMB_V];
} Amb;

void amb_init(Amb *s, uint32_t seed);
void amb_default(AmbPatch *a);
void amb_render(Amb *s, const AmbPatch *pt, const AmbIn *in, int16_t *out, int frames);

int  amb_discrete(int id);                      /* 0 = continuo, senao numero de opcoes */
int  amb_ival(const AmbPatch *a, int id);       /* indice de param discreto */
void amb_set_index(AmbPatch *a, int id, int idx);
void amb_adjust(AmbPatch *a, int id, int dir, float step);
void amb_set_from_pos(AmbPatch *a, int id, float v01);
const char *amb_pname(int id);
void amb_ptext(const AmbPatch *a, int id, char *buf, size_t n);
void amb_opt_text(const AmbPatch *a, int id, int idx, char *buf, size_t n);   /* texto da opcao idx */
const char *amb_snd_name(int s);
const char *amb_snd_short(int s);               /* ate 5 letras (chips) */
const char *amb_lmode_name(int m);
const char *amb_scale_name(int s);
const char *amb_qual_name(int c);
const char *amb_prog_name(int p);
const char *note_name(int pc);
uint16_t amb_scale_mask(int scale);
int  amb_scale_has(int scale, int semitone);
int  amb_scale_note(int scale, int deg);        /* semitons acima da tonica */
int  amb_scale_len(int scale);

/* acordes (para a interface e para o motor) */
int  amb_nd(const AmbPatch *a);                                   /* graus da escala (5..7) */
int  amb_chord_tones(const AmbPatch *a, int deg, int *o);          /* 4 notas MIDI (raiz, 3a, 5a, 4a voz); devolve a nota do baixo */
void amb_chord_label(const AmbPatch *a, int deg, char *buf, size_t n);   /* "Am7", "Gsus4"... */
void amb_roman(const AmbPatch *a, int deg, char *buf, size_t n);         /* "I", "vi"... */
int  amb_plen(const AmbPatch *a);                                 /* tamanho da sequencia de acordes */
void amb_set_slot(AmbPatch *a, int slot, int deg);                /* edita um slot (vira SLOTS) */
void amb_write_chord(AmbPatch *a, int deg);                       /* grava no fim dos slots (modo WRITE) */

/* geradores */
void amb_dice_chords(AmbPatch *a, uint32_t seed);                 /* progressao nova (harmonia funcional) */
void amb_dice_lead(AmbPatch *a, uint32_t seed);                   /* padrao novo para o modo atual do lead */
int  amb_euclid_hit(int hits, int steps, int rot, int pos);

/* presets de ambiente */
const char *amb_preset_name(int n);
void amb_preset(AmbPatch *a, int n);
int  amb_preset_scene(int n);                                     /* 0..3 */
#endif
