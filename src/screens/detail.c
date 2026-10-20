#include "xe/detail.h"
#include "xe/platform.h"
#include "xe/render.h"
#include "xe/text.h"
#include "xe/images.h"
#include "xe/ui.h"
#include "xe/anim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define D_BG ((XeColor){0.035f,0.035f,0.047f,1})
#define D_WHITE ((XeColor){0.95f,0.95f,0.97f,1})
#define D_MUTED ((XeColor){0.62f,0.63f,0.68f,1})
#define D_RED ((XeColor){0.90f,0.035f,0.08f,1})
#define D_MAX_EVENTS 96
#define D_MAX_EP 80

typedef struct { char *id,*title,*thumb; int season,episode; } DEpisode;
static XeEpisode *g_all_eps;
static int g_all_n, g_max_season;
static void d_refill(void);

static struct {
 int initialized, expanded, play_pending, list_added, active_tab, season, ep_count, ep_focus;
 XeMeta meta; int has_meta;
 XeImage *backdrop,*logo; char *backdrop_url,*logo_url;
 DEpisode eps[D_MAX_EP];
 XeId play_id,list_id,trailer_id,expand_id,tab_ids[3],season_ids[32],ep_ids[D_MAX_EP];
 float scroll; double dt; float density;
 char play_type[24],play_value[512];
} D;

static char *ddup(const char *s){size_t n;char *p;if(!s)return NULL;n=strlen(s)+1;p=(char*)malloc(n);if(p)memcpy(p,s,n);return p;}
static void dmeta_free(XeMeta *m){int i;if(!m)return;free(m->id);free(m->type);free(m->name);free(m->poster);free(m->background);free(m->logo);free(m->description);free(m->release);free(m->imdb_rating);free(m->runtime);for(i=0;i<m->n_genres;i++)free(m->genres[i]);free(m->genres);memset(m,0,sizeof(*m));}
static int dmeta_copy(XeMeta *d,const XeMeta *s){int i;memset(d,0,sizeof(*d));if(!s)return 0;
#define DC(f) do{if(s->f&&!(d->f=ddup(s->f)))goto fail;}while(0)
 DC(id);DC(type);DC(name);DC(poster);DC(background);DC(logo);DC(description);DC(release);DC(imdb_rating);DC(runtime);
#undef DC
 if(s->n_genres>0&&s->genres){d->genres=(char**)calloc((size_t)s->n_genres,sizeof(char*));if(!d->genres)goto fail;for(i=0;i<s->n_genres;i++){if(s->genres[i]&&!(d->genres[i]=ddup(s->genres[i])))goto fail;d->n_genres++;}}return 1;
 fail:dmeta_free(d);return 0;}
static int dphone(XeRect a){return a.h>a.w;}
static float dd(float x){return x*(D.density>0?D.density:1);}
static void dtxt(const char *s,float x,float y,float z,XeColor c){if(s&&*s)xe_label(s,dd(x),dd(y),dd(z),c);}
static void dbox(XeRect r,float rad,XeColor c){xe_r_rect(r,rad,c);}
static int deq(const char *a,const char *b){return a&&b&&strcmp(a,b)==0;}
static void dset_image(XeImage **im,char **url,const char *src,int w,int h){char full[2048];if(!src||!*src)return;if(*url&&strcmp(*url,src)==0)return;if(*im){xe_img_release(*im);*im=NULL;}free(*url);*url=ddup(src);if(!*url)return;
 if(strstr(src,"http://")==src||strstr(src,"https://")==src)snprintf(full,sizeof(full),"%s",src);else snprintf(full,sizeof(full),"https://image.tmdb.org/t/p/original%s",src);
 *im=xe_img_get(full,w,h);}
