/* Text engine: font runs with fallback, HarfBuzz shaping, word wrap, ellipsis,
 * glyph atlas with partial uploads, and a small layout cache. Render thread only. */
#include "xe/text.h"
#include "xe/platform.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <hb.h>
#include <hb-ft.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ATLAS 1024
#define MAX_FALLBACK 8
#define GLYPH_SLOTS 8192          /* open-addressing table, power of two */
#define LAYOUT_SLOTS 192
#define MAX_LINES_HARD 64

struct XeFont {
    FT_Face face;
    hb_font_t *hb;
    unsigned char *data;          /* FreeType reads from this memory for the face's lifetime */
    int owns_data;
    float px, ascent, line_h;
    XeFont *fallback[MAX_FALLBACK];
    int nfallback;
    unsigned id;
};

typedef struct { XeFont *font; unsigned gid; int used; short x, y, w, h, left, top; } Glyph;
typedef struct { Glyph *g; float x, y; } Placed;
typedef struct {
    int used; uint64_t hash, stamp; XeFont *font; char *text; float max_w; int max_lines;
    Placed *p; int n; float line_w[MAX_LINES_HARD]; int line_first[MAX_LINES_HARD + 1];
    XeTextSize size;
} Layout;

static FT_Library g_ft;
static int g_ft_ok;
static XeTexture *g_atlas;
static unsigned char *g_px;
static int g_ax = 1, g_ay = 1, g_rowh;
static int g_dirty_y0 = ATLAS, g_dirty_y1 = 0;
static Glyph g_glyphs[GLYPH_SLOTS];
static int g_nglyphs;
static Layout g_layouts[LAYOUT_SLOTS];
static uint64_t g_clock = 1;
static unsigned g_font_ids = 1;

/* ------------------------------------------------------------------ atlas */

static void layouts_clear(void)
{
    int i;
    for (i = 0; i < LAYOUT_SLOTS; i++) { free(g_layouts[i].text); free(g_layouts[i].p); }
    memset(g_layouts, 0, sizeof g_layouts);
}

static int atlas_ready(void)
{
    if (g_atlas) return 1;
    g_px = (unsigned char *)calloc((size_t)ATLAS * ATLAS, 4);
    if (!g_px) return 0;
    g_atlas = xe_tex_create(ATLAS, ATLAS, g_px);
    return g_atlas != NULL;
}

static void atlas_reset(void)
{
    memset(g_px, 0, (size_t)ATLAS * ATLAS * 4);
    memset(g_glyphs, 0, sizeof g_glyphs);
    g_nglyphs = 0;
    g_ax = g_ay = 1; g_rowh = 0;
    g_dirty_y0 = 0; g_dirty_y1 = ATLAS;
    layouts_clear();              /* layouts point at glyph slots */
    xe_log(1, "text: glyph atlas was full and has been rebuilt");
}

static void atlas_flush(void)
{
    if (g_atlas && g_dirty_y1 > g_dirty_y0) {
        xe_tex_update_rows(g_atlas, g_dirty_y0, g_dirty_y1 - g_dirty_y0, g_px + (size_t)g_dirty_y0 * ATLAS * 4);
        g_dirty_y0 = ATLAS; g_dirty_y1 = 0;
    }
}

static int pack(int w, int h, int *ox, int *oy)
{
    if (w + 2 > ATLAS || h + 2 > ATLAS) return 0;
    if (g_ax + w + 1 > ATLAS) { g_ax = 1; g_ay += g_rowh + 1; g_rowh = 0; }
    if (g_ay + h + 1 > ATLAS) return 0;
    *ox = g_ax; *oy = g_ay;
    g_ax += w + 1;
    if (h > g_rowh) g_rowh = h;
    return 1;
}

