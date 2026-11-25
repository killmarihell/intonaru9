/* Renderiza a abertura (art_intro) quadro a quadro no PC: ./build/test_intro <pasta>
 * Escreve top_NN.ppm / bot_NN.ppm e confere que nenhum quadro escreve fora das telas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ui.h"
#include "art.h"

static void save_ppm(const char *fn, const Surf *s) {
    FILE *f = fopen(fn, "wb"); if (!f) { perror(fn); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
    for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) {
        uint16_t p = s->px[x * s->h + (s->h - 1 - y)];
        fputc(((p >> 11) & 31) * 255 / 31, f); fputc(((p >> 5) & 63) * 255 / 63, f); fputc((p & 31) * 255 / 31, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "previews";
    enum { G = 64 };                                    /* guarda: 64 pixels de canario em volta de cada tela */
    static uint16_t tb[400 * 240 + 2 * G], bb[320 * 240 + 2 * G];
    Surf top = { tb + G, 400, 240 }, bot = { bb + G, 320, 240 };
    for (int i = 0; i < G; i++) { tb[i] = tb[G + 400 * 240 + i] = 0xBEEF; bb[i] = bb[G + 320 * 240 + i] = 0xBEEF; }
    char p[256]; int lit_frames = 0;
    for (int t = 0; t < ART_INTRO_FRAMES; t++) {
        art_intro(&top, &bot, t);
        for (int i = 0; i < G; i++)
            if (tb[i] != 0xBEEF || tb[G + 400 * 240 + i] != 0xBEEF || bb[i] != 0xBEEF || bb[G + 320 * 240 + i] != 0xBEEF) { printf("FALHA: escreveu fora da tela no quadro %d\n", t); return 1; }
        int bright = 0;
        for (int i = 0; i < 400 * 240; i++) if (top.px[i] > 0x4000) bright++;
        if (bright > 400) lit_frames++;
        snprintf(p, sizeof p, "%s/intro_top_%02d.ppm", out, t); save_ppm(p, &top);
        snprintf(p, sizeof p, "%s/intro_bot_%02d.ppm", out, t); save_ppm(p, &bot);
    }
    printf("intro OK: %d quadros, %d com painel aceso\n", ART_INTRO_FRAMES, lit_frames);
    return lit_frames > 40 ? 0 : 1;
}
