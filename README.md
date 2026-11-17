# Slither PSP Homebrew — protótipo inicial

Este é um **projeto-fonte experimental**, não uma ISO pronta. Ele usa o PSPSDK
e gera um `EBOOT.PBP` para executar como homebrew em um PSP compatível.

## O que já está no protótipo
- Arena 2D vista de cima e câmera acompanhando a cobra do jogador.
- Cobra segmentada, bolinhas para coletar e crescimento.
- Sete cobras controladas por bots com movimento simples.
- Analógico esquerdo para direcionar.
- START para sair e SELECT para reiniciar a partida.

## O que ainda falta para ficar mais próximo do Slither.io
- Menu inicial, nome do jogador, placar e ranking desenhados na tela.
- IA mais esperta, efeitos sonoros e animações mais polidas.
- Tela de game over e balanceamento de velocidade/colisões.
- Testes em hardware real e ajustes de desempenho.

## Compilar
1. Instale o **PSPSDK** em um computador compatível.
2. Abra um terminal nesta pasta.
3. Execute `make`.
4. O resultado esperado é `EBOOT.PBP`.
5. Copie a pasta do jogo para `PSP/GAME/SLITHER/` no Memory Stick.

Não há uma ISO incluída: homebrew de PSP normalmente é distribuído como
`EBOOT.PBP`. Converter isso em ISO não é necessário para executar pelo menu
Game do PSP e exige um processo separado de empacotamento.

## Controles
- Analógico esquerdo: virar.
- START: sair.
- SELECT: reiniciar.

## Observação
Este é um projeto independente inspirado na mecânica de cobra e bolinhas.
Não inclui código, imagens, sons, servidores ou recursos proprietários do
Slither.io original, e portanto não é uma cópia idêntica do jogo comercial.
O código ainda precisa ser compilado e testado com a versão do PSPSDK usada.


## Build in GitHub Actions (without a PC)
This repository includes `.github/workflows/build.yml`. After uploading all files
to a GitHub repository on the `main` branch, open the **Actions** tab and run
**Build Slither PSP** (or push a commit). When it succeeds, open the workflow run,
scroll to **Artifacts**, and download `slither-psp-homebrew`. The artifact contains
`EBOOT.PBP`.

The workflow uses the official PSPDEV container image documented by PSPDEV.
