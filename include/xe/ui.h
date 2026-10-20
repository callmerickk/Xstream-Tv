#ifndef XE_UI_H
#define XE_UI_H
#include "xe/platform.h"
#include "xe/render.h"
#include "xe/anim.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned long long XeId;
XeId xe_id(const char *s);
XeId xe_id_mix(XeId a, long long n);
void xe_ui_begin(double dt, const XeEvent *events, int n);
void xe_ui_end(void);
int xe_ui_focusable(XeId id, XeRect r);
int xe_ui_activated(XeId id);
void xe_ui_set_focus(XeId id);
XeId xe_ui_focus(void);
float xe_ui_focus_anim(XeId id);
typedef struct { XeId id; float offset; XeSpring spring; int count; float item_w, gap; } XeRow;
void xe_row_scroll_to(XeRow *row, int index, float view_w);
typedef struct { XeId id; float offset; XeSpring spring; int count; float item_h, gap; } XeColumn;
void xe_column_scroll_to(XeColumn *column, int index, float view_h);
/* Register each viewport per frame; offset is owned by the caller. */
void xe_ui_set_scroll_view(XeId id, XeRect viewport, float *offset, float content_extent, int vertical);
float xe_ui_pointer_scroll_delta(void);
#ifdef __cplusplus
}
#endif
#endif
