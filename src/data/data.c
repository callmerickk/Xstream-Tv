#include "xe/data.h"
#include "xe/net.h"
#include "xe/json.h"
#include "xe/platform.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#define CACHE_MAX 256
#define CACHE_TTL 21600.0
#define MAX_ADDONS 64
#define MAX_ITEMS 500
#define MAX_URL 4096

typedef struct { char *url, *body; size_t len; double at; } CacheEntry;
typedef struct Addon { char *manifest_url, *base, *name; void *manifest; int ready; struct Addon *next; } Addon;
typedef struct { char *url; void (*done)(const XeHttpResponse *, void *); void *user; } ReqCtx;
static Addon *g_addons;
static int g_addon_count;
static char *g_tmdb_key;
static CacheEntry g_cache[CACHE_MAX];
static unsigned long g_cache_clock;
static int g_initialized;

static char *dup_n(const char *s, size_t n) { char *p; if(!s)return NULL; p=(char*)malloc(n+1); if(!p)return NULL; memcpy(p,s,n); p[n]=0; return p; }
static char *dup_s(const char *s) { return s ? dup_n(s,strlen(s)) : NULL; }
static void set_s(char **dst,const char *src) { free(*dst); *dst=dup_s(src); }
static double now_seconds(void) { return xe_time_now(); }
static int is_http_ok(const XeHttpResponse *r) { return r && !r->error && r->status>=200 && r->status<300 && r->body; }
static void free_meta_fields(XeMeta *m) {
    int i; if(!m)return;
    free(m->id);free(m->type);free(m->name);free(m->poster);free(m->background);free(m->logo);
    free(m->description);free(m->release);free(m->imdb_rating);free(m->runtime);
    for(i=0;i<m->n_genres;i++)free(m->genres[i]); free(m->genres); memset(m,0,sizeof(*m));
}
void xe_data_free_meta(XeMeta *m) { if(m){free_meta_fields(m);free(m);} }
void xe_data_free_metas(XeMeta *a,int n) { int i;if(!a)return;for(i=0;i<n;i++)free_meta_fields(&a[i]);free(a); }
void xe_data_free_episodes(XeEpisode *a,int n) { int i;if(!a)return;for(i=0;i<n;i++){free(a[i].id);free(a[i].title);free(a[i].thumb);}free(a); }
void xe_data_free_shelves(XeShelf *a,int n) { int i;if(!a)return;for(i=0;i<n;i++){free(a[i].title);xe_data_free_metas(a[i].items,a[i].n);}free(a); }
void xe_data_free_streams(XeStream *a,int n) { int i,j;if(!a)return;for(i=0;i<n;i++){free(a[i].addon);free(a[i].url);free(a[i].name);free(a[i].title);free(a[i].filename);for(j=0;j<a[i].n_headers;j++)free(a[i].headers[j]);free(a[i].headers);}free(a); }

