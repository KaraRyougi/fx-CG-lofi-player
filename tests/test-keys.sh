#!/usr/bin/env bash
# Transport-key logic of src/mp3-live.c against KEYSC models (tests/test_keys.c).
set -uo pipefail
cd "$(dirname "$0")/.."
T=$(mktemp -d)
F="-std=gnu11 -O1 -w -Itests/stub -Isrc -Ivendor/libmad -Ivendor/opus/include -DFPM_64BIT -no-pie -Wl,--unresolved-symbols=ignore-all"
gcc $F -DLIVE_SRC='"../src/mp3-live.c"' tests/test_keys.c -o "$T/keys" || exit 2
"$T/keys" "${1:-300}"; rc=$?
rm -rf "$T"
exit $rc
