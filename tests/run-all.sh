#!/usr/bin/env bash
# Every host test of the player. Needs a C compiler, Python 3 with numpy
# (and scipy for test_sdm.py) and ffmpeg (test_mp3.py's reference decode).
#   bash tests/run-all.sh            host tests
#   bash tests/run-all.sh --docker   also the two that need the fxSDK image
#                                    (test-keys.sh: GNU ld; test-sh-frames.sh:
#                                    the SH4 simulator), FXSDK_IMAGE overrides
set -euo pipefail
cd "$(dirname "$0")/.."
B=build-host-unit; mkdir -p "$B"
CF="-std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc"
unit() { local name=$1; shift; cc $CF "$@" -o "$B/$name" -lm && "$B/$name" >/dev/null && echo "PASS: $name"; }
unit test_flashmap  tests/test_flashmap.c src/flashmap.c
unit test_library   -Itests/include tests/test_library.c src/library.c
unit test_pcm_cache tests/test_pcm_cache.c src/pcm-cache.c
unit test_pcm_stream tests/test_pcm_stream.c src/pcm-stream.c src/pcm-cache.c
unit test_resample  tests/test_resample.c
unit test_resampler tests/test_resampler.c src/sdm-stream.c
unit test_transport tests/test_transport.c src/transport.c
unit test_wav       tests/test_wav.c src/wav.c
bash tests/check-syntax.sh >/dev/null && echo "PASS: check-syntax"
bash tests/test-live-stream.sh
bash tests/test-opus.sh
python3 tests/test_sdm.py | tail -1
python3 tests/test_mp3.py 2>/dev/null | tail -1
if [ "${1:-}" = --docker ]; then
  docker run --rm --network none -v "$PWD:/w" -w /w --entrypoint /bin/bash \
    "${FXSDK_IMAGE:-cgba-fxsdk:14.1-gint-2bc6f7e}" -c 'bash tests/test-keys.sh'
  bash tests/test-sh-frames.sh
fi
echo "all tests passed"