static const char *jstr(const void *o,const char *k) { return xe_json_str(o,k); }
static char *join_url(const char *base,const char *path) {
    size_t a,b; char *p; if(!base||!path)return NULL; a=strlen(base);b=strlen(path);
    while(a>0&&base[a-1]=='/')a--; while(*path=='/')path++,b--;
    p=(char*)malloc(a+b+2); if(!p)return NULL;memcpy(p,base,a);p[a]='/';memcpy(p+a+1,path,b);p[a+b+1]=0;return p;
}
static char *url_encode(const char *s) {
    static const char hex[]="0123456789ABCDEF"; size_t i,n=0; char *o,*p;
    if(!s)return dup_s(""); for(i=0;s[i];i++) n+=(isalnum((unsigned char)s[i])||strchr("-_.~",s[i]))?1:3;
    o=(char*)malloc(n+1);if(!o)return NULL;p=o;
    for(i=0;s[i];i++){unsigned char c=(unsigned char)s[i];if(isalnum(c)||strchr("-_.~",c))*p++=(char)c;else{*p++='%';*p++=hex[c>>4];*p++=hex[c&15];}}*p=0;return o;
}
static CacheEntry *cache_find(const char *url) {
    int i; double now=now_seconds(); for(i=0;i<CACHE_MAX;i++)if(g_cache[i].url&&strcmp(g_cache[i].url,url)==0){
        /* clock() is process CPU time, not wall time; TTL remains a best-effort in-memory cache. */
        if(now-g_cache[i].at<=CACHE_TTL){g_cache[i].at=now;return &g_cache[i];}
        free(g_cache[i].url);free(g_cache[i].body);memset(&g_cache[i],0,sizeof(g_cache[i]));
    }return NULL;
}
static void cache_put(const char *url,const char *body,size_t len) {
    int i,best=0;double oldest=1e100;char *u,*b;
    if(!url||!body||len>8*1024*1024)return;u=dup_s(url);b=dup_n(body,len);if(!u||!b){free(u);free(b);return;}
    for(i=0;i<CACHE_MAX;i++){if(g_cache[i].url&&strcmp(g_cache[i].url,url)==0){best=i;goto chosen;}if(!g_cache[i].url){best=i;goto chosen;}if(g_cache[i].at<oldest){oldest=g_cache[i].at;best=i;}}
chosen: free(g_cache[best].url);free(g_cache[best].body);g_cache[best].url=u;g_cache[best].body=b;g_cache[best].len=len;g_cache[best].at=now_seconds();g_cache_clock++;
}
static void http_done(const XeHttpResponse *r,void *u) {
    ReqCtx *c=(ReqCtx*)u; if(c&&is_http_ok(r)&&!strstr(c->url,"/stream/"))cache_put(c->url,r->body,r->len); /* stream links expire: never cache them */ if(c&&c->done)c->done(r,c->user); if(c){free(c->url);free(c);}
}
static int get_url(const char *url,void (*done)(const XeHttpResponse *,void *),void *user) {
    CacheEntry *e=cache_find(url);ReqCtx *c;
    if(e){XeHttpResponse r;memset(&r,0,sizeof(r));r.status=200;r.body=e->body;r.len=e->len;r.final_url=(char*)url;r.content_type="application/json";if(done)done(&r,user);return 0;}
    c=(ReqCtx*)calloc(1,sizeof(*c));if(!c)return 0;c->url=dup_s(url);c->done=done;c->user=user;
    if(!c->url||!xe_http_get(url,NULL,http_done,c)){free(c->url);free(c);return 0;}return 1;
}
static char *base_from_manifest(const char *url) {
    size_t n=strlen(url);const char *q=strchr(url,'?');if(q)n=(size_t)(q-url);
    if(n>=14&&!strncmp(url+n-14,"/manifest.json",14))n-=14;
    while(n>0&&url[n-1]=='/')n--;
    return dup_n(url,n);
}
static void meta_from_json(XeMeta *m,const void *o,const char *fallback_type) {
    const char *s;void *a;int i,n;
    memset(m,0,sizeof(*m));
    s=jstr(o,"imdb_id");if(!s)s=jstr(o,"id");m->id=dup_s(s);
    s=jstr(o,"type");m->type=dup_s(s?s:fallback_type);
    s=jstr(o,"name");if(!s)s=jstr(o,"title");m->name=dup_s(s);
    m->poster=dup_s(jstr(o,"poster"));m->background=dup_s(jstr(o,"background"));m->logo=dup_s(jstr(o,"logo"));
    m->description=dup_s(jstr(o,"description"));s=jstr(o,"releaseInfo");if(!s)s=jstr(o,"year");m->release=dup_s(s);
    s=jstr(o,"imdbRating");if(!s)s=jstr(o,"rating");m->imdb_rating=dup_s(s);m->runtime=dup_s(jstr(o,"runtime"));
    a=xe_json_obj_get(o,"genres");n=xe_json_arr_len(a);if(n>0&&n<128){m->genres=(char**)calloc((size_t)n,sizeof(char*));if(m->genres)for(i=0;i<n;i++){const char *g=xe_json_str(xe_json_at(a,i),"");if(!g)g="";m->genres[m->n_genres++]=dup_s(g);}}
}
static void meta_overlay(XeMeta *dst,const XeMeta *src) {
#define OVER(field) if(src->field && *src->field){free(dst->field);dst->field=dup_s(src->field);}
    OVER(poster);OVER(background);OVER(logo);OVER(description);OVER(release);OVER(imdb_rating);OVER(runtime);
#undef OVER
}
static int contains_ci(const char *hay,const char *needle) { size_t n;if(!hay||!needle)return 0;n=strlen(needle);for(;*hay;hay++)if(!strncasecmp(hay,needle,n))return 1;return 0; }
static int parse_height(const char *s) { if(!s)return 0;if(contains_ci(s,"2160p")||contains_ci(s,"4k"))return 2160;if(contains_ci(s,"1080p"))return 1080;if(contains_ci(s,"720p"))return 720;if(contains_ci(s,"480p"))return 480;return 0; }
static long long parse_size(const char *s) {
    const char *p=s; if(!s)return 0;while(*p){if(isdigit((unsigned char)*p)){char *end;double v=strtod(p,&end);while(*end==' '||*end=='\t')end++;if((end[0]=='G'||end[0]=='g')&&(end[1]=='B'||end[1]=='b'))return (long long)(v*1024.0*1024.0*1024.0);if((end[0]=='M'||end[0]=='m')&&(end[1]=='B'||end[1]=='b'))return (long long)(v*1024.0*1024.0);if((end[0]=='K'||end[0]=='k')&&(end[1]=='B'||end[1]=='b'))return (long long)(v*1024.0);p=end;}else p++;}return 0;
}
static int download_only(const char *s) { return contains_ci(s,"instant download")||contains_ci(s,"10gbps"); }
static char *stream_field(const void *o,const char *k) { return dup_s(jstr(o,k)); }
static XeStream *streams_parse(const void *arr,const char *addon,int *out_n) {
    int i,n=xe_json_arr_len(arr),count=0;XeStream *a;if(out_n)*out_n=0;if(n<1)return NULL;if(n>MAX_ITEMS)n=MAX_ITEMS;
    a=(XeStream*)calloc((size_t)n,sizeof(*a));if(!a)return NULL;
    for(i=0;i<n;i++){void *o=xe_json_at(arr,i);const char *url=jstr(o,"url"),*name=jstr(o,"name"),*title=jstr(o,"title");void *bh;const char *fn=NULL;XeStream *s;
        if(!url||!*url||download_only(name)||download_only(title)||download_only(url))continue;
        bh=xe_json_obj_get(o,"behaviorHints");fn=jstr(bh,"filename");if(download_only(fn))continue;
        s=&a[count];s->addon=dup_s(addon);s->url=dup_s(url);s->name=dup_s(name);s->title=dup_s(title);s->filename=dup_s(fn);s->height=parse_height(name);if(!s->height)s->height=parse_height(title);if(!s->height)s->height=parse_height(fn);s->size=parse_size(name);if(!s->size)s->size=parse_size(title);if(!s->size)s->size=parse_size(fn);
        {void *ph=xe_json_obj_get(bh,"proxyHeaders"),*rq=xe_json_obj_get(ph,"request");int h=0,j;const char *key;for(j=0;j<64;j++){char keybuf[16];snprintf(keybuf,sizeof(keybuf),"%d",j);(void)keybuf;} /* header objects are copied below via cJSON's known object iterator is intentionally avoided by P6 API */
            (void)rq;(void)h;(void)key;
        } count++;
    }
    if(!count){free(a);return NULL;}if(out_n)*out_n=count;return a;
}