static int dbutton(XeId id,XeRect r,const char *label,int primary){int f=xe_ui_focusable(id,r),a=xe_ui_activated(id);XeColor c=primary?D_RED:(XeColor){0.20f,0.20f,0.23f,0.94f};if(f)c=(XeColor){c.r*0.78f+0.22f,c.g*0.78f+0.22f,c.b*0.78f+0.22f,1};dbox(r,dd(12),c);xe_label_box(label,r.x+dd(12),r.y+r.h*.5f-dd(8),dd(16),600,r.w-dd(24),1,1,D_WHITE);return a;}
static void dmake_play(const char *id,const char *type){snprintf(D.play_type,sizeof(D.play_type),"%s",type&&*type?type:"movie");snprintf(D.play_value,sizeof(D.play_value),"%s",id?id:"");D.play_pending=1;}
void xe_detail_open(const XeMeta *m){int i;if(D.has_meta)dmeta_free(&D.meta);D.has_meta=dmeta_copy(&D.meta,m);D.expanded=0;D.play_pending=0;D.list_added=0;D.active_tab=0;D.season=1;D.ep_count=0;D.ep_focus=0;D.scroll=0;
 if(D.has_meta){dset_image(&D.backdrop,&D.backdrop_url,D.meta.background?D.meta.background:D.meta.poster,1280,720);dset_image(&D.logo,&D.logo_url,D.meta.logo,600,240);}for(i=0;i<32;i++)D.season_ids[i]=xe_id_mix(xe_id("detail-season"),i);D.play_id=xe_id("detail-play");D.list_id=xe_id("detail-list");D.trailer_id=xe_id("detail-trailer");D.expand_id=xe_id("detail-expand");D.tab_ids[0]=xe_id("detail-episodes");D.tab_ids[1]=xe_id("detail-more");D.tab_ids[2]=xe_id("detail-trailers");for(i=0;i<D_MAX_EP;i++)D.ep_ids[i]=xe_id_mix(xe_id("detail-episode"),i);D.initialized=1;xe_ui_set_focus(D.play_id);}

static void d_draw_tv_episodes(float x,float y,float w,float h){int i;float cardw=dd(340),gap=dd(18);char label[256];if(D.ep_count==0){dtxt("Episodes will appear here when episode metadata is supplied.",x,y+dd(28),dd(15),D_MUTED);return;}
 xe_r_push_clip((XeRect){x,y,w,h},dd(4));for(i=0;i<D.ep_count;i++){float cx=x+i*(cardw+gap)-D.scroll;if(cx+cardw<x||cx>x+w)continue;XeRect r={cx,y,cardw,h};int f=xe_ui_focusable(D.ep_ids[i],r);dbox(r,dd(10),(XeColor){0.13f,0.13f,0.16f,1});if(D.eps[i].thumb){XeImage *im=xe_img_get(D.eps[i].thumb,320,180);const XeTexture *t=xe_img_tex(im);if(t)xe_r_image(t,(XeRect){r.x,r.y,r.w,dd(146)},dd(10),xe_img_fade(im),0);}
 snprintf(label,sizeof(label),"S%d E%d",D.eps[i].season,D.eps[i].episode);dtxt(label,cx+dd(12),y+dd(158),dd(13),D_MUTED);dtxt(D.eps[i].title?D.eps[i].title:"Episode",cx+dd(12),y+dd(181),dd(16),D_WHITE);if(f)xe_r_border(r,dd(10),dd(2),D_RED);if(xe_ui_activated(D.ep_ids[i])){char id[512];if(D.eps[i].id)snprintf(id,sizeof(id),"%s",D.eps[i].id);else snprintf(id,sizeof(id),"%s:%d:%d",D.meta.id?D.meta.id:"",D.eps[i].season,D.eps[i].episode);dmake_play(id,D.meta.type);}}
 xe_r_pop_clip();}

static void d_draw_phone_episodes(float x,float y,float w,float bottom){
 int i; float rowh=86.0f; char label[256];
 if(D.ep_count==0){dtxt("Episodes will appear here when metadata is available.",x,y+12,13,D_MUTED);return;}
 xe_r_push_clip((XeRect){dd(x),dd(y),dd(w),dd(fmaxf(0,bottom-y))},0);
 for(i=0;i<D.ep_count;i++){
  float cy=y+i*rowh-D.scroll;
  XeRect r={dd(x),dd(cy),dd(w),dd(rowh-6)};
  if(cy+rowh<y-2||cy>bottom+rowh)continue;
  {int f=xe_ui_focusable(D.ep_ids[i],r);
   dbox(r,dd(8),(XeColor){.12f,.12f,.15f,1});
   if(D.eps[i].thumb){
    XeImage *im=xe_img_get(D.eps[i].thumb,240,135);
    const XeTexture *t=xe_img_tex(im);
    if(t)xe_r_image(t,(XeRect){r.x+dd(6),r.y+dd(6),dd(112),dd(63)},dd(6),xe_img_fade(im),0);
   } else dbox((XeRect){r.x+dd(6),r.y+dd(6),dd(112),dd(63)},dd(6),(XeColor){.20f,.20f,.24f,1});
   snprintf(label,sizeof(label),"S%d E%d",D.eps[i].season,D.eps[i].episode);
   xe_label_box(label,r.x+dd(126),r.y+dd(10),dd(11),500,r.w-dd(134),1,0,D_MUTED);
   xe_label_box(D.eps[i].title?D.eps[i].title:"Episode",r.x+dd(126),r.y+dd(30),dd(13),600,r.w-dd(134),2,0,D_WHITE);
   if(f)xe_r_border(r,dd(8),dd(2),D_RED);
   if(xe_ui_activated(D.ep_ids[i])){char id[512];if(D.eps[i].id)snprintf(id,sizeof(id),"%s",D.eps[i].id);else snprintf(id,sizeof(id),"%s:%d:%d",D.meta.id?D.meta.id:"",D.eps[i].season,D.eps[i].episode);dmake_play(id,D.meta.type);}
  }
 }
 xe_r_pop_clip();
}

