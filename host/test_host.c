/* Testes no PC do motor de som e da UI. NAO e usado no 3DS. */
#include "../source/synth.h"
#include "../source/ui.h"
#include "../source/amb.h"
#include "../source/rec.h"
#include "../source/art.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static void wav(const char *fn, const int16_t *m, int n, float gainfix) {
    FILE *f = fopen(fn, "wb"); uint32_t sr = SYN_RATE, br = sr * 2, ds = n * 2, rs = 36 + ds;
    uint16_t one = 1, bits = 16, ba = 2, pcm = 1; uint32_t fmt = 16;
    fwrite("RIFF",1,4,f); fwrite(&rs,4,1,f); fwrite("WAVEfmt ",1,8,f); fwrite(&fmt,4,1,f);
    fwrite(&pcm,2,1,f); fwrite(&one,2,1,f); fwrite(&sr,4,1,f); fwrite(&br,4,1,f);
    fwrite(&ba,2,1,f); fwrite(&bits,2,1,f); fwrite("data",1,4,f); fwrite(&ds,4,1,f);
    for (int i = 0; i < n; i++) { int16_t q = (int16_t)(m[i] * gainfix); fwrite(&q,2,1,f); }
    fclose(f);
}

static void save_ppm(const char *fn, Surf *s) {
    FILE *f = fopen(fn, "wb"); fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
    for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) {
        uint16_t c = s->px[x * s->h + (s->h - 1 - y)];
        uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : ".";
    char path[256];
    static Synth S; static int16_t buf[512 * 2];
    int bad = 0;

    printf("== PRESETS: 3 s cada (taxa %d Hz) ==\n", SYN_RATE);
    printf("%-11s %7s %7s %8s %6s %7s %s\n", "preset", "pico", "rms", "dc", "zcr", "noTeto", "status");
    for (int p = 0; p < N_PRESETS; p++) {
        float v[P_COUNT]; patch_default(v); patch_preset(v, p); v[P_VOL] = 0.6f;
        synth_init(&S, 0xABCD1234u + p);
        int total = SYN_RATE * 3 / 512 * 512, k = 0;
        int16_t *mono = malloc(total * 2);
        for (int b = 0; b < total / 512; b++) {
            synth_render(&S, v, 0, 0, 0, 0, buf, 512);
            for (int i = 0; i < 512; i++) mono[k++] = buf[i * 2];
        }
        double sum = 0, sq = 0; int pk = 0, zc = 0;
        for (int i = 4000; i < k; i++) {
            sum += mono[i]; sq += (double)mono[i] * mono[i];
            if (abs(mono[i]) > pk) pk = abs(mono[i]);
            if ((mono[i] < 0) != (mono[i-1] < 0)) zc++;
        }
        int atcap = 0; for (int i = 4000; i < k; i++) if (abs(mono[i]) >= pk - 40) atcap++;
        int n = k - 4000; double rms = sqrt(sq / n) / 32768.0, dc = sum / n / 32768.0;
        const char *st = "ok";
        if (pk == 0 || rms < 0.01) { st = "SILENCIO?"; bad++; }
        printf("%-11s %7.3f %7.3f %8.4f %6.3f %6.1f%% %s\n", preset_names[p], pk / 32768.0, rms, dc, (double)zc / n, 100.0 * atcap / n, st);
        float norm = pk ? 16000.0f / pk : 1.0f;
        snprintf(path, sizeof path, "%s/preview_%d_%s.wav", outdir, p + 1, preset_names[p]);
        wav(path, mono, k, norm);
        free(mono);
    }

    printf("\n== ESTRESSE: 2000 patches aleatorios/extremos, com bend/burst/mute ==\n");
    patch_seed(99);
    int fails = 0; float worst = 0; clock_t t0 = clock(); long frames = 0;
    for (int t = 0; t < 2000; t++) {
        float v[P_COUNT]; patch_default(v);
        if (t % 2) patch_randomize(v); else for (int i = 0; i < P_COUNT; i++) v[i] = (rand() & 1) ? (rand() & 1) : (rand() / (float)RAND_MAX);
        v[P_VOL] = 1.0f;
        synth_init(&S, 1000 + t);
        for (int b = 0; b < 6; b++) {
            synth_render(&S, v, (b - 3) * 0.4f, (b % 2) ? 1.f : -1.f, b == 2, b == 4, buf, 512);
            frames += 512;
            for (int i = 0; i < 1024; i++) {
                float a = fabsf(buf[i] / 32768.0f); if (a > worst) worst = a;
            }
            for (int i = 0; i < P_COUNT; i++) if (!(S.sm[i] == S.sm[i])) fails++;
            if (!(S.dc_y == S.dc_y) || !(S.gain == S.gain)) fails++;
        }
    }
    double dt = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("falhas (NaN/overflow): %d | pico maximo: %.3f | custo medio no PC: %.0f ns/amostra\n",
           fails, worst, dt / frames * 1e9);
    bad += fails;

    printf("\n== AMBIENT/DRONE 0.8: 16 presets, 5 modos do lead, acordes em tempo real ==\n");
    static Amb A; static int16_t ab[512 * 2];
    for (int pr = 0; pr < A_NPRESETS; pr++) {            /* cada preset tem que soar, sem NaN, sem estourar e SEM glide entre acordes */
        AmbPatch ap; amb_default(&ap); amb_preset(&ap, pr); ap.p[A_VOL] = 0.8f; ap.p[A_DRATE] = 0.0f;
        AmbIn in = { 0, 0.4f, 0.6f, 0, 0, 0 };
        amb_init(&A, 42 + pr);
        int tot = SYN_RATE * 20 / 512, k = 0; double sq = 0, sum = 0; int pk = 0, nan = 0; long slide = 0, voices = 0;
        int16_t *mono = malloc(sizeof(int16_t) * tot * 512);
        for (int b = 0; b < tot; b++) {
            in.touch = amb_ival(&ap, A_LMODE) == L_KEYS && (b / 20) % 2 == 0;
            amb_render(&A, &ap, &in, ab, 512);
            for (int i = 0; i < 512; i++) {
                int16_t l = ab[i * 2]; mono[k++] = l; if (b > 20) { sq += (double)l * l; sum += l; if (abs(l) > pk) pk = abs(l); } }
            for (int i = 0; i < A_COUNT; i++) if (!(A.sm[i] == A.sm[i])) nan++;
            if (!(A.gain == A.gain)) nan++;
            for (int i = 0; i < AMB_DV; i++) { AVoice *v = &A.v[i]; if (v->env > 0.02f) { voices++; if (fabsf(1200.0f * log2f(v->freq / v->tfreq)) > 15.0f) slide++; } }
        }
        double rms = sqrt(sq / (k - 20 * 512)) / 32768.0;
        const char *st = "ok";
        if (rms < 0.004) { st = "SILENCIO?"; bad++; }
        if (nan) { st = "NaN"; bad++; }
        if (slide > voices / 50) { st = "GLIDE (sirene)"; bad++; }
        printf("%-10s %-9s pico %.3f  rms %.3f  glide %ld/%ld  %s\n", amb_preset_name(pr), amb_lmode_name(amb_ival(&ap, A_LMODE)), pk / 32768.0, rms, slide, voices, st);
        if (pr < 4) { snprintf(path, sizeof path, "%s/preview_amb_%s.wav", outdir, amb_preset_name(pr)); wav(path, mono, k, 16000.0f / (pk ? pk : 1)); }
        free(mono);
    }
    {   /* os 5 modos do lead em 2 escalas, com o lead isolado */
        for (int md = 0; md < A_NLMODE; md++) for (int sc = 0; sc < 2; sc++) {
            AmbPatch ap; amb_default(&ap); ap.drone_on = 0; amb_set_index(&ap, A_LMODE, md); amb_set_index(&ap, A_SCALE, sc ? 6 : 1);
            ap.p[A_VOL] = 0.8f; ap.p[A_DENS] = 1.0f;
            AmbIn in = { md == L_KEYS, 0.4f, 0.6f, 0, 0, 0 };
            amb_init(&A, 99 + md); double sq = 0; int n = 0;
            for (int b = 0; b < SYN_RATE * 8 / 512; b++) { amb_render(&A, &ap, &in, ab, 512); if (b > 10) for (int i = 0; i < 512; i++) { sq += (double)ab[i * 2] * ab[i * 2]; n++; } }
            double rms = sqrt(sq / n) / 32768.0;
            if (rms < 0.002) { printf("lead %s escala %d: SILENCIO (rms %.4f)\n", amb_lmode_name(md), sc, rms); bad++; }
        }
        printf("5 modos do lead x 2 escalas: soam\n");
    }
    {   /* os 8 timbres do drone e do lead tem que soar */
        for (int sn = 0; sn < A_NSND; sn++) {
            AmbPatch ap; amb_default(&ap); amb_set_index(&ap, A_DSND, sn); amb_set_index(&ap, A_LSND, sn); ap.p[A_VOL] = 0.8f;
            AmbIn in = { 0, 0, 0, 0, 0, 0 }; amb_init(&A, 500 + sn);
            double sq = 0; int n = 0, nan = 0;
            for (int b = 0; b < SYN_RATE * 6 / 512; b++) {
                amb_render(&A, &ap, &in, ab, 512);
                if (b > 30) for (int i = 0; i < 512; i++) { sq += (double)ab[i * 2] * ab[i * 2]; n++; }
                if (!(A.gain == A.gain)) nan++;
            }
            double rms = sqrt(sq / (n ? n : 1)) / 32768.0;
            if (rms < 0.004 || nan) { printf("timbre %s: SILENCIO/NaN (rms %.4f)\n", amb_snd_name(sn), rms); bad++; }
        }
        printf("8 timbres drone+lead: soam\n");
    }
    {   /* ACORDES EM TEMPO REAL: pad muda o grau na hora; modificadores mudam as notas; MANUAL nao anda sozinho */
        AmbPatch ap; amb_default(&ap); amb_set_index(&ap, A_PROG, P_MANUAL); ap.lead_on = 0; ap.cgen = 5;
        AmbIn in = { 0, 0, 0, 0, 0, 0 }; amb_init(&A, 3);
        for (int b = 0; b < 40; b++) amb_render(&A, &ap, &in, ab, 512);
        int d0 = A.cur_deg_ui; for (int b = 0; b < 400; b++) amb_render(&A, &ap, &in, ab, 512);
        int still = A.cur_deg_ui == d0;
        ap.cdeg = 4; ap.cgen++; for (int b = 0; b < 6; b++) amb_render(&A, &ap, &in, ab, 512);
        int pad_ok = A.cur_deg_ui == 4;
        int o1[4], o2[4]; amb_chord_tones(&ap, 4, o1); ap.cmod = CM_7 | CM_FLIP; amb_chord_tones(&ap, 4, o2);
        int mod_ok = o1[3] != o2[3] || o1[1] != o2[1];
        char lb[12]; amb_chord_label(&ap, 0, lb, sizeof lb);
        printf("acordes em tempo real: MANUAL parado %s | pad muda grau %s | modificadores %s | rotulo do acorde 0 = %s\n", still ? "ok" : "FALHOU", pad_ok ? "ok" : "FALHOU", mod_ok ? "ok" : "FALHOU", lb);
        if (!still || !pad_ok || !mod_ok) bad++;
        AmbPatch g; amb_default(&g); amb_dice_chords(&g, 1234); int n1 = amb_plen(&g);
        AmbPatch g2 = g; amb_set_slot(&g2, 2, 5);
        printf("DICE de acordes: %d slots, slot editado -> %s\n", n1, amb_prog_name(amb_ival(&g2, A_PROG)));
        if (amb_ival(&g2, A_PROG) != P_SLOTS) bad++;
        int h = 0; for (int p = 0; p < 16; p++) h += amb_euclid_hit(5, 16, 0, p);
        printf("euclid 5/16: %d hits %s\n", h, h == 5 ? "ok" : "FALHOU"); if (h != 5) bad++;
    }
    {   /* liga/desliga das camadas: tudo desligado = silencio; analogico nao pode gerar NaN */
        AmbPatch ap; amb_default(&ap); ap.drone_on = 0; ap.lead_on = 0; AmbIn in = { 0, 0, 0, 0, 1.0f, -1.0f };
        amb_init(&A, 9); double sq = 0; int n = 0;
        for (int b = 0; b < 400; b++) { amb_render(&A, &ap, &in, ab, 512); if (b > 100) for (int i = 0; i < 512; i++) { sq += (double)ab[i * 2] * ab[i * 2]; n++; } }
        double rms = sqrt(sq / n) / 32768.0; printf("camadas desligadas: rms %.5f %s\n", rms, rms < 0.002 ? "ok" : "(AINDA SOA)");
        if (rms >= 0.002) bad++;
    }
    {   /* estresse: parametros aleatorios e extremos, pads e modificadores mudando durante o som */
        int fails = 0; clock_t t0 = clock(); long fr = 0;
        for (int t = 0; t < 600; t++) {
            AmbPatch ap; amb_default(&ap);
            for (int i = 0; i < A_COUNT; i++) ap.p[i] = (rand() & 1) ? (float)(rand() & 1) : rand() / (float)RAND_MAX;
            ap.mask = (uint16_t)rand();
            for (int i = 0; i < 8; i++) { ap.seq[i] = (int8_t)((rand() % 11) - 1); ap.prog[i] = (int8_t)((rand() % 8) - 1); }
            amb_init(&A, 7 + t);
            for (int b = 0; b < 10; b++) {
                if (b == 4) { amb_set_from_pos(&ap, A_LMODE, (rand() % 5 + .5f) / 5); ap.drone_on = rand() & 1; ap.lead_on = rand() & 1; }
                if (b % 3 == 0) { ap.cdeg = (uint8_t)(rand() % 7); ap.cmod = (uint8_t)(rand() & 31); ap.cgen++; }
                AmbIn in = { b & 1, rand() / (float)RAND_MAX, rand() / (float)RAND_MAX, b == 6, (rand() / (float)RAND_MAX) * 2 - 1, (rand() / (float)RAND_MAX) * 2 - 1 };
                amb_render(&A, &ap, &in, ab, 512); fr += 512;
                for (int i = 0; i < A_COUNT; i++) if (!(A.sm[i] == A.sm[i])) fails++;
                if (!(A.gain == A.gain) || !(A.lpl[0] == A.lpl[0]) || A.cur_deg_ui < 0 || A.cur_deg_ui > 6) fails++;
            }
        }
        double dt = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("estresse ambient: falhas %d | custo medio no PC: %.0f ns/amostra\n", fails, dt / fr * 1e9); bad += fails;
    }

    printf("\n== GRAVADOR WAV ==\n");
    {
        static int16_t ring[REC_RING_FRAMES * 2]; static Rec R; int16_t blk[512 * 2];
        snprintf(path, sizeof path, "%s", outdir);
        if (rec_open(&R, path, SYN_RATE, ring) != 0) { printf("falha ao abrir\n"); bad++; }
        else {
            long expect = 0; uint32_t crc_in = 0;
            for (int b = 0; b < 200; b++) {                    /* produtor/consumidor intercalados */
                for (int i = 0; i < 512; i++) { blk[i * 2] = (int16_t)(b * 31 + i); blk[i * 2 + 1] = (int16_t)(-(b * 17 + i)); crc_in += (uint16_t)blk[i * 2] + 3u * (uint16_t)blk[i * 2 + 1]; }
                rec_push(&R, blk, 512); expect += 512;
                if (b % 7 == 0) rec_pump(&R);
            }
            int rc = rec_close(&R);
            uint32_t rate; float secs; int wi = rec_wav_info(R.path, &rate, &secs);
            FILE *f = fopen(R.path, "rb"); uint8_t h[44]; fread(h, 1, 44, f);
            uint32_t crc_out = 0; int16_t fr2[2]; long n = 0;
            while (fread(fr2, 2, 2, f) == 2) { crc_out += (uint16_t)fr2[0] + 3u * (uint16_t)fr2[1]; n++; }
            fclose(f);
            uint32_t dsz = h[40] | h[41] << 8 | h[42] << 16 | (uint32_t)h[43] << 24;
            int ok = rc == 0 && wi == 0 && n == expect && crc_in == crc_out && dsz == (uint32_t)expect * 4 && R.dropped == 0;
            printf("%s: %ld quadros, %.3f s, rate %u, dropped %u, conteudo %s -> %s\n", R.path, n, secs, rate, R.dropped,
                   crc_in == crc_out ? "identico" : "DIFERENTE", ok ? "OK" : "FALHOU");
            remove(R.path); if (!ok) bad++;
            /* anel cheio: deve contar perdas sem corromper */
            rec_open(&R, path, SYN_RATE, ring);
            for (int b = 0; b < 80; b++) rec_push(&R, blk, 512);
            printf("anel cheio sem consumidor: dropped=%u (esperado > 0)\n", R.dropped); if (!R.dropped) bad++;
            rec_close(&R); remove(R.path);
        }
    }

    printf("\n== UI ==\n");
    static uint16_t top[400 * 240], bot[320 * 240];
    Surf st = { top, 400, 240 }, sb = { bot, 320, 240 };
    float v[P_COUNT]; patch_default(v); patch_preset(v, 2);
    synth_init(&S, 7);
    for (int b = 0; b < 8; b++) synth_render(&S, v, 0, 0, 0, 0, buf, 512);
    static float snap[SCOPE_N]; int w = S.scope_w;
    for (int i = 0; i < SCOPE_N; i++) snap[i] = S.scope[(w - SCOPE_N + i) & (SCOPE_N - 1)];
    int trig = 0; for (int i = 0; i < SCOPE_N / 2; i++) if (snap[i] < 0 && snap[i+1] >= 0) { trig = i; break; }
    static FileEnt fl[2] = {{"REC_0002.wav", 75.0f, 32728}, {"REC_0001.wav", 12.4f, 32728}};
    AmbPatch ap; amb_default(&ap); float ve[AMB_V] = {.3f,.2f,.5f,0,.1f,0,0,0,.4f,0};
    int xi, yi; patch_xy(2, &xi, &yi);
    UiState u = { .page = 0, .sub = 0, .v = v, .module = 0, .row = 1, .preset = 2, .modified = 1, .ap = &ap, .venv = ve,
                  .notes_pc = 0x205, .cur_note = 57, .level = 0.7f, .scope = snap + trig, .spp = 2, .frame = 41,
                  .xid = xi, .yid = yi, .touching = 1, .tx = .3f, .ty = .7f, .rec_on = 1, .rec_secs = 75.0f,
                  .set_v = {.35f,1,0,3,1,0}, .files = fl, .nfiles = 2 };
    /* telas de referencia (para inspecao visual) */
    for (int pg = 0; pg < PG_COUNT; pg++) for (int sub = 0; sub < 2; sub++) {
        if (sub && pg != PG_HARSH && pg != PG_AMB) continue;
        u.page = pg; u.sub = sub; u.engine = (pg == PG_AMB || pg == PG_APRE);
        ui_draw_bottom(&sb, &u); snprintf(path, sizeof path, "%s/bot_%d_%d.ppm", outdir, pg, sub); save_ppm(path, &sb);
    }
    u.page = 0; u.engine = 0; ui_draw_top(&st, &u); snprintf(path, sizeof path, "%s/top_harsh.ppm", outdir); save_ppm(path, &st);
    u.engine = 1; ui_draw_top(&st, &u); snprintf(path, sizeof path, "%s/top_amb.ppm", outdir); save_ppm(path, &st);
    ui_draw_error(&sb, 0); snprintf(path, sizeof path, "%s/err.ppm", outdir); save_ppm(path, &sb);

    /* BUG VISUAL: nenhum visual pode escrever fora da janela 132x132 (x 8..140, y 26..158) */
    {
        int leaks = 0;
        for (int id = 0; id < ART_SCENES; id++) for (int t = 0; t < 400; t += 7) for (int lvi = 0; lvi < 3; lvi++) {
            static uint16_t sent[400 * 240]; for (int i = 0; i < 400 * 240; i++) sent[i] = 0x1234;
            Surf ss = { sent, 400, 240 }; ui_vis_test(&ss, id, t, lvi * 0.5f);
            for (int x = 0; x < 400; x++) for (int y = 0; y < 240; y++) {
                int inside = (x >= 8 && x < 140 && y >= 26 && y < 158);
                if (!inside && sent[x * 240 + (239 - y)] != 0x1234) { leaks++; break; }
            }
        }
        printf("visuais fora da janela (vazamentos de desenho): %d\n", leaks); bad += leaks;
    }
    /* todas as combinacoes de pagina nao podem escrever fora do framebuffer (guarda de memoria) */
    {
        static uint16_t guard[(320 * 240) + 2 * 4096]; int over = 0;
        for (int pg = 0; pg < PG_COUNT; pg++) for (int sub = 0; sub < 2; sub++) for (int pr = -1; pr < N_PRESETS; pr++) {
            for (size_t i = 0; i < sizeof guard / 2; i++) guard[i] = 0xABCD;
            Surf sg = { guard + 4096, 320, 240 };
            u.page = pg; u.sub = sub; u.preset = pr; u.module = pr < 0 ? 0 : pr % N_MODULES; u.engine = (pg == PG_AMB || pg == PG_APRE); u.pane = pr & 1; u.holdy = pr * 5;
            if (u.engine && pr >= 0 && (pr & 2)) u.module = pr % A_NGROUPS;
            ui_draw_bottom(&sg, &u);
            for (int i = 0; i < 4096; i++) if (guard[i] != 0xABCD || guard[4096 + 320 * 240 + i] != 0xABCD) over++;
            static uint16_t g2[400 * 240 + 2 * 4096];
            for (size_t i = 0; i < sizeof g2 / 2; i++) g2[i] = 0xABCD;
            Surf s2 = { g2 + 4096, 400, 240 }; u.frame = pr * 13 + 5; ui_draw_top(&s2, &u);
            for (int i = 0; i < 4096; i++) if (g2[i] != 0xABCD || g2[4096 + 400 * 240 + i] != 0xABCD) over++;
        }
        printf("escritas fora do framebuffer: %d\n", over); bad += over;
    }
    /* telas dos 16 presets (miniaturas) */
    for (int pr = 0; pr < N_PRESETS; pr++) {
        float vv[P_COUNT]; patch_default(vv); patch_preset(vv, pr);
        u.v = vv; u.page = 0; u.sub = 0; u.engine = 0; u.preset = pr; u.modified = 0; u.frame = 47; u.level = 0.55f;
        ui_draw_top(&st, &u); snprintf(path, sizeof path, "%s/vis_%d.ppm", outdir, pr); save_ppm(path, &st);
    }
    u.v = v;
    for (int sc = 0; sc < 4; sc++) {
        u.page = PG_AMB; u.engine = 1; u.apreset = sc * 4; u.frame = 61 + sc * 9; u.level = 0.5f; ap.scene = (uint8_t)sc; amb_preset(&ap, sc * 4);
        ui_draw_top(&st, &u); snprintf(path, sizeof path, "%s/vis_amb%d.ppm", outdir, sc); save_ppm(path, &st);
    }
    amb_default(&ap); u.engine = 0; u.page = PG_HARSH;

    /* telas extras: ambient em cada modo do lead (pane LEAD), pane DRONE com acordes/modificadores, presets de ambient */
    u.cur_deg = 3; u.cur_slot = 1; u.step_ui = 2; u.nsteps_ui = 8; ap.cmod = CM_7;
    for (int md = 0; md < A_NLMODE; md++) {
        amb_set_index(&ap, A_LMODE, md); u.page = PG_AMB; u.sub = 0; u.engine = 1; u.touching = md == L_KEYS; u.pane = 1;
        ui_draw_bottom(&sb, &u); snprintf(path, sizeof path, "%s/bot_amb_lead%d.ppm", outdir, md); save_ppm(path, &sb);
    }
    u.pane = 0; u.touching = 0; u.write_on = 1;
    ui_draw_bottom(&sb, &u); snprintf(path, sizeof path, "%s/bot_amb_drone.ppm", outdir); save_ppm(path, &sb);
    u.write_on = 0; ap.cmod = 0;
    u.page = PG_APRE; u.apreset = 4; ui_draw_bottom(&sb, &u); snprintf(path, sizeof path, "%s/bot_apre.ppm", outdir); save_ppm(path, &sb);
    u.pane = 0; u.page = PG_AMB; u.apreset = 0;
    amb_set_index(&ap, A_LMODE, L_DRIFT); u.touching = 0;
    for (int g = 0; g < A_NGROUPS; g++) {
        u.page = PG_AMB; u.sub = 1; u.module = g; u.row = 1;
        ui_draw_bottom(&sb, &u); snprintf(path, sizeof path, "%s/bot_amb_edit%d.ppm", outdir, g); save_ppm(path, &sb);
    }
    for (int m = 0; m < N_MODULES; m++) {
        u.page = PG_HARSH; u.sub = 1; u.module = m; u.row = 0;
        ui_draw_bottom(&sb, &u); snprintf(path, sizeof path, "%s/bot_harsh_edit%d.ppm", outdir, m); save_ppm(path, &sb);
    }
    u.module = 0; u.engine = 0;
    {
        static FileEnt fl2[3] = {{"A_VERY_LONG_RECORDING_NAME_X.wav", 75.0f, 32728}, {"REC_0001.wav", 12.4f, 32728}, {"NOISE-2.wav", 3.2f, 32728}};
        u.page = PG_FILES; u.sub = 0; u.files = fl2; u.nfiles = 3; u.fsel = 0; u.del_arm = 1;
        ui_draw_bottom(&sb, &u); snprintf(path, sizeof path, "%s/bot_files_armed.ppm", outdir); save_ppm(path, &sb);
        /* TEXTO NAO PODE VAZAR: entre o fim do nome (x 210) e a duracao (x >= 262) a linha precisa ficar lisa */
        int leak = 0; uint16_t ref = sb.px[230 * 240 + (239 - 50)];
        for (int x = 214; x < 262; x++) for (int y = 47; y < 63; y++) if (sb.px[x * 240 + (239 - y)] != ref) leak++;
        printf("texto da lista de arquivos vazando da coluna: %d px\n", leak); bad += leak;
        u.files = fl; u.nfiles = 2; u.del_arm = 0;
    }
    u.page = PG_HARSH; u.sub = 0; u.module = 0; u.preset = 2;

    /* toque: cada controle responde onde o desenho esta */
    {
        int fails = 0; UiHit h;
        #define EXPECT(cond, msg) do { if (!(cond)) { fails++; printf("  falhou: %s\n", msg); } } while (0)
        u.page = PG_HARSH; u.sub = 0; u.ap = &ap;
        h = ui_hit(&u, 106, 100); EXPECT(h.kind == H_PAD && h.fx > 0.4f && h.fx < 0.6f, "pad harsh centro");
        h = ui_hit(&u, 6, 169);   EXPECT(h.kind == H_PAD && h.fy < 0.02f, "pad harsh canto");
        h = ui_hit(&u, 236, 45);  EXPECT(h.kind == H_PREV, "prev");
        h = ui_hit(&u, 290, 45);  EXPECT(h.kind == H_NEXT, "next");
        h = ui_hit(&u, 260, 100); EXPECT(h.kind == H_MUTATE, "mutate");
        h = ui_hit(&u, 260, 130); EXPECT(h.kind == H_RANDOM, "random");
        h = ui_hit(&u, 260, 158); EXPECT(h.kind == H_EDIT, "edit");
        h = ui_hit(&u, 170, 185); EXPECT(h.kind == H_VOL && h.fx > 0.4f && h.fx < 0.5f, "volume");
        h = ui_hit(&u, 300, 12);  EXPECT(h.kind == H_REC, "rec");
        h = ui_hit(&u, 260, 12);  EXPECT(h.kind == H_PAUSE, "pause");
        h = ui_hit(&u, 100, 10);  EXPECT(h.kind == H_TAB && h.arg == 2, "aba amb");
        h = ui_hit(&u, 60, 10);   EXPECT(h.kind == H_TAB && h.arg == 1, "aba presets de noise logo depois do harsh");
        h = ui_hit(&u, 140, 10);  EXPECT(h.kind == H_TAB && h.arg == 3, "aba presets de ambient depois do ambient");
        h = ui_hit(&u, 220, 10);  EXPECT(h.kind == H_TAB && h.arg == 5, "aba set");
        /* editor harsh */
        u.sub = 1; u.module = 0;
        h = ui_hit(&u, 90, 40);   EXPECT(h.kind == H_MODTILE && h.arg == 1, "tile modulo 1");
        h = ui_hit(&u, 230, 80);  EXPECT(h.kind == H_PILL && h.arg == P_WAVE && h.arg2 == 2, "WAVE = pills");
        h = ui_hit(&u, 150, 102); EXPECT(h.kind == H_SLIDER && h.arg == P_PITCH, "PITCH = slider");
        u.module = 1;
        h = ui_hit(&u, 300, 80);  EXPECT(h.kind == H_STEPR && h.arg == P_NTYPE, "NOISE TYPE (5 opcoes) = seta");
        h = ui_hit(&u, 40, 80);   EXPECT(h.kind == H_NONE, "toque no rotulo nao e controle");
        /* ambient: tocar - camada DRONE (acordes em tempo real) */
        u.page = PG_AMB; u.sub = 0; u.module = 0; u.pane = 0; amb_set_index(&ap, A_LMODE, L_DRIFT); ap.lead_on = 1; amb_set_index(&ap, A_SCALE, 1);
        h = ui_hit(&u, 50, 40);   EXPECT(h.kind == H_DRONE, "camada drone");
        h = ui_hit(&u, 150, 40);  EXPECT(h.kind == H_LEAD, "camada lead");
        h = ui_hit(&u, 25, 65);   EXPECT(h.kind == H_TIMB && h.arg == 0, "chip de timbre 0 (sempre visivel)");
        h = ui_hit(&u, 290, 65);  EXPECT(h.kind == H_TIMB && h.arg == 7, "chip de timbre 7");
        h = ui_hit(&u, 20, 100);  EXPECT(h.kind == H_CPAD && h.arg == 0, "pad de acorde I");
        h = ui_hit(&u, 300, 100); EXPECT(h.kind == H_CPAD && h.arg == 6, "pad de acorde VII");
        h = ui_hit(&u, 20, 142);  EXPECT(h.kind == H_CMOD && h.arg == CM_7, "modificador 7TH");
        h = ui_hit(&u, 290, 142); EXPECT(h.kind == H_CMOD && h.arg == CM_INV, "modificador INV");
        h = ui_hit(&u, 20, 175);  EXPECT(h.kind == H_CSLOT && h.arg == 0, "slot de acorde 1");
        h = ui_hit(&u, 290, 175); EXPECT(h.kind == H_CSLOT && h.arg == 7, "slot de acorde 8");
        h = ui_hit(&u, 235, 40);  EXPECT(h.kind == H_CWRITE, "WRITE");
        h = ui_hit(&u, 290, 40);  EXPECT(h.kind == H_CDICE, "DICE acordes");
        /* camada LEAD: modos sao abas, cada um com seu controle */
        u.pane = 1;
        h = ui_hit(&u, 235, 40);  EXPECT(h.kind == H_LDICE, "DICE lead");
        h = ui_hit(&u, 290, 40);  EXPECT(h.kind == H_LXTRA, "extra do modo");
        h = ui_hit(&u, 30, 88);   EXPECT(h.kind == H_LMODE && h.arg == 0, "aba DRIFT");
        h = ui_hit(&u, 280, 88);  EXPECT(h.kind == H_LMODE && h.arg == 4, "aba KEYS");
        h = ui_hit(&u, 100, 130); EXPECT(h.kind == H_APAD, "pad do DRIFT");
        h = ui_hit(&u, 15, 190);  EXPECT(h.kind == H_KEY && h.arg == 0, "tecla de nota 0");
        h = ui_hit(&u, 300, 190); EXPECT(h.kind == H_KEY && h.arg == 11, "tecla de nota 11");
        amb_set_index(&ap, A_LMODE, L_ARP);
        h = ui_hit(&u, 40, 110);  EXPECT(h.kind == H_PILL && h.arg == A_LDIR && h.arg2 == 0, "ARP: direcao UP");
        h = ui_hit(&u, 290, 110); EXPECT(h.kind == H_PILL && h.arg == A_LDIR && h.arg2 == 4, "ARP: direcao WALK");
        h = ui_hit(&u, 100, 150); EXPECT(h.kind == H_APAD, "pad do ARP");
        amb_set_index(&ap, A_LMODE, L_SEQ);
        h = ui_hit(&u, 100, 110); EXPECT(h.kind == H_SEQCELL && h.arg == 2 && h.arg2 == 9, "SEQ: passo 3, nota alta");
        h = ui_hit(&u, 100, 199); EXPECT(h.kind == H_SEQCELL && h.arg == 2 && h.arg2 == -1, "SEQ: linha de pausa");
        { int st, vv; EXPECT(ui_seq_cell(6, 102, &st, &vv) && st == 0 && vv == 9, "ui_seq_cell canto"); EXPECT(!ui_seq_cell(5, 102, &st, &vv), "ui_seq_cell fora"); }
        amb_set_index(&ap, A_LMODE, L_EUCL);
        h = ui_hit(&u, 200, 112); EXPECT(h.kind == H_EPAR && h.arg == A_EHITS, "EUCLID: HITS");
        h = ui_hit(&u, 200, 160); EXPECT(h.kind == H_EPAR && h.arg == A_EROT, "EUCLID: ROTATE");
        h = ui_hit(&u, 280, 190); EXPECT(h.kind == H_PILL && h.arg == A_ECONT && h.arg2 == 4, "EUCLID: contorno RND");
        amb_set_index(&ap, A_LMODE, L_KEYS);
        h = ui_hit(&u, 100, 130); EXPECT(h.kind == H_APAD, "KEYS: teclado");
        ap.lead_on = 1; amb_set_index(&ap, A_LMODE, L_DRIFT); u.pane = 0;
        /* ambient: editor */
        u.sub = 1; u.module = 0;
        h = ui_hit(&u, 110, 40);  EXPECT(h.kind == H_MODTILE && h.arg == 1, "grupo LEAD");
        h = ui_hit(&u, 300, 80);  EXPECT(h.kind == H_STEPR && h.arg == A_DSND, "som do drone = seta");
        u.module = 1;
        h = ui_hit(&u, 200, 124); EXPECT(h.kind == H_PILL && h.arg == A_LOCT && h.arg2 == 1, "REGISTER (3 opcoes) = pills");
        h = ui_hit(&u, 150, 146); EXPECT(h.kind == H_SLIDER && h.arg == A_SPEED, "SPEED = slider");
        /* regra geral: todo parametro tem o controle certo, e a linha cabe na tela */
        for (int m = 0; m < N_MODULES; m++) {
            u.page = PG_HARSH; u.sub = 1; u.module = m;
            for (int r = 0; r < modules[m].count; r++) {
                int id = modules[m].first + r, n = param_discrete(id), y = 70 + r * 22 + 10;
                int xs = n == 0 ? 150 : (n <= 4 ? 100 + 0 : 300); if (n >= 2 && n <= 4) xs = 94;
                int want = n == 0 ? H_SLIDER : (n <= 4 ? H_PILL : H_STEPR);
                h = ui_hit(&u, xs, y);
                if (h.kind != want || h.arg != id || y + 10 > 240) { fails++; printf("  harsh %s: controle errado (kind %d)\n", param_name(id), h.kind); }
            }
        }
        for (int g = 0; g < A_NGROUPS; g++) {
            u.page = PG_AMB; u.sub = 1; u.module = g;
            for (int r = 0; r < amb_groups[g].count; r++) {
                int id = amb_groups[g].first + r, n = amb_discrete(id), y = 70 + r * 22 + 10;
                int xs = n == 0 ? 150 : (n <= 4 ? 94 : 300);
                int want = n == 0 ? H_SLIDER : (n <= 4 ? H_PILL : H_STEPR);
                h = ui_hit(&u, xs, y);
                if (h.kind != want || h.arg != id || y + 10 > 240) { fails++; printf("  amb %s: controle errado (kind %d)\n", amb_pname(id), h.kind); }
            }
        }
        u.page = PG_HPRE; h = ui_hit(&u, 120, 150); EXPECT(h.kind == H_PRESET && h.arg == 9, "preset 9");
        u.page = PG_APRE; h = ui_hit(&u, 120, 150); EXPECT(h.kind == H_APRE && h.arg == 9, "preset de ambient 9");
        u.page = PG_FILES; u.nfiles = 2; u.fscroll = 0;
        h = ui_hit(&u, 100, 55);  EXPECT(h.kind == H_FILE && h.arg == 0, "arquivo 0");
        h = ui_hit(&u, 100, 75);  EXPECT(h.kind == H_FILE && h.arg == 1, "arquivo 1");
        h = ui_hit(&u, 100, 95);  EXPECT(h.kind == H_NONE, "linha sem arquivo");
        h = ui_hit(&u, 30, 220);  EXPECT(h.kind == H_FUP, "arquivos: sobe");
        h = ui_hit(&u, 90, 220);  EXPECT(h.kind == H_FDN, "arquivos: desce");
        h = ui_hit(&u, 150, 220); EXPECT(h.kind == H_REN, "renomear");
        h = ui_hit(&u, 260, 220); EXPECT(h.kind == H_DEL, "apagar");
        u.page = PG_SET;
        h = ui_hit(&u, 150, 44);  EXPECT(h.kind == H_SLIDER && h.arg == 0, "SMOOTH slider");
        h = ui_hit(&u, 250, 100); EXPECT(h.kind == H_PILL && h.arg == 2 && h.arg2 == 1, "LITE ON");
        h = ui_hit(&u, 40, 128);  EXPECT(h.kind == H_NONE, "rotulo STICKS");
        h = ui_hit(&u, 300, 128); EXPECT(h.kind == H_PILL && h.arg == 3 && h.arg2 == 3, "STICKS: BOTH");
        h = ui_hit(&u, 200, 156); EXPECT(h.kind == H_PILL && h.arg == 4 && h.arg2 == 1, "HOLD Y: 2 S");
        h = ui_hit(&u, 300, 184); EXPECT(h.kind == H_PILL && h.arg == 5 && h.arg2 == 3, "AUTO MUT: 40 S");
        /* nenhum pixel da tela devolve um hit invalido */
        for (int pg = 0; pg < PG_COUNT; pg++) for (int sb2 = 0; sb2 < 2; sb2++) for (int md = 0; md < A_NLMODE; md++) for (int pn = 0; pn < 2; pn++) {
            u.page = pg; u.sub = sb2; u.module = md % 4; u.pane = pn; amb_set_index(&ap, A_LMODE, md);
            for (int x = 0; x < 320; x++) for (int y = 0; y < 240; y++) {
                h = ui_hit(&u, x, y);
                if (h.kind < 0 || h.kind > H_LDIR || h.fx < 0 || h.fx > 1 || h.fy < 0 || h.fy > 1) fails++;
                if ((h.kind == H_KEY && (h.arg < 0 || h.arg > 11)) || (h.kind == H_SEQCELL && (h.arg < 0 || h.arg > 7 || h.arg2 < -1 || h.arg2 > 9)) || (h.kind == H_CPAD && (h.arg < 0 || h.arg >= amb_nd(&ap))) || (h.kind == H_CSLOT && (h.arg < 0 || h.arg >= AMB_PSLOTS)) || (h.kind == H_TIMB && (h.arg < 0 || h.arg >= A_NSND)) || (h.kind == H_APRE && (h.arg < 0 || h.arg >= A_NPRESETS)) ||
                    (h.kind == H_PRESET && (h.arg < 0 || h.arg >= N_PRESETS)) || (h.kind == H_FILE && (h.arg < 0 || h.arg >= u.nfiles))) fails++;
            }
        }
        printf("mapa de toque: %d falhas\n", fails); bad += fails;
    }
    printf("\nRESULTADO: %s\n", bad ? "PROBLEMAS" : "TUDO OK");
    return bad != 0;
}
