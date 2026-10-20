#ifndef XE_HOME_H
#define XE_HOME_H
#include "xe/render.h"
#include "xe/data.h"
#ifdef __cplusplus
extern "C" {
#endif
void xe_home_init(void);
void xe_home_frame(double dt, XeRect area);
/* Returns 1 once when a title is chosen. If out is non-NULL, ownership of its
   heap fields transfers to the caller; release them using xe_data_free_meta(out)
   only if out is a heap-allocated XeMeta. For stack out, manually free fields or
   add a project helper that frees fields without freeing the struct. */
int xe_home_opened(XeMeta *out);
/* Returns 1 once after the Search button was chosen. */
int xe_home_take_search(void);
#ifdef __cplusplus
}
#endif
#endif
