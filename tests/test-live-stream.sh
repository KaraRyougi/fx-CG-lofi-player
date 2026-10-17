#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash tests/build-mp3-host.sh
cc -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -DFPM_64BIT -DHAVE_LIMITS_H -DHAVE_ASSERT_H -Ivendor/libmad \
 src/mp3-decode.c src/sdm-stream.c tests/test_live_stream.c \
 build-host-mp3/*.o -o build-host-mp3/live-test
build-host-mp3/live-test tests/fixtures/Test.mp3
build-host-mp3/live-test tests/fixtures/Stereo.mp3
