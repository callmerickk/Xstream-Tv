/* Xstream TV app: wires the screens together.
 *   Home -> Detail -> Sources -> Player, and Search from Home.
 * Everything is laid out on a 1280x720 logical canvas that the renderer scales to the
 * window, so TV, tablet and desktop share one layout. Back/Escape goes one screen back. */
#include "xe/platform.h"
#include "xe/render.h"
#include "xe/text.h"
#include "xe/ui.h"
#include "xe/jobs.h"
#include "xe/net.h"
#include "xe/images.h"
#include "xe/data.h"
#include "xe/home.h"
#include "xe/detail.h"
#include "xe/search.h"
#include "xe/player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGICAL_H 720.0f
#define MAX_ADDONS 32
#define MAX_STREAMS 400

typedef enum { SCR_HOME, SCR_DETAIL, SCR_SEARCH, SCR_SOURCES, SCR_PLAYER } Screen;

static Screen g_screen = SCR_HOME, g_before_sources = SCR_DETAIL, g_before_detail = SCR_HOME;
static XeId g_home_focus, g_detail_focus, g_sources_focus;
static float g_scale = 1.0f;

/* ------------------------------------------------------------- addons */

static char *g_addon_store[MAX_ADDONS];
static int g_addon_n;

static void load_addons(void)
{
    char path[1200], line[1024];
    FILE *f;
    g_addon_store[g_addon_n++] = "https://v3-cinemeta.strem.io/manifest.json";
    snprintf(path, sizeof path, "%s/addons.txt", xe_platform_data_dir());
    f = fopen(path, "r");
    if (!f) {
        f = fopen(path, "w");
        if (f) {
            fputs("# Xstream TV addons: one Stremio manifest link per line.\n"
                  "# Cinemeta (catalogs and details) is always included.\n", f);
            fclose(f);
        }
        xe_log(1, "addons: created %s", path);
        return;
    }
    while (fgets(line, sizeof line, f) && g_addon_n < MAX_ADDONS) {
        char *s = line, *e;
        while (*s == ' ' || *s == '\t') s++;
        e = s + strlen(s);
        while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        if (!*s || *s == '#') continue;
        if (strncmp(s, "stremio://", 10) == 0) { memmove(s + 8, s + 10, strlen(s + 10) + 1); memcpy(s, "https://", 8); }
        g_addon_store[g_addon_n] = (char *)malloc(strlen(s) + 1);
        if (g_addon_store[g_addon_n]) strcpy(g_addon_store[g_addon_n++], s);
    }
    fclose(f);
    xe_log(1, "addons: %d loaded from %s", g_addon_n, path);
}

/* ------------------------------------------------------------ sources */

static struct {
    XeStream *items;
    int n, cap, finished, request;
    char type[24], id[256], title[200];
    float scroll;
    XeId ids[MAX_STREAMS];
} S;

static void sources_clear(void)
{
    if (S.items) xe_data_free_streams(S.items, S.n);
    S.items = NULL; S.n = S.cap = 0; S.finished = 0; S.scroll = 0;
}

static int stream_better(const XeStream *a, const XeStream *b)
{
    int ha = a->height ? a->height : 1, hb = b->height ? b->height : 1;
    if (ha != hb) return ha > hb;
    return a->size > b->size;
}

static void streams_cb(XeStream *s, int n, int finished, void *u)
{
    int req = (int)(size_t)u, i;
    if (req != S.request) { xe_data_free_streams(s, n); return; }
    for (i = 0; i < n && S.n < MAX_STREAMS; i++) {
        int j;
        if (S.n >= S.cap) {
            int cap = S.cap ? S.cap * 2 : 32;
            XeStream *p = (XeStream *)realloc(S.items, sizeof *p * (size_t)cap);
            if (!p) break;
            S.items = p; S.cap = cap;
        }
        j = S.n++;
        S.items[j] = s[i];
        while (j > 0 && stream_better(&S.items[j], &S.items[j - 1])) { XeStream t = S.items[j]; S.items[j] = S.items[j - 1]; S.items[j - 1] = t; j--; }
        memset(&s[i], 0, sizeof s[i]);          /* ownership moved */
    }
    if (s) xe_data_free_streams(s, n);
    if (finished) S.finished = 1;
}

