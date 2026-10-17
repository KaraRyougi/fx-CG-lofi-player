#!/usr/bin/env bash
# -Werror syntax check of the calculator sources against host stubs of gint
# (tests/stub). Catches C errors and warnings, not SH or gint ABI issues.
set -uo pipefail
cd "$(dirname "$0")/.."
F="-std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Itests/stub -Isrc -Ivendor/libmad -Ivendor/opus/include -DFPM_64BIT"
rc=0
for f in src/main.c src/mp3-live.c src/tx-feed.c src/sdm-stream.c src/opus-file.c src/nor-source.c src/mp3-decode.c src/overclock.c src/platform.c src/library.c src/ui.c src/wav.c src/transport.c src/flashmap.c; do
  if gcc $F -fsyntax-only "$f" 2>/tmp/check.$$; then echo "ok   $f"; else echo "FAIL $f"; head -20 /tmp/check.$$; rc=1; fi
done
rm -f /tmp/check.$$
exit $rc
