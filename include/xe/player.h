#ifndef XE_PLAYER_H
#define XE_PLAYER_H

#include <stddef.h>
#include "xe/render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One player for the whole app. Windows/Linux: libmpv. Android: ExoPlayer.
 * headers: NULL-terminated name/value pairs, e.g. {"Referer","https://x", NULL}. */
void xe_player_open(const char *url, const char *const *headers, double start_sec);
void xe_player_toggle(void);

/* Relative seconds. */
void xe_player_seek(double sec);

void xe_player_close(void);
int  xe_player_active(void);
const char *xe_player_error(void);
int  xe_player_paused(void);
double xe_player_pos(void);
double xe_player_dur(void);
int  xe_player_buffering(void);

/* kind: 0 audio, 1 subtitles, 2 video. Names end with "  •" when selected. */
int  xe_player_tracks(int kind, char names[][64], int max);
void xe_player_select(int kind, int index);

/* Backend: draws the current video frame into the window (desktop). */
void xe_player_render_video(int w, int h);

/* Requests a preview near sec; returns a texture when one is ready
 * (may be for a nearby time), else NULL. Never blocks.
 * Textures are owned by the player and stay valid until the next call
 * or close. */
const XeTexture *xe_player_preview(double sec, double *shown_sec);

/* Full player screen: video, then the TV overlay. Polls input itself. */
void xe_player_set_title(const char *title);
void xe_player_frame(double dt, XeRect area);

#ifdef __cplusplus
}
#endif

#endif
