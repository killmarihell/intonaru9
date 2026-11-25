#include "amb.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BLK 64
#define NSC 10
#define INV_R (1.0f / (float)SYN_RATE)
#define ECHO_MASK (ECHO_N - 1)

static const int8_t SC[NSC][8] = {
    {0,2,4,5,7,9,11,-1},{0,2,3,5,7,8,10,-1},{0,2,3,5,7,9,10,-1},{0,1,3,5,7,8,10,-1},
    {0,2,4,6,7,9,11,-1},{0,2,4,5,7,9,10,-1},{0,2,4,7,9,-1,-1,-1},{0,3,5,7,10,-1,-1,-1},
    {0,1,5,7,8,-1,-1,-1},{0,2,4,6,8,10,-1,-1}};
static const char *const SCN[NSC] = {"MAJOR","MINOR","DORIAN","PHRYGIAN","LYDIAN","MIXOLYD","PENT MAJ","PENT MIN","HIRAJOSHI","WHOLETONE"};
/* qualidade do acorde: 0 = DIATONIC (empilha tercas da escala); demais = intervalos fixos sobre o grau */
static const int8_t QI[A_NQUAL][4] = {{0,0,0,0},{0,4,7,12},{0,3,7,12},{0,2,7,12},{0,5,7,12},{0,7,12,19},{0,4,7,11},{0,3,7,10}};
static const char *const QN[A_NQUAL] = {"DIATONIC","MAJOR","MINOR","SUS2","SUS4","POWER","MAJOR 7","MINOR 7"};
/* progressoes (graus 0-based, -1 = fim). indices 0, 1 e 9 (MANUAL, SLOTS, GENERATE) nao carregam slots */
static const int8_t PRG[A_NPROG][AMB_PSLOTS] = {
    {0,-1,-1,-1,-1,-1,-1,-1},{0,-1,-1,-1,-1,-1,-1,-1},
    {0,4,5,3,-1,-1,-1,-1},{0,5,3,4,-1,-1,-1,-1},{5,3,0,4,-1,-1,-1,-1},{0,3,5,4,-1,-1,-1,-1},
    {0,6,5,4,-1,-1,-1,-1},{0,6,-1,-1,-1,-1,-1,-1},{0,3,-1,-1,-1,-1,-1,-1},{0,-1,-1,-1,-1,-1,-1,-1}};