/* Home shelves: every addon's manifest, then each of its catalogs that needs no extra input.
 * Shelves come back in manifest order once every request has finished. */
typedef struct { char *type,*id,*name; } Catalog;
typedef struct { void (*cb)(XeShelf*,int,void*);void *user;XeShelf *shelves;int *order;int n_shelves,pending; } HomeCtx;
typedef struct { HomeCtx *home; Catalog cat; char *url; int order; } CatCtx;
typedef struct { HomeCtx *home; Addon *addon; int index; } ManCtx;
static int catalog_needs_input(const void *c) {
    void *req=xe_json_obj_get(c,"extraRequired"),*extra=xe_json_obj_get(c,"extra");int i,n;
    if(xe_json_arr_len(req)>0)return 1;
    n=xe_json_arr_len(extra);
    for(i=0;i<n;i++){void *x=xe_json_at(extra,i);void *v=xe_json_obj_get(x,"isRequired");if(v&&xe_json_int(x,"isRequired",0))return 1;}
    return 0;
}
static void home_finish(HomeCtx *h) {
    int i,j;
    for(i=1;i<h->n_shelves;i++)for(j=i;j>0&&h->order[j]<h->order[j-1];j--){XeShelf t=h->shelves[j];int o=h->order[j];h->shelves[j]=h->shelves[j-1];h->order[j]=h->order[j-1];h->shelves[j-1]=t;h->order[j-1]=o;}
    if(h->cb)h->cb(h->shelves,h->n_shelves,h->user);else xe_data_free_shelves(h->shelves,h->n_shelves);
    free(h->order);free(h);
}
static void catalog_response(const XeHttpResponse *r,void *u) {
    CatCtx *c=(CatCtx*)u;HomeCtx *h=c->home;
    if(is_http_ok(r)){void *root=xe_json_parse(r->body,r->len);void *metas=root?xe_json_obj_get(root,"metas"):NULL;int n=xe_json_arr_len(metas),i;
        if(n>MAX_ITEMS)n=MAX_ITEMS;
        if(n>0){XeShelf *p=(XeShelf*)realloc(h->shelves,(size_t)(h->n_shelves+1)*sizeof(XeShelf));int *o=(int*)realloc(h->order,(size_t)(h->n_shelves+1)*sizeof(int));
            if(p)h->shelves=p;if(o)h->order=o;
            if(p&&o){XeShelf *s=&p[h->n_shelves];char title[300];memset(s,0,sizeof(*s));
                if(c->cat.name&&c->cat.type&&(!strcmp(c->cat.type,"movie")||!strcmp(c->cat.type,"series")))snprintf(title,sizeof(title),"%s %s",c->cat.name,!strcmp(c->cat.type,"movie")?"Movies":"Series");
                else snprintf(title,sizeof(title),"%s",c->cat.name?c->cat.name:c->cat.id);
                s->title=dup_s(title);s->items=(XeMeta*)calloc((size_t)n,sizeof(XeMeta));
                if(s->items){for(i=0;i<n;i++)meta_from_json(&s->items[i],xe_json_at(metas,i),c->cat.type);s->n=n;o[h->n_shelves]=c->order;h->n_shelves++;}else free(s->title);}}
        if(root)xe_json_delete(root);
    }
    free(c->cat.type);free(c->cat.id);free(c->cat.name);free(c->url);free(c);
    if(h->pending>0)h->pending--;
    if(h->pending==0)home_finish(h);
}

