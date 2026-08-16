#!/usr/bin/env bash
# Reproducible public AArch64 build. The pinned Debian Buster toolchain keeps
# every Linux ELF produced by this port at GLIBC <= 2.30, which is what ArkOS
# and R36S ship. The NextOS sysroot contributes headers only, never libraries.
set -euo pipefail

PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUTPUT=build/suzycube-nextos
BUILDER_IMAGE=playfetch-builder:buster
BUILDER_IMAGE_ID=sha256:036c7910ea53bc78cc213452afa92fa83d55de1c51ae54f315af58b5a41a45cf
FRAMEWORK_PIN="$PORT_DIR/FRAMEWORK-BUILD-PIN.json"
export LC_ALL=C
export TZ=UTC
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1786233600}

if [[ -n ${SC_UNIVERSAL_OUTPUT:-} && $SC_UNIVERSAL_OUTPUT != "$OUTPUT" ]]; then
  echo "SC_UNIVERSAL_OUTPUT may only select canonical $OUTPUT" >&2
  exit 1
fi

if [ "${SC_BUSTER_IN_CONTAINER:-0}" != "1" ]; then
  # shellcheck source=tools/framework-source.sh
  source "$PORT_DIR/tools/framework-source.sh"
  FRAMEWORK_REPOSITORY=$(sc_resolve_framework_repository "$PORT_DIR")
  FRAMEWORK_PIN_HELPER="$FRAMEWORK_REPOSITORY/framework/nxgenerator/framework_pin.py"
  [ -f "$FRAMEWORK_PIN" ] || {
    echo "immutable framework build pin is missing: $FRAMEWORK_PIN" >&2
    exit 1
  }
  NEXTOS_ROOT=${NEXTOS_ROOT:-"$HOME/NextOS-Elite-Edition"}
  [ -d "$NEXTOS_ROOT" ] || {
    echo "NEXTOS_ROOT must identify the read-only NextOS toolchain archive: $NEXTOS_ROOT" >&2
    exit 1
  }
  NEXTOS_TOOLCHAIN=$(
    find -H "$NEXTOS_ROOT" -maxdepth 2 -type d \
      -path '*/build.NextOS-Retro-Elite-Edition-Amlogic-old.aarch64-*/toolchain' \
      -print | sort -V | tail -1
  )
  [ -n "$NEXTOS_TOOLCHAIN" ] || {
    echo "NextOS header sysroot not found below $NEXTOS_ROOT" >&2
    exit 1
  }
  NEXTOS_SYSROOT=$NEXTOS_TOOLCHAIN/aarch64-libreelec-linux-gnu/sysroot
  [ -d "$NEXTOS_SYSROOT/usr/include/SDL2" ] || {
    echo "NextOS SDL2 headers unavailable: $NEXTOS_SYSROOT" >&2
    exit 1
  }
  command -v docker >/dev/null 2>&1 || {
    echo "docker is required for the pinned low-glibc build" >&2
    exit 1
  }
  ACTUAL_IMAGE_ID=$(docker image inspect "$BUILDER_IMAGE" \
    --format '{{.Id}}' 2>/dev/null) || {
      echo "offline builder image missing: $BUILDER_IMAGE" >&2
      exit 1
    }
  [ "$ACTUAL_IMAGE_ID" = "$BUILDER_IMAGE_ID" ] || {
    echo "builder image changed: $ACTUAL_IMAGE_ID" >&2
    exit 1
  }

  FRAMEWORK_WORK=$(mktemp -d "${TMPDIR:-/tmp}/suzycube-framework.XXXXXX")
  FRAMEWORK_SNAPSHOT="$FRAMEWORK_WORK/source"
  cleanup_framework() {
    case $FRAMEWORK_WORK in
      "${TMPDIR:-/tmp}"/suzycube-framework.*)
        [[ -d $FRAMEWORK_WORK ]] && rm -rf -- "$FRAMEWORK_WORK"
        ;;
      *)
        echo "refusing unsafe framework cleanup target: $FRAMEWORK_WORK" >&2
        ;;
    esac
  }
  trap cleanup_framework EXIT INT TERM
  python3 -B "$FRAMEWORK_PIN_HELPER" materialize \
    --repository "$FRAMEWORK_REPOSITORY" \
    --pin "$FRAMEWORK_PIN" \
    --destination "$FRAMEWORK_SNAPSHOT"
  python3 -B "$FRAMEWORK_PIN_HELPER" verify \
    --pin "$FRAMEWORK_PIN" \
    --snapshot "$FRAMEWORK_SNAPSHOT"

  docker run --rm --network none \
    -e SC_BUSTER_IN_CONTAINER=1 \
    -e SC_UNIVERSAL_OUTPUT="$OUTPUT" \
    -e SC_WERROR="${SC_WERROR:-1}" \
    -e SC_HOST_UID="$(id -u)" -e SC_HOST_GID="$(id -g)" \
    -e LC_ALL=C -e TZ=UTC -e SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" \
    -v "$PORT_DIR":/repo \
    -v "$FRAMEWORK_SNAPSHOT/framework":/framework:ro \
    -v "$NEXTOS_SYSROOT":/nxsr:ro \
    "$BUILDER_IMAGE_ID" bash /repo/build_universal.sh
  exit $?