/* Returns the cached glyph, rasterising it on first use. *reset is set if the atlas was rebuilt. */
static Glyph *glyph_get(XeFont *f, unsigned gid, int *reset)
{
    unsigned h = (f->id * 2654435761u) ^ (gid * 40503u);
    unsigned i, slot;
    FT_Bitmap *bm;
    int w, hh, x, y, px = 0, py = 0;
    if (!atlas_ready()) return NULL;
    for (i = 0; i < GLYPH_SLOTS; i++) {
        slot = (h + i) & (GLYPH_SLOTS - 1);
        if (!g_glyphs[slot].used) break;
        if (g_glyphs[slot].font == f && g_glyphs[slot].gid == gid) return &g_glyphs[slot];
    }
    if (g_nglyphs > GLYPH_SLOTS * 3 / 4) { atlas_reset(); if (reset) *reset = 1; return glyph_get(f, gid, NULL); }
    if (FT_Load_Glyph(f->face, gid, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT) != 0) return NULL;
    if (FT_Render_Glyph(f->face->glyph, FT_RENDER_MODE_LIGHT) != 0) return NULL;
    bm = &f->face->glyph->bitmap;
    w = (int)bm->width; hh = (int)bm->rows;
    if (w > 0 && hh > 0 && !pack(w, hh, &px, &py)) {
        atlas_reset(); if (reset) *reset = 1;
        if (!pack(w, hh, &px, &py)) return NULL;
    }
    for (i = 0; i < GLYPH_SLOTS; i++) {
        slot = (h + i) & (GLYPH_SLOTS - 1);
        if (!g_glyphs[slot].used) break;
    }
    {
        Glyph *g = &g_glyphs[slot];
        g->used = 1; g->font = f; g->gid = gid;
        g->w = (short)w; g->h = (short)hh;
        g->left = (short)f->face->glyph->bitmap_left; g->top = (short)f->face->glyph->bitmap_top;
        if (w > 0 && hh > 0) {
            g->x = (short)px; g->y = (short)py;
            for (y = 0; y < hh; y++) {
                const unsigned char *src = bm->buffer + (ptrdiff_t)y * bm->pitch;
                unsigned char *dst = g_px + ((size_t)(py + y) * ATLAS + (size_t)px) * 4;
                for (x = 0; x < w; x++) { dst[0] = dst[1] = dst[2] = 255; dst[3] = src[x]; dst += 4; }
            }
            if (py < g_dirty_y0) g_dirty_y0 = py;
            if (py + hh > g_dirty_y1) g_dirty_y1 = py + hh;
        }
        g_nglyphs++;
        return g;
    }
}

/* ------------------------------------------------------------------ fonts */

static XeFont *font_from_memory(unsigned char *data, size_t len, float px, int weight)
{
    XeFont *f;
    if (!g_ft_ok) { if (FT_Init_FreeType(&g_ft) != 0) return NULL; g_ft_ok = 1; }
    f = (XeFont *)calloc(1, sizeof *f);
    if (!f) return NULL;
    if (FT_New_Memory_Face(g_ft, data, (FT_Long)len, 0, &f->face) != 0) { free(f); return NULL; }
    f->data = data;
    if (weight > 0 && (f->face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS)) {
        FT_MM_Var *mm = NULL;
        if (FT_Get_MM_Var(f->face, &mm) == 0 && mm) {
            FT_Fixed coords[16];
            FT_UInt a, n = mm->num_axis < 16 ? mm->num_axis : 16;
            for (a = 0; a < n; a++) {
                coords[a] = mm->axis[a].def;
                if (mm->axis[a].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
                    FT_Fixed v = (FT_Fixed)weight << 16;
                    if (v < mm->axis[a].minimum) v = mm->axis[a].minimum;
                    if (v > mm->axis[a].maximum) v = mm->axis[a].maximum;
                    coords[a] = v;
                }
            }
            FT_Set_Var_Design_Coordinates(f->face, n, coords);
            FT_Done_MM_Var(g_ft, mm);
        }
    }
    FT_Set_Pixel_Sizes(f->face, 0, (FT_UInt)(px > 1 ? px + 0.5f : 16));
    f->px = px;
    f->ascent = (float)f->face->size->metrics.ascender / 64.0f;
    f->line_h = (float)f->face->size->metrics.height / 64.0f;
    if (f->line_h < px * 1.15f) f->line_h = px * 1.25f;
    f->hb = hb_ft_font_create_referenced(f->face);
    if (!f->hb) { FT_Done_Face(f->face); free(f); return NULL; }
    hb_ft_font_set_load_flags(f->hb, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT);
    hb_ft_font_changed(f->hb);
    f->id = g_font_ids++;
    return f;
}

/* Font files are read once and shared by every size/weight that uses them. */
static struct { char path[96]; unsigned char *data; size_t len; } g_files[8];
static int g_nfiles;

