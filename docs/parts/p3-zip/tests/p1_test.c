#include "xe/platform.h"
#include <SDL_opengl.h>

#include <stdio.h>

static float red = 0.12f, green = 0.18f, blue = 0.28f;

/* Weak defaults let this test file be replaced by a real app without changing the host. */
#if defined(_MSC_VER)
#define XE_WEAK
#else
#define XE_WEAK __attribute__((weak))
#endif

XE_WEAK int xe_app_init(void)
{
    xe_log(1, "P1 test started: use arrow keys to change clear colour; F11 toggles fullscreen.");
    return 1;
}

XE_WEAK void xe_app_frame(double dt)
{
    (void)dt;
    XeEvent ev;
    while (xe_platform_poll(&ev)) {
        printf("event type=%d key=%d repeat=%d codepoint=%u x=%.1f y=%.1f dx=%.1f dy=%.1f size=%dx%d\n",
               (int)ev.type, (int)ev.key, ev.repeat, ev.codepoint, ev.x, ev.y, ev.dx, ev.dy, ev.w, ev.h);
        fflush(stdout);
        if (ev.type == XE_EV_KEY_DOWN) {
            switch (ev.key) {
            case XE_KEY_UP: green += 0.08f; break;
            case XE_KEY_DOWN: green -= 0.08f; break;
            case XE_KEY_LEFT: red -= 0.08f; break;
            case XE_KEY_RIGHT: red += 0.08f; break;
            default: break;
            }
            if (red < 0.0f) red = 0.0f; if (red > 1.0f) red = 1.0f;
            if (green < 0.0f) green = 0.0f; if (green > 1.0f) green = 1.0f;
        }
    }
    int w, h; float density;
    xe_platform_size(&w, &h, &density);
    glViewport(0, 0, w, h);
    glClearColor(red, green, blue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

XE_WEAK void xe_app_shutdown(void)
{
    xe_log(1, "P1 test stopped.");
}
