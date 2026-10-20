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
 float scroll,density;
} S;
static char *sdup(const char *s){size_t n;char *p;if(!s)return NULL;n=strlen(s)+1;p=(char*)malloc(n);if(p)memcpy(p,s,n);return p;}
static void smeta_free(XeMeta *m){int i;if(!m)return;free(m->id);free(m->type);free(m->name);free(m->poster);free(m->background);free(m->logo);free(m->description);free(m->release);free(m->imdb_rating);free(m->runtime);for(i=0;i<m->n_genres;i++)free(m->genres[i]);free(m->genres);memset(m,0,sizeof(*m));}
static int smeta_copy(XeMeta *d,const XeMeta *s){int i;memset(d,0,sizeof(*d));if(!s)return 0;
#define SC(f) do{if(s->f&&!(d->f=sdup(s->f)))goto fail;}while(0)
 SC(id);SC(type);SC(name);SC(poster);SC(background);SC(logo);SC(description);SC(release);SC(imdb_rating);SC(runtime);
#undef SC
 if(s->n_genres>0&&s->genres){d->genres=(char**)calloc((size_t)s->n_genres,sizeof(char*));if(!d->genres)goto fail;for(i=0;i<s->n_genres;i++){if(s->genres[i]&&!(d->genres[i]=sdup(s->genres[i])))goto fail;d->n_genres++;}}return 1;fail:smeta_free(d);return 0;}
