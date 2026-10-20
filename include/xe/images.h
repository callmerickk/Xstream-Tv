#ifndef XE_IMAGES_H
#define XE_IMAGES_H
#include <stddef.h>
#include "xe/render.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XeImage XeImage;
/* URL and requested dimensions identify a shared, reference-counted request. */
XeImage *xe_img_get(const char *url, int want_w, int want_h);
/* Valid only while the caller holds a reference; NULL until uploaded and ready. */
const XeTexture *xe_img_tex(XeImage *img);
float xe_img_fade(XeImage *img);
int xe_img_failed(XeImage *img);
void xe_img_release(XeImage *img);
/* Call on render thread once per frame; performs at most two GPU uploads. */
void xe_img_pump(void);
/* Byte budgets; defaults are 64 MiB GPU and 150 MiB disk. */
void xe_img_set_budget(size_t gpu_bytes, size_t disk_bytes);
/* Optional diagnostics for tests/telemetry. */
size_t xe_img_gpu_bytes(void);
size_t xe_img_disk_bytes(void);
unsigned long xe_img_cache_hits(void);
#ifdef __cplusplus
}
#endif
#endif
