#!/usr/bin/env bash
# Monta o pacote distribuivel do Suzy Cube (tar.gz + sha256) FORA do repo.
#
# Regra #14b: dado de jogo nunca entra no git.  O pacote e' montado no staging
# e o repo guarda so' codigo, launcher e documentacao.
set -euo pipefail
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
STAGE=${STAGE:-"/mnt/ARQUIVOS/TRABALHO CLAUDE CODE/99-TEMP-CLAUDE/claude-1000/suzycube/stage"}
OUT=${OUT:-"/mnt/ARQUIVOS/TRABALHO CLAUDE CODE/99-TEMP-CLAUDE/claude-1000/suzycube/pkg"}
VER=$(tr -d '\r\n' < "$PORT_DIR/version.txt")

[ -d "$STAGE/assets/bin/Data" ] || { echo "staging dos dados ausente: $STAGE" >&2; exit 1; }
[ -s "$PORT_DIR/suzycube" ] || { echo "binario ausente; rode build.sh" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/ports/suzycube"
cp -a "$STAGE/assets" "$STAGE/lib" "$OUT/ports/suzycube/"
install -m 755 "$PORT_DIR/suzycube" "$OUT/ports/suzycube/suzycube"
install -m 755 "$PORT_DIR/run.sh"   "$OUT/ports/suzycube/run.sh"
install -m 755 "$PORT_DIR/es_map.sh" "$OUT/ports/suzycube/es_map.sh"
install -m 644 "$PORT_DIR/es2sdl.awk" "$OUT/ports/suzycube/es2sdl.awk"
install -m 644 "$PORT_DIR/version.txt" "$OUT/ports/suzycube/version.txt"
install -m 644 "$PORT_DIR/README.md" "$OUT/ports/suzycube/README.md"
install -m 755 "$PORT_DIR/Suzy Cube.sh" "$OUT/ports/Suzy Cube.sh"
mkdir -p "$OUT/ports_scripts"
install -m 755 "$PORT_DIR/Suzy Cube.sh" "$OUT/ports_scripts/Suzy Cube.sh"
mkdir -p "$OUT/ports/suzycube/home"

TAR="$OUT/SuzyCube.NextOS-v$VER.tar.gz"
tar -C "$OUT" -czf "$TAR" ports ports_scripts
sha256sum "$TAR" | tee "$TAR.sha256"
ls -la "$TAR"