static float sd(float x){return x*(S.density>0?S.density:1.0f);}
static void stxt(const char *t,float x,float y,float size,XeColor c){if(t&&*t)xe_label(t,sd(x),sd(y),sd(size),c);}
static void sbox(XeRect r,float radius,XeColor c){xe_r_rect(r,radius,c);}
static int sbutton(XeId id,XeRect r,const char *label,int primary){int f=xe_ui_focusable(id,r),a=xe_ui_activated(id);sbox(r,sd(8),primary?S_RED:(XeColor){0.15f,0.15f,0.18f,1});if(f)xe_r_border(r,sd(8),sd(2),S_WHITE);stxt(label,r.x+sd(9),r.y+sd(11),sd(14),S_WHITE);return a;}
static void srequest(void){if(!S.query_len)return;S.search_pending=1;S.result_ready=0;}
static void sresults_cb(XeMeta *items,int n,void *u){int i;(void)u;for(i=0;i<S.result_count;i++)smeta_free(&S.results[i].m);S.result_count=0;if(n>S_MAX_RESULTS)n=S_MAX_RESULTS;for(i=0;i<n;i++){if(smeta_copy(&S.results[i].m,&items[i])){char full[2048];S.results[i].image=NULL;if(items[i].poster){if(strstr(items[i].poster,"http://")==items[i].poster||strstr(items[i].poster,"https://")==items[i].poster)snprintf(full,sizeof(full),"%s",items[i].poster);else snprintf(full,sizeof(full),"https://image.tmdb.org/t/p/w342%s",items[i].poster);S.results[i].image=xe_img_get(full,342,513);}S.result_ids[i]=xe_id_mix(xe_id("search-result"),i);S.result_count++;}}S.search_pending=0;S.result_ready=1;}
static void srun_search(void){char q[S_QUERY_CAP];if(!S.query_len)return;snprintf(q,sizeof(q),"%s",S.query);if(S.recent_count<S_MAX_RECENT){int i;for(i=S.recent_count;i>0;i--)memcpy(S.recent[i],S.recent[i-1],S_QUERY_CAP);snprintf(S.recent[0],S_QUERY_CAP,"%s",q);S.recent_count++;}else{int i;for(i=S_MAX_RECENT-1;i>0;i--)memcpy(S.recent[i],S.recent[i-1],S_QUERY_CAP);snprintf(S.recent[0],S_QUERY_CAP,"%s",q);}xe_data_search(q,sresults_cb,NULL);S.search_pending=1;}
static void sadd_char(char c){if(S.query_len+1<S_QUERY_CAP){S.query[S.query_len++]=c;S.query[S.query_len]=0;}}
static void sdelete(void){if(S.query_len){S.query[--S.query_len]=0;}}
static const char *KEYS[]={"1","2","3","4","5","6","7","8","9","0","q","w","e","r","t","y","u","i","o","p","a","s","d","f","g","h","j","k","l","z","x","c","v","b","n","m","Space","Delete","Clear","Search"};
void xe_search_frame(double dt,XeRect area){XeEvent ev[S_MAX_EVENTS];int n=0,i;float w=area.w,h=area.h;S.density=1.0f;if(!S.initialized){for(i=0;i<40;i++)S.key_ids[i]=xe_id_mix(xe_id("search-key"),i);for(i=0;i<S_MAX_RECENT;i++)S.recent_ids[i]=xe_id_mix(xe_id("search-recent"),i);S.search_id=xe_id("search-submit");S.clear_id=xe_id("search-clear");S.initialized=1;xe_ui_set_focus(S.key_ids[0]);}while(n<S_MAX_EVENTS&&xe_platform_poll(&ev[n]))n++;xe_ui_begin(dt,ev,n);xe_img_pump();xe_data_pump();
 /* Windows physical keyboard: text input goes directly into the query. Event field names follow P1 platform.h. */
 for(i=0;i<n;i++){if(ev[i].type==XE_EV_KEY_DOWN&&ev[i].key==XE_KEY_DELETE)sdelete();else if(ev[i].type==XE_EV_TEXT&&ev[i].codepoint>=32&&ev[i].codepoint<127)sadd_char((char)ev[i].codepoint);}
 xe_r_begin((int)w,(int)h);sbox((XeRect){0,0,w,h},0,S_BG);stxt("Search",sd(54),sd(48),sd(30),S_WHITE);
 sbox((XeRect){sd(54),sd(70),w-sd(108),sd(54)},sd(8),(XeColor){0.12f,0.12f,0.15f,1});stxt(S.query,sd(70),sd(88),sd(20),S_WHITE);if(!S.query_len)stxt("Search movies and series…",sd(70),sd(88),sd(18),S_MUTED);
 /* Remote keyboard grid: typing is local; only the Search key triggers a request. */
 for(i=0;i<40;i++){int col=i%10,row=i/10;float kw=sd(52),gap=sd(6),x=sd(54)+col*(kw+gap),y=sd(142)+row*sd(45);XeRect r={x,y,kw,sd(38)};int f=xe_ui_focusable(S.key_ids[i],r);sbox(r,sd(6),(XeColor){0.14f,0.14f,0.17f,1});if(f)xe_r_border(r,sd(6),sd(2),S_WHITE);const char *label=KEYS[i];if(i==36)label="Space";if(i==37)label="Del";if(i==38)label="Clear";if(i==39)label="Search";stxt(label,x+sd(6),y+sd(11),sd(i>=36?11:14),S_WHITE);if(xe_ui_activated(S.key_ids[i])){if(i<36)sadd_char(KEYS[i][0]);else if(i==36)sadd_char(' ');else if(i==37)sdelete();else if(i==38){S.query_len=0;S.query[0]=0;}else srun_search();}}
 stxt("Recent",sd(54),sd(345),sd(16),S_MUTED);for(i=0;i<S.recent_count;i++){XeRect r={sd(54+i*150),sd(362),sd(138),sd(34)};int f=xe_ui_focusable(S.recent_ids[i],r);sbox(r,sd(16),(XeColor){0.12f,0.12f,0.15f,1});if(f)xe_r_border(r,sd(16),sd(2),S_RED);stxt(S.recent[i],r.x+sd(10),r.y+sd(10),sd(12),S_WHITE);if(xe_ui_activated(S.recent_ids[i])){snprintf(S.query,sizeof(S.query),"%s",S.recent[i]);S.query_len=strlen(S.query);srun_search();}}
 stxt(S.search_pending?"Searching…":(S.result_ready?"Results":"Press Search to find titles"),sd(54),sd(424),sd(17),S_MUTED);
 {int cols=8;float cw=sd(125),ch=sd(188),gap=sd(14),y0=sd(452);for(i=0;i<S.result_count;i++){int row=i/cols,col=i%cols;float x=sd(54)+col*(cw+gap),y=y0+row*(ch+sd(22))-S.scroll;if(y+ch<y0||y>h)continue;XeRect r={x,y,cw,ch};int f=xe_ui_focusable(S.result_ids[i],r);sbox(r,sd(8),(XeColor){0.12f,0.12f,0.15f,1});if(S.results[i].image){const XeTexture *t=xe_img_tex(S.results[i].image);if(t)xe_r_image(t,r,sd(8),xe_img_fade(S.results[i].image),0);}if(f)xe_r_border(r,sd(8),sd(2),S_RED);stxt(S.results[i].m.name?S.results[i].m.name:"Untitled",x,y+ch+sd(4),sd(12),S_WHITE);if(xe_ui_activated(S.result_ids[i])){if(S.opened_valid)smeta_free(&S.opened);S.opened_valid=smeta_copy(&S.opened,&S.results[i].m);}}}
 xe_ui_end();xe_r_end();}
int xe_search_opened(XeMeta *out){if(!S.opened_valid)return 0;if(out){*out=S.opened;memset(&S.opened,0,sizeof(S.opened));}else smeta_free(&S.opened);S.opened_valid=0;return 1;}
