/* TV player overlay (from P11): loading card, auto-hiding controls, seek bar,
 * audio/subtitle menu. Shared by the desktop and Android players. */
#include "xe/player.h"
#include "xe/platform.h"
#include "xe/render.h"
#include "xe/text.h"
#include "xe/ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define P_RED   ((XeColor){0.898f, 0.035f, 0.078f, 1})
#define P_WHITE ((XeColor){0.95f, 0.95f, 0.97f, 1})
#define P_MUTED ((XeColor){0.63f, 0.64f, 0.69f, 1})

static struct {
    double idle, seek_toast, t;
    int visible, menu, menu_kind;
    char title[160];
    XeId ids[4], track_ids[2][24];
    int inited;
} O;

void xe_player_set_title(const char *title) { snprintf(O.title, sizeof O.title, "%s", title ? title : ""); }

static void fmt_time(double v, char *out, size_t cap)
{
    int h, m, s;
    if (v < 0 || v != v) v = 0;
    h = (int)(v / 3600); m = (int)(v / 60) % 60; s = (int)v % 60;
    if (h) snprintf(out, cap, "%d:%02d:%02d", h, m, s); else snprintf(out, cap, "%02d:%02d", m, s);
}

static int button(XeId id, XeRect r, const char *label, float k)
{
    int f = xe_ui_focusable(id, r);
    xe_r_rect(r, 10 * k, f ? (XeColor){1, 1, 1, 0.95f} : (XeColor){0.12f, 0.12f, 0.15f, 0.85f});
    xe_label_box(label, r.x, r.y + r.h * 0.5f - 11 * k, 17 * k, 600, r.w, 1, 1, f ? (XeColor){0.05f, 0.05f, 0.06f, 1} : P_WHITE);
    return xe_ui_activated(id);
}

static void loading_card(XeRect area, float k)
{
    float w = 420 * k, h = 236 * k;
    XeRect card = { area.x + (area.w - w) * 0.5f, area.y + (area.h - h) * 0.5f, w, h };
    float p = (float)fmod(O.t * 0.55, 1.0);
    int i;
    xe_r_rect(card, 18 * k, (XeColor){0.06f, 0.06f, 0.08f, 0.94f});
    xe_r_border(card, 18 * k, 1.5f * k, (XeColor){1, 1, 1, 0.08f});
    xe_r_border_progress(card, 18 * k, 3 * k, p, P_RED);
    xe_label_box(O.title[0] ? O.title : "Loading", card.x + 24 * k, card.y + 58 * k, 24 * k, 700, card.w - 48 * k, 1, 1, P_WHITE);
    xe_label_box("Loading stream\xE2\x80\xA6", card.x, card.y + 104 * k, 16 * k, 400, card.w, 1, 1, P_MUTED);
    for (i = 0; i < 24; i++) {                     /* heartbeat bars */
        float ph = (float)O.t * 6.0f - i * 0.45f;
        float hh = (4 + 18 * (0.5f + 0.5f * sinf(ph)) * (i % 5 == 2 ? 1.6f : 1.0f)) * k;
        float bx = card.x + 60 * k + i * ((card.w - 120 * k) / 24);
        xe_r_rect((XeRect){ bx, card.y + 176 * k - hh * 0.5f, 5 * k, hh }, 2.5f * k, (XeColor){0.898f, 0.035f, 0.078f, 0.55f + 0.45f * (hh / (30 * k))});
    }
}

