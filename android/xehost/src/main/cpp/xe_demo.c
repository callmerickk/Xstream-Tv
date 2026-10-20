#include "xe/platform.h"
#include <GLES2/gl2.h>
#include "xe_android.h"

static int g_colour = 0;
static const GLfloat colours[][3] = {
    { 0.08f, 0.12f, 0.22f }, { 0.72f, 0.12f, 0.16f }, { 0.12f, 0.62f, 0.28f },
    { 0.10f, 0.34f, 0.82f }, { 0.76f, 0.54f, 0.10f }
};
int xe_app_init(void) { g_colour = 0; xe_platform_set_wants_frames(0); return 0; }
void xe_app_frame(double dt)
{
    (void)dt;
    XeEvent ev;
    while (xe_platform_poll(&ev)) {
        if (ev.type == XE_EV_KEY_DOWN) {
            if (ev.key == XE_KEY_LEFT || ev.key == XE_KEY_UP) g_colour = (g_colour + 4) % 5;
            else if (ev.key == XE_KEY_RIGHT || ev.key == XE_KEY_DOWN) g_colour = (g_colour + 1) % 5;
            else if (ev.key == XE_KEY_BACK) xe_android_call_void("toast", "(Ljava/lang/String;)V", "Back pressed");
        }
    }
    glClearColor(colours[g_colour][0], colours[g_colour][1], colours[g_colour][2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    xe_platform_set_wants_frames(0);
}
void xe_app_shutdown(void) {}