static void d_draw_phone(XeRect area){
 float w=area.w,h=area.h,margin=14,hero_w=w-2*margin,hero_h=hero_w*9.0f/16.0f;
 float y=12,desc_y,play_y,season_y,episodes_y;int i;
 if(D.backdrop){const XeTexture *t=xe_img_tex(D.backdrop);if(t)xe_r_image(t,(XeRect){dd(margin),dd(y),dd(hero_w),dd(hero_h)},dd(10),xe_img_fade(D.backdrop),0);}
 else dbox((XeRect){dd(margin),dd(y),dd(hero_w),dd(hero_h)},dd(10),(XeColor){.13f,.13f,.17f,1});
 xe_r_rect_vgrad((XeRect){dd(margin),dd(y+hero_h*.45f),dd(hero_w),dd(hero_h*.55f)},dd(10),(XeColor){.02f,.02f,.03f,0},(XeColor){.02f,.02f,.03f,.9f});
 y+=hero_h+12;
 if(D.logo){const XeTexture *t=xe_img_tex(D.logo);if(t)xe_r_image(t,(XeRect){dd(margin),dd(y),dd(hero_w),dd(56)},0,xe_img_fade(D.logo),0);}
 else xe_label_box(D.meta.name?D.meta.name:"Untitled",dd(margin),dd(y+5),dd(23),700,dd(hero_w),2,0,D_WHITE);
 y+=60;
 {
  char facts[512];int used=0;facts[0]=0;
  if(D.meta.release)used+=snprintf(facts+used,sizeof(facts)-(size_t)used,"%s",D.meta.release);
  if(D.meta.runtime&&used<(int)sizeof(facts)-16)used+=snprintf(facts+used,sizeof(facts)-(size_t)used," · %s",D.meta.runtime);
  if(D.meta.imdb_rating&&used<(int)sizeof(facts)-24)used+=snprintf(facts+used,sizeof(facts)-(size_t)used," · IMDb %s",D.meta.imdb_rating);
  for(i=0;i<D.meta.n_genres&&used<(int)sizeof(facts)-48;i++)used+=snprintf(facts+used,sizeof(facts)-(size_t)used," · %s",D.meta.genres[i]);
  xe_label_box(facts,dd(margin),dd(y),dd(11),400,dd(hero_w),2,0,D_MUTED);
 }
 y+=32;
 desc_y=y;
 if(D.meta.description&&*D.meta.description){
  xe_label_box(D.meta.description,dd(margin),dd(y),dd(13),400,dd(hero_w),D.expanded?12:4,0,D_WHITE);
  y+=D.expanded?90:60;
  if(strlen(D.meta.description)>150){XeRect more={dd(margin),dd(y),dd(90),dd(26)};int f=xe_ui_focusable(D.expand_id,more);dtxt(D.expanded?"Show less":"More",margin+4,y+6,12,f?D_WHITE:D_RED);if(f)xe_r_border(more,dd(6),dd(1),D_RED);if(xe_ui_activated(D.expand_id))D.expanded=!D.expanded;y+=30;}
 }
 play_y=y;
 if(dbutton(D.play_id,(XeRect){dd(margin),dd(play_y),dd(hero_w),dd(44)},"Play",1))dmake_play(D.meta.id,D.meta.type);
 y+=56;
 if(D.meta.type&&strcmp(D.meta.type,"movie")!=0){
  season_y=y;dtxt("Seasons",margin,season_y,14,D_WHITE);y+=24;
  for(i=0;i<(g_max_season>0?(g_max_season<14?g_max_season:14):1);i++){
   float cw=48,xx=margin+i*(cw+7);XeRect r={dd(xx),dd(y),dd(cw),dd(30)};int f=xe_ui_focusable(D.season_ids[i],r);
   dbox(r,dd(15),D.season==i+1?D_RED:(XeColor){.16f,.16f,.19f,1});
   xe_label_box("Season",r.x+dd(2),r.y+dd(2),dd(1),400,dd(cw-4),1,1,(XeColor){0,0,0,0}); /* Preserve chip hit area without adding another label API. */
   {char ss[12];snprintf(ss,sizeof(ss),"S%d",i+1);xe_label_box(ss,r.x,r.y+dd(8),dd(12),600,r.w,1,1,D_WHITE);}
   if(f)xe_r_border(r,dd(15),dd(2),D_WHITE);
   if(xe_ui_activated(D.season_ids[i])){D.season=i+1;d_refill();D.scroll=0;}
  }
  y+=38;episodes_y=y;
  d_draw_phone_episodes(margin,episodes_y,hero_w,h-8);
 }
}