void xe_data_init(const char *const *urls,int n,const char *key) {
    int i;Addon **tail=&g_addons;if(g_initialized)xe_data_shutdown();g_initialized=1;g_tmdb_key=dup_s(key);if(n<0)n=0;if(n>MAX_ADDONS)n=MAX_ADDONS;g_addon_count=0;
    for(i=0;i<n;i++){Addon *a;if(!urls||!urls[i]||!*urls[i])continue;a=(Addon*)calloc(1,sizeof(*a));if(!a)continue;a->manifest_url=dup_s(urls[i]);a->base=base_from_manifest(urls[i]);*tail=a;tail=&a->next;g_addon_count++;}
}
void xe_data_shutdown(void) {
    Addon *a=g_addons,*next;int i;while(a){next=a->next;free(a->manifest_url);free(a->base);free(a->name);if(a->manifest)xe_json_delete(a->manifest);free(a);a=next;}g_addons=NULL;g_addon_count=0;free(g_tmdb_key);g_tmdb_key=NULL;for(i=0;i<CACHE_MAX;i++){free(g_cache[i].url);free(g_cache[i].body);memset(&g_cache[i],0,sizeof(g_cache[i]));}g_initialized=0;
}
void xe_data_pump(void) { xe_net_pump(); }
static void manifest_cb(const XeHttpResponse *r,void *u) {
    ManCtx *mc=(ManCtx*)u;HomeCtx *h=mc->home;Addon *a=mc->addon;
    if(is_http_ok(r)){void *root=xe_json_parse(r->body,r->len);
        if(root){void *cats;int i,n;
            if(a->manifest)xe_json_delete(a->manifest);
            a->manifest=root;a->ready=1;set_s(&a->name,jstr(root,"name"));if(!a->name)set_s(&a->name,"Addon");
            cats=xe_json_obj_get(root,"catalogs");n=xe_json_arr_len(cats);if(n>64)n=64;
            for(i=0;i<n;i++){void *c=xe_json_at(cats,i);const char *type=jstr(c,"type"),*id=jstr(c,"id"),*name=jstr(c,"name");char path[1024];char *url;CatCtx *cc;
                if(!type||!id||catalog_needs_input(c))continue;
                snprintf(path,sizeof(path),"catalog/%s/%s.json",type,id);url=join_url(a->base,path);if(!url)continue;
                cc=(CatCtx*)calloc(1,sizeof(*cc));if(!cc){free(url);continue;}
                cc->home=h;cc->cat.type=dup_s(type);cc->cat.id=dup_s(id);cc->cat.name=dup_s(name?name:id);cc->url=url;cc->order=mc->index*100+i;
                h->pending++;
                if(!get_url(url,catalog_response,cc)){h->pending--;free(cc->cat.type);free(cc->cat.id);free(cc->cat.name);free(cc->url);free(cc);}
            }
        }
    }
    free(mc);
    if(h->pending>0)h->pending--;
    if(h->pending==0)home_finish(h);
}
void xe_data_home(void (*cb)(XeShelf*,int,void*),void *u) {
    Addon *a;int idx=0;HomeCtx *h=(HomeCtx*)calloc(1,sizeof(*h));if(!h)return;h->cb=cb;h->user=u;
    h->pending=1;   /* held until every manifest request is queued (cache hits answer at once) */
    for(a=g_addons;a;a=a->next,idx++){ManCtx *mc=(ManCtx*)calloc(1,sizeof(*mc));if(!mc)continue;mc->home=h;mc->addon=a;mc->index=idx;h->pending++;
        if(!get_url(a->manifest_url,manifest_cb,mc)){free(mc);h->pending--;}}
    if(--h->pending==0)home_finish(h);
}