static XeFont *font_load_weight(const char *path, float px, int weight)
{
    void *data = NULL;
    size_t len = 0;
    int i;
    if (!path || !*path) return NULL;
    for (i = 0; i < g_nfiles; i++)
        if (!strcmp(g_files[i].path, path)) return font_from_memory(g_files[i].data, g_files[i].len, px, weight);
    if (!xe_asset_read(path, &data, &len)) { xe_log(2, "font missing: %s", path); return NULL; }
    if (g_nfiles < 8) {
        snprintf(g_files[g_nfiles].path, sizeof g_files[0].path, "%s", path);
        g_files[g_nfiles].data = (unsigned char *)data; g_files[g_nfiles].len = len; g_nfiles++;
        return font_from_memory((unsigned char *)data, len, px, weight);
    }
    {
        XeFont *f = font_from_memory((unsigned char *)data, len, px, weight);
        if (!f) free(data); else f->owns_data = 1;
        return f;
    }
}

XeFont *xe_font_load(const char *asset_path, float px) { return font_load_weight(asset_path, px, 0); }

void xe_font_add_fallback(XeFont *f, XeFont *fb)
{
    if (f && fb && f != fb && f->nfallback < MAX_FALLBACK) f->fallback[f->nfallback++] = fb;
}

static XeFont *font_for(XeFont *f, unsigned cp)
{
    int i;
    if (FT_Get_Char_Index(f->face, cp)) return f;
    for (i = 0; i < f->nfallback; i++)
        if (f->fallback[i] && FT_Get_Char_Index(f->fallback[i]->face, cp)) return f->fallback[i];
    return f;
}

/* ----------------------------------------------------------------- layout */

static unsigned utf8_next(const char **sp)
{
    const unsigned char *s = (const unsigned char *)*sp;
    unsigned c;
    if (!*s) return 0;
    if (s[0] < 0x80) { *sp += 1; return s[0]; }
    if ((s[0] & 0xE0) == 0xC0 && s[1]) { c = ((s[0] & 31u) << 6) | (s[1] & 63u); *sp += 2; return c; }
    if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) { c = ((s[0] & 15u) << 12) | ((s[1] & 63u) << 6) | (s[2] & 63u); *sp += 3; return c; }
    if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) { c = ((s[0] & 7u) << 18) | ((s[1] & 63u) << 12) | ((s[2] & 63u) << 6) | (s[3] & 63u); *sp += 4; return c; }
    *sp += 1; return 0xFFFD;
}

static int sticks_to_previous(unsigned cp)
{
    hb_unicode_general_category_t gc = hb_unicode_general_category(hb_unicode_funcs_get_default(), cp);
    return gc == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ||
           gc == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK ||
           gc == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK ||
           cp == 0x200C || cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F);
}

/* One shaped glyph before line breaking. */
typedef struct { XeFont *font; unsigned gid; float adv, xo, yo; int space, newline; } Shaped;

static int shape_text(XeFont *f, const char *s, Shaped **out)
{
    int cap = 64, n = 0;
    Shaped *a = (Shaped *)malloc(sizeof *a * (size_t)cap);
    const char *p = s;
    if (!a) return 0;
    while (*p) {
        const char *run = p, *q = p;
        unsigned cp = utf8_next(&q);
        XeFont *rf;
        if (cp == '\n') {
            if (n >= cap) { Shaped *b = (Shaped *)realloc(a, sizeof *a * (size_t)(cap *= 2)); if (!b) break; a = b; }
            memset(&a[n], 0, sizeof a[n]); a[n].font = f; a[n].newline = 1; n++;
            p = q; continue;
        }
        rf = font_for(f, cp);
        p = q;
        while (*p) {                       /* extend the run while the same font applies */
            const char *r = p;
            unsigned c2 = utf8_next(&r);
            if (c2 == '\n') break;
            if (!sticks_to_previous(c2) && font_for(f, c2) != rf) break;
            p = r;
        }
        {
            hb_buffer_t *b = hb_buffer_create();
            unsigned cnt = 0, j;
            hb_glyph_info_t *info;
            hb_glyph_position_t *pos;
            hb_buffer_add_utf8(b, run, (int)(p - run), 0, (int)(p - run));
            hb_buffer_guess_segment_properties(b);
            hb_shape(rf->hb, b, NULL, 0);
            info = hb_buffer_get_glyph_infos(b, &cnt);
            pos = hb_buffer_get_glyph_positions(b, &cnt);
            for (j = 0; j < cnt; j++) {
                if (n >= cap) { Shaped *nb = (Shaped *)realloc(a, sizeof *a * (size_t)(cap *= 2)); if (!nb) break; a = nb; }
                a[n].font = rf; a[n].gid = info[j].codepoint;
                a[n].adv = pos[j].x_advance / 64.0f;
                a[n].xo = pos[j].x_offset / 64.0f; a[n].yo = pos[j].y_offset / 64.0f;
                a[n].space = run[info[j].cluster] == ' ' || run[info[j].cluster] == '\t';
                a[n].newline = 0;
                n++;
            }
            hb_buffer_destroy(b);
        }
    }
    *out = a;
    return n;
}

