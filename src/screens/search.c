#include "xe/search.h"
#include "xe/platform.h"
#include "xe/render.h"
#include "xe/text.h"
#include "xe/images.h"
#include "xe/ui.h"
#include "xe/anim.h"
#include "xe/data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define S_BG ((XeColor){0.035f,0.035f,0.047f,1})
#define S_WHITE ((XeColor){0.95f,0.95f,0.97f,1})
#define S_MUTED ((XeColor){0.62f,0.63f,0.68f,1})
#define S_RED ((XeColor){0.90f,0.035f,0.08f,1})
#define S_MAX_EVENTS 96
#define S_MAX_RESULTS 160
#define S_MAX_RECENT 8
#define S_QUERY_CAP 192

typedef struct { XeMeta m; XeImage *image; } SResult;
static struct {
 int initialized, search_pending, result_ready, result_count, focus_result, recent_count;
 char query[S_QUERY_CAP]; size_t query_len;
 char recent[S_MAX_RECENT][S_QUERY_CAP];
 SResult results[S_MAX_RESULTS]; XeMeta opened; int opened_valid;
 XeId key_ids[40], recent_ids[S_MAX_RECENT], search_id, clear_id, result_ids[S_MAX_RESULTS];
 float scroll,density,last_w,last_h;
} S;
static char *sdup(const char *s){size_t n;char *p;if(!s)return NULL;n=strlen(s)+1;p=(char*)malloc(n);if(p)memcpy(p,s,n);return p;}
static void smeta_free(XeMeta *m){int i;if(!m)return;free(m->id);free(m->type);free(m->name);free(m->poster);free(m->background);free(m->logo);free(m->description);free(m->release);free(m->imdb_rating);free(m->runtime);for(i=0;i<m->n_genres;i++)free(m->genres[i]);free(m->genres);memset(m,0,sizeof(*m));}
static int smeta_copy(XeMeta *d,const XeMeta *s){int i;memset(d,0,sizeof(*d));if(!s)return 0;
#define SC(f) do{if(s->f&&!(d->f=sdup(s->f)))goto fail;}while(0)
 SC(id);SC(type);SC(name);SC(poster);SC(background);SC(logo);SC(description);SC(release);SC(imdb_rating);SC(runtime);
#undef SC
 if(s->n_genres>0&&s->genres){d->genres=(char**)calloc((size_t)s->n_genres,sizeof(char*));if(!d->genres)goto fail;for(i=0;i<s->n_genres;i++){if(s->genres[i]&&!(d->genres[i]=sdup(s->genres[i])))goto fail;d->n_genres++;}}return 1;fail:smeta_free(d);return 0;}
static int sphone(XeRect a){return a.h>a.w;}
static float sd(float x){return x*(S.density>0?S.density:1.0f);}
static void stxt(const char *t,float x,float y,float size,XeColor c){if(t&&*t)xe_label(t,sd(x),sd(y),sd(size),c);}
static void sbox(XeRect r,float radius,XeColor c){xe_r_rect(r,radius,c);}
static int sbutton(XeId id,XeRect r,const char *label,int primary){int f=xe_ui_focusable(id,r),a=xe_ui_activated(id);sbox(r,sd(8),primary?S_RED:(XeColor){0.15f,0.15f,0.18f,1});if(f)xe_r_border(r,sd(8),sd(2),S_WHITE);xe_label_box(label,r.x+sd(8),r.y+sd(9),sd(13),600,r.w-sd(16),1,1,S_WHITE);return a;}
static void srequest(void){if(!S.query_len)return;S.search_pending=1;S.result_ready=0;}
static void sresults_cb(XeMeta *items,int n,void *u){int i;(void)u;for(i=0;i<S.result_count;i++)smeta_free(&S.results[i].m);S.result_count=0;if(n>S_MAX_RESULTS)n=S_MAX_RESULTS;for(i=0;i<n;i++){if(smeta_copy(&S.results[i].m,&items[i])){char full[2048];S.results[i].image=NULL;if(items[i].poster){if(strstr(items[i].poster,"http://")==items[i].poster||strstr(items[i].poster,"https://")==items[i].poster)snprintf(full,sizeof(full),"%s",items[i].poster);else snprintf(full,sizeof(full),"https://image.tmdb.org/t/p/w342%s",items[i].poster);S.results[i].image=xe_img_get(full,342,513);}S.result_ids[i]=xe_id_mix(xe_id("search-result"),i);S.result_count++;}}S.search_pending=0;S.result_ready=1;}
static double s_dirty_at;
static void sadd_char(char c){if(S.query_len+1<S_QUERY_CAP){S.query[S.query_len++]=c;S.query[S.query_len]=0;s_dirty_at=xe_time_now();}}
static void sdelete(void){if(S.query_len){S.query[--S.query_len]=0;s_dirty_at=xe_time_now();}}
static const char *KEYS[]={"1","2","3","4","5","6","7","8","9","0","q","w","e","r","t","y","u","i","o","p","a","s","d","f","g","h","j","k","l","z","x","c","v","b","n","m","Space","Delete","Clear","Search"};
static void srun_search(void){char q[S_QUERY_CAP];int i;if(!S.query_len)return;snprintf(q,sizeof(q),"%s",S.query);for(i=S.recent_count;i>0&&i<S_MAX_RECENT;i--)memcpy(S.recent[i],S.recent[i-1],S_QUERY_CAP);if(S.recent_count<S_MAX_RECENT)S.recent_count++;snprintf(S.recent[0],S_QUERY_CAP,"%s",q);xe_data_search(q,sresults_cb,NULL);S.search_pending=1;S.scroll=0;}

