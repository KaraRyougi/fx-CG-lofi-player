#!/usr/bin/env bash
# Big-endian SH4 run of the frame generator and the movca.l/ocbp ring writer
# (tests/test_sh_frames.c) in the GDB simulator of the fxSDK build image,
# plus a negative control that must fail.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-shsim
docker run --rm --network none -v "$PWD:/w" -w /w \
  --entrypoint /bin/bash "${FXSDK_IMAGE:-cgba-fxsdk:14.1-gint-2bc6f7e}" -eu -c '
  CC="sh-elf-gcc -m4-nofpu -mb -O2 -ffreestanding -nostdlib -Wall -Wextra -Werror -Isrc -e _start -Wl,-Ttext=0x1000"
  $CC tests/test_sh_frames.c -o build-shsim/frames.elf -lgcc
  [ "$(sh-elf-objdump -d build-shsim/frames.elf | grep -cE "movca\.l|ocbp")" = 2 ]
  if sh-elf-run build-shsim/frames.elf; then echo "PASS: SH4 big-endian frames"; else echo "FAIL: exit $?"; exit 1; fi
  $CC -DNEG tests/test_sh_frames.c -o build-shsim/neg.elf -lgcc
  if sh-elf-run build-shsim/neg.elf; then echo "FAIL: negative control passed"; exit 1; else echo "PASS: negative control fails (exit $?)"; fi
'