static void sources_open(const char *type, const char *id, const char *title)
{
    sources_clear();
    snprintf(S.type, sizeof S.type, "%s", type);
    snprintf(S.id, sizeof S.id, "%s", id);
    snprintf(S.title, sizeof S.title, "%s", title ? title : "");
    S.request++;
    xe_data_streams(S.type, S.id, streams_cb, (void *)(size_t)S.request);
    if (!S.ids[0]) { int i; for (i = 0; i < MAX_STREAMS; i++) S.ids[i] = xe_id_mix(xe_id("sources.item"), i); }
    xe_ui_set_focus(S.ids[0]);
}

static void human_size(long long b, char *out, size_t cap)
{
    if (b <= 0) { out[0] = 0; return; }
    if (b >= 1024LL * 1024 * 1024) snprintf(out, cap, "%.1f GB", b / (1024.0 * 1024 * 1024));
    else snprintf(out, cap, "%.0f MB", b / (1024.0 * 1024));
}

static void sources_frame(double dt, XeRect area)
{
    XeEvent ev[64];
    int n = 0, i, focus_i = -1;
    const XeColor white = { 0.95f, 0.95f, 0.97f, 1 }, muted = { 0.62f, 0.63f, 0.68f, 1 }, red = { 0.898f, 0.035f, 0.078f, 1 };
    float row_h = 76, top = 128, list_h = area.h - top - 24;
    while (n < 64 && xe_platform_poll(&ev[n])) n++;
    xe_ui_begin(dt, ev, n);
    xe_r_begin((int)area.w, (int)area.h);
    xe_r_rect(area, 0, (XeColor){ 0.035f, 0.035f, 0.047f, 1 });
    xe_label_box(S.title, 64, 40, 30, 700, area.w - 128, 1, 0, white);
    {
        char sub[96];
        if (!S.finished && !S.n) snprintf(sub, sizeof sub, "Finding sources\xE2\x80\xA6");
        else if (!S.finished) snprintf(sub, sizeof sub, "%d found so far\xE2\x80\xA6", S.n);
        else snprintf(sub, sizeof sub, "%d source%s", S.n, S.n == 1 ? "" : "s");
        xe_label(sub, 64, 86, 17, muted);
    }
    if (S.finished && !S.n) {
        char msg[1400];
        xe_label_box("No sources found for this title.", 64, 200, 22, 600, area.w - 128, 1, 0, white);
        snprintf(msg, sizeof msg, "Add stream addon links (one per line) to %s/addons.txt and restart Xstream.", xe_platform_data_dir());
        xe_label_box(msg, 64, 240, 17, 400, area.w - 128, 3, 0, muted);
    }
    for (i = 0; i < S.n; i++) if (xe_ui_focus() == S.ids[i]) focus_i = i;
    if (focus_i >= 0) {                              /* keep the focused row on screen */
        float y = focus_i * row_h;
        if (y - S.scroll > list_h - row_h) S.scroll += (y - S.scroll - (list_h - row_h)) * 0.35f + 0.5f;
        if (y < S.scroll) S.scroll -= (S.scroll - y) * 0.35f + 0.5f;
        if (S.scroll < 0) S.scroll = 0;
    }
    xe_r_push_clip((XeRect){ 0, top - 8, area.w, list_h + 16 }, 0);
    for (i = 0; i < S.n; i++) {
        XeStream *s = &S.items[i];
        XeRect r = { 56, top + i * row_h - S.scroll, area.w - 112, row_h - 10 };
        char q[16], size[32], line[160];
        int f;
        if (r.y + r.h < top - 80 || r.y > area.h + 80) { xe_ui_focusable(S.ids[i], r); continue; }
        f = xe_ui_focusable(S.ids[i], r);
        xe_r_rect(r, 10, f ? (XeColor){ 1, 1, 1, 0.12f } : (XeColor){ 1, 1, 1, 0.04f });
        if (f) xe_r_border(r, 10, 2, red);
        if (s->height >= 2160) snprintf(q, sizeof q, "4K");
        else if (s->height) snprintf(q, sizeof q, "%dp", s->height);
        else snprintf(q, sizeof q, "HD");
        xe_r_rect((XeRect){ r.x + 16, r.y + 18, 64, 30 }, 6, s->height >= 2160 ? red : (XeColor){ 0.22f, 0.22f, 0.26f, 1 });
        xe_label_box(q, r.x + 16, r.y + 22, 15, 700, 64, 1, 1, white);
        human_size(s->size, size, sizeof size);
        snprintf(line, sizeof line, "%s", s->title && *s->title ? s->title : (s->filename ? s->filename : (s->name ? s->name : "Stream")));
        for (f = 0; line[f]; f++) if (line[f] == '\n') line[f] = ' ';
        xe_label_box(line, r.x + 100, r.y + 12, 16, 500, r.w - 230, 2, 0, white);
        if (size[0]) xe_label_box(size, r.x + r.w - 130, r.y + 22, 16, 400, 110, 1, 2, muted);
        if (xe_ui_activated(S.ids[i]) && s->url) {
            g_sources_focus = S.ids[i];
            xe_player_set_title(S.title);
            {
                const char *hdr[33] = { 0 };
                int k;
                for (k = 0; k < s->n_headers && k < 32; k++) hdr[k] = s->headers[k];
                xe_player_open(s->url, s->n_headers ? hdr : NULL, 0);
            }
            g_screen = SCR_PLAYER;
        }
    }
    xe_r_pop_clip();
    xe_ui_end();
    xe_r_end();
    xe_data_pump();
}