static const char *const PRN[A_NPROG] = {"MANUAL","SLOTS","1-5-6-4","1-6-4-5","6-4-1-5","1-4-6-5","1-7-6-5","1-7 SWAY","1-4 DEEP","GENERATE"};
static const char *const SNDN[A_NSND] = {"PAD","STRINGS","ORGAN","CHOIR","GLASS","SUB","REED","BELL"};
static const char *const SNDS[A_NSND] = {"PAD","STRNG","ORGAN","CHOIR","GLASS","SUB","REED","BELL"};
static const char *const LMN[A_NLMODE + 1] = {"DRIFT","ARPEGGIO","STEP SEQ","EUCLID","KEYS","OFF"};
static const char *const LOCN[3] = {"LOW","MID","HIGH"};
static const char *const VCN[3] = {"SMOOTH","CLOSE","OPEN"};
static const char *const DIRN[5] = {"UP","DOWN","PENDULUM","RANDOM","WALK"};
static const char *const CNTN[5] = {"WALK","UP","DOWN","ARCH","RANDOM"};
static const char *const NN[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static const char *const ROM[7] = {"I","II","III","IV","V","VI","VII"};

const AGroup amb_groups[A_NGROUPS] = { {"DRONE", A_DSND, 7}, {"LEAD", A_LSND, 6}, {"PLAY", A_LDIR, 7}, {"MIX", A_DVOL, 7} };

/* timbres: amplitude dos 8 primeiros harmonicos */
static const float HARM[A_NSND][8] = {
    {1.00f, 0.50f, 0.33f, 0.22f, 0.12f, 0.06f, 0.03f, 0.00f},   /* PAD: serra suave */
    {1.00f, 0.55f, 0.45f, 0.35f, 0.28f, 0.22f, 0.16f, 0.12f},   /* STRINGS: serra rica */
    {1.00f, 0.80f, 0.00f, 0.60f, 0.00f, 0.00f, 0.00f, 0.40f},   /* ORGAN: barras */
    {0.70f, 1.00f, 0.90f, 0.60f, 0.25f, 0.12f, 0.06f, 0.03f},   /* CHOIR: formante */
    {1.00f, 0.00f, 0.45f, 0.00f, 0.30f, 0.00f, 0.18f, 0.00f},   /* GLASS */
    {1.00f, 0.22f, 0.05f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f},   /* SUB */
    {1.00f, 0.00f, 0.60f, 0.00f, 0.42f, 0.00f, 0.30f, 0.00f},   /* REED: oco */
    {1.00f, 0.00f, 0.00f, 0.55f, 0.00f, 0.00f, 0.00f, 0.35f}};  /* BELL */
/* envelopes: drone (ataque/release em s - longos para o crossfade entre acordes) e lead */
static const float DATT[A_NSND] = {2.6f, 3.0f, 0.5f, 3.0f, 2.0f, 3.0f, 1.5f, 0.8f};
static const float DREL[A_NSND] = {3.6f, 4.0f, 1.2f, 4.0f, 3.2f, 4.0f, 2.6f, 3.0f};
static const float LATT[A_NSND] = {0.08f, 0.15f, 0.01f, 0.12f, 0.004f, 0.01f, 0.03f, 0.002f};
static const float LRK[A_NSND]  = {0.5f, 0.6f, 0.15f, 0.6f, 1.2f, 0.2f, 0.3f, 1.6f};

static float stab[2050], wtab[A_NSND][2050];
static int tab_ok = 0;

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float rnd_u(uint32_t *s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}
static inline float tlook(const float *t, float p) {
    float x = p * 2048.0f; int i = (int)x; float f = x - (float)i;
    return t[i] + (t[i + 1] - t[i]) * f;
}
static inline float midi_hz(int m) { return 440.0f * exp2f((float)(m - 69) / 12.0f); }
static inline float blk_k(float sec) { float n = sec * ((float)SYN_RATE / BLK); return 1.0f - expf(-1.0f / (n < 0.5f ? 0.5f : n)); }
static inline int imod(int a, int n) { a %= n; return a < 0 ? a + n : a; }

int amb_scale_len(int s) { int n = 0; while (n < 8 && SC[s][n] >= 0) n++; return n; }
int amb_scale_note(int s, int deg) {
    int n = amb_scale_len(s), o = deg / n, i = deg - o * n;
    if (i < 0) { i += n; o--; }
    return SC[s][i] + 12 * o;
}
int amb_scale_has(int s, int st) { st = ((st % 12) + 12) % 12; for (int i = 0; i < amb_scale_len(s); i++) if (SC[s][i] == st) return 1; return 0; }
uint16_t amb_scale_mask(int s) { uint16_t m = 0; for (int i = 0; i < 12; i++) if (amb_scale_has(s, i)) m |= (uint16_t)(1u << i); return m; }

const char *amb_snd_name(int s) { return SNDN[(unsigned)s % A_NSND]; }
const char *amb_snd_short(int s) { return SNDS[(unsigned)s % A_NSND]; }
const char *amb_lmode_name(int m) { return LMN[(unsigned)m % (A_NLMODE + 1)]; }
const char *amb_scale_name(int s) { return SCN[(unsigned)s % NSC]; }
const char *amb_qual_name(int c) { return QN[(unsigned)c % A_NQUAL]; }
const char *amb_prog_name(int p) { return PRN[(unsigned)p % A_NPROG]; }
const char *note_name(int pc) { return NN[((pc % 12) + 12) % 12]; }

/* ---------------- parametros ---------------- */
static const char *const PN[A_COUNT] = {
    [A_DSND]="SOUND", [A_ROOT]="ROOT", [A_SCALE]="SCALE", [A_QUAL]="CHORD TYPE", [A_PROG]="SEQUENCE", [A_VOICING]="VOICING", [A_DRATE]="CHANGE EVERY",
    [A_LSND]="SOUND", [A_LMODE]="PLAY MODE", [A_LOCT]="REGISTER", [A_SPEED]="SPEED", [A_DENS]="DENSITY", [A_DUR]="NOTE LENGTH",
    [A_LDIR]="ARP DIRECTION", [A_EHITS]="EUCLID HITS", [A_ESTEPS]="EUCLID STEPS", [A_EROT]="EUCLID ROTATE", [A_ECONT]="CONTOUR", [A_STRUM]="CHORD STRUM", [A_KHOLD]="KEYS HOLD",
    [A_DVOL]="DRONE LEVEL", [A_LVOL]="LEAD LEVEL", [A_ECHO]="ECHO", [A_REV]="REVERB", [A_MOD]="MODULATE", [A_BRIGHT]="BRIGHTNESS", [A_VOL]="MASTER" };
const char *amb_pname(int id) { return (id >= 0 && id < A_COUNT) ? PN[id] : "?"; }
int amb_discrete(int id) {
    switch (id) { case A_DSND: case A_LSND: return A_NSND; case A_ROOT: return 12; case A_SCALE: return NSC;
                  case A_QUAL: return A_NQUAL; case A_PROG: return A_NPROG; case A_VOICING: return 3;
                  case A_LMODE: return A_NLMODE; case A_LOCT: return 3; case A_LDIR: return 5;
                  case A_EHITS: return 16; case A_ESTEPS: return 13; case A_EROT: return 16; case A_ECONT: return 5;
                  case A_KHOLD: return 2; default: return 0; }
}
int amb_ival(const AmbPatch *a, int id) {
    int n = amb_discrete(id); if (!n) return 0;
    int i = (int)(a->p[id] * (float)n); return i < 0 ? 0 : (i >= n ? n - 1 : i);
}
int amb_plen(const AmbPatch *a) {
    int n = 0; while (n < AMB_PSLOTS && a->prog[n] >= 0) n++;
    return n < 1 ? 1 : n;
}
int amb_nd(const AmbPatch *a) { return amb_scale_len(amb_ival(a, A_SCALE)); }
void amb_set_index(AmbPatch *a, int id, int i) {
    int n = amb_discrete(id); if (!n) return;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    a->p[id] = ((float)i + 0.5f) / (float)n;
    if (id == A_SCALE) a->mask = amb_scale_mask(i);
    if (id == A_PROG && i >= P_FIRST_PRESET && i < P_GEN) memcpy(a->prog, PRG[i], AMB_PSLOTS);   /* progressao pronta -> slots */
}
void amb_set_from_pos(AmbPatch *a, int id, float v) {
    int n = amb_discrete(id); v = clampf(v, 0.0f, 1.0f);
    if (n) { int q = (int)(v * (float)n); if (q >= n) q = n - 1; amb_set_index(a, id, q); }
    else a->p[id] = v;
}
void amb_adjust(AmbPatch *a, int id, int dir, float step) {
    int n = amb_discrete(id);
    if (n) {
        int i = amb_ival(a, id) + dir; if (i < 0) i = n - 1; if (i >= n) i = 0;
        amb_set_index(a, id, i);
    } else a->p[id] = clampf(a->p[id] + dir * step, 0.0f, 1.0f);
}
void amb_ptext(const AmbPatch *a, int id, char *b, size_t n) {
    int iv = amb_ival(a, id);
    switch (id) {
    case A_DSND: case A_LSND: snprintf(b, n, "%s", amb_snd_name(iv)); break;
    case A_ROOT:  snprintf(b, n, "%s", note_name(iv)); break;
    case A_SCALE: snprintf(b, n, "%s", amb_scale_name(iv)); break;
    case A_QUAL:  snprintf(b, n, "%s", amb_qual_name(iv)); break;
    case A_PROG:  snprintf(b, n, "%s", amb_prog_name(iv)); break;
    case A_VOICING: snprintf(b, n, "%s", VCN[iv]); break;
    case A_LMODE: snprintf(b, n, "%s", amb_lmode_name(iv)); break;
    case A_LOCT:  snprintf(b, n, "%s", LOCN[iv]); break;
    case A_LDIR:  snprintf(b, n, "%s", DIRN[iv]); break;
    case A_ECONT: snprintf(b, n, "%s", CNTN[iv]); break;
    case A_EHITS: snprintf(b, n, "%d", iv + 1); break;
    case A_ESTEPS:snprintf(b, n, "%d", iv + 4); break;
    case A_EROT:  snprintf(b, n, "%d", iv); break;
    case A_KHOLD: snprintf(b, n, "%s", iv ? "ON" : "OFF"); break;
    case A_DRATE: snprintf(b, n, "%d S", 4 + (int)(a->p[id] * 36.0f + 0.5f)); break;
    case A_SPEED: snprintf(b, n, "%d BPM", 30 + (int)(a->p[id] * 210.0f)); break;
    case A_DUR:   snprintf(b, n, "%.2f S", 0.05f + a->p[id] * 3.0f); break;
    case A_STRUM: if (a->p[id] < 0.03f) snprintf(b, n, "OFF"); else snprintf(b, n, "%d MS", (int)(a->p[id] * 90.0f + 0.5f)); break;
    default:      snprintf(b, n, "%d%%", (int)(a->p[id] * 100.0f + 0.5f)); break;
    }
}
void amb_opt_text(const AmbPatch *a, int id, int idx, char *b, size_t n) {
    AmbPatch t = *a; amb_set_index(&t, id, idx); amb_ptext(&t, id, b, n);
}

/* ---------------- acordes ---------------- */
int amb_chord_tones(const AmbPatch *a, int deg, int *o) {
    int root = amb_ival(a, A_ROOT), scale = amb_ival(a, A_SCALE), q = amb_ival(a, A_QUAL);
    int base = 48 + root, nd = amb_scale_len(scale), cm = a->cmod;
    deg = imod(deg, nd);
    if (q == 0) {                                     /* empilha tercas da propria escala: o acorde sempre "cabe" */
        for (int i = 0; i < 4; i++) o[i] = base + amb_scale_note(scale, deg + 2 * i);
        if (!(cm & (CM_7 | CM_9))) o[3] = o[0] + 12;  /* so triade (+ oitava): 7a e 9a entram pelos modificadores */
        if (cm & CM_9) { o[3] = base + amb_scale_note(scale, deg + 8); if (cm & CM_7) o[2] = base + amb_scale_note(scale, deg + 6); }
        if (cm & CM_SUS) o[1] = base + amb_scale_note(scale, deg + 3);
    } else {
        int r = base + amb_scale_note(scale, deg);
        for (int i = 0; i < 4; i++) o[i] = r + QI[q][i];
        if (cm & CM_SUS) o[1] = r + 5;
        if (cm & CM_7) o[3] = r + (QI[q][1] == 3 ? 10 : 11);
        if (cm & CM_9) { if (cm & CM_7) o[2] = o[3]; o[3] = r + 14; }
    }
    if (cm & CM_FLIP) { int t = o[1] - o[0]; if (t == 4) o[1]--; else if (t == 3) o[1]++; }
    return (cm & CM_INV) ? o[1] : o[0];
}
void amb_chord_label(const AmbPatch *a, int deg, char *b, size_t n) {
    int o[4]; amb_chord_tones(a, deg, o);
    int third = imod(o[1] - o[0], 12), fifth = imod(o[2] - o[0], 12), top = imod(o[3] - o[0], 12);
    char q[10] = "";
    if (third == 7) snprintf(q, sizeof q, "5");
    else if (third == 3) snprintf(q, sizeof q, fifth == 6 ? "dim" : "m");
    else if (third == 2) snprintf(q, sizeof q, "sus2");
    else if (third == 5) snprintf(q, sizeof q, "sus4");
    else if (third == 4 && fifth == 8) snprintf(q, sizeof q, "aug");
    char x[8] = "";
    if (third != 7) {
        if (top == 10) snprintf(x, sizeof x, third == 3 && fifth != 6 ? "7" : "7");
        else if (top == 11) snprintf(x, sizeof x, "maj7");
        else if (top == 2) snprintf(x, sizeof x, "9");
    }
    if (top == 2 && (a->cmod & CM_7) == 0 && third != 7) snprintf(x, sizeof x, "add9");
    snprintf(b, n, "%s%s%s", note_name(o[0] % 12), q, x);
}
void amb_roman(const AmbPatch *a, int deg, char *b, size_t n) {
    int nd = amb_nd(a); deg = imod(deg, nd);
    int o[4]; AmbPatch t = *a; t.cmod = 0; amb_chord_tones(&t, deg, o);
    char r[8]; snprintf(r, sizeof r, "%s", ROM[deg % 7]);
    if (imod(o[1] - o[0], 12) == 3) for (char *c = r; *c; c++) *c = (char)(*c + 32);     /* menor: minuscula */
    snprintf(b, n, "%s", r);
}
void amb_set_slot(AmbPatch *a, int slot, int deg) {
    if (slot < 0 || slot >= AMB_PSLOTS) return;
    if (deg < 0) { for (int j = slot; j < AMB_PSLOTS; j++) a->prog[j] = -1; }
    else { for (int j = 0; j < slot; j++) if (a->prog[j] < 0) a->prog[j] = 0; a->prog[slot] = (int8_t)deg; }
    amb_set_index(a, A_PROG, P_SLOTS);
}
void amb_write_chord(AmbPatch *a, int deg) {
    int n = 0; while (n < AMB_PSLOTS && a->prog[n] >= 0) n++;
    if (n >= AMB_PSLOTS) { memmove(a->prog, a->prog + 1, AMB_PSLOTS - 1); n = AMB_PSLOTS - 1; }
    a->prog[n] = (int8_t)deg;
    amb_set_index(a, A_PROG, P_SLOTS);
}

/* harmonia funcional: pesos de transicao I..VII (linha = grau atual) */
static const uint8_t GENW[7][7] = {
    /* I   ii  iii IV  V   vi  vii */
    {  0,  10, 10, 30, 25, 25,  0 },
    {  5,   0,  5, 15, 55, 20,  0 },
    {  5,  10,  0, 30,  5, 50,  0 },
    { 30,  20,  0,  0, 35, 15,  0 },
    { 50,   0,  5, 15,  0, 30,  0 },
    { 15,  25,  5, 35, 20,  0,  0 },
    { 60,   0, 40,  0,  0,  0,  0 }};
static int gen_next(int cur, int nd, uint32_t *rng) {
    int c = cur % 7, tot = 0; for (int i = 0; i < 7; i++) tot += GENW[c][i];
    int r = (int)(rnd_u(rng) * (float)tot), d = 0;
    for (int i = 0; i < 7; i++) { r -= GENW[c][i]; if (r < 0) { d = i; break; } }
    return d % nd;
}
void amb_dice_chords(AmbPatch *a, uint32_t seed) {
    uint32_t r = seed ? seed : 0xA5A5u; int nd = amb_nd(a);
    int d = (rnd_u(&r) < 0.7f) ? 0 : (nd > 5 ? 5 : 0);
    int len = 4 + (int)(rnd_u(&r) * 2.0f) * 4 * (rnd_u(&r) < 0.3f ? 1 : 0);   /* 4 (mais comum) ou 8 */
    if (len > AMB_PSLOTS) len = AMB_PSLOTS;
    for (int i = 0; i < AMB_PSLOTS; i++) a->prog[i] = -1;
    for (int i = 0; i < len; i++) { a->prog[i] = (int8_t)d; d = gen_next(d, nd, &r); }
    amb_set_index(a, A_PROG, P_SLOTS);
}

/* ---------------- euclid / geradores do lead ---------------- */
int amb_euclid_hit(int hits, int steps, int rot, int pos) {
    if (steps < 1) return 0;
    int i = imod(pos + rot, steps);
    return ((i * hits) % steps) < hits;
}
void amb_dice_lead(AmbPatch *a, uint32_t seed) {
    uint32_t r = seed ? seed : 0x77u; int lm = amb_ival(a, A_LMODE), sc = amb_ival(a, A_SCALE);
    if (lm == L_SEQ) {
        int d = 2 + (int)(rnd_u(&r) * 4.0f);
        for (int i = 0; i < 8; i++) {
            if (rnd_u(&r) < 0.22f) { a->seq[i] = -1; continue; }
            d += (int)(rnd_u(&r) * 5.0f) - 2; if (d < 0) d = 1; if (d > 9) d = 7;
            a->seq[i] = (int8_t)d;
        }
        a->seq[0] = (a->seq[0] < 0) ? 0 : a->seq[0];
    } else if (lm == L_EUCL) {
        static const int st[4] = {8, 12, 16, 16};
        int steps = st[(int)(rnd_u(&r) * 4.0f) & 3];
        int hits = 3 + (int)(rnd_u(&r) * (float)(steps / 3));
        amb_set_index(a, A_ESTEPS, steps - 4); amb_set_index(a, A_EHITS, hits - 1);
        amb_set_index(a, A_EROT, (int)(rnd_u(&r) * (float)steps) % 16);
        amb_set_index(a, A_ECONT, (int)(rnd_u(&r) * 5.0f) % 5);
    } else if (lm == L_ARP) {
        amb_set_index(a, A_LDIR, (int)(rnd_u(&r) * 5.0f) % 5);
    } else {                                                  /* DRIFT / KEYS: novo subconjunto de notas da escala */
        uint16_t m = amb_scale_mask(sc), o = 0; int c = 0;
        for (int i = 0; i < 12; i++) if ((m >> i) & 1) if (rnd_u(&r) < 0.7f) { o |= (uint16_t)(1u << i); c++; }
        a->mask = c >= 3 ? o : m;
    }
}

/* ---------------- presets de ambiente ---------------- */
typedef struct {
    const char *name; uint8_t scene, root, scale, dsnd, qual, prog, voicing, drate, lsnd, lmode, loct, bpm;
    uint8_t dens, dur, ldir, hits, steps, rot, cont, strum, dvol, lvol, echo, rev, mod, bright;
    int8_t seq[8];
} APre;
static const APre PRE[A_NPRESETS] = {
 /*          name          sc rt scl ds q  pg vc dr ls lm lo bpm dens dur ld ht st rt ct str dv  lv  ec  rv  md  br   seq */
 { "TIDE",        0, 9, 1, 0, 0, 2, 0, 20, 4, L_DRIFT, 1,  70, 45, 45, 0, 5,16,0,0, 20, 80, 55, 45, 70, 35, 45, {0,2,4,7,4,2,-1,0} },
 { "HARBOR",0, 2, 2, 1, 0, 5, 0, 16, 7, L_ARP,   1,  84, 70, 40, 0, 5,16,0,0, 30, 70, 50, 40, 65, 30, 55, {0,2,4,7,4,2,-1,0} },
 { "NEON NIGHT",  0, 4, 1, 1, 0, 4, 0, 14, 4, L_ARP,   1, 110, 80, 30, 2, 5,16,0,0, 15, 70, 55, 62, 55, 40, 60, {0,2,4,7,4,2,-1,0} },
 { "LIGHTHOUSE",  0, 5, 4, 0, 0, 2, 2, 18, 7, L_SEQ,   1,  76, 60, 55, 0, 5,16,0,0, 40, 75, 55, 50, 70, 30, 55, {0,4,2,4,7,4,2,-1} },
 { "WINDOW RAIN", 1, 0, 0, 0, 0, 3, 0, 22, 4, L_EUCL,  1,  90, 80, 22, 0, 4,12,2,0, 10, 75, 55, 55, 65, 30, 45, {0,2,4,7,4,2,-1,0} },
 { "COLD FRONT",  1, 4, 3, 1, 0, 7, 2, 24, 4, L_DRIFT, 1,  60, 35, 60, 0, 5,16,0,0, 30, 85, 45, 40, 75, 45, 38, {0,2,4,7,4,2,-1,0} },
 { "CATHEDRAL",   1, 7, 0, 2, 0, 5, 2, 14, 7, L_KEYS,  1,  80, 50, 70, 0, 5,16,0,0, 60, 70, 55, 35, 85, 25, 42, {0,2,4,7,4,2,-1,0} },
 { "LULLABY",     1, 0, 0, 6, 0, 3, 0, 26, 7, L_SEQ,   2,  66, 70, 30, 0, 5,16,0,0, 20, 70, 55, 50, 60, 25, 50, {4,2,0,2,4,4,4,-1} },
 { "DUNE",        2, 2, 8, 6, 5, 7, 0, 22, 6, L_SEQ,   1,  72, 60, 45, 0, 5,16,0,0, 25, 80, 55, 55, 55, 30, 40, {0,1,0,3,2,1,-1,0} },
 { "MIRAGE",      2, 6, 5, 2, 1, 8, 2, 18, 7, L_EUCL,  1,  96, 70, 35, 0, 5,16,0,0, 30, 70, 50, 55, 70, 35, 55, {0,2,4,7,4,2,-1,0} },
 { "DAWN",        2, 0, 4, 0, 0, 9, 0, 16, 4, L_ARP,   2,  88, 65, 35, 0, 5,16,0,0, 35, 75, 50, 45, 70, 35, 60, {0,2,4,7,4,2,-1,0} },
 { "HYMN",  2, 7, 2, 3, 0, 3, 2, 18, 4, L_KEYS,  1,  80, 50, 70, 0, 5,16,0,0, 60, 70, 55, 30, 80, 30, 40, {0,2,4,7,4,2,-1,0} },
 { "DEEP SPACE",  3, 0, 3, 5, 0, 0, 0, 30, 7, L_DRIFT, 2,  50, 22, 80, 0, 5,16,0,0, 10, 90, 45, 60, 90, 35, 35, {0,2,4,7,4,2,-1,0} },
 { "ORBIT",       3, 7, 7, 4, 0, 7, 0, 20, 4, L_EUCL,  1, 100, 80, 25, 0, 6,16,3,3, 10, 70, 55, 55, 70, 30, 55, {0,2,4,7,4,2,-1,0} },
 { "GLACIER",     3, 6, 4, 4, 0, 7, 2, 28, 7, L_DRIFT, 2,  56, 30, 90, 0, 5,16,0,0, 40, 80, 50, 50, 85, 35, 55, {0,2,4,7,4,2,-1,0} },
 { "CRYSTALS",3, 3, 9, 7, 0, 0, 2, 30, 4, L_EUCL,  2,  84, 65, 30, 0, 4,13,1,4, 30, 60, 55, 60, 80, 40, 60, {0,2,4,7,4,2,-1,0} },
};
const char *amb_preset_name(int n) { return PRE[(unsigned)n % A_NPRESETS].name; }
int amb_preset_scene(int n) { return PRE[(unsigned)n % A_NPRESETS].scene & 3; }

void amb_default(AmbPatch *a) {
    memset(a, 0, sizeof *a);
    a->drone_on = 1; a->lead_on = 1;
    amb_preset(a, 0);
}
void amb_preset(AmbPatch *a, int n) {
    const APre *p = &PRE[(unsigned)n % A_NPRESETS];
    uint8_t on_d = a->drone_on, on_l = a->lead_on; float vol = a->p[A_VOL];
    memset(a, 0, sizeof *a);
    a->drone_on = on_d; a->lead_on = on_l;
    amb_set_index(a, A_DSND, p->dsnd); amb_set_index(a, A_ROOT, p->root); amb_set_index(a, A_SCALE, p->scale);
    amb_set_index(a, A_QUAL, p->qual); amb_set_index(a, A_PROG, p->prog); amb_set_index(a, A_VOICING, p->voicing);
    a->p[A_DRATE] = ((float)p->drate - 4.0f) / 36.0f;
    amb_set_index(a, A_LSND, p->lsnd); amb_set_index(a, A_LMODE, p->lmode); amb_set_index(a, A_LOCT, p->loct);
    a->p[A_SPEED] = ((float)p->bpm - 30.0f) / 210.0f; a->p[A_DENS] = (float)p->dens / 100.0f;
    a->p[A_DUR] = (float)p->dur / 100.0f;
    amb_set_index(a, A_LDIR, p->ldir); amb_set_index(a, A_EHITS, p->hits - 1);
    amb_set_index(a, A_ESTEPS, p->steps ? p->steps - 4 : 12); amb_set_index(a, A_EROT, p->rot); amb_set_index(a, A_ECONT, p->cont);
    a->p[A_STRUM] = (float)p->strum / 90.0f; amb_set_index(a, A_KHOLD, 0);
    a->p[A_DVOL] = (float)p->dvol / 100.0f; a->p[A_LVOL] = (float)p->lvol / 100.0f; a->p[A_ECHO] = (float)p->echo / 100.0f;
    a->p[A_REV] = (float)p->rev / 100.0f; a->p[A_MOD] = (float)p->mod / 100.0f; a->p[A_BRIGHT] = (float)p->bright / 100.0f;
    a->p[A_VOL] = vol > 0.01f ? vol : 0.35f;
    memcpy(a->seq, p->seq, 8);
    a->scene = p->scene & 3;
    if (p->prog == P_MANUAL || p->prog == P_GEN) { a->prog[0] = 0; for (int i = 1; i < AMB_PSLOTS; i++) a->prog[i] = -1; }
    a->cdeg = 0; a->cmod = 0; a->cgen = 1;
}

/* ---------------- vozes ---------------- */
static int vscore_best(Amb *s, int lo, int hi, const int *taken) {
    int best = lo; float bs = 1e9f;
    for (int i = lo; i < hi; i++) {
        if (taken && taken[i - lo]) continue;
        AVoice *v = &s->v[i];
        float score = (!v->gate && v->env < 0.001f && v->pdly <= 0) ? -1.0f : (v->gate ? 1000.0f + (float)(v->age & 0xFFFFFF) : v->env);
        if (v->pdly > 0) score += 2000.0f;
        if (v->held) score += 5000.0f;
        if (score < bs) { bs = score; best = i; }
    }
    return best;
}
static void vtrig(Amb *s, int idx, int snd, int midi, float vel, float att, float rel, float hold_s, float glide, int held) {
    AVoice *v = &s->v[idx];
    int fresh = (v->env < 0.002f && !v->gate);
    v->midi = midi; v->tfreq = midi_hz(midi); v->snd = snd; v->pdly = 0;
    if (fresh) { v->freq = v->tfreq; v->env = 0.0f; }
    else if (glide < 0.05f && v->env < 0.2f) { v->env *= 0.1f; v->freq = v->tfreq; }   /* voz roubada ainda soando: some e ja entra afinada (sem chirp) */
    v->envt = vel; v->ak = blk_k(att); v->rk = blk_k(rel); v->gk = blk_k(glide);
    v->gate = 1; v->held = held; v->age = ++s->age;
    v->hold = hold_s < 0.0f ? -1 : (int)(hold_s * (float)SYN_RATE);
}
static void lead_note(Amb *s, int snd, int midi, float vel, float dur, float glide, int held) {
    float rel = 0.25f + LRK[snd] * (0.5f + dur);
    vtrig(s, vscore_best(s, AMB_DV, AMB_V, NULL), snd, midi, vel, LATT[snd], rel, held ? -1.0f : dur, glide, held);
}
static void release_range(Amb *s, int lo, int hi, int only_held) {
    for (int i = lo; i < hi; i++) if (!only_held || s->v[i].held) { s->v[i].gate = 0; s->v[i].held = 0; if (!only_held) s->v[i].pdly = 0; }
}

/* ---------------- notas permitidas ---------------- */
static int allowed(const AmbPatch *pt, int base, int lo, int hi, int *out) {
    int n = 0;
    for (int m = base + lo; m <= base + hi && n < 48; m++) {
        int iv = ((m - base) % 12 + 12) % 12;
        if (pt->mask & (1u << iv)) out[n++] = m;
    }
    if (n == 0) out[n++] = base + lo;
    return n;
}

/* ---------------- voicing (conducao de vozes) e disparo do acorde ---------------- */
static void voice_chord(Amb *s, const AmbPatch *pt, int deg, int out[4]) {
    int o[4], bass = amb_chord_tones(pt, deg, o);
    int base = 48 + amb_ival(pt, A_ROOT), voicing = amb_ival(pt, A_VOICING);
    int up[3] = { (pt->cmod & CM_INV) ? o[0] + 12 : o[1], o[2], o[3] }, res[3];
    out[0] = bass - 12;                                            /* baixo: grave, sem conducao */
    if (voicing == 0 && s->have_prev) {                            /* SMOOTH: cada voz vai para a nota mais proxima da anterior */
        int used[3] = { 0, 0, 0 };
        for (int j = 0; j < 3; j++) {
            int bt = 0, bn = up[0], bd = 1000;
            for (int t = 0; t < 3; t++) if (!used[t]) {
                int k = (int)floorf((float)(s->prev_up[j] - up[t]) / 12.0f + 0.5f), n = up[t] + 12 * k, d = abs(n - s->prev_up[j]);
                if (d < bd) { bd = d; bt = t; bn = n; }
            }
            used[bt] = 1; res[j] = bn;
        }
    } else if (voicing == 2) { res[0] = up[1]; res[1] = up[0] + 12; res[2] = up[2]; }     /* OPEN */
    else { res[0] = up[0]; res[1] = up[1]; res[2] = up[2]; }                              /* CLOSE */
    for (int j = 0; j < 3; j++) { while (res[j] < base - 2) res[j] += 12; while (res[j] > base + 22) res[j] -= 12; }
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2 - i; j++) if (res[j] > res[j + 1]) { int t = res[j]; res[j] = res[j + 1]; res[j + 1] = t; }
    for (int j = 0; j < 3; j++) { out[1 + j] = res[j]; s->prev_up[j] = res[j]; }
    s->have_prev = 1;
}
/* o acorde novo entra em vozes livres (fade-in) enquanto o antigo faz fade-out: crossfade sem glide */
static void drone_strike(Amb *s, const AmbPatch *pt, int deg, int dsnd) {
    int nt[4]; voice_chord(s, pt, deg, nt);
    release_range(s, 0, AMB_DV, 0);
    int taken[AMB_DV]; memset(taken, 0, sizeof taken);
    int strum = pt->p[A_STRUM] < 0.03f ? 0 : (int)(pt->p[A_STRUM] * 0.09f * (float)SYN_RATE);
    for (int k = 0; k < 4; k++) {
        int vi = vscore_best(s, 0, AMB_DV, taken); taken[vi] = 1;
        AVoice *v = &s->v[vi];
        if (k == 0 || strum == 0) vtrig(s, vi, dsnd, nt[k], k ? 0.40f : 0.50f, DATT[dsnd], DREL[dsnd], -1.0f, 0.03f, 0);
        else { v->pmidi = nt[k]; v->psnd = dsnd; v->pdly = k * strum; }
    }
    s->cur_note = nt[0] + 12;
}

