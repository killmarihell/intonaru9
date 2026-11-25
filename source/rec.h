/* INTONARUMORI - gravador WAV em fluxo (anel de memoria -> arquivo no SD). */
#ifndef REC_H
#define REC_H
#include <stdint.h>

#define REC_RING_FRAMES 32768     /* ~1 s de audio (128 KB) */

typedef struct {
    void *fp;                      /* FILE* */
    int16_t *ring;                 /* quadros estereo intercalados */
    volatile uint32_t wr, rd;      /* indices crescentes (em quadros) */
    volatile int active;           /* 1 = audio thread pode empurrar */
    volatile uint32_t dropped;     /* quadros perdidos por anel cheio */
    uint32_t frames;               /* quadros ja escritos no arquivo */
    uint32_t rate;
    int error;                     /* != 0 se escrita falhou */
    char path[96];
} Rec;

int  rec_open(Rec *r, const char *dir, uint32_t rate, int16_t *ring_mem);  /* cria REC_nnnn.wav */
void rec_push(Rec *r, const int16_t *st, int frames);  /* thread de audio */
int  rec_pump(Rec *r);                                  /* thread de escrita; devolve quadros gravados */
int  rec_close(Rec *r);                                 /* esvazia, corrige cabecalho, fecha */
float rec_seconds(const Rec *r);
int  rec_wav_info(const char *path, uint32_t *rate, float *secs);   /* le cabecalho */
#endif