static void s_phone_results(float w,float h,float top)
{
 int i,cols=3;float margin=12,gap=8,cw=(w-margin*2-gap*2)/3.0f,ch=cw*1.48f+28;
 xe_r_push_clip((XeRect){0,sd(top),sd(w),sd(h-top-8)},0);
 for(i=0;i<S.result_count;i++){
  int row=i/cols,col=i%cols;float x=margin+col*(cw+gap),y=top+row*ch-S.scroll;
  XeRect r={sd(x),sd(y),sd(cw),sd(ch-26)};
  int f;
  if(y+ch<top-10||y>h+ch)continue;
  f=xe_ui_focusable(S.result_ids[i],r);
  sbox(r,sd(6),(XeColor){.12f,.12f,.15f,1});
  if(S.results[i].image){const XeTexture *t=xe_img_tex(S.results[i].image);if(t)xe_r_image(t,r,sd(6),xe_img_fade(S.results[i].image),0);}
  if(f)xe_r_border(r,sd(6),sd(2),S_RED);
  xe_label_box(S.results[i].m.name?S.results[i].m.name:"Untitled",sd(x),sd(y+ch-23),sd(11),400,sd(cw),2,0,f?S_WHITE:S_MUTED);
  if(xe_ui_activated(S.result_ids[i])){if(S.opened_valid)smeta_free(&S.opened);S.opened_valid=smeta_copy(&S.opened,&S.results[i].m);}
  if(f){float want=(float)row*ch;float target=want;float k=.2f;S.scroll+=(target-S.scroll)*k;}
 }
 xe_r_pop_clip();
}

