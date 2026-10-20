#include "xe/platform.h"
#include "xe/render.h"
#include "xe/text.h"
#include "xe/images.h"
#include "xe/ui.h"
#include "xe/anim.h"
#include "xe/data.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOME_MAX_SHELVES 32
#define HOME_MAX_ITEMS 160
#define HOME_MAX_ROWS 24
#define HOME_MAX_IMAGES (HOME_MAX_SHELVES * 18)
#define HOME_ACCENT ((XeColor){0.898f,0.035f,0.078f,1.0f})
#define HOME_WHITE  ((XeColor){0.949f,0.949f,0.961f,1.0f})
#define HOME_MUTED  ((XeColor){0.604f,0.612f,0.675f,1.0f})
#define HOME_BG     ((XeColor){0.043f,0.043f,0.059f,1.0f})

typedef struct HomeRow {
    char *title;
    XeMeta *items;
    int n;
    float scroll;
    XeSpring spring;
    int owns_items;
} HomeRow;

typedef struct HomeImage {
    char *url;
    XeImage *image;
} HomeImage;

typedef struct HomeState {
    int initialized, data_requested;
    HomeRow rows[HOME_MAX_ROWS];
    int n_rows;
    HomeImage images[HOME_MAX_IMAGES];
    int n_images;
    XeMeta selected;
    int selected_valid;
    XeMeta opened;
    int opened_valid;
    XeSpring deck_slide;
    float page_scroll;
    float backdrop_mix;
    double backdrop_clock;
    char *backdrop_url;
    XeImage *backdrop;
    char *logo_url;
    XeImage *logo;
    XeId tab_ids[4], search_id, profile_id, play_id, list_id;
    XeId deck_ids[3];
    XeRow row_scroll[HOME_MAX_ROWS];
    int focused_row, focused_col;
    int want_search;
    float last_w, last_h, density;
} HomeState;
static HomeState H;

