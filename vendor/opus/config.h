/* CGplayer100: decoder-only, fixed-point libopus 1.3.1 for SH4 (gint). */
#define OPUS_BUILD 1
#define FIXED_POINT 1
#define DISABLE_FLOAT_API 1
/* Scratch memory from one heap block instead of the (small) gint stack.
   Measured high water for 24 kHz mono output: 8.5 KiB (stereo stream,
   seek); 24 KiB leaves margin for PLC and 120 ms frames. Allocated on first
   use and kept for the life of the add-in. */
#define NONTHREADSAFE_PSEUDOSTACK 1
#define GLOBAL_STACK_SIZE 24000
/* Output 4 dB below full scale so that codec overshoot above 0 dBFS is kept
   instead of saturated (celt_decoder.c OUT_HEADROOM, opus_decoder.c SILK
   path); the player adds it back in front of its limiter. Same value as
   DECODE_HEADROOM_Q15 in src/mp3-decode.h. */
#define OPUS_OUTPUT_HEADROOM_Q15 20675