/* ------------------------------------------------------------- detail */

static int g_meta_request;

static void meta_cb(XeMeta *m, XeEpisode *eps, int n, void *u)
{
    if ((int)(size_t)u == g_meta_request && g_screen != SCR_HOME) {
        if (m) xe_detail_update_meta(m);
        xe_detail_set_episodes(eps, n);
    }
    xe_data_free_meta(m);
    xe_data_free_episodes(eps, n);
}

static void free_meta_fields(XeMeta *m)
{
    XeMeta *heap = (XeMeta *)malloc(sizeof *heap);     /* reuse the data layer's free */
    if (!heap) return;
    *heap = *m;
    xe_data_free_meta(heap);
    memset(m, 0, sizeof *m);
}

static void open_detail(XeMeta *m, Screen from)
{
    g_before_detail = from;
    xe_detail_open(m);
    xe_detail_set_episodes(NULL, 0);
    g_meta_request++;
    if (m->type && m->id) xe_data_meta(m->type, m->id, meta_cb, (void *)(size_t)g_meta_request);
    g_screen = SCR_DETAIL;
}

/* ------------------------------------------------------------ routing */

static void route_events(void)
{
    XeEvent keep[128];
    int n = 0, i;
    XeEvent e;
    while (xe_platform_poll(&e)) {
        if (e.type == XE_EV_POINTER_DOWN || e.type == XE_EV_POINTER_MOVE || e.type == XE_EV_POINTER_UP || e.type == XE_EV_SCROLL) {
            e.x /= g_scale; e.y /= g_scale;
            if (e.type == XE_EV_POINTER_MOVE) { e.dx /= g_scale; e.dy /= g_scale; }
            if (e.type == XE_EV_SCROLL) { e.dx *= 60; e.dy *= -60; }
        }
        if (e.type == XE_EV_KEY_DOWN && g_screen != SCR_PLAYER &&
            (e.key == XE_KEY_BACK || (e.key == XE_KEY_DELETE && g_screen != SCR_SEARCH))) {
            switch (g_screen) {
            case SCR_DETAIL: g_screen = g_before_detail; xe_ui_set_focus(g_screen == SCR_HOME ? g_home_focus : 0); break;
            case SCR_SEARCH: g_screen = SCR_HOME; xe_ui_set_focus(g_home_focus); break;
            case SCR_SOURCES: S.request++; g_screen = g_before_sources; xe_ui_set_focus(g_detail_focus); break;
            default: break;
            }
            continue;
        }
        if (e.type == XE_EV_KEY_DOWN && e.key == XE_KEY_SEARCH && g_screen == SCR_HOME) { g_screen = SCR_SEARCH; xe_ui_set_focus(0); continue; }
        if (n < 128) keep[n++] = e;
    }
    for (i = 0; i < n; i++) xe_platform_push_event(&keep[i]);
}