static char *hdup(const char *s) {
    size_t n; char *p; if (!s) return NULL; n=strlen(s)+1; p=(char*)malloc(n); if(p)memcpy(p,s,n); return p;
}
static void hfree_meta(XeMeta *m) {
    int i; if(!m)return;
    free(m->id); free(m->type); free(m->name); free(m->poster); free(m->background); free(m->logo);
    free(m->description); free(m->release); free(m->imdb_rating); free(m->runtime);
    for(i=0;i<m->n_genres;i++) free(m->genres[i]); free(m->genres); memset(m,0,sizeof(*m));
}
static int hcopy_meta(XeMeta *d, const XeMeta *s) {
    int i; if(!d||!s)return 0; memset(d,0,sizeof(*d));
#define HC(field) do { if(s->field && !(d->field=hdup(s->field))) goto fail; } while(0)
    HC(id); HC(type); HC(name); HC(poster); HC(background); HC(logo); HC(description); HC(release); HC(imdb_rating); HC(runtime);
#undef HC
    if(s->n_genres>0 && s->genres) {
        d->genres=(char**)calloc((size_t)s->n_genres,sizeof(char*)); if(!d->genres)goto fail;
        for(i=0;i<s->n_genres;i++){ if(s->genres[i] && !(d->genres[i]=hdup(s->genres[i])))goto fail; d->n_genres++; }
    }
    return 1;
fail: hfree_meta(d); return 0;
}
static float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}
static float dp(float v){return v*(H.density>0?H.density:1.0f);}
static void draw_text(const char *s,float x,float y,float size,XeColor c) {
    if(s && *s) xe_label(s,dp(x),dp(y),dp(size),c);
}
static void draw_text_fit(const char *s,float x,float y,float size,float maxw,XeColor c) {
    if(s && *s) xe_label_box(s,dp(x),dp(y),dp(size),400,dp(maxw),1,0,c);
}
static void draw_rect(float x,float y,float w,float h,float radius,XeColor c) {
    xe_r_rect((XeRect){dp(x),dp(y),dp(w),dp(h)},dp(radius),c);
}
static void draw_border(float x,float y,float w,float h,float radius,float thick,XeColor c) {
    xe_r_border((XeRect){dp(x),dp(y),dp(w),dp(h)},dp(radius),dp(thick),c);
}
static void draw_image(XeImage *im,float x,float y,float w,float h,float radius) {
    const XeTexture *t=im?xe_img_tex(im):NULL;
    if(t) xe_r_image(t,(XeRect){dp(x),dp(y),dp(w),dp(h)},dp(radius),xe_img_fade(im),0);
}
static XeImage *image_get_cached(const char *url,int w,int h) {
    int i; if(!url||!*url)return NULL;
    for(i=0;i<H.n_images;i++) if(H.images[i].url && strcmp(H.images[i].url,url)==0) return H.images[i].image;
    if(H.n_images>=HOME_MAX_IMAGES)return NULL;
    XeImage *im=xe_img_get(url,w,h); if(!im)return NULL;
    H.images[H.n_images].url=hdup(url); H.images[H.n_images].image=im; H.n_images++;
    return im;
}
static void image_url_set(char **dst,XeImage **handle,const char *url,int w,int h) {
    if((!url&&! *dst)||(url&&*dst&&strcmp(url,*dst)==0))return;
    /* Image handles are owned by the shared URL cache for the screen lifetime.
       Do not release a shared handle here: other UI elements may use the same URL. */
    *handle=NULL;
    free(*dst); *dst=url?hdup(url):NULL;
    if(url)*handle=image_get_cached(url,w,h);
}
static void meta_changed(const XeMeta *m) {
    if(!m)return;
    if(!H.selected_valid || !H.selected.id || !m->id || strcmp(H.selected.id,m->id)!=0) {
        XeMeta copy; if(hcopy_meta(&copy,m)) { if(H.selected_valid)hfree_meta(&H.selected); H.selected=copy; H.selected_valid=1; }
        image_url_set(&H.backdrop_url,&H.backdrop,m->background,1280,720);
        image_url_set(&H.logo_url,&H.logo,m->logo,500,220);
        H.backdrop_mix=0.0f; H.backdrop_clock=0.0;
    }
}
static void home_data_cb(XeShelf *shelves,int n,void *u) {
    int i,j; (void)u;
    for(i=0;i<H.n_rows;i++){free(H.rows[i].title); if(H.rows[i].owns_items)xe_data_free_metas(H.rows[i].items,H.rows[i].n); memset(&H.rows[i],0,sizeof(H.rows[i]));}
    H.n_rows=0;
    if(!shelves||n<=0){ if(shelves)xe_data_free_shelves(shelves,n); return; }
    if(n>HOME_MAX_SHELVES)n=HOME_MAX_SHELVES;
    for(i=0;i<n && H.n_rows<HOME_MAX_ROWS;i++) {
        HomeRow *r=&H.rows[H.n_rows]; int take=shelves[i].n;
        if(take>HOME_MAX_ITEMS)take=HOME_MAX_ITEMS;
        r->title=hdup(shelves[i].title?shelves[i].title:"Recommended");
        if(take>0) {
            r->items=(XeMeta*)calloc((size_t)take,sizeof(XeMeta));
            if(r->items) { for(j=0;j<take;j++)if(hcopy_meta(&r->items[j],&shelves[i].items[j]))r->n++; r->owns_items=1; }
        }
        r->spring.value=r->spring.target=0; r->spring.velocity=0; H.row_scroll[H.n_rows].id=xe_id_mix(xe_id("xe.home.row"),H.n_rows); H.row_scroll[H.n_rows].count=r->n; H.row_scroll[H.n_rows].item_w=150; H.row_scroll[H.n_rows].gap=18; H.row_scroll[H.n_rows].offset=0; xe_spring_init(&H.row_scroll[H.n_rows].spring,0,190,25);
        H.n_rows++;
    }
    xe_data_free_shelves(shelves,n);
    if(H.n_rows>0 && H.rows[0].n>0) meta_changed(&H.rows[0].items[0]);
}
static void meta_noop_cb(XeMeta *m,XeEpisode *eps,int n,void *u) {
    (void)eps;(void)n;(void)u; if(m){meta_changed(m);xe_data_free_meta(m);}
}
static void request_home_once(void) {
    if(H.data_requested)return; H.data_requested=1;
    xe_data_home(home_data_cb,NULL);
}
static const char *meta_genre(const XeMeta *m) {
    if(!m||m->n_genres<=0||!m->genres||!m->genres[0])return "Drama";
    return m->genres[0];
}
static const char *meta_year(const XeMeta *m) {
    if(!m||!m->release||!*m->release)return "New";
    return m->release;
}
static XeId card_id(int row,int col) { return xe_id_mix(xe_id_mix(xe_id("xe.home.poster"),row),col); }
static void draw_topbar(float w) {
    float top=22, x=56; int i; const char *tabs[4]={"Home","Movies","Series","Live"};
    /* A compact wordmark fallback remains visible if no bundled logo is configured. */
    draw_text("STREAM",x,top+3,27,HOME_WHITE); draw_text("X",x+101,top+3,27,HOME_ACCENT);
    x=220;
    for(i=0;i<4;i++) {
        XeRect r={dp(x),dp(top-2),dp(94),dp(42)}; int f=xe_ui_focusable(H.tab_ids[i],r);
        draw_text(tabs[i],x+12,top+9,18,(i==0||f)?HOME_WHITE:HOME_MUTED);
        if(i==0)draw_rect(x+12,top+37,36,3,1,HOME_ACCENT);
        if(f)draw_border(x+2,top-3,88,42,8,2,HOME_ACCENT);
        x+=102;
    }
    { XeRect r={dp(w-174),dp(top-3),dp(56),dp(44)};int f=xe_ui_focusable(H.search_id,r);if(xe_ui_activated(H.search_id))H.want_search=1;draw_text("Search",w-170,top+9,17,f?HOME_WHITE:HOME_MUTED);if(f)draw_border(w-178,top-3,64,44,8,2,HOME_ACCENT); }
    { XeRect r={dp(w-96),dp(top-3),dp(50),dp(44)};int f=xe_ui_focusable(H.profile_id,r);draw_rect(w-91,top+1,34,34,17,(XeColor){.17f,.18f,.23f,1});draw_text("R",w-80,top+8,18,HOME_WHITE);if(f)draw_border(w-96,top-3,50,44,22,2,HOME_ACCENT); }
}
static void draw_backdrop(float w,float h) {
    draw_rect(0,0,w,h,0,HOME_BG);
    if(H.backdrop)draw_image(H.backdrop,0,0,w,h,0);
    /* Low-cost translucent panels simulate a left-to-right and bottom gradient using layered alpha. */
    { int i; for(i=0;i<12;i++){float a=0.06f+(float)(11-i)*0.012f; draw_rect((float)i*w/12.0f,0,w/12.0f+1,h,0,(XeColor){0.025f,0.025f,0.035f,a});} }
    { int i; for(i=0;i<9;i++){float a=0.045f+(float)i*0.045f;draw_rect(0,h-(float)(i+1)*h/9.0f,w,h/9.0f+1,0,(XeColor){0.025f,0.025f,0.035f,a});} }
}
static void draw_deck(float w,float h) {
    float y=116, left=56, cardw=fminf(550.0f,w*0.43f), cardh=300, gap=24; int k;
    XeMeta *hero=NULL; int best_row=-1,best_col=-1; XeId focus=xe_ui_focus();
    for(k=0;k<H.n_rows;k++){int j;for(j=0;j<H.rows[k].n;j++)if(card_id(k,j)==focus){best_row=k;best_col=j;}}
    if(best_row>=0&&best_col>=0)hero=&H.rows[best_row].items[best_col];
    if(!hero && H.selected_valid)hero=&H.selected;
    if(!hero && H.n_rows>0&&H.rows[0].n>0)hero=&H.rows[0].items[0];
    if(hero)meta_changed(hero);
    for(k=2;k>=0;k--) {
        float x=left+(float)k*(cardw*0.70f+gap), scale=k==0?1.0f:(k==1?0.86f:0.74f);
        float cw=cardw*scale,ch=cardh*scale, yy=y+(cardh-ch)*0.5f, dim=k==0?1.0f:(k==1?0.52f:0.30f);
        XeMeta *m=NULL; if(H.n_rows>0&&H.rows[0].n>k)m=&H.rows[0].items[k];
        XeRect r={dp(x),dp(yy),dp(cw),dp(ch)}; int f=xe_ui_focusable(H.deck_ids[k],r);
        draw_rect(x,yy,cw,ch,12,(XeColor){.12f,.13f,.17f,dim});
        if(m&&m->poster){XeImage *im=image_get_cached(m->poster,640,960);draw_image(im,x,yy,cw,ch,12);}
        draw_rect(x,yy,cw,ch,12,(XeColor){.02f,.02f,.03f,(1.0f-dim)*0.68f});
        if(k==0&&hero) {
            if(H.logo)draw_image(H.logo,x+24,yy+24,fminf(cw-48,330),76,0);
            else draw_text_fit(hero->name?hero->name:"Featured",x+24,yy+38,32,cw-48,HOME_WHITE);
            char line[256];snprintf(line,sizeof(line),"%s  ·  %s  ·  ★ %s",meta_year(hero),meta_genre(hero),hero->imdb_rating?hero->imdb_rating:"—");
            draw_text_fit(line,x+24,yy+124,16,cw-48,HOME_MUTED);
            if(hero->description)draw_text_fit(hero->description,x+24,yy+158,15,cw-60,(XeColor){.84f,.84f,.87f,1});
            { XeRect pr={dp(x+24),dp(yy+ch-62),dp(112),dp(42)};int pf=xe_ui_focusable(H.play_id,pr);draw_rect(x+24,yy+ch-62,112,42,6,HOME_WHITE);draw_text("▶  Play",x+43,yy+ch-51,17,(XeColor){.04f,.04f,.05f,1});if(pf)draw_border(x+21,yy+ch-65,118,48,7,2,HOME_ACCENT);if(xe_ui_activated(H.play_id)){if(H.opened_valid)hfree_meta(&H.opened);H.opened_valid=hcopy_meta(&H.opened,hero);} }
            { XeRect lr={dp(x+148),dp(yy+ch-62),dp(112),dp(42)};int lf=xe_ui_focusable(H.list_id,lr);draw_rect(x+148,yy+ch-62,112,42,6,(XeColor){.18f,.19f,.23f,.96f});draw_text("＋  List",x+168,yy+ch-51,17,HOME_WHITE);if(lf)draw_border(x+145,yy+ch-65,118,48,7,2,HOME_ACCENT); }
        }
        if(f||xe_ui_focus_anim(H.deck_ids[k])>0.02f)draw_border(x-2,yy-2,cw+4,ch+4,12,2,HOME_ACCENT);
    }
    (void)h;
}
static void draw_rows(float w,float h) {
    int row; float y=440.0f-H.page_scroll;
    for(row=0;row<H.n_rows;row++) {
        HomeRow *r=&H.rows[row]; float cardw=150,cardh=225,gap=18,vieww=w-112; int col,focus_col=-1; float rowy=y;
        if(rowy>h+20||rowy+cardh+52<0){y+=cardh+66;continue;}
        draw_text(r->title?r->title:"Recommended",56,rowy,22,HOME_WHITE);
        if(r->n<=0){y+=cardh+66;continue;}
        H.row_scroll[row].count=r->n;H.row_scroll[row].item_w=cardw;H.row_scroll[row].gap=gap;
        xe_ui_set_scroll_view(xe_id_mix(xe_id("xe.home.scroll"),row),(XeRect){dp(56),dp(rowy+34),dp(vieww),dp(cardh+18)},&r->scroll,(float)r->n*(cardw+gap),0);
        for(col=0;col<r->n;col++) {
            XeMeta *m=&r->items[col]; float x=56+col*(cardw+gap)-r->scroll; XeRect rect={dp(x),dp(rowy+36),dp(cardw),dp(cardh)}; XeId id=card_id(row,col); int f=xe_ui_focusable(id,rect); float a=xe_ui_focus_anim(id),scale=1.0f+0.08f*a; float sw=cardw*scale,sh=cardh*scale,sx=x-(sw-cardw)*0.5f,sy=rowy+36-(sh-cardh)*0.5f;
            if(f)focus_col=col;
            if(x+cardw<45||x>w-10)continue;
            draw_rect(sx,sy,sw,sh,7,(XeColor){.14f,.15f,.19f,1});
            if(m->poster)draw_image(image_get_cached(m->poster,342,513),sx,sy,sw,sh,7);
            draw_rect(sx,sy,sw,sh,7,(XeColor){.02f,.02f,.03f,0.12f});
            if(a>0.01f){draw_rect(sx-4,sy-4,sw+8,sh+8,10,(XeColor){.898f,.035f,.078f,.10f*a});draw_border(sx-2,sy-2,sw+4,sh+4,9,2,HOME_ACCENT);}
            draw_text_fit(m->name?m->name:"Untitled",x,rowy+36+cardh+9,14,cardw,HOME_WHITE);
            if(f)meta_changed(m);
            if(xe_ui_activated(id)){if(H.opened_valid)hfree_meta(&H.opened);H.opened_valid=hcopy_meta(&H.opened,m);}
        }
        if(focus_col>=0){xe_row_scroll_to(&H.row_scroll[row],focus_col,vieww);r->scroll=H.row_scroll[row].offset;H.focused_row=row;H.focused_col=focus_col;}
        y+=cardh+66;
    }
}
void xe_home_init(void) {
    int i; if(H.initialized)return; memset(&H,0,sizeof(H)); H.initialized=1; H.density=1.0f;
    H.tab_ids[0]=xe_id("xe.home.tab.home");H.tab_ids[1]=xe_id("xe.home.tab.movies");H.tab_ids[2]=xe_id("xe.home.tab.series");H.tab_ids[3]=xe_id("xe.home.tab.live");
    H.search_id=xe_id("xe.home.search");H.profile_id=xe_id("xe.home.profile");H.play_id=xe_id("xe.home.play");H.list_id=xe_id("xe.home.list");
    for(i=0;i<3;i++)H.deck_ids[i]=xe_id_mix(xe_id("xe.home.deck"),i);
    xe_spring_init(&H.deck_slide,0,170,24); request_home_once();
}
void xe_home_frame(double dt,XeRect area) {
    XeEvent events[96]; int n=0; float w=area.w,h=area.h; int i;
    if(!H.initialized)xe_home_init();
    if(w<=0||h<=0){w=1280;h=720;}
    H.density=1.0f; /* Hosts should pass logical dp-sized XeRect; render viewport remains the host's responsibility. */
    H.last_w=w;H.last_h=h;
    while(n<96&&xe_platform_poll(&events[n]))n++;
    xe_ui_begin(dt,events,n); xe_img_pump(); xe_data_pump();
    xe_r_begin((int)w,(int)h); draw_backdrop(w,h); draw_topbar(w); draw_deck(w,h); draw_rows(w,h); xe_ui_end(); xe_r_end();
    if(H.backdrop_mix<1.0f)H.backdrop_mix=clampf(H.backdrop_mix+(float)(dt/0.4),0,1);
    (void)i;
}
int xe_home_opened(XeMeta *out) {
    if(!H.opened_valid)return 0;
    if(out){*out=H.opened;memset(&H.opened,0,sizeof(H.opened));}
    else hfree_meta(&H.opened);
    H.opened_valid=0;return 1;
}
int xe_home_take_search(void) { int v=H.want_search; H.want_search=0; return v; }
XeId xe_home_last_focus(void) { return xe_ui_focus(); }