void xe_detail_frame(double dt,XeRect area){
 XeEvent ev[D_MAX_EVENTS];int n=0,i;float w=area.w,h=area.h;int phone=dphone(area);
 D.dt=dt;D.density=1.0f;
 while(n<D_MAX_EVENTS&&xe_platform_poll(&ev[n]))n++;
 xe_ui_begin(dt,ev,n);xe_img_pump();xe_r_begin((int)w,(int)h);dbox((XeRect){0,0,w,h},0,D_BG);
 if(!D.initialized||!D.has_meta){dtxt("No title selected",phone?14:64,phone?20:70,phone?22:28,D_WHITE);xe_ui_end();xe_r_end();return;}
 if(phone){d_draw_phone(area);xe_ui_end();xe_r_end();return;}

 if(D.backdrop){const XeTexture *t=xe_img_tex(D.backdrop);if(t)xe_r_image(t,(XeRect){0,0,w,h},0,xe_img_fade(D.backdrop),0);}
 xe_r_rect_hgrad((XeRect){0,0,w*0.75f,h},0,(XeColor){0.02f,0.02f,0.03f,0.92f},(XeColor){0.02f,0.02f,0.03f,0.0f});
 xe_r_rect_vgrad((XeRect){0,h*0.45f,w,h*0.55f},0,(XeColor){0.02f,0.02f,0.03f,0.0f},(XeColor){0.02f,0.02f,0.03f,0.95f});
 if(D.logo){const XeTexture *t=xe_img_tex(D.logo);if(t)xe_r_image(t,(XeRect){dd(64),dd(46),dd(380),dd(150)},0,xe_img_fade(D.logo),0);}else dtxt(D.meta.name?D.meta.name:"Untitled",dd(64),dd(88),dd(36),D_WHITE);
 {char facts[512];int used=0;facts[0]=0;if(D.meta.release)used+=snprintf(facts+used,sizeof(facts)-(size_t)used,"%s",D.meta.release);if(D.meta.runtime&&used<(int)sizeof(facts)-8)used+=snprintf(facts+used,sizeof(facts)-(size_t)used,"  ·  %s",D.meta.runtime);if(D.meta.imdb_rating&&used<(int)sizeof(facts)-8)used+=snprintf(facts+used,sizeof(facts)-(size_t)used,"  ·  IMDb %s",D.meta.imdb_rating);for(i=0;i<D.meta.n_genres&&used<(int)sizeof(facts)-48;i++)used+=snprintf(facts+used,sizeof(facts)-(size_t)used,"  ·  %s",D.meta.genres[i]);dtxt(facts,dd(66),dd(218),dd(15),D_MUTED);}
 if(D.meta.description){if(!D.expanded)xe_label_box(D.meta.description,dd(66),dd(252),dd(16),400,dd(620),3,0,D_WHITE);else xe_label_box(D.meta.description,dd(66),dd(252),dd(16),400,dd(620),10,0,D_WHITE);if(strlen(D.meta.description)>205){if(dbutton(D.expand_id,(XeRect){dd(64),dd(322),dd(150),dd(38)},D.expanded?"Show less":"More details",0))D.expanded=!D.expanded;}}
 if(dbutton(D.play_id,(XeRect){dd(64),dd(382),dd(230),dd(62)},"Play",1))dmake_play(D.meta.id,D.meta.type);
 if(dbutton(D.trailer_id,(XeRect){dd(310),dd(382),dd(150),dd(62)},"Trailer",0)){}
 if(dbutton(D.list_id,(XeRect){dd(474),dd(382),dd(150),dd(62)},D.list_added?"In My List":"+ My List",0))D.list_added=!D.list_added;
 if(D.meta.type&&strcmp(D.meta.type,"movie")!=0){
  {const char *tabs[]={"Episodes","More Like This","Trailers & More"};float tx=dd(64);for(i=0;i<3;i++){XeRect r={tx,dd(492),dd(i==1?190:150),dd(44)};int f=xe_ui_focusable(D.tab_ids[i],r);if(f||D.active_tab==i)xe_r_border(r,dd(8),dd(2),D.active_tab==i?D_RED:D_WHITE);dtxt(tabs[i],r.x+dd(8),r.y+dd(14),dd(15),D_WHITE);if(xe_ui_activated(D.tab_ids[i]))D.active_tab=i;tx+=r.w+dd(18);}}
  dtxt("Season",dd(64),dd(560),dd(16),D_WHITE);
  for(i=0;i<(g_max_season>0?(g_max_season<14?g_max_season:14):1);i++){char s[32];XeRect r={dd(140+i*72),dd(548),dd(60),dd(40)};snprintf(s,sizeof(s),"S%d",i+1);int f=xe_ui_focusable(D.season_ids[i],r);dbox(r,dd(8),D.season==i+1?D_RED:(XeColor){0.16f,0.16f,0.18f,1});if(f)xe_r_border(r,dd(8),dd(2),D_WHITE);dtxt(s,r.x+dd(18),r.y+dd(12),dd(14),D_WHITE);if(xe_ui_activated(D.season_ids[i])){D.season=i+1;d_refill();}}
  d_draw_tv_episodes(dd(64),dd(610),w-dd(128),dd(235));
 }
 xe_ui_end();xe_r_end();
}
int xe_detail_play(char *out_type,char *out_id,size_t cap){if(!D.play_pending)return 0;D.play_pending=0;if(out_type)snprintf(out_type,cap,"%s",D.play_type);if(out_id&&cap)snprintf(out_id,cap,"%s",D.play_value);return 1;}