fi

for tool in aarch64-linux-gnu-gcc aarch64-linux-gnu-nm \
            aarch64-linux-gnu-readelf file; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "missing pinned-builder tool: $tool" >&2
    exit 1
  }
done

CC=aarch64-linux-gnu-gcc
NM=aarch64-linux-gnu-nm
READELF=aarch64-linux-gnu-readelf
FRAMEWORK_ROOT=${SC_FRAMEWORK_ROOT:-/framework}
cd /repo
mkdir -p build

OBJDIR=$(mktemp -d)
STUBDIR=$(mktemp -d)
cleanup() {
  find "$OBJDIR" "$STUBDIR" -type f -delete 2>/dev/null || true
  rmdir "$OBJDIR" "$STUBDIR" 2>/dev/null || true
}
trap cleanup EXIT

# The port keeps its own ELF loader (src/nx_elf.c): jni.c and egl.c consume
# dynsym/pltgot/probe surface that nxloader does not expose. Only nxcompat,
# nxinput and nxaudio are linked in here.
COMMON_INCLUDES=(
  -I src
  -I "$FRAMEWORK_ROOT/nxcompat/include"
  -I "$FRAMEWORK_ROOT/nxcompat/src"
  -I "$FRAMEWORK_ROOT/nxinput/include"
  -I "$FRAMEWORK_ROOT/nxinput/src"
  -I "$FRAMEWORK_ROOT/nxaudio/include"
)

WERROR_FLAGS=(-Wall -Wextra -Werror -Wno-error=unused-parameter
              -Wno-error=unused-function -Wno-error=format-truncation)
if [ "${SC_WERROR:-1}" != "1" ]; then
  # Diagnostic pass only: never used for a release artifact.
  WERROR_FLAGS=(-Wall -Wextra -Wno-error=unused-parameter)
fi

OBJS=()
compile_source() {
  group=$1
  source=$2
  object="$OBJDIR/${group}_$(basename "${source%.c}").o"
  "$CC" -std=gnu11 "${COMMON_INCLUDES[@]}" \
    -idirafter /nxsr/usr/include -idirafter /nxsr/usr/include/SDL2 \
    -O2 -fPIE -fno-omit-frame-pointer -fno-strict-aliasing \
    -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
    "${WERROR_FLAGS[@]}" \
    -c "$source" -o "$object"
  OBJS+=("$object")
}