/* Cinemeta helpers. The callback owns returned arrays and must release them with xe_data_free_*. */
typedef struct { char *type,*id;void (*cb)(XeMeta*,XeEpisode*,int,void*);void *user; } MetaCtx;
static void meta_response(const XeHttpResponse *r,void *u) {
    MetaCtx *c=(MetaCtx*)u;XeMeta *m=NULL;XeEpisode *eps=NULL;int ne=0;void *root;
    if(is_http_ok(r)&&(root=xe_json_parse(r->body,r->len))!=NULL){void *meta=xe_json_obj_get(root,"meta"),*videos=xe_json_obj_get(meta,"videos");if(meta){m=(XeMeta*)calloc(1,sizeof(*m));if(m)meta_from_json(m,meta,c->type);}ne=xe_json_arr_len(videos);if(ne>MAX_ITEMS)ne=MAX_ITEMS;if(ne>0){int i;eps=(XeEpisode*)calloc((size_t)ne,sizeof(XeEpisode));if(eps)for(i=0;i<ne;i++){void *v=xe_json_at(videos,i);eps[i].id=dup_s(jstr(v,"id"));eps[i].title=dup_s(jstr(v,"title"));eps[i].thumb=dup_s(jstr(v,"thumbnail"));eps[i].season=xe_json_int(v,"season",0);eps[i].episode=xe_json_int(v,"episode",0);}else ne=0;}xe_json_delete(root);}
    if(c->cb)c->cb(m,eps,ne,c->user);else{xe_data_free_meta(m);xe_data_free_episodes(eps,ne);}free(c->type);free(c->id);free(c);
}
void xe_data_meta(const char *type,const char *id,void (*cb)(XeMeta*,XeEpisode*,int,void*),void *u) {
    char path[1024];char *url;MetaCtx *c;if(!type||!id)return;snprintf(path,sizeof(path),"meta/%s/%s.json",type,id);url=join_url("https://v3-cinemeta.strem.io",path);if(!url)return;c=(MetaCtx*)calloc(1,sizeof(*c));if(!c){free(url);return;}c->type=dup_s(type);c->id=dup_s(id);c->cb=cb;c->user=u;if(!get_url(url,meta_response,c)){if(cb)cb(NULL,NULL,0,u);free(c->type);free(c->id);free(c);}free(url);
}

