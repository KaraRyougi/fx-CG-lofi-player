#!/usr/bin/env bash
# Host test of the vendored fixed-point libopus + Ogg reader (src/opus-file.c):
# full decode, then a seek (default 5 s into tests/fixtures/Synth.opus) that
# must match the full decode exactly. Usage: tests/test-opus.sh [file.opus] [seek_ms]
set -euo pipefail
cd "$(dirname "$0")/.."
V=vendor/opus
SR=$(sed -n 's/^set(OPUS_CELT \(.*\))$/\1/p' CMakeLists.txt | tr ' ' '\n' | sed "s|^|$V/celt/|;s|$|.c|")
SR="$SR $(sed -n 's/^set(OPUS_SILK \(.*\))$/\1/p' CMakeLists.txt | tr ' ' '\n' | sed "s|^|$V/silk/|;s|$|.c|")"
mkdir -p build-host-opus
gcc -O2 -w -DHAVE_CONFIG_H -I$V -I$V/include -I$V/celt -I$V/silk -I$V/silk/fixed -Isrc \
  $SR $V/src/opus.c $V/src/opus_decoder.c src/opus-file.c tests/opus_host.c -o build-host-opus/opus-host -lm
F=${1:-tests/fixtures/Synth.opus}
SEEK_MS=${2:-5000}
build-host-opus/opus-host "$F" build-host-opus/full.s16
build-host-opus/opus-host "$F" build-host-opus/seek.s16 "$SEEK_MS"
SEEK_MS=$SEEK_MS python3 - <<'PY'
import os, numpy as np
ms=int(os.environ['SEEK_MS']); k=ms*24
a=np.fromfile('build-host-opus/full.s16',dtype='<i2'); b=np.fromfile('build-host-opus/seek.s16',dtype='<i2')
assert len(b)>0 and np.array_equal(a[k:k+len(b)],b) and len(a)-k==len(b)
print(f'PASS: seek to {ms/1000:g} s is sample-exact')
PY
