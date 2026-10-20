/* Desktop host (Windows, and Linux for CI): SDL2 window + GL 3.3 core context,
 * vsync, high-DPI, F11 fullscreen. Test options:
 *   --frames N            quit after N frames
 *   --script "T:ACTION;…" at T seconds: UP/DOWN/LEFT/RIGHT/OK/BACK/MENU/SEARCH/DELETE,
 *                         TEXT=abc, SHOT=file.png (save the window), QUIT        */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "xe/platform.h"

static int   g_w, g_h;
static float g_density = 1.0f;
static float g_ptr_scale = 1.0f; /* window coords -> drawable pixels */
static int   g_search_active;    /* Ctrl+F is held */

/* ------------------------------------------------------------------ helpers */

static void push_simple(XeEventType t)
{
    XeEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.type = t;
    xe_platform_push_event(&ev);
}

static void push_key(XeEventType t, XeKey k, int repeat)
{
    XeEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.type = t;
    ev.key = k;
    ev.repeat = repeat;
    xe_platform_push_event(&ev);
}

static void push_text(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned cp;
        int n, i;
        XeEvent ev;
        if (*p < 0x80)                { cp = *p;        n = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 3; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; n = 4; }
        else { p++; continue; }
        for (i = 1; i < n; i++) {
            if ((p[i] & 0xC0) != 0x80) { n = i; cp = 0xFFFD; break; }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        memset(&ev, 0, sizeof ev);
        ev.type = XE_EV_TEXT;
        ev.codepoint = cp;
        xe_platform_push_event(&ev);
        p += n;
    }
}

static XeKey map_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP:        return XE_KEY_UP;
    case SDLK_DOWN:      return XE_KEY_DOWN;
    case SDLK_LEFT:      return XE_KEY_LEFT;
    case SDLK_RIGHT:     return XE_KEY_RIGHT;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return XE_KEY_OK;
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:   return XE_KEY_BACK;
    case SDLK_BACKSPACE: return XE_KEY_DELETE;
    case SDLK_SPACE:
    case SDLK_AUDIOPLAY: return XE_KEY_PLAYPAUSE;
    case SDLK_m:         return XE_KEY_MENU;
    case SDLK_AUDIOFASTFORWARD: return XE_KEY_FF;
    case SDLK_AUDIOREWIND:      return XE_KEY_RW;
    default:             return XE_KEY_NONE;
    }
}