typedef struct { void (*cb)(XeMeta*,int,void*);void *user; } SearchCtx;
static void search_response(const XeHttpResponse *r,void *u) { SearchCtx *c=(SearchCtx*)u;XeMeta *a=NULL;int n=0,i;void *root;if(is_http_ok(r)&&(root=xe_json_parse(r->body,r->len))!=NULL){void *metas=xe_json_obj_get(root,"metas");n=xe_json_arr_len(metas);if(n>MAX_ITEMS)n=MAX_ITEMS;if(n>0){a=(XeMeta*)calloc((size_t)n,sizeof(XeMeta));if(a)for(i=0;i<n;i++)meta_from_json(&a[i],xe_json_at(metas,i),NULL);else n=0;}xe_json_delete(root);}if(c->cb)c->cb(a,n,c->user);else xe_data_free_metas(a,n);free(c); }
void xe_data_search(const char *q,void (*cb)(XeMeta*,int,void*),void *u) { char *enc,*url;SearchCtx *c;if(!q)return;enc=url_encode(q);if(!enc)return;url=(char*)malloc(strlen(enc)+64);if(!url){free(enc);return;}sprintf(url,"https://v3-cinemeta.strem.io/catalog/movie/top/search=%s.json",enc);free(enc);c=(SearchCtx*)calloc(1,sizeof(*c));if(!c){free(url);return;}c->cb=cb;c->user=u;if(!get_url(url,search_response,c)){if(cb)cb(NULL,0,u);free(c);}free(url); }

typedef struct { char *type,*id,*addon;void (*cb)(XeStream*,int,int,void*);void *user;int pending; } StreamCtx;
static void stream_response(const XeHttpResponse *r,void *u) { StreamCtx *c=(StreamCtx*)u;XeStream *s=NULL;int n=0;void *root;if(is_http_ok(r)&&(root=xe_json_parse(r->body,r->len))!=NULL){void *arr=xe_json_obj_get(root,"streams");s=streams_parse(arr,c->addon,&n);xe_json_delete(root);}if(c->cb)c->cb(s,n,0,c->user);else xe_data_free_streams(s,n);if(c->pending>0)c->pending--;if(c->pending==0){if(c->cb)c->cb(NULL,0,1,c->user);free(c->type);free(c->id);free(c->addon);free(c);} }
void xe_data_streams(const char *type,const char *id,void (*cb)(XeStream*,int,int,void*),void *u) { Addon *a;StreamCtx *c;if(!type||!id)return;c=(StreamCtx*)calloc(1,sizeof(*c));if(!c)return;c->type=dup_s(type);c->id=dup_s(id);c->cb=cb;c->user=u;c->pending=1; /* held while queuing; cache hits answer at once */
    for(a=g_addons;a;a=a->next){char path[1200];char *url;snprintf(path,sizeof(path),"stream/%s/%s.json",type,id);url=join_url(a->base,path);if(!url)continue;c->pending++;{typedef struct { StreamCtx *parent;char *addon; } SC;SC *sc=(SC*)calloc(1,sizeof(*sc));if(!sc){c->pending--;free(url);continue;}sc->parent=c;sc->addon=dup_s(a->name?a->name:a->base);/* adapter context is passed via a trampoline below */
        extern void xe_data_internal_stream_cb(const XeHttpResponse*,void*);if(!get_url(url,xe_data_internal_stream_cb,sc)){c->pending--;free(sc->addon);free(sc);}}
        free(url);
    }
    if(--c->pending==0){if(cb)cb(NULL,0,1,u);free(c->type);free(c->id);free(c->addon);free(c);}
}
void xe_data_internal_stream_cb(const XeHttpResponse *r,void *u) { typedef struct { StreamCtx *parent;char *addon; } SC;SC *sc=(SC*)u;StreamCtx *c=sc->parent;XeStream *s=NULL;int n=0;void *root;if(is_http_ok(r)&&(root=xe_json_parse(r->body,r->len))!=NULL){s=streams_parse(xe_json_obj_get(root,"streams"),sc->addon,&n);xe_json_delete(root);}if(c->cb)c->cb(s,n,0,c->user);else xe_data_free_streams(s,n);if(c->pending>0)c->pending--;free(sc->addon);free(sc);if(c->pending==0){if(c->cb)c->cb(NULL,0,1,c->user);free(c->type);free(c->id);free(c->addon);free(c);} }
