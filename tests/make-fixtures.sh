#!/usr/bin/env bash
# Regenerates the synthetic Opus test file (needs ffmpeg with libopus):
# 12 s of stepped tones, a slow tremolo at 3 kHz and pink noise, mono,
# 48 kHz in, 32 kbps out. tests/fixtures/Test.mp3 and Stereo.mp3 are
# locally synthesized test tones as well (2 s mono 22.05 kHz, 3 s stereo
# 44.1 kHz VBR).
set -euo pipefail
cd "$(dirname "$0")/.."
ffmpeg -nostdin -hide_banner -loglevel error -y \
  -f lavfi -i "aevalsrc='0.25*sin(2*PI*(220+110*floor(mod(t\,4)))*t)+0.1*sin(2*PI*3000*t)*sin(2*PI*0.5*t)':s=48000:d=12" \
  -f lavfi -i "anoisesrc=d=12:c=pink:r=48000:a=0.03:seed=1" \
  -filter_complex "amix=inputs=2:normalize=0" \
  -ac 1 -c:a libopus -b:a 32k -map_metadata -1 -fflags +bitexact tests/fixtures/Synth.opus
ls -l tests/fixtures/Synth.opus
