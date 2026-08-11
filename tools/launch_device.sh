#!/bin/bash
# Roda NO DEVICE: abre o Suzy Cube pela entrada do EmulationStation, com o
# ambiente de teste passado por argumentos.  Existe como ARQUIVO justamente
# para nao depender de aspas aninhadas num comando ssh -- foi o que antes fez
# o lancamento sair errado e o veredito culpar o jogo.
#
#   launch_device.sh <roteiro-de-input> <quadros-de-print>
set -u
DST=/storage/roms/ports/suzycube
SCRIPT=${1:-}
SHOTS=${2:-}

cd "$DST" || exit 9
rm -f verify_shot.*.ppm

export SC_LOGCAT=1
export SC_FPS=1
export SC_AUDIO_TRACE=1
[ -n "$SCRIPT" ] && export SC_INPUTTEST="$SCRIPT"
if [ -n "$SHOTS" ]; then
  export SC_SCREENSHOT="$DST/verify_shot"
  export SC_SCREENSHOT_FRAME="$SHOTS"
fi

# Jogo aberto a pedido: sem timeout e sobrevivendo ao fim do ssh.
nohup bash "/storage/roms/ports/Suzy Cube.sh" < /dev/null > /dev/null 2>&1 &
echo "lancado pid=$!"