static uint64_t fnv(const void *v, size_t n, uint64_t h)
{
    const unsigned char *p = (const unsigned char *)v;
    while (n--) { h ^= *p++; h *= 1099511628211ULL; }
    return h;
}

static Layout *layout_get(XeFont *f, const char *s, float max_w, int max_lines);

static Layout *layout_build(Layout *L, XeFont *f, const char *s, float max_w, int max_lines, int *reset)
{
    Shaped *sh = NULL;
    int ns = shape_text(f, s, &sh), i, line = 0, lim = max_lines > 0 ? max_lines : MAX_LINES_HARD;
    float pen = 0;
    int line_start = 0, last_space = -1;
    float widest = 0;
    if (lim > MAX_LINES_HARD) lim = MAX_LINES_HARD;
    L->p = (Placed *)calloc((size_t)(ns + 2), sizeof *L->p);
    if (!L->p) { free(sh); return NULL; }
    L->line_first[0] = 0;
    /* Greedy line breaking over shaped glyphs; break at the last space when possible. */
    for (i = 0; i < ns; i++) {
        Shaped *g = &sh[i];
        int brk = g->newline;
        if (!brk && max_w > 0 && pen + g->adv > max_w && i > line_start && !g->space) {
            if (last_space > line_start) {
                int k; float back = 0;
                for (k = last_space + 1; k < i; k++) back += sh[k].adv;
                L->line_w[line] = pen - back - sh[last_space].adv;
                /* glyphs after the space move to the next line */
                if (line + 1 >= lim) { L->n = last_space; L->size.truncated = 1; break; }
                L->n = last_space + 1;
                line++; L->line_first[line] = last_space + 1;
                pen = 0;
                for (k = last_space + 1; k < i; k++) {
                    L->p[k].x = pen + sh[k].xo; L->p[k].y = line * f->line_h - sh[k].yo; pen += sh[k].adv;
                }
                line_start = last_space + 1; last_space = -1;
            } else {
                L->line_w[line] = pen;
                if (line + 1 >= lim) { L->n = i; L->size.truncated = 1; break; }
                line++; L->line_first[line] = i; pen = 0; line_start = i;
            }
        }
        if (brk) {
            L->line_w[line] = pen;
            if (line + 1 >= lim) { L->n = i; L->size.truncated = i + 1 < ns; break; }
            line++; L->line_first[line] = i + 1; pen = 0; line_start = i + 1; last_space = -1;
            L->p[i].g = NULL;
            L->n = i + 1;
            continue;
        }
        {
            Glyph *gl = glyph_get(g->font, g->gid, reset);
            if (reset && *reset) { free(sh); return NULL; }
            L->p[i].g = gl;
            L->p[i].x = pen + g->xo;
            L->p[i].y = line * f->line_h - g->yo;
        }
        if (g->space) last_space = i;
        pen += g->adv;
        L->n = i + 1;
    }
    if (!L->size.truncated) L->line_w[line] = pen;
    if (L->size.truncated) {                  /* append an ellipsis on the last line */
        XeFont *ef = font_for(f, 0x2026);
        unsigned gid = FT_Get_Char_Index(ef->face, 0x2026);
        Glyph *eg;
        float ew;
        if (!gid) { ef = f; gid = FT_Get_Char_Index(f->face, '.'); }
        eg = glyph_get(ef, gid, reset);
        if (reset && *reset) { free(sh); return NULL; }
        FT_Load_Glyph(ef->face, gid, FT_LOAD_DEFAULT);
        ew = ef->face->glyph->advance.x / 64.0f;
        pen = L->line_w[line];
        while (L->n > L->line_first[line] && max_w > 0 && pen + ew > max_w) {
            L->n--; pen = L->p[L->n].x - sh[L->n].xo;
        }
        while (L->n > L->line_first[line] && sh[L->n - 1].space) { L->n--; pen = L->p[L->n].x - sh[L->n].xo; }
        L->p[L->n].g = eg; L->p[L->n].x = pen; L->p[L->n].y = line * f->line_h;
        L->n++;
        L->line_w[line] = pen + ew;
    }
    L->line_first[line + 1] = L->n;
    for (i = 0; i <= line; i++) if (L->line_w[i] > widest) widest = L->line_w[i];
    L->size.w = widest;
    L->size.lines = line + 1;
    L->size.h = (float)(line + 1) * f->line_h;
    L->size.ascent = f->ascent;
    free(sh);
    return L;
}

