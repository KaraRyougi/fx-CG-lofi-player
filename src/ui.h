#ifndef LOFI100_UI_H
#define LOFI100_UI_H
#include <stdint.h>
#define UI_APP_NAME "Lo-Fi Player 100"
#define UI_APP_VERSION "1.1.11"
/* One player screen for the browser-side states (main.c: loading, ready,
   finished, errors) and the stream-side ones (mp3-live.c: buffering,
   playing, paused), so the layout and the key hints never differ. */
typedef struct {
    const char *title;      /* file name */
    const char *format;     /* "WAV 16-bit 44100 Hz", "MP3", "Opus"; NULL: none */
    const char *state;      /* "Ready", "Playing", "Paused", an error message */
    uint32_t position;      /* seconds */
    uint32_t duration;      /* seconds; 0 when unknown (MP3 / Opus) */
    const char *volume;     /* live_volume_label(): "12/16", "16/16 +4.5 dB" */
    const char *wait_load;  /* live_wait_name() */
    int playing;            /* hints say pause / stop instead of play / file list */
} ui_player_t;
void ui_player(const ui_player_t *p);                 /* draws and updates */
void ui_message(const char *line1, const char *line2); /* fatal startup message */
void ui_library_title(const char *directory, int total);
void ui_library_row(int row, const char *name, int selected, int folder);
#endif
