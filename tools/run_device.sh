#!/usr/bin/env bash
# Abre o jogo no device em PRIMEIRO PLANO com teto de tempo, le o codigo de
# saida e traz o log de volta.  Nunca laco cego de espera.
#
#   tools/run_device.sh [segundos] [VAR=val ...]
set -euo pipefail
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEV=${DEV:-192.168.31.73}
DST=/storage/roms/ports/suzycube
SECS=${1:-90}
shift || true
ENVS="$*"

timeout $((SECS + 90)) ssh -o BatchMode=yes "root@$DEV" "
cd $DST || exit 9
nice -n 19 timeout -s KILL $SECS env $ENVS ./run.sh >/dev/null 2>&1
echo \"__EXIT=\$?\"
" 2>&1 | tail -3

timeout 120 scp -o BatchMode=yes "root@$DEV:$DST/debug.log" \
  "$PORT_DIR/logs/last.log" >/dev/null 2>&1 || true
echo "[run] log em logs/last.log ($(wc -l < "$PORT_DIR/logs/last.log" 2>/dev/null || echo 0) linhas)"
