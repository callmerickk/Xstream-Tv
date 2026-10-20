/* Start animation, the same as Xstream on phones (Startup.kt StartupSplash + Look.kt SignalMark "broadcast"):
 * black -> red / purple glow, the Signal X mark pops in while its X draws stroke by stroke and the waves ripple
 * out, the red X pops, "stream" slides out from behind it; then the mark and "stream" fade, the red X zooms
 * towards you while fading, and the glow fades out. About 2.9 s, once per start. */
#include "xe/splash.h"
#include "xe/render.h"
#include "xe/text.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

static const XeColor BRAND_RED = { 0.898f, 0.035f, 0.078f, 1 };

/* A spring like Compose's spring(dampingRatio, stiffness), started at time t0 (s), value 0 -> 1. */
typedef struct { float t0, damping, stiffness, x, v; int started; } Spring;

static struct {
    double t;
    int done, started;
    Spring mark, x;
} SP;

static void spring_step(Spring *s, double t, float dt)
{
    float k, c;
    int i;
    if (t < s->t0) return;
    if (!s->started) { s->started = 1; s->x = 0; s->v = 0; }
    k = s->stiffness;
    c = 2.0f * s->damping * sqrtf(k);
    for (i = 0; i < 8; i++) {                   /* small sub-steps keep it stable at low frame rates */
        float h = dt / 8.0f, a = -k * (s->x - 1.0f) - c * s->v;
        s->v += a * h;
        s->x += s->v * h;
    }
}

/* CSS-style cubic-bezier easing (x1,y1,x2,y2), solved for x by bisection. */
static float bezier(float x1, float y1, float x2, float y2, float x)
{
    float lo = 0, hi = 1, t = x;
    int i;
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    for (i = 0; i < 24; i++) {
        float u = 1 - t, bx = 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t;
        if (bx < x) lo = t; else hi = t;
        t = (lo + hi) * 0.5f;
    }
    {
        float u = 1 - t;
        return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t;
    }
}
static float fast_out_slow_in(float x) { return bezier(0.4f, 0.0f, 0.2f, 1.0f, x); }

