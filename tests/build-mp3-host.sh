#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-host-mp3
for f in fixed bit timer stream frame synth layer12 layer3 huffman; do
 cc -std=c11 -O2 -g -fwrapv -fsanitize=address,undefined -fno-sanitize=shift \
  -DFPM_64BIT -DHAVE_LIMITS_H -DHAVE_ASSERT_H -Ivendor/libmad \
  -c "vendor/libmad/$f.c" -o "build-host-mp3/$f.o"
done
cc -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -DFPM_64BIT -DHAVE_LIMITS_H -DHAVE_ASSERT_H -Ivendor/libmad \
 src/mp3-decode.c src/pcm-stream.c src/pcm-cache.c tests/mp3_host.c \
 build-host-mp3/*.o -o build-host-mp3/mp3-host