void xe_search_frame(double dt,XeRect area)
{
 XeEvent ev[S_MAX_EVENTS];int n=0,i;float w=area.w,h=area.h;int phone=sphone(area);
 S.density=1.0f;S.last_w=w;S.last_h=h;
 if(!S.initialized){for(i=0;i<40;i++)S.key_ids[i]=xe_id_mix(xe_id("search-key"),i);for(i=0;i<S_MAX_RECENT;i++)S.recent_ids[i]=xe_id_mix(xe_id("search-recent"),i);S.search_id=xe_id("search-submit");S.clear_id=xe_id("search-clear");S.initialized=1;xe_ui_set_focus(phone?S.search_id:S.key_ids[0]);}
 while(n<S_MAX_EVENTS&&xe_platform_poll(&ev[n]))n++;
 xe_ui_begin(dt,ev,n);xe_img_pump();xe_data_pump();
 for(i=0;i<n;i++){
  if(ev[i].type==XE_EV_KEY_DOWN&&ev[i].key==XE_KEY_DELETE)sdelete();
  else if(ev[i].type==XE_EV_TEXT&&ev[i].codepoint>=32&&ev[i].codepoint<127)sadd_char((char)ev[i].codepoint);
 }
 if(s_dirty_at>0&&xe_time_now()-s_dirty_at>0.6){s_dirty_at=0;if(S.query_len>=2){xe_data_search(S.query,sresults_cb,NULL);S.search_pending=1;}}
 xe_r_begin((int)w,(int)h);sbox((XeRect){0,0,w,h},0,S_BG);
 stxt("Search",phone?14:54,phone?10:18,phone?23:30,S_WHITE);
 if(phone){
  XeRect qr={sd(12),sd(48),sd(w-24),sd(42)};int qf=xe_ui_focusable(S.search_id,qr);
  sbox(qr,sd(8),(XeColor){.12f,.12f,.15f,1});
  xe_label_box(S.query_len?S.query:"Search movies and series…",qr.x+sd(10),qr.y+sd(12),sd(14),400,qr.w-sd(20),1,0,S.query_len?S_WHITE:S_MUTED);
  if(qf)xe_r_border(qr,sd(8),sd(2),S_RED);
  if(xe_ui_activated(S.search_id)){srun_search();}
  stxt(S.search_pending?"Searching…":(S.result_ready?"Results":"Type to search"),12,100,12,S_MUTED);
  s_phone_results(w,h,120);
 }else{
  sbox((XeRect){sd(54),sd(70),w-sd(108),sd(54)},sd(8),(XeColor){0.12f,0.12f,0.15f,1});
  stxt(S.query,70,88,20,S_WHITE);if(!S.query_len)stxt("Search movies and series…",70,88,18,S_MUTED);
  for(i=0;i<40;i++){int col=i%10,row=i/10;float kw=52,gap=6,x=54+col*(kw+gap),y=142+row*45;XeRect r={sd(x),sd(y),sd(kw),sd(38)};int f=xe_ui_focusable(S.key_ids[i],r);sbox(r,sd(6),(XeColor){.14f,.14f,.17f,1});if(f)xe_r_border(r,sd(6),sd(2),S_WHITE);const char *label=KEYS[i];if(i==37)label="Del";if(i==38)label="Clear";stxt(label,x+6,y+11,i>=36?11:14,S_WHITE);if(xe_ui_activated(S.key_ids[i])){if(i<36)sadd_char(KEYS[i][0]);else if(i==36)sadd_char(' ');else if(i==37)sdelete();else if(i==38){S.query_len=0;S.query[0]=0;}else srun_search();}}
  stxt("Recent",54,345,16,S_MUTED);for(i=0;i<S.recent_count;i++){XeRect r={sd(54+i*150),sd(362),sd(138),sd(34)};int f=xe_ui_focusable(S.recent_ids[i],r);sbox(r,sd(16),(XeColor){.12f,.12f,.15f,1});if(f)xe_r_border(r,sd(16),sd(2),S_RED);stxt(S.recent[i],r.x+sd(10),r.y+sd(10),sd(12),S_WHITE);if(xe_ui_activated(S.recent_ids[i])){snprintf(S.query,sizeof(S.query),"%s",S.recent[i]);S.query_len=strlen(S.query);srun_search();}}
  stxt(S.search_pending?"Searching…":(S.result_ready?"Results":"Press Search to find titles"),54,424,17,S_MUTED);
  {int cols=8;float cw=125,ch=188,gap=14,y0=452;xe_r_push_clip((XeRect){0,sd(y0-6),w,h-sd(y0-6)},0);for(i=0;i<S.result_count;i++){int row=i/cols,col=i%cols;float x=54+col*(cw+gap),y=y0+row*(ch+22)-S.scroll;XeRect r={sd(x),sd(y),sd(cw),sd(ch)};if(y+ch<y0-ch*2||y>h+ch*2)continue;int f=xe_ui_focusable(S.result_ids[i],r);if(y+ch<y0)continue;sbox(r,sd(8),(XeColor){.12f,.12f,.15f,1});if(S.results[i].image){const XeTexture *t=xe_img_tex(S.results[i].image);if(t)xe_r_image(t,r,sd(8),xe_img_fade(S.results[i].image),0);}if(f)xe_r_border(r,sd(8),sd(2),S_RED);xe_label_box(S.results[i].m.name?S.results[i].m.name:"Untitled",sd(x),sd(y+ch+4),sd(12),400,sd(cw),1,0,f?S_WHITE:S_MUTED);if(f){float want=(float)row*(ch+22);S.scroll+=(want-S.scroll)*.25f;}if(xe_ui_activated(S.result_ids[i])){if(S.opened_valid)smeta_free(&S.opened);S.opened_valid=smeta_copy(&S.opened,&S.results[i].m);}}xe_r_pop_clip();}
 }
 xe_ui_end();xe_r_end();
}
int xe_search_opened(XeMeta *out){if(!S.opened_valid)return 0;if(out){*out=S.opened;memset(&S.opened,0,sizeof(S.opened));}else smeta_free(&S.opened);S.opened_valid=0;return 1;}

