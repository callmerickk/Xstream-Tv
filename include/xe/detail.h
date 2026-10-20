#ifndef XE_DETAIL_H
#define XE_DETAIL_H
#include <stddef.h>
#include "xe/render.h"
#include "xe/data.h"
#ifdef __cplusplus
extern "C" {
#endif
void xe_detail_open(const XeMeta *m);
void xe_detail_frame(double dt, XeRect area);
/* Returns 1 once after Play/Resume; out_type/out_id are NUL-terminated when cap > 0.
   Series episodes return the Stremio video id, e.g. tt0944947:1:2. */
int xe_detail_play(char *out_type, char *out_id, size_t cap);
void xe_detail_set_episodes(const XeEpisode *eps, int n);
void xe_detail_update_meta(const XeMeta *m);
const XeMeta *xe_detail_meta(void);
#ifdef __cplusplus
}
#endif
#endif
