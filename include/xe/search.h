#ifndef XE_SEARCH_H
#define XE_SEARCH_H
#include "xe/render.h"
#include "xe/data.h"
#ifdef __cplusplus
extern "C" {
#endif
void xe_search_frame(double dt, XeRect area);
/* Returns 1 once when a result is selected; the caller owns heap fields in out. */
int xe_search_opened(XeMeta *out);
#ifdef __cplusplus
}
#endif
#endif