/* ---------------- reverb ---------------- */
static const int CL[8] = {828, 882, 948, 1007, 851, 905, 971, 1030};
static const int AL[4] = {413, 327, 436, 350};

void amb_init(Amb *s, uint32_t seed) {
    if (!tab_ok) {
        for (int i = 0; i < 2049; i++) {
            float p = 6.28318530718f * (float)i / 2048.0f;
            stab[i] = sinf(p);
        }
        stab[2049] = stab[1];
        for (int w = 0; w < A_NSND; w++) {
            float pk = 0.0f;
            for (int i = 0; i < 2048; i++) {
                float p = 6.28318530718f * (float)i / 2048.0f, y = 0.0f;
                for (int h = 0; h < 8; h++) y += HARM[w][h] * sinf(p * (float)(h + 1));
                wtab[w][i] = y; if (fabsf(y) > pk) pk = fabsf(y);
            }
            for (int i = 0; i < 2048; i++) wtab[w][i] *= 0.8f / pk;
            wtab[w][2048] = wtab[w][0]; wtab[w][2049] = wtab[w][1];
        }
        tab_ok = 1;
    }
    memset(s, 0, sizeof *s);
    s->rng = seed ? seed : 0xBEEF1234u; s->sigd = s->sigl = -1; s->chord_sig = -12345; s->last_idx = 4; s->ed_s = 8000.0f;
    for (int i = 0; i < 8; i++) s->motif[i] = -1;
}

