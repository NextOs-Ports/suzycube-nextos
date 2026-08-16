# Suzy Cube — port universal AArch64 / universal AArch64 port

[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

**Idioma / Language:** [Português](#português) · [English](#english)

Este repositório contém um loader de compatibilidade independente e o pacote
PortMaster BYO-data. Ele não contém nem redistribui APK, OBB, bibliotecas Unity,
arte, música ou qualquer outro dado proprietário de Suzy Cube.

This repository contains an independent compatibility loader and a BYO-data
PortMaster package. It does not contain or redistribute Suzy Cube's APK, OBB,
Unity libraries, artwork, music, or any other proprietary game data.

## Português

### Visão geral e estado

O port executa os objetos ARM64 originais do Suzy Cube 1.0.13 (Unity
2017.4.40f1 IL2CPP) diretamente no Linux AArch64. Não há emulação de CPU nem
atalho no boot: o loader preserva `init_array`, `JNI_OnLoad` e o ciclo nativo
`initJni` → `nativeRecreateGfxState` → `nativeResume` → `nativeRender`.

A linha 1.1.12 reconcilia todos os fixes de input das versões 1.1.10 e 1.1.11 e
migra launcher, metadata e empacotamento para uma fonte declarativa única. A
alegação de suporte físico da versão candidata só é promovida depois que o ZIP
exato e seu SHA-256 passam pela matriz de aparelhos; resultados de releases
anteriores permanecem apenas como baseline histórico.

![Gameplay nativo](media/screenshot.png)

![Seleção de save com controle físico](media/menu.png)

### Arquitetura

- `src/`: loader ELF, bridges Bionic/JNI/EGL/áudio/input e loop Unity.
- `nxproject.json`: fonte declarativa de `nxport.json`, `port.json`, launcher,
  NXExtract e NXSplash.
- `extractor.json`: receita transacional que valida package ID, ABI, estrutura
  e payloads internos antes de publicar os dados.
- `FRAMEWORK-BUILD-PIN.json`: fixa por commit e digest as árvores de
  `nxcompat 0.2.1`, `nxinput 0.3.1` e `nxaudio 0.2.0` usadas no build.
- `nxrelease.json`: inventário completo, hashes, dependências, licença e teto
  público `GLIBC_2.30`.

O launcher autocontido é gerado por `nxbootstrap 0.6.15`. A instalação usa o
engine NXExtract 1.2.10 com a interface gráfica imutável 1.2.9; a tela NEXT OS
de cinco segundos é o NXSplash 0.1.2 separado. O fluxo público é:

```text
PortMaster → NXExtract (quando necessário) → validação dos dados
           → NXSplash → loader → Unity nativa
```

### Problemas resolvidos

- Dados divididos entre APK e OBB, inclusive arquivos `.splitN`, são unidos em
  ordem numérica e publicados somente após validação completa.
- O D-pad envia nível contínuo; sticks usam deadzone radial com histerese
  0,40/0,30; gatilhos respeitam o repouso SDL; perda de foco e hotplug enviam
  estado neutro.
- A posição dos botões de face só é normalizada quando o mapeamento invertido é
  comprovado. O port não rouba D-pad, botão sul ou analógico direito para um
  cursor.
- A metadata PortMaster v4 declara `runtime` como lista explícita e passa pelo
  parser real e pelo ciclo offline instalar/atualizar/remover/reinstalar.
- Falhas antes do log normal, fases NXExtract/NXSplash/runtime e resultado
  terminal deixam evidência estruturada sem alterar a interface visual.

### Controles

| Entrada | Ação |
|---|---|
| D-pad / analógico esquerdo | mover e navegar |
| Botão de face sul | pular / confirmar |
| L1 / R1 | trocar de mundo no mapa |
| START | iniciar / pausar |
| SELECT + START | salvar e sair pelo fluxo nativo |

### Dados e instalação

Consulte [INSTALLATION.md](INSTALLATION.md). O ZIP é BYO-data: copie sua cópia
legal compatível para `suzycube/gamedata/` e inicie o port. O instalador aceita
diferenças legítimas de assinatura ou empacotamento quando package, versão,
ABI, estrutura e payloads internos permanecem compatíveis; o SHA-256 integral
do APK de referência é evidência, não a única trava da receita.

### Build e gates

O build público usa uma imagem offline por digest, um sysroot apenas para
headers e as árvores do framework materializadas do pin imutável:

```sh
export SC_FRAMEWORK_REPOSITORY=/caminho/do/nextos_ports_android
export NEXTOS_ROOT=/caminho/do/arquivo-nextos
./tests/run-host.sh
./package/build-package.sh /caminho/novo/para-o-candidato
```

O destino de release deve ser novo: o pipeline nunca sobrescreve candidato.
Ele recompila, audita todos os ELFs Linux, valida JSON estrito, rejeita o
comando externo `stat`, cria o ZIP, reabre seus bytes e executa o contrato real
do PortMaster. Dados do jogo nunca entram no build nem no ZIP.

### Mapa de fontes e licenças

| Caminho | Responsabilidade |
|---|---|
| `src/main.c`, `src/android.c`, `src/jni.c` | lifecycle e ambiente Android |
| `src/nx_elf.c` | carga e relocação ELF do jogo |
| `src/egl*.c`, `src/etc2_decode.c` | apresentação GLES2 e fallback ETC2 |
| `src/audio.c` | áudio Unity/FMOD para SDL |
| `src/input*.c` | controle SDL/evdev e política de input |
| `nxextract/merge-suzycube-data.py` | merge transacional específico do jogo |
| `tests/` | unidades de input e contrato hermético de release |

O código do port é [GPL-3.0-only](LICENSE). Avisos e atribuições estão em
[NOTICE.md](NOTICE.md) e `licenses/`. Os dados e marcas do jogo pertencem aos
respectivos titulares e não são licenciados por este projeto.

## English

### Overview and status

The port runs the original ARM64 objects from Suzy Cube 1.0.13 (Unity
2017.4.40f1 IL2CPP) directly on AArch64 Linux. It does not emulate the CPU or
shortcut boot: `init_array`, `JNI_OnLoad`, and the native Unity lifecycle are
preserved in their original order.

Version line 1.1.12 reconciles the 1.1.10 and 1.1.11 input fixes and migrates
the launcher, metadata, and packaging to one declarative source. Physical
support for a candidate is promoted only after that exact ZIP and SHA-256 pass
the device matrix; older release results remain historical baselines.

### Runtime and compatibility

- Resolution comes from the device and is never hardcoded.
- Unity's FMOD output is bridged through the firmware SDL audio provider.
- The factory InControl path receives a real physical controller through SDL
  plus an authoritative evdev snapshot; there is no synthetic touch cursor.
- PlayerPrefs persist under the port-owned home directory.
- The game uses its GLES2 shader path and ETC1 textures; residual ETC2 is
  decoded only where the driver lacks support.

The generated public chain is PortMaster → NXExtract → payload validation →
NXSplash → loader → native Unity. `nxbootstrap 0.6.15`, NXExtract engine
1.2.10, immutable NXExtract UI 1.2.9, and NXSplash 0.1.2 are content-pinned.

### Controls

| Input | Action |
|---|---|
| D-pad / left stick | move and navigate |
| South face button | jump / confirm |
| L1 / R1 | switch worlds on the map |
| START | start / pause |
| SELECT + START | save and leave through the native path |

### Installation, build, and diagnostics

Follow [INSTALLATION.md](INSTALLATION.md) for the exact accepted owner-data
identity and directory layout. The full reference APK hash records tested
evidence but does not lock compatibility to one container signature.

For a source build, set `SC_FRAMEWORK_REPOSITORY` and `NEXTOS_ROOT` to clean
checkouts/archives, then run `./tests/run-host.sh`. Logs are written to
`suzycube/log.txt`; NXExtract also keeps a detailed private installation log
and an atomic terminal-result receipt. Diagnostic environment variables are
opt-in and the release binary is quiet by default.

The loader and integration code are GPL-3.0-only. See [NOTICE.md](NOTICE.md)
for upstream acknowledgements. No proprietary game data is included.