static void d_refill(void)
{
 int i;
 for(i=0;i<D.ep_count;i++){free(D.eps[i].id);free(D.eps[i].title);free(D.eps[i].thumb);}
 D.ep_count=0;D.scroll=0;
 for(i=0;i<g_all_n&&D.ep_count<D_MAX_EP;i++){XeEpisode *e=&g_all_eps[i];if(e->season!=D.season)continue;D.eps[D.ep_count].id=ddup(e->id);D.eps[D.ep_count].title=ddup(e->title);D.eps[D.ep_count].thumb=ddup(e->thumb);D.eps[D.ep_count].season=e->season;D.eps[D.ep_count].episode=e->episode;D.ep_count++;}
}
void xe_detail_set_episodes(const XeEpisode *eps,int n)
{
 int i;
 for(i=0;i<g_all_n;i++){free(g_all_eps[i].id);free(g_all_eps[i].title);free(g_all_eps[i].thumb);}
 free(g_all_eps);g_all_eps=NULL;g_all_n=0;g_max_season=0;
 if(n>0&&eps)g_all_eps=(XeEpisode*)calloc((size_t)n,sizeof(*g_all_eps));
 for(i=0;g_all_eps&&i<n;i++){g_all_eps[g_all_n].id=ddup(eps[i].id);g_all_eps[g_all_n].title=ddup(eps[i].title);g_all_eps[g_all_n].thumb=ddup(eps[i].thumb);g_all_eps[g_all_n].season=eps[i].season;g_all_eps[g_all_n].episode=eps[i].episode;if(eps[i].season>g_max_season)g_max_season=eps[i].season;g_all_n++;}
 if(D.season<1||D.season>g_max_season)D.season=g_max_season>=1?1:0;
 d_refill();
}
void xe_detail_update_meta(const XeMeta *m)
{
 XeMeta copy;
 if(!m||!D.has_meta||!D.meta.id||!m->id||strcmp(D.meta.id,m->id)!=0)return;
 if(!dmeta_copy(&copy,m))return;
 dmeta_free(&D.meta);D.meta=copy;
 dset_image(&D.backdrop,&D.backdrop_url,D.meta.background?D.meta.background:D.meta.poster,1280,720);
 dset_image(&D.logo,&D.logo_url,D.meta.logo,600,240);
}
const XeMeta *xe_detail_meta(void){return D.has_meta?&D.meta:NULL;}