for source in src/*.c; do
  compile_source suzycube "$source"
done

for source in \
  "$FRAMEWORK_ROOT"/nxcompat/src/nxcompat.c \
  "$FRAMEWORK_ROOT"/nxcompat/src/nxcompat_backend.c \
  "$FRAMEWORK_ROOT"/nxcompat/src/nxcompat_graphics.c \
  "$FRAMEWORK_ROOT"/nxcompat/src/nxcompat_plan.c \
  "$FRAMEWORK_ROOT"/nxcompat/src/nxcompat_probe.c \
  "$FRAMEWORK_ROOT"/nxcompat/src/nxcompat_receipts.c \
  "$FRAMEWORK_ROOT"/nxcompat/src/nxcompat_registry.c \
  "$FRAMEWORK_ROOT"/nxcompat/src/nxcompat_report.c; do
  compile_source nxcompat "$source"
done

for source in \
  "$FRAMEWORK_ROOT"/nxinput/src/nxinput.c \
  "$FRAMEWORK_ROOT"/nxinput/src/nxinput_core.c \
  "$FRAMEWORK_ROOT"/nxinput/src/nxinput_nxcompat.c; do
  compile_source nxinput "$source"
done
compile_source nxaudio "$FRAMEWORK_ROOT/nxaudio/src/nxaudio.c"

# Link only against a generated SDL SONAME stub. The firmware owns the actual
# SDL library; NextOS headers are read-only and never leak its newer glibc into
# this universal executable.
UNDEFINED=$($NM --undefined-only "${OBJS[@]}" 2>/dev/null |
  awk '{print $NF}' | sort -u)
: > "$STUBDIR/SDL2.c"
for symbol in $(printf '%s\n' "$UNDEFINED" | grep -E '^SDL_' || true); do
  printf 'void %s(void) {}\n' "$symbol" >> "$STUBDIR/SDL2.c"
done
"$CC" -shared -fPIC -nostdlib -Wl,-soname,libSDL2-2.0.so.0 \
  "$STUBDIR/SDL2.c" -o "$STUBDIR/libSDL2.so"

"$CC" -fPIE -pie -rdynamic -o "$OUTPUT" "${OBJS[@]}" \
  -L"$STUBDIR" -Wl,--as-needed -lSDL2 -ldl -lm -lpthread -lz -lgcc_s \
  -Wl,-z,relro,-z,now,-z,noexecstack

MAX_GLIBC=$(
  "$READELF" --version-info "$OUTPUT" |
    grep -oE 'GLIBC_[0-9]+([.][0-9]+)*' | sort -Vu | tail -1
)
[ -n "$MAX_GLIBC" ] || {
  echo "unable to determine the GLIBC requirement" >&2
  exit 1
}
version_number=${MAX_GLIBC#GLIBC_}
major=${version_number%%.*}
rest=${version_number#*.}
minor=${rest%%.*}
if [ "$major" -gt 2 ] || {
  [ "$major" -eq 2 ] && [ "$minor" -gt 30 ];
}; then
  echo "public build rejected: $MAX_GLIBC exceeds GLIBC_2.30" >&2
  exit 1
fi

TLS_MEMSZ=$(
  "$READELF" -lW "$OUTPUT" | awk '$1 == "TLS" { value = $6 } END { print value }'
)
PAD_LAYOUT=$(
  "$READELF" -sW "$OUTPUT" |
    awk '$4 == "TLS" && $8 == "g_bionic_guard_pad" { value = $2 ":" $3 } END { print value }'
)
[ "$PAD_LAYOUT" = "0000000000000000:256" ] || {
  echo "Bionic guard-pad layout changed: $PAD_LAYOUT" >&2
  exit 1
}
[ $((TLS_MEMSZ)) -ge 256 ] || {
  echo "TLS block is smaller than the Bionic guard pad" >&2
  exit 1
}

if "$READELF" -lW "$OUTPUT" | awk '$1 == "LOAD" && $0 ~ /RWE/ {bad=1} END {exit !bad}'; then
  echo "public build contains an RWX PT_LOAD" >&2
  exit 1
fi

if "$READELF" -dW "$OUTPUT" | grep -Eq '\((RPATH|RUNPATH)\)'; then
  echo "public build contains a forbidden DT_RPATH/DT_RUNPATH" >&2
  exit 1
fi

NEEDED=$($READELF -dW "$OUTPUT" | awk -F'[][]' '/NEEDED/ {print $2}' | sort)
EXPECTED=$(printf '%s\n' libSDL2-2.0.so.0 libc.so.6 libdl.so.2 libgcc_s.so.1 \
  libm.so.6 libpthread.so.0 libz.so.1 ld-linux-aarch64.so.1 | sort)
if [ "$NEEDED" != "$EXPECTED" ]; then
  echo "unexpected DT_NEEDED set:" >&2
  printf '%s\n' "$NEEDED" >&2
  exit 1
fi

INTERPRETER=$(
  "$READELF" -lW "$OUTPUT" |
    sed -n 's/.*Requesting program interpreter: \([^]]*\)].*/\1/p'
)
[ "$INTERPRETER" = "/lib/ld-linux-aarch64.so.1" ] || {
  echo "unexpected AArch64 interpreter: $INTERPRETER" >&2
  exit 1
}

