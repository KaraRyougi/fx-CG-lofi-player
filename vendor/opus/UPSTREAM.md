# libopus (decoder subset)

Source: libopus 1.3.1 (`package_version`), as bundled in the npm package
`@discordjs/opus` 0.10.0 (`deps/opus`). BSD-3-Clause, see `COPYING`.

Only the files needed to decode (CELT + SILK, fixed point) are kept:
`celt/entenc.c` and `silk/biquad_alt.c` are present because shared
decoder/encoder files reference them. Build options are in `config.h` and
`CMakeLists.txt`. One source change, recorded as `vendor/opus-headroom.patch`
(1.0.2): with `OPUS_OUTPUT_HEADROOM_Q15` defined, the CELT de-emphasis output
(`celt/celt_decoder.c`, `OUT_HEADROOM`) and each SILK frame
(`src/opus_decoder.c`) are scaled by it before they are saturated to 16 bits,
so that codec overshoot above 0 dBFS reaches the player's limiter instead of
being clipped. The de-emphasis state is not scaled. Without the define the
sources behave exactly as upstream.

Verified on the host (x86-64) against the same 1.3.1 sources built in
float: 66.5 dB SNR on a 5-minute song encoded with 3 dB headroom (the
remaining difference is fixed vs float arithmetic); the float build matches
FFmpeg's system libopus 1.4 at 113 dB.
