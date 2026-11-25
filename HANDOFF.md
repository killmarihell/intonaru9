# HANDOFF — INTONARUMORI (antes VFDHARSHCORE) 0.9 (CONCLUÍDO)

Base: 0.7 (zip original do usuário). Referências clonadas (só leitura): karma-fm1 v3.1.6, ChoralRootFM1 v0.46, JIANT.
Toolchain: sem devkitARM aqui. Validação = `make host` (motor+UI+arte+gravador no PC) + checagem de sintaxe de main.c com host/stub_3ds.h.

## Pedidos do usuário (checklist)
- [x] Etapa 1 — amb.c/amb.h reescritos: crossfade sem glide (era a "sirene": 93% dos blocos de voz fora de afinação na 0.7 → 0% na 0.8), voice leading, pads de acorde em tempo real + modificadores (7/9/SUS/FLIP/INV), progressão MANUAL/SLOTS/prontas/GENERATE, lead DRIFT/ARP(DIR)/SEQ/EUCLID/KEYS(+HOLD), eco ping-pong, 16 presets de ambiente (amb_preset), dice (amb_dice_chords/lead).
- [x] Etapa 2 (FEITA; make host passa com ASAN/UBSAN; main.c passa -fsyntax-only com shim de 3ds.h) — ui.c/ui.h/main.c: 6 abas (HARSH, H-PRE, AMB, A-PRE, FILES, SET), PAUSE/REC viram ícones; analógico (circle pad + C-stick, SET: STICKS), Y segurado 2–3 s = PAUSE, tela AMB com pads de acorde + chips de timbre sempre visíveis + abas de modo do lead; página de presets de ambiente; SET novo (STICKS, HOLD-Y, AUTO-MUT).
- [x] Etapa 3 (FEITA) — art.c: VOID e AMBIENT refeitos no estilo segmentos Game&Watch (ghost/halo/core); 4 cenas ambient (mar, chuva, deserto, espaço); anunciadores LCD (PL/RC/PA/MU/BU) no cabeçalho.
- [x] Etapa 4 (FEITA) — host/test_host.c atualizado, README 0.8, build.yml (nome do artefato 0.8), zip final.

## Abertura (0.9, feita depois do fechamento)
- `art_intro(top, bot, t)` em art.c (+ `ART_INTRO_FRAMES` 90 em art.h), chamada em main.c logo antes do loop principal; tempo por osGetTime (30 quadros/s), pula com qualquer tecla/toque apos o quadro 12. Audio: `g_pause=1; pgain=0` ANTES do pre-enchimento da fila e g_pause NAO e zerado ao fim da abertura: o app comeca pausado/mudo e so toca quando o usuario aperta PLAY.
- Painel 12 celulas x 14 segmentos (glifos I N T O A R U M, ordem de tracos em `in_ord`), 2 regioes de 232 px apresentadas em colunas [0,200) e [200,400) para caber no FB 232x132 sem mexer em FB_W. Cronograma (30 q/s, 150 quadros = 5 s; IN_GL=130 e IN_END=146 em art.c controlam a saida): 0-4 off | 5-8 lampadas | 12-51 escrita | 52-129 segura (legenda, versao, credito MADE BY MJCDN digitando, feixe ida 54-68 e volta 100-114) | 130-145 glitch | 146-149 off.
- Nome/versao removidos do cabecalho de ui_draw_top (REC/HOLD Y foram para x=8). A versao 0.9 esta hardcoded na abertura ("V0.9" em in_band) e no rodape da pagina SET: trocar os dois ao subir de versao.
- Teste: `make intro` (host/test_intro.c, 150 quadros: ASAN/UBSAN, canario fora das telas, >40 quadros acesos).

## Notas de design
- Ordem de abas pedida: noise → presets de noise → ambient → presets de ambient → FILES → SET.
- Cena ART: renumerar (0..15 harsh, 16 blank/cassete, 17..20 ambient).
- Limites de layout (tela baixo 320x240): linhas do editor ROW0=70, ROW_H=22 (máx 7 linhas por grupo).
- Pasta de trabalho: /home/claude/v08/intonarumori ; original intocado em /home/claude/v07.

## Como continuar (se a sessão cair)
1. Descompacte o backup mais recente, entre em `intonarumori/`, rode `make host` (deve dar TUDO OK). Checar main.c: criar um 3ds.h que inclui host/stub_3ds.h e rodar `gcc -fsyntax-only -I<dir> -Isource source/main.c`.
2. API nova do motor (amb.h): AmbPatch tem `cdeg`, `cmod` (CM_7/9/SUS/FLIP/INV), `cgen` (++ a cada toque num pad), `prog[8]`, `seq[8]`, `scene`. Modos do lead: L_DRIFT, L_ARP, L_SEQ, L_EUCL, L_KEYS, L_OFF(=5, só UI). Grupos do editor: DRONE(7) LEAD(6) PLAY(7) MIX(7). Estado para a UI: `amb.cur_deg_ui`, `step_ui`, `nsteps_ui`, `hit_ui` (contador). Helpers: amb_chord_label, amb_roman, amb_nd, amb_plen, amb_set_slot, amb_write_chord, amb_dice_chords, amb_dice_lead, amb_preset(n), amb_preset_name, amb_preset_scene.
3. O que fazer na UI/main: ver checklist acima (Etapas 2–4).
4. Prova de que a sirene sumiu: scratchpad/t/spec_cmp.png (espectrograma 0.7 vs 0.8).
