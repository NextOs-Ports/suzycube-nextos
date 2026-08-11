#!/usr/bin/env bash
# Hermetic release gate. It audits/builds but never executes owner game code.
set -euo pipefail

export LC_ALL=C
export TZ=UTC
export PYTHONDONTWRITEBYTECODE=1

PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
REPO_ROOT=$(git -C "$PORT_DIR" rev-parse --show-toplevel)
cd "$PORT_DIR"

INPUT_EVDEV_TEST=$(mktemp "${TMPDIR:-/tmp}/suzycube-input-evdev-test.XXXXXX")
INPUT_POLICY_TEST=$(mktemp "${TMPDIR:-/tmp}/suzycube-input-policy-test.XXXXXX")
cleanup() {
  rm -f -- "$INPUT_EVDEV_TEST" "$INPUT_POLICY_TEST"
}
trap cleanup EXIT INT TERM

"${HOSTCC:-cc}" -std=c11 -Wall -Wextra -Werror -Isrc \
  tests/test_input_evdev.c src/input_evdev.c -o "$INPUT_EVDEV_TEST"
"$INPUT_EVDEV_TEST"

SDL_CFLAGS=$(pkg-config --cflags sdl2)
"${HOSTCC:-cc}" -std=c11 -Wall -Wextra -Werror -Isrc $SDL_CFLAGS \
  -I"$REPO_ROOT/framework/nxinput/include" \
  -I"$REPO_ROOT/framework/nxinput/src" \
  tests/test_input_policy.c \
  "$REPO_ROOT/framework/nxinput/src/nxinput_core.c" \
  -lm -o "$INPUT_POLICY_TEST"
"$INPUT_POLICY_TEST"

for script in build_universal.sh "Suzy Cube.sh" \
              nxextract/run-extractor.sh nxextract/nxextract-runtime-env.sh \
              package/build-package.sh; do
  bash -n "$script"
done

python3 -B nxextract/nxextract.py recipe-check --recipe extractor.json
python3 -B -m py_compile nxextract/merge-suzycube-data.py
./build_universal.sh
python3 -B tests/test_suzycube_contract.py
python3 -B ../../framework/nxrelease/nxrelease.py validate \
  --manifest nxrelease.json

DRY_ADD=$(git -C "$REPO_ROOT" add -n --all ports/suzycube)
if grep -E \
  "ports/suzycube/(\.build|build|gamedata|stage|verify)/|ports/suzycube/(suzycube|suzycube-nextos)'|\.(apk|apkm|apks|xapk|obb|so|zip|raw|png)'" \
  <<< "$DRY_ADD"; then
  printf '%s\n' 'Git dry-run would stage built or owner data' >&2
  exit 1
fi

git -C "$REPO_ROOT" diff --check -- ports/suzycube
printf '%s\n' \
  'SUZY CUBE HOST GATE: PASS' \
  'physical_device_evidence=0 baseline_physical_release=1.1.0 proprietary_payload_packaged=0 guest_execution=0'
