#ifndef XE_RENDER_H
#define XE_RENDER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float r, g, b, a; } XeColor;
typedef struct { float x, y, w, h; } XeRect;
typedef struct XeTexture { unsigned id; int w, h; int external; } XeTexture;
void xe_r_init(void); void xe_r_shutdown(void);
/* Physical framebuffer size. When set, xe_r_begin(w, h) treats w x h as the logical
 * canvas and scales it (by output height / h) onto the framebuffer. */
void xe_r_set_output(int fb_w, int fb_h);
void xe_r_clear(XeColor c);   /* clears the whole window */
void xe_r_output_size(int *fb_w, int *fb_h);
void xe_r_begin(int fb_w, int fb_h); void xe_r_end(void);
void xe_r_rect(XeRect r, float radius, XeColor c);
void xe_r_rect_vgrad(XeRect r, float radius, XeColor top, XeColor bottom);
void xe_r_rect_hgrad(XeRect r, float radius, XeColor left, XeColor right);
void xe_r_border(XeRect r, float radius, float width, XeColor c);
void xe_r_border_progress(XeRect r, float radius, float width, float progress, XeColor c);
void xe_r_glow(XeRect r, float radius, float spread, XeColor c);
void xe_r_image(const XeTexture *t, XeRect dst, float radius, float alpha, int contain);
/* Draws the texture area (u0,v0)-(u1,v1) multiplied by tint; used for glyphs and icons. */
void xe_r_image_tint(const XeTexture *t, XeRect dst, float radius, XeColor tint, float u0, float v0, float u1, float v1);
void xe_r_image_blurred(const XeTexture *t, XeRect dst, float strength, float alpha);
void xe_r_line(float x0, float y0, float x1, float y1, float width, XeColor c);   /* round caps, any angle */
/* Stroke along a circle: angles in radians, 0 = right, growing clockwise on screen. */
void xe_r_arc(float cx, float cy, float radius, float a0, float a1, float width, XeColor c);
void xe_r_push_clip(XeRect r, float radius); void xe_r_pop_clip(void);
void xe_r_push_layer(float tx, float ty, float scale, float alpha); void xe_r_pop_layer(void);
/* Scale of the innermost layer (1 when none); text uses it to rasterise crisply. */
float xe_r_current_scale(void);
XeTexture *xe_tex_create(int w, int h, const void *rgba); void xe_tex_update(XeTexture *t, const void *rgba);
/* Uploads rows [y0, y0+rows) from a full-width RGBA buffer that starts at row y0. */
void xe_tex_update_rows(XeTexture *t, int y0, int rows, const void *rgba_rows);
void xe_tex_destroy(XeTexture *t); size_t xe_tex_bytes_in_use(void);
#ifdef __cplusplus
}
#endif
#endif
