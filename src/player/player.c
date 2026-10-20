/* Desktop player backend: one libmpv instance, rendered with mpv's OpenGL render API
 * straight into the window before the XE overlay is drawn on top.
 * libmpv is loaded at runtime (libmpv-2.dll on Windows, libmpv.so.2 on Linux), so the
 * app still starts, and says so, when the library is missing. Android uses
 * android/.../player_android.c instead. */
#if !defined(__ANDROID__)
#include "xe/player.h"
#include "xe/platform.h"
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define LIBMPV_NAME "libmpv-2.dll"
static HMODULE g_lib;
static void *lib_open(void) { g_lib = LoadLibraryA(LIBMPV_NAME); if (!g_lib) g_lib = LoadLibraryA("mpv-2.dll"); return g_lib; }
static void *lib_sym(const char *n) { return (void *)GetProcAddress(g_lib, n); }
static void *gl_proc(void *ctx, const char *name)
{
    void *p = (void *)wglGetProcAddress(name);
    static HMODULE gl;
    (void)ctx;
    if (!p || p == (void *)1 || p == (void *)2 || p == (void *)3 || p == (void *)-1) {
        if (!gl) gl = LoadLibraryA("opengl32.dll");
        p = gl ? (void *)GetProcAddress(gl, name) : NULL;
    }
    return p;
}
#else
#include <dlfcn.h>
#define LIBMPV_NAME "libmpv.so.2"
static void *g_lib;
static void *lib_open(void) { g_lib = dlopen(LIBMPV_NAME, RTLD_NOW); if (!g_lib) g_lib = dlopen("libmpv.so", RTLD_NOW); return g_lib; }
static void *lib_sym(const char *n) { return dlsym(g_lib, n); }
/* SDL owns the GL context on desktop Linux; ask it for entry points. */
extern void *SDL_GL_GetProcAddress(const char *proc);
static void *gl_proc(void *ctx, const char *name) { (void)ctx; return SDL_GL_GetProcAddress(name); }
#endif

/* The handful of libmpv entry points we use. */
static struct {
    mpv_handle *(*create)(void);
    int (*initialize)(mpv_handle *);
    void (*terminate_destroy)(mpv_handle *);
    int (*set_option_string)(mpv_handle *, const char *, const char *);
    int (*command)(mpv_handle *, const char **);
    int (*command_async)(mpv_handle *, uint64_t, const char **);
    int (*get_property)(mpv_handle *, const char *, mpv_format, void *);
    int (*set_property)(mpv_handle *, const char *, mpv_format, void *);
    int (*set_property_string)(mpv_handle *, const char *, const char *);
    void (*free_node_contents)(mpv_node *);
    mpv_event *(*wait_event)(mpv_handle *, double);
    int (*render_context_create)(mpv_render_context **, mpv_handle *, mpv_render_param *);
    void (*render_context_set_update_callback)(mpv_render_context *, mpv_render_update_fn, void *);
    uint64_t (*render_context_update)(mpv_render_context *);
    int (*render_context_render)(mpv_render_context *, mpv_render_param *);
    void (*render_context_free)(mpv_render_context *);
} M;
static int g_loaded = -1;          /* -1 unknown, 0 missing, 1 ready */

static int load_mpv(void)
{
    if (g_loaded >= 0) return g_loaded;
    g_loaded = 0;
    if (!lib_open()) { xe_log(3, "player: %s not found next to the app", LIBMPV_NAME); return 0; }
#define L(field, name) *(void **)&M.field = lib_sym(name); if (!M.field) { xe_log(3, "player: libmpv lacks %s", name); return 0; }
    L(create, "mpv_create") L(initialize, "mpv_initialize") L(terminate_destroy, "mpv_terminate_destroy")
    L(set_option_string, "mpv_set_option_string") L(command, "mpv_command") L(command_async, "mpv_command_async")
    L(get_property, "mpv_get_property") L(set_property, "mpv_set_property")
    L(set_property_string, "mpv_set_property_string") L(free_node_contents, "mpv_free_node_contents")
    L(wait_event, "mpv_wait_event")
    L(render_context_create, "mpv_render_context_create")
    L(render_context_set_update_callback, "mpv_render_context_set_update_callback")
    L(render_context_update, "mpv_render_context_update")
    L(render_context_render, "mpv_render_context_render") L(render_context_free, "mpv_render_context_free")
#undef L
    g_loaded = 1;
    return 1;
}