static Layout *layout_get(XeFont *f, const char *s, float max_w, int max_lines)
{
    uint64_t h = 1469598103934665603ULL;
    int i, slot = 0, reset = 0;
    Layout *L;
    h = fnv(&f, sizeof f, h); h = fnv(s, strlen(s), h); h = fnv(&max_w, sizeof max_w, h); h = fnv(&max_lines, sizeof max_lines, h);
    for (i = 0; i < LAYOUT_SLOTS; i++) {
        L = &g_layouts[i];
        if (L->used && L->hash == h && L->font == f && L->max_w == max_w && L->max_lines == max_lines && strcmp(L->text, s) == 0) {
            L->stamp = g_clock++;
            return L;
        }
        if (!L->used) { slot = i; break; }
        if (L->stamp < g_layouts[slot].stamp) slot = i;
    }
    L = &g_layouts[slot];
    free(L->text); free(L->p);
    memset(L, 0, sizeof *L);
    L->text = (char *)malloc(strlen(s) + 1);
    if (!L->text) return NULL;
    strcpy(L->text, s);
    if (!layout_build(L, f, s, max_w, max_lines, &reset)) {
        if (reset) return layout_get(f, s, max_w, max_lines);   /* atlas rebuilt: try once more */
        free(L->text); free(L->p); memset(L, 0, sizeof *L);
        return NULL;
    }
    L->used = 1; L->hash = h; L->font = f; L->max_w = max_w; L->max_lines = max_lines; L->stamp = g_clock++;
    return L;
}

XeTextSize xe_text_measure(XeFont *f, const char *s, float max_w, int max_lines)
{
    XeTextSize z = { 0, 0, 0, 0, 0 };
    Layout *L;
    if (!f || !s) return z;
    L = layout_get(f, s, max_w, max_lines);
    return L ? L->size : z;
}

/* inv: logical units per font pixel (1/scale) so text stays sharp inside scaled layers. */
static void draw_layout(XeFont *f, const char *s, float x, float y, float max_w, int max_lines, int align, XeColor c, float inv)
{
    Layout *L;
    int line, i;
    if (!f || !s || !*s) return;
    L = layout_get(f, s, max_w, max_lines);
    if (!L) return;
    atlas_flush();
    for (line = 0; line < L->size.lines; line++) {
        float off = 0;
        if (max_w > 0 && align == 1) off = (max_w - L->line_w[line]) * 0.5f;
        else if (max_w > 0 && align == 2) off = max_w - L->line_w[line];
        for (i = L->line_first[line]; i < L->line_first[line + 1] && i < L->n; i++) {
            Placed *p = &L->p[i];
            Glyph *g = p->g;
            float gx, gy;
            if (!g || g->w <= 0 || g->h <= 0) continue;
            gx = x + (off + p->x + g->left) * inv;
            gy = y + (p->y + f->ascent - g->top) * inv;
            if (inv == 1.0f) { gx = floorf(gx + 0.5f); gy = floorf(gy + 0.5f); }
            xe_r_image_tint(g_atlas, (XeRect){ gx, gy, g->w * inv, g->h * inv }, 0, c,
                            (float)g->x / ATLAS, (float)g->y / ATLAS,
                            (float)(g->x + g->w) / ATLAS, (float)(g->y + g->h) / ATLAS);
        }
    }
}

