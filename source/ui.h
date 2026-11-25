/* INTONARUMORI 0.9 - interface (desenha direto no framebuffer RGB565).
 * Todo o layout da tela de baixo fica em ui.c; main.c so conhece os retangulos exportados aqui. */
#ifndef UI_H
#define UI_H
#include <stdint.h>
#include "amb.h"

typedef struct {
    uint16_t *px;   /* framebuffer do 3DS: coluna a coluna, origem embaixo-esquerda */
    int w, h;       /* largura (400 ou 320) e altura (240) logicas */
} Surf;

/* pad XY do harsh (tela de baixo) */
#define UI_PAD_X 6
#define UI_PAD_Y 30
#define UI_PAD_W 200
#define UI_PAD_H 140
/* faixa do slider nas linhas do editor */
#define UI_SLD_X 92
#define UI_SLD_W 168
/* slider de volume da tela de tocar (harsh) */
#define UI_VOL_X 92
#define UI_VOL_W 168
/* sliders do EUCLID (tela de tocar do ambient) */
#define UI_ES_X 156
#define UI_ES_W 150

/* abas: noise -> presets de noise -> ambient -> presets de ambient -> arquivos -> ajustes */
enum { PG_HARSH, PG_HPRE, PG_AMB, PG_APRE, PG_FILES, PG_SET, PG_COUNT };
#define SET_N 6            /* SMOOTH, SPRING, LITE, STICKS, HOLD-Y, AUTO-MUT */
#define MAX_FILES 8
#define FNAME_MAX 32

typedef struct { char name[FNAME_MAX]; float secs; uint32_t rate; } FileEnt;

typedef struct {
    int page, sub;               /* pagina e sub-vista (0 = tocar, 1 = editar) */
    const float *v;              /* parametros do motor harsh */
    int module;                  /* modulo (harsh) ou grupo (ambient) no editor */
    int row, preset, modified, mute, burst, paused;
    const AmbPatch *ap; int arow;
    int apreset;                 /* preset de ambiente carregado (-1 = nenhum / editado) */
    int pane;                    /* ambient: 0 = DRONE (acordes), 1 = LEAD (melodia) */
    int cur_deg, cur_slot;       /* acorde atual (grau) e slot da sequencia que esta tocando (-1 = nenhum) */
    int step_ui, nsteps_ui, hit_ui;   /* playhead do lead */
    int write_on;                /* pads gravam na sequencia de acordes */
    int notes_pc, cur_note; const float *venv;
    int engine;                  /* 0 = harsh, 1 = ambient (qual motor soa) */
    float level; const float *scope; int spp; int frame;
    int xid, yid; int touching; float tx, ty;
    int rec_on, rec_err; float rec_secs; uint32_t rec_drop;
    float set_v[SET_N];          /* [0] = SMOOTH 0..1; os demais = indice da opcao */
    int setrow;
    const FileEnt *files; int nfiles, fscroll, fsel;
    int del_arm;                 /* 1 = DELETE esta armado (pede o segundo toque) */
    float stick_x, stick_y;      /* leitura atual do analogico (-1..1) */
    int holdy;                   /* 0..100: progresso de Y segurado (pausa) */
    int lite;
} UiState;

enum { H_NONE, H_TAB, H_REC, H_PAUSE, H_PAD, H_APAD, H_MODTILE, H_VOL,
       H_SLIDER, H_PILL, H_STEPL, H_STEPR,
       H_PRESET, H_PREV, H_NEXT, H_MUTATE, H_RANDOM, H_EDIT,
       H_DRONE, H_LEAD, H_KEY,
       H_FUP, H_FDN, H_FILE, H_REN, H_DEL,
       H_APRE, H_CPAD, H_CMOD, H_CSLOT, H_CWRITE, H_CDICE,
       H_TIMB, H_LMODE, H_SEQCELL, H_EPAR, H_LDICE, H_LXTRA, H_LDIR };

/* kind = tipo; arg = pagina/modulo/id do parametro/indice; arg2 = indice da opcao (pills) ou valor (SEQCELL);
 * row = linha no editor; fx, fy = posicao 0..1 dentro do controle (fy: 0 = embaixo) */
typedef struct { int kind, arg, arg2, row; float fx, fy; } UiHit;

UiHit ui_hit(const UiState *u, int x, int y);
/* retangulo do pad do ambient (so existe no pane LEAD, nos modos DRIFT / ARP / KEYS) */
int  ui_apad_rect(const UiState *u, int *x, int *y, int *w, int *h);   /* 0 = nao ha pad */
/* celula do STEP SEQ sob o toque: devolve 1 e preenche passo (0..7) e valor (-1 = pausa, 0..9) */
int  ui_seq_cell(int x, int y, int *step, int *val);
void ui_draw_top(Surf *s, const UiState *u);
void ui_draw_bottom(Surf *s, const UiState *u);
void ui_draw_error(Surf *s, int kind);
void ui_vis_test(Surf *s, int id, int t, float lv);   /* so para teste: desenha a cena na janela */
#endif