static mpv_handle *g_mpv;
static mpv_render_context *g_rc;
static int g_active, g_error, g_loaded_file;
static char g_err[160];

static void on_update(void *ctx) { (void)ctx; }

/* mpv wants "Name: value" entries separated by commas; commas inside values are escaped. */
static void set_headers(const char *const *h)
{
    char buf[4096];
    size_t n = 0;
    int i;
    buf[0] = 0;
    for (i = 0; h && h[i] && h[i + 1]; i += 2) {
        const char *v = h[i + 1];
        if (n && n + 1 < sizeof buf) buf[n++] = ',';
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s: ", h[i]);
        for (; *v && n + 3 < sizeof buf; v++) {
            if (*v == ',' || *v == '\\') buf[n++] = '\\';
            if (*v != '\n' && *v != '\r') buf[n++] = *v;
        }
        buf[n] = 0;
        if (!strcmp(h[i], "User-Agent")) M.set_option_string(g_mpv, "user-agent", h[i + 1]);
        if (!strcmp(h[i], "Referer")) M.set_option_string(g_mpv, "referrer", h[i + 1]);
    }
    if (n) M.set_option_string(g_mpv, "http-header-fields", buf);
}

static void destroy(void)
{
    if (g_rc) { M.render_context_set_update_callback(g_rc, NULL, NULL); M.render_context_free(g_rc); g_rc = NULL; }
    if (g_mpv) { M.terminate_destroy(g_mpv); g_mpv = NULL; }
    g_active = 0; g_loaded_file = 0;
}

void xe_player_open(const char *url, const char *const *headers, double start_sec)
{
    const char *cmd[] = { "loadfile", url, NULL };
    xe_player_close();
    g_error = 0; g_err[0] = 0;
    if (!url || !*url) return;
    if (!load_mpv()) { g_error = 1; snprintf(g_err, sizeof g_err, "Video engine missing (%s)", LIBMPV_NAME); g_active = 1; return; }
    g_mpv = M.create();
    if (!g_mpv) { g_error = 1; snprintf(g_err, sizeof g_err, "Could not start the video engine"); g_active = 1; return; }
    M.set_option_string(g_mpv, "vo", "libmpv");
    M.set_option_string(g_mpv, "hwdec", "auto-safe");
    M.set_option_string(g_mpv, "cache", "yes");
    M.set_option_string(g_mpv, "demuxer-max-bytes", "150MiB");
    M.set_option_string(g_mpv, "demuxer-max-back-bytes", "50MiB");
    M.set_option_string(g_mpv, "keep-open", "yes");
    M.set_option_string(g_mpv, "sub-auto", "fuzzy");
    M.set_option_string(g_mpv, "osd-level", "0");
    if (getenv("XE_MPV_AO")) M.set_option_string(g_mpv, "ao", getenv("XE_MPV_AO"));
    M.set_option_string(g_mpv, "user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Xstream");
    if (start_sec > 1) { char s[48]; snprintf(s, sizeof s, "%.3f", start_sec); M.set_option_string(g_mpv, "start", s); }
    set_headers(headers);
    if (M.initialize(g_mpv) < 0) { destroy(); g_error = 1; snprintf(g_err, sizeof g_err, "Could not start the video engine"); g_active = 1; return; }
    {
        mpv_opengl_init_params gl = { gl_proc, NULL };
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_OPENGL },
            { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl },
            { MPV_RENDER_PARAM_INVALID, NULL } };
        if (M.render_context_create(&g_rc, g_mpv, params) < 0) {
            destroy(); g_error = 1; snprintf(g_err, sizeof g_err, "Video output could not start"); g_active = 1; return;
        }
        M.render_context_set_update_callback(g_rc, on_update, NULL);
    }
    M.command_async(g_mpv, 0, cmd);
    g_active = 1;
}

void xe_player_toggle(void)
{
    int pause = 0;
    if (!g_mpv) return;
    M.get_property(g_mpv, "pause", MPV_FORMAT_FLAG, &pause);
    pause = !pause;
    M.set_property(g_mpv, "pause", MPV_FORMAT_FLAG, &pause);
}

void xe_player_seek(double sec)
{
    char v[48];
    const char *cmd[] = { "seek", v, "relative", NULL };
    if (!g_mpv) return;
    snprintf(v, sizeof v, "%.3f", sec);
    M.command_async(g_mpv, 0, cmd);
}