static void tracks_menu(XeRect area, float k)
{
    char names[2][24][64];
    int n[2], c, i;
    float pw = 380 * k, x0 = area.x + area.w - 2 * pw - 60 * k, y0 = area.y + 90 * k;
    const char *heads[2] = { "Audio", "Subtitles" };
    n[0] = xe_player_tracks(0, names[0], 24);
    n[1] = xe_player_tracks(1, names[1], 24);
    xe_r_rect((XeRect){ x0 - 20 * k, y0 - 20 * k, 2 * pw + 60 * k, area.h - y0 - 150 * k }, 16 * k, (XeColor){0.04f, 0.04f, 0.05f, 0.92f});
    for (c = 0; c < 2; c++) {
        float x = x0 + c * (pw + 20 * k);
        xe_label_w(heads[c], x, y0, 20 * k, 700, P_WHITE);
        if (!n[c]) xe_label(c ? "No subtitles in this stream" : "Default", x, y0 + 44 * k, 16 * k, P_MUTED);
        for (i = 0; i < n[c]; i++) {
            XeRect r = { x, y0 + (40 + i * 50) * k, pw, 44 * k };
            int f = xe_ui_focusable(O.track_ids[c][i], r);
            if (r.y > area.h - 200 * k) break;
            xe_r_rect(r, 8 * k, f ? (XeColor){1, 1, 1, 0.14f} : (XeColor){0, 0, 0, 0});
            xe_label_box(names[c][i], r.x + 14 * k, r.y + 11 * k, 17 * k, strstr(names[c][i], "\xE2\x80\xA2") ? 700 : 400, r.w - 28 * k, 1, 0, P_WHITE);
            if (xe_ui_activated(O.track_ids[c][i])) xe_player_select(c, i);
        }
    }
}

