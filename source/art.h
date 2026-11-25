/* INTONARUMORI 0.9 - arte vetorial anti-aliased (motor gfx do projeto h1, MIT) apresentada
 * como fosforo de VFD/LCD. Desenha as cenas dos presets, o osciloscopio e os digitos de 7 segmentos. */
#ifndef ART_H
#define ART_H
#include <stdint.h>
#include "ui.h"

#define ART_BLANK 16           /* sem preset (fita cassete) */
#define ART_AMB0  17           /* 17..20: cenas do ambient (mar, chuva, deserto, espaco) */
#define ART_SCENES 21          /* 0..15 presets harsh + 16 + 17..20 */
#define ART_W 132              /* janela da cena */

typedef struct {
    const float *scope; int spp;     /* osciloscopio: amostras e passo */
    float level;
    int hz; const char *sub;         /* leitura de 7 segmentos e legenda (onda / nota) */
    const char *title; int idx, total;
    int scene; int frame; int paused;
} ArtDisp;

/* cena do preset `id` na janela 132x132 em (x,y) da tela */
void art_scene(Surf *s, int x, int y, int id, int t, float lv, const float *venv);
/* painel 232x132: osciloscopio + Hz + nome do preset */
void art_display(Surf *s, int x, int y, const ArtDisp *d);
/* abertura (painel VFD de 14 segmentos escrevendo o nome): ART_INTRO_FRAMES quadros a 30/s.
 * Desenha as duas telas inteiras da abertura no quadro t (0..ART_INTRO_FRAMES-1). */
#define ART_INTRO_FRAMES 150
void art_intro(Surf *top, Surf *bot, int t);
/* cor de destaque de uma cena */
void art_accent(int id, uint8_t rgb[3]);          /* cor principal da cena (para a moldura) */
#endif