void amb_render(Amb *s, const AmbPatch *pt, const AmbIn *in, int16_t *out, int frames)
{
    if (!s->primed) { memcpy(s->sm, pt->p, sizeof s->sm); s->primed = 1; s->cgen_seen = pt->cgen; s->cur_deg = pt->cdeg; }
    float peak = 0.0f; int sw = s->scope_w;
    int root = amb_ival(pt, A_ROOT), scale = amb_ival(pt, A_SCALE), qual = amb_ival(pt, A_QUAL), pm = amb_ival(pt, A_PROG);
    int dsnd = amb_ival(pt, A_DSND), lsnd = amb_ival(pt, A_LSND), voicing = amb_ival(pt, A_VOICING);
    int lmode = pt->lead_on ? amb_ival(pt, A_LMODE) : L_OFF, loct = amb_ival(pt, A_LOCT);
    int nd = amb_scale_len(scale);
    int base = 48 + root, lbase = 12 + 12 * loct;
    int lspan = loct == 0 ? 18 : (loct == 1 ? 14 : 12);
    float spd = pt->p[A_SPEED], dens = pt->p[A_DENS];
    int period_fr = (int)((4.0f + 36.0f * pt->p[A_DRATE]) * (float)SYN_RATE);
    int cmod = pt->cmod;

    int sigl = (int)((uint32_t)(lmode | loct << 4 | root << 8 | scale << 12) * 2654435761u ^ pt->mask);
    if (sigl != s->sigl) { s->clk2 = 0; s->step = 0; s->arp_i = 0; release_range(s, AMB_DV, AMB_V, 0); s->touch_prev = 0; }
    s->sigl = sigl;

    for (int b0 = 0; b0 + BLK <= frames; b0 += BLK) {
        for (int i = 0; i < A_COUNT; i++) s->sm[i] += (pt->p[i] - s->sm[i]) * 0.1f;
        const float *m = s->sm;
        s->clk -= BLK; s->clk2 -= BLK;
        float step_sec = 30.0f / (30.0f + 210.0f * spd);       /* colcheia */
        int step_fr = (int)(step_sec * (float)SYN_RATE);
        float dur = 0.05f + pt->p[A_DUR] * 3.0f, bright_add = 0.0f;
        int al[48], tn[4];

        /* ---- DRONE: pads (tempo real) ou progressao ---- */
        if (pt->cgen != s->cgen_seen) { s->cgen_seen = pt->cgen; s->cur_deg = imod(pt->cdeg, nd); s->clk = period_fr; }
        if (pt->drone_on && pm != P_MANUAL && s->clk <= 0) {
            s->clk = period_fr;
            if (pm == P_GEN) s->cur_deg = gen_next(s->cur_deg, nd, &s->rng);
            else { int pl = amb_plen(pt), d = pt->prog[s->prog_i % pl]; s->cur_deg = imod(d < 0 ? 0 : d, nd); s->prog_i++; }
        }
        if (pm == P_MANUAL) s->clk = period_fr;
        s->cur_deg = imod(s->cur_deg, nd); s->cur_deg_ui = s->cur_deg;
        if (pt->drone_on) {
            int csig = root | scale << 4 | qual << 8 | voicing << 12 | cmod << 14 | dsnd << 19 | s->cur_deg << 22;
            if (csig != s->chord_sig) { s->chord_sig = csig; drone_strike(s, pt, s->cur_deg, dsnd); }
            for (int i = 0; i < AMB_DV; i++) {                     /* notas do strum que esperam a vez */
                AVoice *v = &s->v[i];
                if (v->pdly > 0) { v->pdly -= BLK; if (v->pdly <= 0) vtrig(s, i, v->psnd, v->pmidi, 0.40f, DATT[v->psnd], DREL[v->psnd], -1.0f, 0.03f, 0); }
            }
        } else { release_range(s, 0, AMB_DV, 0); s->chord_sig = -12345; }

        /* ---- LEAD ---- */
        if (lmode == L_KEYS) {
            int khold = amb_ival(pt, A_KHOLD);
            if (in->touch) {
                int n = allowed(pt, base, lbase, lbase + lspan, al);
                int note = al[(int)(clampf(in->x, 0.0f, 0.999f) * (float)n)];
                if (!s->touch_prev || note != s->touch_note) {
                    release_range(s, AMB_DV, AMB_V, 1); s->touch_note = note;
                    lead_note(s, lsnd, note, 0.5f, 0.0f, 0.03f, 1);
                    s->cur_note = note;
                }
                bright_add += (in->y - 0.5f) * 0.5f;
            } else if (!khold) release_range(s, AMB_DV, AMB_V, 1);   /* HOLD (latch): a nota continua ate o proximo toque */
            s->touch_prev = in->touch;
        } else if (lmode != L_OFF && s->clk2 <= 0) {
            s->clk2 = step_fr; int pos = s->step; s->step++;
            int p8 = pos & 7;
            if (lmode == L_DRIFT) {
                if (p8 == 0) s->cyc++;
                int n = allowed(pt, base, lbase, lbase + lspan, al), idx;
                if ((s->cyc & 1) && s->motif[p8] != -2) {              /* repete (com variacao) o motivo do ciclo anterior */
                    idx = s->motif[p8]; if (idx >= 0 && rnd_u(&s->rng) < 0.25f) idx += (rnd_u(&s->rng) < 0.5f) ? -1 : 1;
                } else {
                    idx = (rnd_u(&s->rng) < 0.25f) ? -1 : s->last_idx + (int)(rnd_u(&s->rng) * 5.0f) - 2;
                    if (idx >= 0) { idx = (int)clampf((float)idx, 0.0f, (float)(n - 1)); s->last_idx = idx; }
                }
                s->motif[p8] = idx;
                s->step_ui = p8; s->nsteps_ui = 8;
                if (idx >= 0 && idx < n && rnd_u(&s->rng) < 0.2f + 0.8f * dens) { lead_note(s, lsnd, al[idx], 0.5f, dur, 0.01f, 0); s->cur_note = al[idx]; s->hit_ui++; }
            } else if (lmode == L_ARP) {
                int dir = amb_ival(pt, A_LDIR), t = s->arp_i++, k;
                amb_chord_tones(pt, s->cur_deg, tn);                  /* segue o acorde atual (com os modificadores) */
                if (dir == 0) k = t % 8;
                else if (dir == 1) k = 7 - t % 8;
                else if (dir == 2) { int q = t % 14; k = q < 8 ? q : 14 - q; }
                else if (dir == 3) k = (int)(rnd_u(&s->rng) * 8.0f) & 7;
                else { k = (int)clampf((float)s->last_idx + (float)((int)(rnd_u(&s->rng) * 3.0f) - 1), 0.0f, 7.0f); s->last_idx = k; }
                int note = tn[k & 3] + 12 * (k >> 2) + 12 * (1 + loct);
                s->step_ui = k; s->nsteps_ui = 8;
                if (rnd_u(&s->rng) < 0.35f + 0.65f * dens) { lead_note(s, lsnd, note, 0.5f, dur, 0.01f, 0); s->cur_note = note; s->hit_ui++; }
            } else if (lmode == L_SEQ) {
                int d = pt->seq[p8];
                s->step_ui = p8; s->nsteps_ui = 8;
                if (d >= 0) { int note = base + 12 * (1 + loct) + amb_scale_note(scale, d); lead_note(s, lsnd, note, 0.55f, dur, 0.01f, 0); s->cur_note = note; s->hit_ui++; }
            } else {                                                  /* EUCLID */
                int hits = amb_ival(pt, A_EHITS) + 1, steps = 4 + amb_ival(pt, A_ESTEPS), rot = amb_ival(pt, A_EROT) % steps, cont = amb_ival(pt, A_ECONT);
                if (hits > steps) hits = steps;
                int ep = pos % steps;
                s->step_ui = ep; s->nsteps_ui = steps;
                if (ep == 0) s->cyc++;
                if (amb_euclid_hit(hits, steps, rot, ep)) {
                    int hk = 0; for (int q = 0; q < ep; q++) hk += amb_euclid_hit(hits, steps, rot, q);
                    int n = allowed(pt, base, lbase, lbase + lspan, al), rng = n - 1 > 9 ? 9 : n - 1, idx;
                    float ph = hits > 1 ? (float)hk / (float)(hits - 1) : 0.5f;
                    switch (cont) {
                    case 1: idx = (int)(ph * (float)rng + 0.5f); break;
                    case 2: idx = rng - (int)(ph * (float)rng + 0.5f); break;
                    case 3: idx = (int)((1.0f - fabsf(2.0f * ph - 1.0f)) * (float)rng + 0.5f); break;
                    case 4: idx = (int)(rnd_u(&s->rng) * (float)(rng + 1)); break;
                    default: idx = (int)clampf((float)s->last_idx + (float)((int)(rnd_u(&s->rng) * 5.0f) - 2), 0.0f, (float)rng); s->last_idx = idx; break;
                    }
                    if (idx > rng) idx = rng;
                    if (rnd_u(&s->rng) < 0.25f + 0.75f * dens) { lead_note(s, lsnd, al[idx], hk == 0 ? 0.62f : 0.5f, dur, 0.01f, 0); s->cur_note = al[idx]; s->hit_ui++; }
                }
            }
        }

        /* ---- taxa de bloco: envelopes e frequencias ---- */
        float e0[AMB_V], e1[AMB_V], inc1[AMB_V], inc2[AMB_V]; int act[AMB_V], na = 0, pcm = 0;
        s->lfo1 += 0.11f * INV_R * BLK; if (s->lfo1 >= 1.0f) s->lfo1 -= 1.0f;
        s->lfo2 += 0.07f * INV_R * BLK; if (s->lfo2 >= 1.0f) s->lfo2 -= 1.0f;
        s->lfo3 += 5.2f * INV_R * BLK;  if (s->lfo3 >= 1.0f) s->lfo3 -= 1.0f;
        float l1 = tlook(stab, s->lfo1), l2 = tlook(stab, s->lfo2), lfo_v = tlook(stab, s->lfo3);
        float vibd = 1.0f + l1 * m[A_MOD] * 0.0015f;              /* drone: ondulacao lenta e minuscula (nada de sirene) */
        float vibl = 1.0f + lfo_v * m[A_MOD] * 0.003f;            /* lead: vibrato leve */
        float bend = exp2f(clampf(in->cx, -1.0f, 1.0f) * (2.0f / 12.0f));       /* analogico X: bend do lead */
        bright_add += clampf(in->cy, -1.0f, 1.0f) * 0.35f;                        /* analogico Y: brilho */
        for (int i = 0; i < AMB_V; i++) {
            AVoice *v = &s->v[i];
            float old = v->env;
            if (v->gate) {
                v->env += (v->envt - v->env) * v->ak;
                if (v->hold > 0) { v->hold -= BLK; if (v->hold <= 0) { v->gate = 0; v->held = 0; } }
            } else v->env -= v->env * v->rk;
            if (v->env < 0.0003f && !v->gate) { v->env = 0.0f; }
            v->freq += (v->tfreq - v->freq) * v->gk;
            s->venv[i] = v->env;
            if (v->env > 0.0f || old > 0.0f) {
                int lead = i >= AMB_DV;
                float bd = lead ? bend * vibl : vibd;
                e0[na] = old; e1[na] = v->env;
                inc1[na] = v->freq * bd * INV_R; inc2[na] = inc1[na] * (1.0f + 0.0022f * (float)(1 + i % 3) + m[A_MOD] * 0.002f);
                act[na] = i; na++;
                pcm |= 1 << (v->midi % 12);
            }
        }
        s->notes_pc = pcm;
        float bright = clampf(m[A_BRIGHT] + bright_add + l1 * m[A_MOD] * 0.25f, 0.0f, 1.0f);
        float fc = 180.0f * exp2f(bright * 6.5f);
        float lpa = clampf(fc * 6.2831853f * INV_R, 0.001f, 0.95f);
        float lpb = clampf(fc * 2.4f * 6.2831853f * INV_R, 0.001f, 0.95f);   /* o lead e mais aberto que o drone */
        float trem = 1.0f - m[A_MOD] * 0.3f * (0.5f + 0.5f * l2);
        float revm = m[A_REV], fb = 0.78f + 0.18f * revm;
        float gtarget = (in->mute ? 0.0f : m[A_VOL] * 0.9f) * trem;
        float dg = m[A_DVOL] * 0.62f / 1.5f, lg = m[A_LVOL] * 0.75f;
        float ed = clampf((float)step_fr * 1.5f, 3000.0f, (float)(ECHO_N - 8)), echo_mix = m[A_ECHO] * 0.8f;
        s->ed_s += (ed - s->ed_s) * 0.02f;

        for (int n = 0; n < BLK; n++) {
            float sl[2] = {0.0f, 0.0f}, sr[2] = {0.0f, 0.0f}, f = (float)n * (1.0f / BLK);
            for (int k = 0; k < na; k++) {
                AVoice *v = &s->v[act[k]];
                v->ph1 += inc1[k]; if (v->ph1 >= 1.0f) v->ph1 -= 1.0f;
                v->ph2 += inc2[k]; if (v->ph2 >= 1.0f) v->ph2 -= 1.0f;
                float e = e0[k] + (e1[k] - e0[k]) * f;
                const float *tb = wtab[v->snd & (A_NSND - 1)];
                float w = (tlook(tb, v->ph1) + 0.6f * tlook(tb, v->ph2)) * e;
                int ix = act[k], bus = ix >= AMB_DV; float pan = 0.15f + 0.7f * (float)((ix * 3) & 7) / 7.0f;
                sl[bus] += w * (1.0f - pan); sr[bus] += w * pan;
            }
            s->lpl[0] += lpa * (sl[0] - s->lpl[0]); s->lpr[0] += lpa * (sr[0] - s->lpr[0]);
            s->lpl[1] += lpb * (sl[1] - s->lpl[1]); s->lpr[1] += lpb * (sr[1] - s->lpr[1]);
            /* eco ping-pong (so no lead), sincronizado com a velocidade */
            float lL = s->lpl[1] * lg, lR = s->lpr[1] * lg;
            float rp = (float)s->ei - s->ed_s; if (rp < 0.0f) rp += (float)ECHO_N;
            int i0 = (int)rp, i1 = (i0 + 1) & ECHO_MASK; float fr = rp - (float)i0; i0 &= ECHO_MASK;
            float el = s->ec[0][i0] + (s->ec[0][i1] - s->ec[0][i0]) * fr, er = s->ec[1][i0] + (s->ec[1][i1] - s->ec[1][i0]) * fr;
            s->ec[0][s->ei] = lL + er * 0.48f; s->ec[1][s->ei] = lR + el * 0.48f;
            s->ei = (s->ei + 1) & ECHO_MASK;
            float dl = s->lpl[0] * dg + lL + el * echo_mix, dr = s->lpr[0] * dg + lR + er * echo_mix;
            float in_m = (dl + dr) * 0.12f, wl = 0.0f, wr = 0.0f;
            for (int c = 0; c < 8; c++) {
                float o = s->comb[c][s->ci[c]];
                s->cf[c] = o * 0.7f + s->cf[c] * 0.3f;
                s->comb[c][s->ci[c]] = in_m + s->cf[c] * fb;
                if (++s->ci[c] >= CL[c]) s->ci[c] = 0;
                if (c < 4) wl += o; else wr += o;
            }
            for (int a = 0; a < 4; a++) {
                float x = (a < 2) ? wl : wr, bo = s->ap[a][s->ai[a]];
                s->ap[a][s->ai[a]] = x + bo * 0.5f;
                float y = bo - x;
                if (++s->ai[a] >= AL[a]) s->ai[a] = 0;
                if (a < 2) wl = y; else wr = y;
            }
            float dry = 1.0f - 0.35f * revm, wet = revm * 1.2f;
            float L = dl * dry + wl * wet, R = dr * dry + wr * wet;
            s->gain += (gtarget - s->gain) * 0.002f;
            L *= s->gain; R *= s->gain;
            L = L / (1.0f + fabsf(L)); R = R / (1.0f + fabsf(R));          /* saturacao suave */
            for (int c = 0; c < 2; c++) {                                  /* bloqueio de DC */
                float x = c ? R : L, y = x - s->dcx[c] + 0.995f * s->dcy[c];
                s->dcx[c] = x; s->dcy[c] = y; if (c) R = y; else L = y;
            }
            if (!(L > -4.0f && L < 4.0f && R > -4.0f && R < 4.0f)) {
                L = R = 0.0f; memset(s->comb, 0, sizeof s->comb); memset(s->ap, 0, sizeof s->ap); memset(s->ec, 0, sizeof s->ec);
                s->lpl[0] = s->lpl[1] = s->lpr[0] = s->lpr[1] = 0.0f; s->dcx[0] = s->dcx[1] = s->dcy[0] = s->dcy[1] = 0.0f;
            }
            L = clampf(L, -1.0f, 1.0f); R = clampf(R, -1.0f, 1.0f);
            out[(b0 + n) * 2] = (int16_t)(L * 32767.0f); out[(b0 + n) * 2 + 1] = (int16_t)(R * 32767.0f);
            float ao = fabsf(L) > fabsf(R) ? fabsf(L) : fabsf(R); if (ao > peak) peak = ao;
            s->scope[sw & (SCOPE_N - 1)] = (L + R) * 0.5f; sw++;
        }
    }
    s->scope_w = sw; s->peak = peak;
}
