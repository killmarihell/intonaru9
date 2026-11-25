# INTONARUMORI 0.9

Instrumento de harsh noise + ambient/drone para **Old 3DS com Luma3DS** (toque + botões, gravação WAV no SD).

## Gerar o .3dsx
1. Suba **todo o conteúdo desta pasta** (inclusive `.github`) para um repositório GitHub.
2. Aba **Actions** → espere o job *Compilar INTONARUMORI 3DS* ficar verde.
3. Baixe o artefato `INTONARUMORI_0.9_para_cartao_SD`.
4. Copie a pasta `3ds` para a raiz do SD: `SD:/3ds/intonarumori/intonarumori.3dsx` e abra pelo Homebrew Launcher.

## Obrigatório no 3DS
`SD:/3ds/dspfirm.cdc` (Rosalina: L + ↓ + SELECT → Miscellaneous options → Dump DSP firmware). Sem ele o app mostra tela de erro.

## Novidades da 0.9
- **Abertura animada** (~5 s, painel VFD de 14 segmentos): teste de lâmpadas, o nome escrito traço a traço com barra de carga âmbar, legenda, versão, crédito "MADE BY MJCDN" digitando, uma pausa com o nome inteiro, feixe de brilho e saída em glitch de harsh noise; a tela de baixo vira um analisador de 16 colunas. **Qualquer botão ou toque pula** (depois de 0,4 s). **O app começa em silêncio (pausado) e só toca quando você aperta PLAY** (ícone na tela, ZL/ZR ou segurar Y). Como o nome aparece só ali, **saiu do cabeçalho** da tela de cima (sobraram só os anunciadores; REC e HOLD Y passam para o canto esquerdo).
- **Novo nome: INTONARUMORI** (pasta no SD: `SD:/3ds/intonarumori/`; gravações em `.../rec/`).
- **6 abas**: HARSH · H-PRE · AMB · A-PRE · FILES · SET. PAUSE e REC viram ícones.
- **Ambient reescrito**: crossfade sem glide (acabou a "sirene"), voice leading, **pads de acorde** em tempo real com modificadores (7 / 9 / SUS / FLIP / INV), progressão MANUAL / SLOTS / prontas / GENERATE.
- **Lead**: DRIFT · ARP (com direção) · STEP SEQ · EUCLID · KEYS (+HOLD), eco ping-pong, **16 presets de ambiente** e dados (B sorteia acordes / notas).
- **Analógico** (circle pad + C-stick, ligável em SET: STICKS). **Y segurado 2–3 s = PAUSE**. SET ganhou STICKS, HOLD-Y e AUTO-MUT.
- **Arte em segmentos estilo Game&Watch** (fantasma / halo / núcleo): VOID refeita e 4 cenas de ambient — **mar, chuva, deserto, espaço**.
- **Anunciadores LCD** no cabeçalho: PL (tocando) · RC (gravando) · PA (pausa) · MU (mute) · BU (burst) · AM/HR (motor).

## Controles
- **L / R**: troca de página · **START**: sair · **SELECT**: alterna TOCAR ↔ EDITAR
- **Toque**: abas, pad XY, controles, **PAUSE**, **REC** (grava em `SD:/3ds/intonarumori/rec/REC_nnnn.wav`)
- **HARSH**: ←→ preset, ↑↓ volume, A mutate, B random, X burst (segurar), Y mute (segurar)
- **AMB**: ←→ modo do lead, A próximo acorde, B dados, X troca de painel (DRONE/LEAD), Y mute. No modo KEYBOARD o pad toca o lead; em STEP SEQ toque nos 8 passos para mudar a nota (– = pausa)
- **FILES**: toque num arquivo para escolher · RENAME (ou A) · DELETE (toque duas vezes)

⚠️ Harsh noise é intenso: comece com o volume baixo.

## Estrutura
`source/` código (synth, amb, rec, ui, art, gfx, main) · `Makefile` · `icon.png` · `host/` testes no PC (`make host`; `make intro` renderiza a abertura) · `.github/workflows/build.yml` build automático.

## Créditos
O motor gráfico anti-aliased (`gfx.c`, `gfx.h`, `mathx.h`) é portado do projeto **h1** (MIT) — licença em `source/LICENSE-h1.txt`.