void xe_player_frame(double dt, XeRect area)
{
    XeEvent ev[64];
    int n = 0, i, used = 0;
    float k = area.h / 1080.0f;
    if (k < 0.5f) k = 0.5f;
    if (!O.inited) {
        for (i = 0; i < 4; i++) O.ids[i] = xe_id_mix(xe_id("player.btn"), i);
        for (i = 0; i < 24; i++) { O.track_ids[0][i] = xe_id_mix(xe_id("player.audio"), i); O.track_ids[1][i] = xe_id_mix(xe_id("player.sub"), i); }
        O.inited = 1;
    }
    O.t += dt;
    while (n < 64 && xe_platform_poll(&ev[n])) n++;
    /* Keys that act while the controls are hidden never reach the focus system. */
    for (i = 0; i < n; i++) {
        XeEvent *e = &ev[i];
        if (e->type == XE_EV_POINTER_UP || e->type == XE_EV_POINTER_MOVE) { if (!O.visible) e->type = XE_EV_NONE; O.visible = 1; O.idle = 0; }
        if (e->type != XE_EV_KEY_DOWN) continue;
        O.idle = 0;
        if (e->key == XE_KEY_BACK || e->key == XE_KEY_DELETE) {
            if (O.menu) O.menu = 0; else if (O.visible) O.visible = 0; else { xe_player_close(); O.visible = 0; }
            e->type = XE_EV_NONE; continue;
        }
        if (e->key == XE_KEY_PLAYPAUSE) { xe_player_toggle(); O.visible = 1; e->type = XE_EV_NONE; continue; }
        if (e->key == XE_KEY_FF || e->key == XE_KEY_RW) { xe_player_seek(e->key == XE_KEY_FF ? 30 : -10); O.seek_toast = 1.2; e->type = XE_EV_NONE; continue; }
        if (!O.visible) {
            if (e->key == XE_KEY_LEFT || e->key == XE_KEY_RIGHT) { xe_player_seek(e->key == XE_KEY_LEFT ? -10 : 10); O.seek_toast = 1.2; }
            else { O.visible = 1; xe_ui_set_focus(O.ids[1]); }
            e->type = XE_EV_NONE;
        }
    }
    (void)used;
    O.idle += dt;
    if (O.seek_toast > 0) O.seek_toast -= dt;
    if (O.idle > 5.0 && !O.menu && !xe_player_paused()) O.visible = 0;

    { int pw, ph; xe_r_output_size(&pw, &ph); xe_player_render_video(pw, ph); }
    xe_ui_begin(dt, ev, n);
    xe_r_begin((int)area.w, (int)area.h);

    if (xe_player_error()) {
        xe_r_rect(area, 0, (XeColor){0.03f, 0.03f, 0.04f, 1});
        xe_label_box(xe_player_error(), area.x, area.h * 0.45f, 28 * k, 700, area.w, 1, 1, P_WHITE);
        xe_label_box("Press Back to choose another source", area.x, area.h * 0.45f + 48 * k, 18 * k, 400, area.w, 1, 1, P_MUTED);
    } else if (xe_player_buffering()) {
        loading_card(area, k);
    }

    if (O.seek_toast > 0 && !O.visible) {
        char a[32], b[32], s[80];
        double pos = xe_player_pos(), dur = xe_player_dur();
        fmt_time(pos, a, sizeof a); fmt_time(dur, b, sizeof b);
        snprintf(s, sizeof s, "%s / %s", a, b);
        xe_r_rect((XeRect){ area.w * 0.5f - 120 * k, area.h - 120 * k, 240 * k, 48 * k }, 24 * k, (XeColor){0, 0, 0, 0.7f});
        xe_label_box(s, area.w * 0.5f - 120 * k, area.h - 108 * k, 18 * k, 600, 240 * k, 1, 1, P_WHITE);
    }

    if (O.visible && !xe_player_error()) {
        char a[32], b[32], left[48], times[128];
        double pos = xe_player_pos(), dur = xe_player_dur();
        float by = area.h - 210 * k, bw;
        const char *labels[4];
        xe_r_rect_vgrad((XeRect){ 0, 0, area.w, 180 * k }, 0, (XeColor){0, 0, 0, 0.75f}, (XeColor){0, 0, 0, 0});
        xe_label_box(O.title, 64 * k, 48 * k, 30 * k, 700, area.w - 128 * k, 1, 0, P_WHITE);
        xe_r_rect_vgrad((XeRect){ 0, area.h - 300 * k, area.w, 300 * k }, 0, (XeColor){0, 0, 0, 0}, (XeColor){0, 0, 0, 0.85f});
        xe_r_rect((XeRect){ 64 * k, by, area.w - 128 * k, 6 * k }, 3 * k, (XeColor){1, 1, 1, 0.25f});
        if (dur > 0) {
            float fx = (float)((area.w - 128 * k) * (pos / dur));
            xe_r_rect((XeRect){ 64 * k, by, fx, 6 * k }, 3 * k, P_RED);
            xe_r_rect((XeRect){ 64 * k + fx - 9 * k, by - 6 * k, 18 * k, 18 * k }, 9 * k, P_WHITE);
        }
        fmt_time(pos, a, sizeof a); fmt_time(dur, b, sizeof b); fmt_time(dur > pos ? dur - pos : 0, left, sizeof left);
        snprintf(times, sizeof times, "%s / %s", a, b);
        xe_label(times, 64 * k, by + 22 * k, 17 * k, P_WHITE);
        if (dur > 0) { char t2[64]; snprintf(t2, sizeof t2, "-%s", left); xe_label_box(t2, area.w - 364 * k, by + 22 * k, 17 * k, 400, 300 * k, 1, 2, P_MUTED); }
        labels[0] = "-10s"; labels[1] = xe_player_paused() ? "Play" : "Pause"; labels[2] = "+10s"; labels[3] = "Audio & Subtitles";
        bw = 150 * k;
        for (i = 0; i < 4; i++) {
            XeRect r = { 64 * k + i * (bw + 16 * k), by + 72 * k, i == 3 ? 240 * k : bw, 58 * k };
            if (button(O.ids[i], r, labels[i], k)) {
                if (i == 0) xe_player_seek(-10);
                else if (i == 1) xe_player_toggle();
                else if (i == 2) xe_player_seek(10);
                else { O.menu = !O.menu; if (O.menu) xe_ui_set_focus(O.track_ids[0][0]); }
            }
        }
        if (O.menu) tracks_menu(area, k);
    }
    xe_ui_end();
    xe_r_end();
    xe_platform_set_wants_frames(1);
}
