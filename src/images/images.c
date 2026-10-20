#include "xe/images.h"
#include "xe/jobs.h"
#include "xe/platform.h"
#include "xe/net.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define xe_mkdir(p) _mkdir(p)
#else
#include <dirent.h>
#include <unistd.h>
#define xe_mkdir(p) mkdir((p),0700)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#ifdef XE_HAVE_WEBP
#include <webp/decode.h>
#endif

#define XE_IMG_DEFAULT_GPU ((size_t)64u*1024u*1024u)
#define XE_IMG_DEFAULT_DISK ((size_t)150u*1024u*1024u)
typedef enum { IMG_LOADING, IMG_DECODED, IMG_READY, IMG_FAILED } ImgState;
typedef struct ImgBytes { unsigned char *p; size_t n, cap; } ImgBytes;
struct XeImage { char *url; int want_w,want_h,refs; ImgState state; XeTexture *tex; int has_tex, evict; unsigned char *rgba; size_t rgba_bytes; int w,h; double ready_at,last_use; char disk_path[1024]; struct XeImage *next; };
#ifdef _WIN32
#define IMG_MUTEX CRITICAL_SECTION
#define IMG_LOCK(m) EnterCriticalSection(m)
#define IMG_UNLOCK(m) LeaveCriticalSection(m)
#else
#include <pthread.h>
#define IMG_MUTEX pthread_mutex_t
#define IMG_LOCK(m) pthread_mutex_lock(m)
#define IMG_UNLOCK(m) pthread_mutex_unlock(m)
#endif
static IMG_MUTEX g_mu;
static int g_mu_init;
static XeImage *g_imgs;
static size_t g_gpu_budget=XE_IMG_DEFAULT_GPU,g_disk_budget=XE_IMG_DEFAULT_DISK,g_gpu_bytes,g_disk_bytes;
static unsigned long g_hits;
static unsigned long long hash_url(const char *s,int w,int h) { uint64_t x=1469598103934665603ULL; while(*s){x^=(unsigned char)*s++;x*=1099511628211ULL;} x^=(uint32_t)w; x*=1099511628211ULL; x^=(uint32_t)h; x*=1099511628211ULL; return (unsigned long long)x; }
static void img_lock_init(void) { if(g_mu_init)return;
#ifdef _WIN32
InitializeCriticalSection(&g_mu);
#else
pthread_mutex_init(&g_mu,NULL);
#endif
 g_mu_init=1; }
