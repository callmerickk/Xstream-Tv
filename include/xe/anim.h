#ifndef XE_ANIM_H
#define XE_ANIM_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float value, target, velocity; float stiffness, damping; } XeSpring;
void xe_spring_init(XeSpring *s, float v, float stiffness, float damping);
void xe_spring_step(XeSpring *s, double dt);
int xe_spring_resting(const XeSpring *s);
float xe_ease_out_cubic(float t);
float xe_ease_in_out(float t);
typedef struct { float from, to; double start, dur; } XeTween;
float xe_tween_value(XeTween *t, double now);
int xe_anim_any_running(void);
#ifdef __cplusplus
}
#endif
#endif