void xe_player_close(void) { if (g_loaded == 1) destroy(); g_active = 0; }
int xe_player_active(void) { return g_active; }
const char *xe_player_error(void) { return g_error ? g_err : NULL; }

int xe_player_paused(void)
{
    int v = 0;
    if (g_mpv) M.get_property(g_mpv, "pause", MPV_FORMAT_FLAG, &v);
    return v;
}
double xe_player_pos(void) { double v = 0; if (g_mpv) M.get_property(g_mpv, "time-pos", MPV_FORMAT_DOUBLE, &v); return v; }
double xe_player_dur(void) { double v = 0; if (g_mpv) M.get_property(g_mpv, "duration", MPV_FORMAT_DOUBLE, &v); return v; }

int xe_player_buffering(void)
{
    int idle = 0, cache = 0;
    if (g_error) return 0;
    if (!g_mpv) return 0;
    if (!g_loaded_file) return 1;
    M.get_property(g_mpv, "paused-for-cache", MPV_FORMAT_FLAG, &cache);
    M.get_property(g_mpv, "seeking", MPV_FORMAT_FLAG, &idle);
    return cache || idle;
}

static const char *kinds[] = { "audio", "sub", "video" };

int xe_player_tracks(int kind, char names[][64], int max)
{
    mpv_node list;
    int n = 0, i, j;
    if (!g_mpv || kind < 0 || kind > 2 || max < 1) return 0;
    if (M.get_property(g_mpv, "track-list", MPV_FORMAT_NODE, &list) < 0) return 0;
    if (list.format == MPV_FORMAT_NODE_ARRAY)
        for (i = 0; i < list.u.list->num && n < max; i++) {
            mpv_node *t = &list.u.list->values[i];
            const char *type = NULL, *title = NULL, *lang = NULL;
            int sel = 0;
            if (t->format != MPV_FORMAT_NODE_MAP) continue;
            for (j = 0; j < t->u.list->num; j++) {
                const char *k = t->u.list->keys[j];
                mpv_node *v = &t->u.list->values[j];
                if (!strcmp(k, "type") && v->format == MPV_FORMAT_STRING) type = v->u.string;
                else if (!strcmp(k, "title") && v->format == MPV_FORMAT_STRING) title = v->u.string;
                else if (!strcmp(k, "lang") && v->format == MPV_FORMAT_STRING) lang = v->u.string;
                else if (!strcmp(k, "selected") && v->format == MPV_FORMAT_FLAG) sel = v->u.flag;
            }
            if (!type || strcmp(type, kinds[kind])) continue;
            snprintf(names[n++], 64, "%s%s%s%s", title ? title : (lang ? lang : "Track"),
                     title && lang ? " · " : "", title && lang ? lang : "", sel ? "  \xE2\x80\xA2" : "");
        }
    M.free_node_contents(&list);
    return n;
}

void xe_player_select(int kind, int index)
{
    char v[16];
    if (!g_mpv || kind < 0 || kind > 2) return;
    snprintf(v, sizeof v, "%d", index + 1);       /* mpv track ids start at 1 */
    M.set_property_string(g_mpv, kind == 0 ? "aid" : kind == 1 ? "sid" : "vid", v);
}

/* Drains mpv events and draws the current video frame into the default framebuffer. */
void xe_player_render_video(int w, int h)
{
    mpv_event *e;
    if (!g_mpv) return;
    while ((e = M.wait_event(g_mpv, 0)) && e->event_id != MPV_EVENT_NONE) {
        if (e->event_id == MPV_EVENT_FILE_LOADED || e->event_id == MPV_EVENT_PLAYBACK_RESTART) g_loaded_file = 1;
        if (e->event_id == MPV_EVENT_END_FILE) {
            mpv_event_end_file *ef = (mpv_event_end_file *)e->data;
            if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
                g_error = 1;
                snprintf(g_err, sizeof g_err, "This stream could not be played");
            }
        }
    }
    if (g_rc) {
        mpv_opengl_fbo fbo = { 0, w, h, 0 };
        int flip = 1;
        mpv_render_param rp[] = { { MPV_RENDER_PARAM_OPENGL_FBO, &fbo }, { MPV_RENDER_PARAM_FLIP_Y, &flip }, { MPV_RENDER_PARAM_INVALID, NULL } };
        M.render_context_update(g_rc);
        M.render_context_render(g_rc, rp);
    }
}
#endif