int xe_app_init(void)
{
    xe_r_init();
    xe_jobs_init(3);
    xe_net_init();
    xe_net_set_user_agent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Xstream/0.1");
    xe_img_set_budget(96u * 1024u * 1024u, 200u * 1024u * 1024u);
    load_addons();
    xe_data_init((const char *const *)g_addon_store, g_addon_n, NULL);
    xe_home_init();
    xe_platform_set_wants_frames(1);
    xe_log(1, "Xstream TV started");
    {   /* test hook: XE_TEST_URL opens the player straight away (used by CI) */
        const char *u = getenv("XE_TEST_URL");
        if (u && *u) { xe_player_set_title("Test stream"); xe_player_open(u, NULL, 0); g_screen = SCR_PLAYER; }
    }
    return 0;
}

void xe_app_frame(double dt)
{
    int w = 1280, h = 720;
    float density = 1;
    XeRect area;
    XeMeta m;
    xe_platform_size(&w, &h, &density);
    if (w < 1 || h < 1) return;
    g_scale = h / LOGICAL_H;
    area = (XeRect){ 0, 0, w / g_scale, LOGICAL_H };
    xe_r_set_output(w, h);
    xe_r_clear((XeColor){ 0, 0, 0, 1 });
    route_events();
    xe_data_pump();
    xe_img_pump();

    switch (g_screen) {
    case SCR_HOME:
        xe_home_frame(dt, area);
        g_home_focus = xe_ui_focus();
        if (xe_home_opened(&m)) { open_detail(&m, SCR_HOME); free_meta_fields(&m); }
        else if (xe_home_take_search()) { g_screen = SCR_SEARCH; xe_ui_set_focus(0); }
        break;
    case SCR_SEARCH:
        xe_search_frame(dt, area);
        if (xe_search_opened(&m)) { open_detail(&m, SCR_SEARCH); free_meta_fields(&m); }
        break;
    case SCR_DETAIL: {
        char type[32], id[512];
        xe_detail_frame(dt, area);
        g_detail_focus = xe_ui_focus();
        if (xe_detail_play(type, id, sizeof id)) {
            const XeMeta *dm = xe_detail_meta();
            char title[200];
            int season = 0, ep = 0;
            const char *c1 = strchr(id, ':');
            if (c1 && sscanf(c1, ":%d:%d", &season, &ep) == 2)
                snprintf(title, sizeof title, "%s \xC2\xB7 S%d E%d", dm && dm->name ? dm->name : "", season, ep);
            else snprintf(title, sizeof title, "%s", dm && dm->name ? dm->name : "");
            g_before_sources = SCR_DETAIL;
            sources_open(strcmp(type, "movie") == 0 ? "movie" : "series", id, title);
            g_screen = SCR_SOURCES;
        }
        break;
    }
    case SCR_SOURCES:
        sources_frame(dt, area);
        break;
    case SCR_PLAYER:
        xe_player_frame(dt, area);
        if (!xe_player_active()) { g_screen = SCR_SOURCES; xe_ui_set_focus(g_sources_focus); }
        break;
    }
    xe_platform_set_wants_frames(1);
}

void xe_app_shutdown(void)
{
    xe_player_close();
    sources_clear();
    xe_data_shutdown();
    xe_net_shutdown();
    xe_jobs_shutdown();
    xe_text_shutdown();
    xe_r_shutdown();
}