SYMBOL_TABLE=$($READELF -sW "$OUTPUT")
# The parenthesized version-table index is link-order metadata and can change
# when another import disappears.  The provider/version contract is checked
# independently below; never pin that incidental numeric index.
grep -Eq 'UND[[:space:]]+__stack_chk_guard@GLIBC_2[.]17([[:space:]]+[(][0-9]+[)])?$' \
  <<< "$SYMBOL_TABLE" || {
    echo "AArch64 stack-protector guard contract changed" >&2
    exit 1
  }
if grep -Eq '__tls_get_addr(@|$)' <<< "$SYMBOL_TABLE"; then
  echo "unexpected guest TLS resolver dependency" >&2
  exit 1
fi

VERSION_NEEDS=$($READELF -VW "$OUTPUT")
printf '%s\n' "$VERSION_NEEDS" | awk '
  /Version: 1  File:/ { provider = $0 }
  /Name: GLIBC_2[.]17/ &&
      provider ~ /File: ld-linux-aarch64[.]so[.]1/ {
    found = 1
  }
  END { exit !found }
' || {
  echo "stack guard is not versioned by ld-linux-aarch64.so.1/GLIBC_2.17" >&2
  exit 1
}

# No build-machine path may reach a published artifact. The pattern needs a
# character AFTER `/home/`: the framework legitimately carries the bare literals
# `/home/` and `/var/home/` to locate the player's home at runtime, and only a
# named directory under them identifies this machine. The listing goes to a file
# first, because `strings | grep -q` under `pipefail` decides by race: grep
# closes the pipe on the first match and the SIGPIPE turns a real hit into a
# silent pass.
LC_ALL=C strings "$OUTPUT" > "$OBJDIR/public-strings.txt"
if grep -Eq '/home/[A-Za-z0-9._-]|/mnt/ARQUIVOS|192[.]168[.][0-9]' \
    "$OBJDIR/public-strings.txt"; then
  echo "public build leaks a build-machine path or address:" >&2
  grep -Eo '/home/[A-Za-z0-9._/-]*|/mnt/ARQUIVOS[A-Za-z0-9._/-]*|192[.]168[.][0-9.]*' \
    "$OBJDIR/public-strings.txt" | sort -u | head -5 >&2
  exit 1
fi

$NM "$OUTPUT" > "$OBJDIR/public-nm.txt"
# A lista de nomes vai para um arquivo antes de ser consultada: `awk | grep -q`
# sob `pipefail` reprova o build a esmo, porque o grep fecha o pipe no primeiro
# casamento e o awk morre de SIGPIPE.
awk '{print $3}' "$OBJDIR/public-nm.txt" > "$OBJDIR/public-symbols.txt"
for symbol in nxcompat_probe nxinput_create nxaudio_classify_backend \
              nx_load nx_relocate sc_jni_init; do
  grep -qx "$symbol" "$OBJDIR/public-symbols.txt" || {
    echo "required symbol missing from public ELF: $symbol" >&2
    exit 1
  }
done
if grep -Eq '^(so_load|so_relocate)$' "$OBJDIR/public-symbols.txt"; then
  echo "legacy loader symbol leaked into public ELF" >&2
  exit 1
fi

if [ -n "${SC_HOST_UID:-}" ] && [ -n "${SC_HOST_GID:-}" ]; then
  chown "$SC_HOST_UID:$SC_HOST_GID" "$OUTPUT" 2>/dev/null || true
fi

echo "SUZY CUBE UNIVERSAL BUILD OK -> $OUTPUT"
echo "glibc maximum: $MAX_GLIBC (limit GLIBC_2.30)"
echo "TLS guard pad: $PAD_LAYOUT; TLS memsz=$TLS_MEMSZ"
echo "DT_NEEDED: $(printf '%s\n' "$NEEDED" | tr '\n' ' ')"
file "$OUTPUT"
sha256sum "$OUTPUT"