static char *dupstr(const char *s) { size_t n=strlen(s)+1; char *p=(char*)malloc(n); if(p)memcpy(p,s,n); return p; }
static int read_file(const char *path, ImgBytes *b) { FILE *f=fopen(path,"rb"); if(!f)return 0; if(fseek(f,0,SEEK_END)){fclose(f);return 0;} long n=ftell(f); if(n<1||n>64*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return 0;} b->p=(unsigned char*)malloc((size_t)n); if(!b->p){fclose(f);return 0;} b->n=b->cap=(size_t)n; int ok=fread(b->p,1,b->n,f)==b->n;fclose(f);if(!ok){free(b->p);memset(b,0,sizeof(*b));}return ok; }
static int write_file(const char *path,const void *p,size_t n) { FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(p,1,n,f)==n;fclose(f);return ok; }
static void make_cache_path(XeImage *im) { const char *dir=xe_platform_cache_dir(); if(!dir||!*dir)dir="."; snprintf(im->disk_path,sizeof(im->disk_path),"%s/xe-images-%016llx.bin",dir,hash_url(im->url,im->want_w,im->want_h)); }
static void ensure_cache_dir(void) { const char *d=xe_platform_cache_dir(); if(d&&*d)xe_mkdir(d); }
static void disk_recount_and_prune(void) {
    const char *dir=xe_platform_cache_dir(); if(!dir||!*dir)return;
    /* The engine owns only files with the xe-images- prefix. LRU uses filesystem access time. */
    for(;;) {
        size_t total=0; char oldest[1024]={0}; time_t oldest_t=0;
#ifdef _WIN32
        char pattern[1024];snprintf(pattern,sizeof(pattern),"%s\\xe-images-*.bin",dir);WIN32_FIND_DATAA fd;HANDLE h=FindFirstFileA(pattern,&fd);if(h==INVALID_HANDLE_VALUE){g_disk_bytes=0;return;}do{if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;ULARGE_INTEGER z;z.LowPart=fd.nFileSizeLow;z.HighPart=fd.nFileSizeHigh;total+=(size_t)z.QuadPart;char p[1024];snprintf(p,sizeof(p),"%s\\%s",dir,fd.cFileName);struct _stat st;if(_stat(p,&st)==0&&(!oldest[0]||st.st_atime<oldest_t)){strncpy(oldest,p,sizeof(oldest)-1);oldest_t=st.st_atime;}}while(FindNextFileA(h,&fd));FindClose(h);
#else
        DIR *d=opendir(dir);if(!d){g_disk_bytes=0;return;}struct dirent *e;while((e=readdir(d))){if(strncmp(e->d_name,"xe-images-",10)||!strstr(e->d_name,".bin"))continue;char p[1024];snprintf(p,sizeof(p),"%s/%s",dir,e->d_name);struct stat st;if(stat(p,&st))continue;total+=(size_t)st.st_size;if(!oldest[0]||st.st_atime<oldest_t){strncpy(oldest,p,sizeof(oldest)-1);oldest_t=st.st_atime;}}closedir(d);
#endif
        g_disk_bytes=total;if(total<=g_disk_budget||!oldest[0])break;remove(oldest);
    }
}
static void downscale_box(unsigned char **pixels,int *w,int *h,int target_w,int target_h) {
    int sw=*w,sh=*h; if(target_w<=0)target_w=sw;if(target_h<=0)target_h=sh;
    double scale=(double)target_w/sw;if((double)target_h/sh<scale)scale=(double)target_h/sh;if(scale>=1.0)return;
    int dw=(int)(sw*scale),dh=(int)(sh*scale);if(dw<1)dw=1;if(dh<1)dh=1;
    unsigned char *out=(unsigned char*)malloc((size_t)dw*dh*4);if(!out)return;
    for(int y=0;y<dh;y++){int y0=(int)((long long)y*sh/dh),y1=(int)((long long)(y+1)*sh/dh);if(y1<=y0)y1=y0+1;for(int x=0;x<dw;x++){int x0=(int)((long long)x*sw/dw),x1=(int)((long long)(x+1)*sw/dw);if(x1<=x0)x1=x0+1;unsigned sum[4]={0,0,0,0};unsigned count=0;for(int sy=y0;sy<y1&&sy<sh;sy++)for(int sx=x0;sx<x1&&sx<sw;sx++){const unsigned char *q=*pixels+((size_t)sy*sw+sx)*4;for(int c=0;c<4;c++)sum[c]+=q[c];count++;}unsigned char *d=out+((size_t)y*dw+x)*4;for(int c=0;c<4;c++)d[c]=(unsigned char)(sum[c]/(count?count:1));}}
    free(*pixels);*pixels=out;*w=dw;*h=dh;
}
static unsigned char *decode_image(const unsigned char *data,size_t n,int *w,int *h,int want_w,int want_h) {
    unsigned char *rgba=NULL; int ok=0;
    if(n>=12&&!memcmp(data,"RIFF",4)&&!memcmp(data+8,"WEBP",4)){
#ifdef XE_HAVE_WEBP
        unsigned char *webp=WebPDecodeRGBA(data,n,w,h);if(webp){size_t bytes=(size_t)(*w)*(*h)*4;rgba=(unsigned char*)malloc(bytes);if(rgba)memcpy(rgba,webp,bytes);WebPFree(webp);ok=rgba!=NULL;}
#endif
    }
    else {int ch=0;rgba=stbi_load_from_memory(data,(int)n,w,h,&ch,4);ok=rgba!=NULL;}
    if(!ok)return NULL;
    downscale_box(&rgba,w,h,want_w,want_h);return rgba;
}
static void image_work(void *arg) {
    XeImage *im=(XeImage*)arg; ImgBytes b={0}; make_cache_path(im); ensure_cache_dir();
    int cached=read_file(im->disk_path,&b);
    if(!cached) {
        XeHttpResponse res; if(xe_http_get_sync(im->url,NULL,&res)&&res.status>=200&&res.status<300&&res.body&&res.len>0){b.p=(unsigned char*)res.body;b.n=b.cap=res.len;res.body=NULL;} xe_http_response_free(&res);
        if(b.p) { static int writes; if(write_file(im->disk_path,b.p,b.n)&&(++writes%40)==1){IMG_LOCK(&g_mu);disk_recount_and_prune();IMG_UNLOCK(&g_mu);} }
    }
    int w=0,h=0;unsigned char *rgba=b.p?decode_image(b.p,b.n,&w,&h,im->want_w,im->want_h):NULL;free(b.p);
    IMG_LOCK(&g_mu);if(rgba){im->rgba=rgba;im->rgba_bytes=(size_t)w*h*4;im->w=w;im->h=h;im->state=IMG_DECODED;}else im->state=IMG_FAILED;IMG_UNLOCK(&g_mu);
}
static void image_done(void *arg) { (void)arg; /* Upload is deliberately deferred to xe_img_pump on the render thread. */ }
static void evict_gpu(void) {
    while(g_gpu_bytes>g_gpu_budget){XeImage *victim=NULL;for(XeImage *p=g_imgs;p;p=p->next)if(p->has_tex&&p->refs==0&&(!victim||p->last_use<victim->last_use))victim=p;if(!victim)break;xe_tex_destroy(victim->tex);victim->tex=NULL;victim->has_tex=0;g_gpu_bytes-=(size_t)victim->w*victim->h*4;victim->state=IMG_DECODED;}
}
XeImage *xe_img_get(const char *url,int want_w,int want_h) {
    if(!url||!*url)return NULL;img_lock_init();if(want_w<1)want_w=342;if(want_h<1)want_h=512;
    IMG_LOCK(&g_mu);for(XeImage *p=g_imgs;p;p=p->next)if(p->want_w==want_w&&p->want_h==want_h&&!strcmp(p->url,url)&&!p->evict){p->refs++;p->last_use=(double)time(NULL);g_hits++;IMG_UNLOCK(&g_mu);return p;}
    XeImage *im=(XeImage*)calloc(1,sizeof(*im));if(!im){IMG_UNLOCK(&g_mu);return NULL;}im->url=dupstr(url);if(!im->url){free(im);IMG_UNLOCK(&g_mu);return NULL;}im->want_w=want_w;im->want_h=want_h;im->refs=1;im->state=IMG_LOADING;im->last_use=(double)time(NULL);im->next=g_imgs;g_imgs=im;IMG_UNLOCK(&g_mu);
    xe_jobs_submit(image_work,image_done,im,5);return im;
}
const XeTexture *xe_img_tex(XeImage *im) { if(!im)return NULL;img_lock_init();IMG_LOCK(&g_mu);const XeTexture *t=im->has_tex?im->tex:NULL;im->last_use=(double)time(NULL);IMG_UNLOCK(&g_mu);return t; }
float xe_img_fade(XeImage *im) { if(!im)return 0;IMG_LOCK(&g_mu);float f=0;if(im->state==IMG_READY){double d=xe_time_now()-im->ready_at;f=(float)(d/0.180);if(f>1)f=1;if(f<0)f=0;}IMG_UNLOCK(&g_mu);return f; }
int xe_img_failed(XeImage *im) { if(!im)return 1;IMG_LOCK(&g_mu);int v=im->state==IMG_FAILED;IMG_UNLOCK(&g_mu);return v; }
void xe_img_release(XeImage *im) { if(!im)return;IMG_LOCK(&g_mu);if(im->refs>0)im->refs--;if(!im->refs)im->evict=1;IMG_UNLOCK(&g_mu); }
void xe_img_pump(void) {
    if(!g_mu_init)return;xe_jobs_pump();int uploaded=0;
    IMG_LOCK(&g_mu);XeImage *p=g_imgs;IMG_UNLOCK(&g_mu);
    for(;p&&uploaded<2;p=p->next){IMG_LOCK(&g_mu);int can=p->state==IMG_DECODED&&p->rgba&&p->refs>0;unsigned char *rgba=can?p->rgba:NULL;int w=p->w,h=p->h;IMG_UNLOCK(&g_mu);if(!can)continue;
        XeTexture *t=xe_tex_create(w,h,rgba);IMG_LOCK(&g_mu);if(t&&p->state==IMG_DECODED){p->tex=t;p->has_tex=1;p->state=IMG_READY;p->ready_at=xe_time_now();g_gpu_bytes+=(size_t)w*h*4;free(p->rgba);p->rgba=NULL;p->rgba_bytes=0;uploaded++;}else if(t){xe_tex_destroy(t);}IMG_UNLOCK(&g_mu);
    }
    IMG_LOCK(&g_mu);evict_gpu();XeImage **link=&g_imgs;while(*link){XeImage *im=*link;if(im->evict&&im->state!=IMG_LOADING){*link=im->next;if(im->has_tex){xe_tex_destroy(im->tex);g_gpu_bytes-=(size_t)im->w*im->h*4;}free(im->rgba);free(im->url);free(im);}else link=&im->next;}IMG_UNLOCK(&g_mu);
}
void xe_img_set_budget(size_t gpu,size_t disk) { img_lock_init();IMG_LOCK(&g_mu);g_gpu_budget=gpu;g_disk_budget=disk;evict_gpu();disk_recount_and_prune();IMG_UNLOCK(&g_mu); }
size_t xe_img_gpu_bytes(void){img_lock_init();IMG_LOCK(&g_mu);size_t n=g_gpu_bytes;IMG_UNLOCK(&g_mu);return n;}
size_t xe_img_disk_bytes(void){img_lock_init();IMG_LOCK(&g_mu);disk_recount_and_prune();size_t n=g_disk_bytes;IMG_UNLOCK(&g_mu);return n;}
unsigned long xe_img_cache_hits(void){img_lock_init();IMG_LOCK(&g_mu);unsigned long n=g_hits;IMG_UNLOCK(&g_mu);return n;}
