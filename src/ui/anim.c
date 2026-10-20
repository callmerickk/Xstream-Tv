#include "xe/anim.h"
#include <math.h>
#define XE_ANIM_EPS 0.001f
static int g_running;
void xe_spring_init(XeSpring *s,float v,float stiffness,float damping) {
    if(!s) return; s->value=v; s->target=v; s->velocity=0;
    s->stiffness=stiffness>0?stiffness:180.0f; s->damping=damping>=0?damping:24.0f;
}
void xe_spring_step(XeSpring *s,double dt) {
    float d,a; if(!s||dt<=0) return; if(dt>0.05) dt=0.05; d=(float)dt;
    /* Cap dt so a resume after a pause cannot kick the spring far off target. */
    a=(s->target-s->value)*s->stiffness-s->velocity*s->damping;
    s->velocity+=a*d; s->value+=s->velocity*d;
    if(fabsf(s->target-s->value)<XE_ANIM_EPS && fabsf(s->velocity)<XE_ANIM_EPS) {
        s->value=s->target; s->velocity=0;
    }
}
int xe_spring_resting(const XeSpring *s) {
    return !s || (fabsf(s->target-s->value)<XE_ANIM_EPS && fabsf(s->velocity)<XE_ANIM_EPS);
}
float xe_ease_out_cubic(float t) { float u; if(t<0)t=0; if(t>1)t=1; u=1-t; return 1-u*u*u; }
float xe_ease_in_out(float t) {
    if(t<0)t=0; if(t>1)t=1;
    return t<0.5f ? 4*t*t*t : 1.0f-(float)(pow((double)(-2*t+2),3.0)*0.5);
}
float xe_tween_value(XeTween *t,double now) {
    float p; if(!t)return 0; if(t->dur<=0)return t->to;
    p=(float)((now-t->start)/t->dur); if(p<=0)return t->from; if(p>=1)return t->to;
    return t->from+(t->to-t->from)*xe_ease_out_cubic(p);
}
int xe_anim_any_running(void) { return g_running; }
void xe_anim_internal_set_running(int running) { g_running=running!=0; }
