#ifndef XE_SPLASH_H
#define XE_SPLASH_H
#include "xe/render.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The Xstream start animation (same as the phone app). Draws a full frame; done after ~2.9 s. */
void xe_splash_frame(double dt, XeRect area);
int  xe_splash_done(void);
void xe_splash_skip(void);
void xe_splash_reset(void);
/* The Signal X mark at (x, y), side s. x1 / x2: how much of each X stroke is drawn (0..1);
 * wave: how far each of the three waves has appeared (NULL = all); alpha: overall opacity. */
void xe_draw_signal_mark(float x, float y, float s, float x1, float x2, const float wave[3], float alpha);
#ifdef __cplusplus
}
#endif
#endif