static void toggle_fullscreen(SDL_Window *w)
{
    Uint32 f = SDL_GetWindowFlags(w);
    int fs = (f & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (SDL_SetWindowFullscreen(w, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
        xe_log(2, "fullscreen toggle failed: %s", SDL_GetError());
}

static float query_density(SDL_Window *win)
{
    int ww = 0, wh = 0, dw = 0, dh = 0;
    float d = 1.0f;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    if (ww > 0) d = (float)dw / (float)ww;
    if (d < 1.01f) {
        /* Windows reports window size in real pixels; derive scale from DPI. */
        float ddpi = 0.0f;
        int idx = SDL_GetWindowDisplayIndex(win);
        if (idx >= 0 && SDL_GetDisplayDPI(idx, &ddpi, NULL, NULL) == 0 && ddpi > 0.0f)
            d = ddpi / 96.0f;
    }
    return d < 0.5f ? 1.0f : d;
}

static void update_size(SDL_Window *win, int force)
{
    int ww = 0, wh = 0, dw = 0, dh = 0;
    float d;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    if (dw <= 0 || dh <= 0) return; /* minimised */
    d = query_density(win);
    g_ptr_scale = ww > 0 ? (float)dw / (float)ww : 1.0f;
    if (!force && dw == g_w && dh == g_h && d == g_density) return;
    g_w = dw;
    g_h = dh;
    g_density = d;
    xe_platform_set_size(dw, dh, d);
    {
        XeEvent ev;
        memset(&ev, 0, sizeof ev);
        ev.type = XE_EV_RESIZE;
        ev.w = dw;
        ev.h = dh;
        xe_platform_push_event(&ev);
    }
}

static void make_dir_utf8(const char *path)
{
#ifdef _WIN32
    wchar_t w[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) _wmkdir(w);
#else
    mkdir(path, 0700);
#endif
}

/* ------------------------------------------------------------ test scripts */

typedef struct { double t; char what[200]; int done; } Step;
static Step g_steps[64];
static int  g_nsteps;

static void parse_script(const char *s)
{
    while (s && *s && g_nsteps < 64) {
        const char *end = strchr(s, ';');
        size_t n = end ? (size_t)(end - s) : strlen(s);
        char buf[220];
        char *colon;
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, s, n); buf[n] = 0;
        colon = strchr(buf, ':');
        if (colon) {
            *colon = 0;
            g_steps[g_nsteps].t = atof(buf);
            snprintf(g_steps[g_nsteps].what, sizeof g_steps[0].what, "%s", colon + 1);
            g_nsteps++;
        }
        s = end ? end + 1 : NULL;
    }
}

static void save_shot(const char *file, int w, int h)
{
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    int y;
    if (!px) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (y = 0; y < h / 2; y++) { /* GL rows start at the bottom */
        unsigned char tmp[4 * 4096];
        size_t row = (size_t)w * 4;
        if (row > sizeof tmp) break;
        memcpy(tmp, px + y * row, row);
        memcpy(px + y * row, px + (h - 1 - y) * row, row);
        memcpy(px + (h - 1 - y) * row, tmp, row);
    }
    for (y = 0; y < w * h; y++) px[y * 4 + 3] = 255;
    if (stbi_write_png(file, w, h, 4, px, w * 4)) xe_log(1, "saved %s", file);
    free(px);
}

static XeKey key_by_name(const char *s)
{
    static const char *names[] = { "NONE","UP","DOWN","LEFT","RIGHT","OK","BACK","MENU",
                                   "PLAYPAUSE","FF","RW","SEARCH","DELETE" };
    int i;
    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (strcmp(s, names[i]) == 0) return (XeKey)i;
    return XE_KEY_NONE;
}

/* Returns 0 when the script asks to quit. Shots are taken after the frame is drawn. */
static int run_script(double elapsed, int after_draw, int w, int h)
{
    int i;
    for (i = 0; i < g_nsteps; i++) {
        Step *s = &g_steps[i];
        int is_shot = strncmp(s->what, "SHOT=", 5) == 0;
        if (s->done || elapsed < s->t || is_shot != after_draw) continue;
        s->done = 1;
        if (is_shot) save_shot(s->what + 5, w, h);
        else if (strcmp(s->what, "QUIT") == 0) return 0;
        else if (strncmp(s->what, "TEXT=", 5) == 0) {
            const unsigned char *p = (const unsigned char *)s->what + 5;
            for (; *p; p++) {
                XeEvent ev; memset(&ev, 0, sizeof ev);
                ev.type = XE_EV_TEXT; ev.codepoint = *p;
                xe_platform_push_event(&ev);
            }
        } else {
            XeEvent ev; memset(&ev, 0, sizeof ev);
            ev.type = XE_EV_KEY_DOWN; ev.key = key_by_name(s->what);
            xe_platform_push_event(&ev);
            ev.type = XE_EV_KEY_UP;
            xe_platform_push_event(&ev);
        }
    }
    return 1;
}

static void setup_dirs(void)
{
    char *pref = SDL_GetPrefPath("xe", "xe");
    char data[1024], cache[1100];
    size_t n;
    if (!pref) {
        xe_log(2, "SDL_GetPrefPath failed: %s", SDL_GetError());
        return;
    }
    snprintf(data, sizeof data, "%s", pref);
    SDL_free(pref);
    n = strlen(data);
    while (n > 0 && (data[n - 1] == '\\' || data[n - 1] == '/')) data[--n] = 0;
#ifdef _WIN32
    snprintf(cache, sizeof cache, "%s\\cache", data);
#else
    snprintf(cache, sizeof cache, "%s/cache", data);
#endif
    make_dir_utf8(cache);
    xe_platform_set_dirs(data, cache);
}

/* --------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_GLContext ctx;
    int i, running = 1, minimized = 0, vsync_ok;
    unsigned long frames = 0, max_frames = 0;
    double last;
    float d0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            max_frames = strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc)
            parse_script(argv[++i]);
    }

    SDL_SetMainReady();
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        xe_log(3, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    win = SDL_CreateWindow("Xstream TV", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        xe_log(3, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* On high-DPI displays make the window physically ~1280x720 logical. */
    d0 = query_density(win);
    if (d0 > 1.01f && (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) == 0) {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(win, &ww, &wh);
        if ((float)ww / 1280.0f < d0 * 0.99f && ww == 1280) {
            SDL_SetWindowSize(win, (int)(1280 * d0), (int)(720 * d0));
            SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }

    ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        xe_log(3, "GL 3.3 core context failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(win, ctx);

    vsync_ok = (SDL_GL_SetSwapInterval(1) == 0);
    if (!vsync_ok) xe_log(2, "vsync unavailable: %s", SDL_GetError());

    setup_dirs();
    {
        char *base = SDL_GetBasePath();
        char dir[1100];
        snprintf(dir, sizeof dir, "%sassets", base ? base : "");
        xe_platform_set_asset_dir(dir);
        {   /* the Xstream mark as the window / taskbar icon */
            char icon[1200];
            SDL_Surface *ic;
            snprintf(icon, sizeof icon, "%s/brand/icon-128.bmp", dir);
            ic = SDL_LoadBMP(icon);
            if (ic) { SDL_SetWindowIcon(win, ic); SDL_FreeSurface(ic); }
        }
        if (base) SDL_free(base);
    }
    update_size(win, 1);

    xe_log(1, "GL_VERSION  : %s", (const char *)glGetString(GL_VERSION));
    xe_log(1, "GL_RENDERER : %s", (const char *)glGetString(GL_RENDERER));
    xe_log(1, "drawable %dx%d density %.2f", g_w, g_h, g_density);

    if (xe_app_init() != 0) {
        xe_log(3, "xe_app_init failed");
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 2;
    }

    last = xe_time_now();
    {
    double started = last;
    while (running) {
        SDL_Event e;
        double now, dt;

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                push_simple(XE_EV_QUIT);
                running = 0; /* the app still gets one more frame to see it */
                break;

            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    minimized = 1;
                    push_simple(XE_EV_PAUSE);
                } else if (e.window.event == SDL_WINDOWEVENT_RESTORED && minimized) {
                    minimized = 0;
                    push_simple(XE_EV_RESUME);
                }
                break;

            case SDL_KEYDOWN: {
                SDL_Keycode sym = e.key.keysym.sym;
                if (sym == SDLK_F11) {
                    if (!e.key.repeat) toggle_fullscreen(win);
                    break;
                }
                if (sym == SDLK_f && (e.key.keysym.mod & KMOD_CTRL)) {
                    g_search_active = 1;
                    push_key(XE_EV_KEY_DOWN, XE_KEY_SEARCH, e.key.repeat != 0);
                    break;
                }
                {
                    XeKey k = map_key(sym);
                    if (k != XE_KEY_NONE) push_key(XE_EV_KEY_DOWN, k, e.key.repeat != 0);
                }
                break;
            }

            case SDL_KEYUP: {
                SDL_Keycode sym = e.key.keysym.sym;
                if (sym == SDLK_f && g_search_active) {
                    g_search_active = 0;
                    push_key(XE_EV_KEY_UP, XE_KEY_SEARCH, 0);
                    break;
                }
                {
                    XeKey k = map_key(sym);
                    if (k != XE_KEY_NONE) push_key(XE_EV_KEY_UP, k, 0);
                }
                break;
            }

            case SDL_TEXTINPUT:
                push_text(e.text.text);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    XeEvent ev;
                    memset(&ev, 0, sizeof ev);
                    ev.type = e.type == SDL_MOUSEBUTTONDOWN ? XE_EV_POINTER_DOWN : XE_EV_POINTER_UP;
                    ev.x = (float)e.button.x * g_ptr_scale;
                    ev.y = (float)e.button.y * g_ptr_scale;
                    xe_platform_push_event(&ev);
                }
                break;

            case SDL_MOUSEMOTION: {
                XeEvent ev;
                memset(&ev, 0, sizeof ev);
                ev.type = XE_EV_POINTER_MOVE;
                ev.x = (float)e.motion.x * g_ptr_scale;
                ev.y = (float)e.motion.y * g_ptr_scale;
                ev.dx = (float)e.motion.xrel * g_ptr_scale;
                ev.dy = (float)e.motion.yrel * g_ptr_scale;
                xe_platform_push_event(&ev);
                break;
            }

            case SDL_MOUSEWHEEL: {
                XeEvent ev;
                int mx = 0, my = 0;
                float sgn = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                SDL_GetMouseState(&mx, &my);
                memset(&ev, 0, sizeof ev);
                ev.type = XE_EV_SCROLL;
                ev.x = (float)mx * g_ptr_scale;
                ev.y = (float)my * g_ptr_scale;
                ev.dx = sgn * (float)e.wheel.x;
                ev.dy = sgn * (float)e.wheel.y;
                xe_platform_push_event(&ev);
                break;
            }

            default:
                break;
            }
        }

        update_size(win, 0);
        if (g_nsteps && !run_script(xe_time_now() - started, 0, g_w, g_h)) running = 0;

        now = xe_time_now();
        dt = now - last;
        last = now;
        if (dt < 0.0) dt = 0.0;
        if (dt > 0.25) dt = 0.25;

        glViewport(0, 0, g_w, g_h);
        xe_app_frame(dt);
        if (g_nsteps) run_script(xe_time_now() - started, 1, g_w, g_h);
        SDL_GL_SwapWindow(win);

        if (minimized) SDL_Delay(50);
        else if (!vsync_ok) SDL_Delay(1);

        frames++;
        if (max_frames && frames >= max_frames) running = 0;
    }
    }

    xe_app_shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
