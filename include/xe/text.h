#ifndef XE_TEXT_H
#define XE_TEXT_H
#include "xe/render.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Text: HarfBuzz shaping + FreeType glyphs in a shared atlas. Render thread only.
 * Coordinates are pixels; (x, y) is the top of the first line box. */

typedef struct XeFont XeFont;

/* Low-level fonts (P4 contract). */
XeFont *xe_font_load(const char *asset_path, float px);
void xe_font_add_fallback(XeFont *f, XeFont *fallback);
typedef struct { float w, h, ascent; int lines; int truncated; } XeTextSize;
XeTextSize xe_text_measure(XeFont *f, const char *utf8, float max_w, int max_lines);
/* align: 0 left, 1 centre, 2 right (inside max_w). max_w <= 0 means no wrapping. */
void xe_text_draw(XeFont *f, const char *utf8, float x, float y, float max_w, int max_lines, int align, XeColor c);
void xe_text_shutdown(void);

/* App-level labels using the bundled Noto Sans family with Indic fallbacks.
 * weight: 400 regular, 500 medium, 600 semibold, 700 bold. Fonts are cached per size.
 * XE_FONT_BRAND selects Poppins Bold, the Xstream wordmark font. */
#define XE_FONT_BRAND 1000
XeFont *xe_font_get(float px, int weight);
void xe_label(const char *utf8, float x, float y, float px, XeColor c);
void xe_label_w(const char *utf8, float x, float y, float px, int weight, XeColor c);
void xe_label_box(const char *utf8, float x, float y, float px, int weight,
                  float max_w, int max_lines, int align, XeColor c);
XeTextSize xe_label_measure(const char *utf8, float px, int weight, float max_w, int max_lines);

#ifdef __cplusplus
}
#endif
#endif