void xe_text_draw(XeFont *f, const char *s, float x, float y, float max_w, int max_lines, int align, XeColor c)
{
    draw_layout(f, s, x, y, max_w, max_lines, align, c, 1.0f);
}

/* ------------------------------------------------------------- app labels */

typedef struct { int px, weight; XeFont *font; } SizedFont;
static SizedFont g_sized[48];
static int g_nsized;
static const char *k_main = "fonts/NotoSans.ttf";
static const char *k_fallbacks[] = { "fonts/NotoSansDevanagari.ttf", "fonts/NotoSansTamil.ttf", "fonts/NotoSansTelugu.ttf" };

XeFont *xe_font_get(float pxf, int weight)
{
    int px = (int)(pxf + 0.5f), i;
    XeFont *f;
    if (px < 6) px = 6;
    if (weight <= 0) weight = 400;
    for (i = 0; i < g_nsized; i++) if (g_sized[i].px == px && g_sized[i].weight == weight) return g_sized[i].font;
    if (weight == XE_FONT_BRAND) {         /* Poppins Bold: the Xstream wordmark */
        f = font_load_weight("fonts/Poppins-Bold.ttf", (float)px, 0);
        if (!f) f = font_load_weight(k_main, (float)px, 700);
        if (f && g_nsized < (int)(sizeof g_sized / sizeof g_sized[0])) { g_sized[g_nsized].px = px; g_sized[g_nsized].weight = weight; g_sized[g_nsized].font = f; g_nsized++; }
        return f;
    }
    f = font_load_weight(k_main, (float)px, weight);
    if (!f) return NULL;
    for (i = 0; i < (int)(sizeof k_fallbacks / sizeof k_fallbacks[0]); i++) {
        XeFont *fb = font_load_weight(k_fallbacks[i], (float)px, weight);
        if (fb) xe_font_add_fallback(f, fb);
    }
    if (g_nsized < (int)(sizeof g_sized / sizeof g_sized[0])) {
        g_sized[g_nsized].px = px; g_sized[g_nsized].weight = weight; g_sized[g_nsized].font = f; g_nsized++;
    }
    return f;
}

static float label_scale(void)
{
    float s = xe_r_current_scale();
    return s > 0.05f ? s : 1.0f;
}

void xe_label_box(const char *s, float x, float y, float px, int weight, float max_w, int max_lines, int align, XeColor c)
{
    float sc = label_scale();
    draw_layout(xe_font_get(px * sc, weight), s, x, y, max_w > 0 ? max_w * sc : 0, max_lines, align, c, 1.0f / sc);
}
void xe_label(const char *s, float x, float y, float px, XeColor c) { xe_label_box(s, x, y, px, 400, 0, 1, 0, c); }
void xe_label_w(const char *s, float x, float y, float px, int weight, XeColor c) { xe_label_box(s, x, y, px, weight, 0, 1, 0, c); }
XeTextSize xe_label_measure(const char *s, float px, int weight, float max_w, int max_lines)
{
    float sc = label_scale();
    XeTextSize z = xe_text_measure(xe_font_get(px * sc, weight), s, max_w > 0 ? max_w * sc : 0, max_lines);
    z.w /= sc; z.h /= sc; z.ascent /= sc;
    return z;
}

static void font_free(XeFont *f)
{
    if (!f) return;
    hb_font_destroy(f->hb);          /* releases the referenced FT_Face */
    if (f->owns_data) free(f->data);
    free(f);
}

void xe_text_shutdown(void)
{
    int i, j;
    layouts_clear();
    for (i = 0; i < g_nsized; i++) {
        for (j = 0; j < g_sized[i].font->nfallback; j++) font_free(g_sized[i].font->fallback[j]);
        font_free(g_sized[i].font);
    }
    g_nsized = 0;
    for (i = 0; i < g_nfiles; i++) free(g_files[i].data);
    g_nfiles = 0;
    if (g_atlas) { xe_tex_destroy(g_atlas); g_atlas = NULL; }
    free(g_px); g_px = NULL;
    memset(g_glyphs, 0, sizeof g_glyphs); g_nglyphs = 0;
    g_ax = g_ay = 1; g_rowh = 0;
    if (g_ft_ok) { FT_Done_FreeType(g_ft); g_ft_ok = 0; }
}