/* tween from a to b over [t0, t0+d] seconds with FastOutSlowIn (Compose's default). */
static float tween(double t, float t0, float d, float a, float b)
{
    float p = d > 0 ? (float)((t - t0) / d) : 1;
    if (p <= 0) return a;
    if (p >= 1) return b;
    return a + (b - a) * fast_out_slow_in(p);
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* Radial glow: stacked translucent discs, strongest in the middle. */
static void radial(float cx, float cy, float radius, XeColor c, float alpha)
{
    const int n = 14;
    int i;
    float a = 1.0f - powf(1.0f - c.a * alpha, 1.0f / n);
    for (i = n; i >= 1; i--) {
        float r = radius * (float)i / n;
        xe_r_rect((XeRect){ cx - r, cy - r, r * 2, r * 2 }, r, (XeColor){ c.r, c.g, c.b, a });
    }
}

/* The Signal X mark (Look.kt SignalMark), drawn at (x, y) with side s. */
void xe_draw_signal_mark(float x, float y, float s, float x1, float x2, const float wave[3], float alpha)
{
    float k = s / 100.0f;
    static const float radii[3] = { 20, 29, 38 }, alphas[3] = { 1.0f, 0.7f, 0.4f };
    XeColor red = BRAND_RED;
    int i;
    red.a = alpha;
    xe_r_rect((XeRect){ x, y, s, s }, 23 * k, (XeColor){ 0.071f, 0.071f, 0.086f, alpha });
    if (x1 > 0) xe_r_line(x + 22 * k, y + 36 * k, x + (22 + 24 * x1) * k, y + (36 + 28 * x1) * k, 9 * k, red);
    if (x2 > 0) xe_r_line(x + 22 * k, y + 64 * k, x + (22 + 24 * x2) * k, y + (64 - 28 * x2) * k, 9 * k, red);
    for (i = 0; i < 3; i++) {
        float ap = wave ? wave[i] : 1, sc, px = x + 34 * k, py = y + 50 * k, cx, cy;
        if (ap <= 0) continue;
        sc = 0.6f + 0.4f * ap;
        cx = px + (x + 40 * k - px) * sc;          /* scaled around (34, 50) like the Compose scale() block */
        cy = py + (y + 50 * k - py) * sc;
        xe_r_arc(cx, cy, radii[i] * k * sc, -40 * PI_F / 180, 40 * PI_F / 180, 6 * k * sc,
                 (XeColor){ 0.957f, 0.957f, 0.965f, clamp01(alphas[i] * ap) * alpha });
    }
}

void xe_splash_reset(void) { memset(&SP, 0, sizeof SP); }
int  xe_splash_done(void) { return SP.done; }
void xe_splash_skip(void) { SP.done = 1; }

void xe_splash_frame(double dt, XeRect area)
{
    double t;
    float u, bg, out, xa, zoom, word, markv, xv, x1, x2, wave[3], ms, row_w, xbox_w, word_w, rx, cy;
    int i;
    if (SP.done) return;
    if (!SP.started) {
        SP.started = 1;
        SP.mark = (Spring){ 0.26f, 0.55f, 260.0f, 0, 0, 0 };
        SP.x = (Spring){ 0.72f, 0.50f, 300.0f, 0, 0, 0 };
    }
    if (dt > 0.1) dt = 0.1;                        /* a slow first frame must not skip the animation */
    SP.t += dt;
    t = SP.t;
    spring_step(&SP.mark, t, (float)dt);
    spring_step(&SP.x, t, (float)dt);

    /* Timeline (seconds), as in StartupSplash. */
    bg = t < 2.21 ? tween(t, 0, 0.6f, 0, 1) : tween(t, 2.21f, 0.8f, 1, 0);
    word = tween(t, 0.96f, 0.48f, 0, 1);
    out = tween(t, 1.96f, 0.32f, 1, 0);
    xa = tween(t, 2.22f, 0.7f, 1, 0);
    zoom = t < 1.96 ? 1.0f : 1.0f + 25.0f * bezier(0.6f, 0, 0.8f, 0.4f, clamp01((float)((t - 1.96) / 0.95)));
    markv = SP.mark.x;
    xv = SP.x.x;
    /* SignalMark "broadcast": strokes 150-500 ms and 500-850 ms, then waves 150 ms apart over 600 ms each. */
    x1 = tween(t, 0.15f, 0.35f, 0, 1);
    x2 = tween(t, 0.50f, 0.35f, 0, 1);
    for (i = 0; i < 3; i++) wave[i] = tween(t, 0.85f + 0.15f * i, 0.6f, 0, 1);
    if (t >= 2.91) SP.done = 1;

    /* dp: a phone is ~400 dp wide; a TV ~960 x 540 dp. */
    u = area.w < area.h ? area.w / 400.0f : area.h / 540.0f;

    xe_r_begin((int)area.w, (int)area.h);
    xe_r_rect(area, 0, (XeColor){ 0, 0, 0, 1 });
    if (bg > 0) {
        float big = area.w > area.h ? area.w : area.h;
        xe_r_rect_vgrad((XeRect){ 0, 0, area.w, area.h * 0.5f }, 0,
                        (XeColor){ 0.078f, 0.024f, 0.047f, bg }, (XeColor){ 0.102f, 0.027f, 0.094f, bg });
        xe_r_rect_vgrad((XeRect){ 0, area.h * 0.5f, area.w, area.h * 0.5f }, 0,
                        (XeColor){ 0.102f, 0.027f, 0.094f, bg }, (XeColor){ 0.027f, 0.016f, 0.039f, bg });
        radial(area.w * 0.2f, area.h * 0.85f, big * 0.75f, (XeColor){ 0.898f, 0.035f, 0.078f, 0.55f }, bg);
        radial(area.w * 0.85f, area.h * 0.15f, big * 0.55f, (XeColor){ 0.471f, 0.157f, 0.745f, 0.45f }, bg);
    }

    /* Row: [mark 76] 14 [X box] [stream], centred, 20 dp above the middle. */
    ms = 76 * u;
    xbox_w = 44 * 0.62f * u;
    word_w = xe_label_measure("stream", 44 * u, XE_FONT_BRAND, 0, 1).w;
    row_w = ms + 14 * u + xbox_w + word_w;
    rx = area.x + (area.w - row_w) * 0.5f;
    cy = area.y + area.h * 0.5f - 20 * u;

    {   /* the mark pops (scale 0.5 -> 1) */
        float a = clamp01(markv) * out, sc = 0.5f + 0.5f * markv;
        if (a > 0.001f) {
            xe_r_push_layer(rx + ms * 0.5f, cy, sc, 1);
            xe_draw_signal_mark(-ms * 0.5f, -ms * 0.5f, ms, x1, x2, wave, a);
            xe_r_pop_layer();
        }
    }
    {   /* "stream" slides out from behind the X */
        float a = word * out;
        if (a > 0.001f) {
            float sx = rx + ms + 14 * u + xbox_w - (1 - word) * 18 * u;
            float th = xe_label_measure("stream", 44 * u, XE_FONT_BRAND, 0, 1).h;
            xe_label_box("stream", sx, cy - th * 0.5f, 44 * u, XE_FONT_BRAND, 0, 1, 0, (XeColor){ 1, 1, 1, a });
        }
    }
    {   /* the red X with its glow: drawn once at 6x and only scaled, like ZoomX */
        float a = clamp01(xv) * xa;
        if (a > 0.001f) {
            const float K = 6.0f;
            float s = (0.4f + 0.6f * xv) * zoom / K, fx = 44 * u * K, gw = fx * 1.5f;
            float cx = rx + ms + 14 * u + xbox_w * 0.5f;
            XeFont *f = xe_font_get(fx, XE_FONT_BRAND);
            xe_r_push_layer(cx, cy, s, a);
            radial(0, 0, gw * 0.5f, (XeColor){ 0.898f, 0.035f, 0.078f, 0.6f }, 1);
            if (f) {
                XeTextSize z = xe_text_measure(f, "X", 0, 1);
                xe_text_draw(f, "X", -z.w * 0.5f, -z.h * 0.5f, 0, 1, 0, BRAND_RED);
            }
            xe_r_pop_layer();
        }
    }
    xe_r_end();
}
