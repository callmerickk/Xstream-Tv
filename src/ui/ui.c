#include "xe/ui.h"
#include <stdint.h>
#include <math.h>
#define MAX_TARGETS 1024
#define MAX_ANIMS 512
#define MAX_SCROLLS 32
#define DRAG_THRESHOLD 6.0f
typedef struct { XeId id; XeRect r; } Target;
typedef struct { XeId id; XeSpring spring; unsigned long age; int used; } FocusAnim;
typedef struct { XeId id; XeRect rect; float *offset; float extent; int vertical,used; } ScrollView;
static Target prev[MAX_TARGETS],cur[MAX_TARGETS]; static int np,nc;
static FocusAnim anims[MAX_ANIMS]; static ScrollView scrolls[MAX_SCROLLS];
static XeId focus,activated[MAX_TARGETS]; static int na;
static const XeEvent *evs; static int nev; static double frame_dt;
static unsigned long age_counter; static float scroll_delta;
static int pointer_down,dragging; static float px0,py0,px,py;
static int any_running;
extern void xe_anim_internal_set_running(int running);
static uint64_t hashstr(const char *s) {
    uint64_t h=UINT64_C(1469598103934665603); const unsigned char *p=(const unsigned char*)s;
    while(*p){h^=*p++;h*=UINT64_C(1099511628211);} return h?h:1;
}
XeId xe_id(const char *s){return s?(XeId)hashstr(s):1;}
XeId xe_id_mix(XeId a,long long n){
    uint64_t x=(uint64_t)a^((uint64_t)n+UINT64_C(0x9e3779b97f4a7c15)+((uint64_t)a<<6)+((uint64_t)a>>2));
    x^=x>>30;x*=UINT64_C(0xbf58476d1ce4e5b9);x^=x>>27;x*=UINT64_C(0x94d049bb133111eb);x^=x>>31;return x?(XeId)x:1;
}
static int contains(XeRect r,float x,float y){return x>=r.x&&y>=r.y&&x<=r.x+r.w&&y<=r.y+r.h;}
static int find_target(Target *a,int n,XeId id){int i;for(i=0;i<n;i++)if(a[i].id==id)return i;return -1;}
static FocusAnim *get_anim(XeId id,int create){
    int i,slot=-1,oldest=0; unsigned long oldage=~0UL;
    for(i=0;i<MAX_ANIMS;i++){
        if(anims[i].used&&anims[i].id==id){anims[i].age=++age_counter;return &anims[i];}
        if(!anims[i].used&&slot<0)slot=i;
        if(anims[i].used&&anims[i].age<oldage){oldage=anims[i].age;oldest=i;}
    }
    if(!create)return NULL; if(slot<0)slot=oldest;
    anims[slot].used=1;anims[slot].id=id;anims[slot].age=++age_counter;
    xe_spring_init(&anims[slot].spring,id==focus?1.0f:0.0f,220,27);return &anims[slot];
}
static XeId navigate(XeId id,XeKey key){
    int fi=find_target(prev,np,id),i,best=-1;float bestscore=1.0e30f,ax,ay;XeRect a;
    if(fi<0)return id;a=prev[fi].r;ax=a.x+a.w*.5f;ay=a.y+a.h*.5f;
    for(i=0;i<np;i++){
        XeRect b=prev[i].r;float bx,by,p,s,score;if(prev[i].id==id)continue;
        bx=b.x+b.w*.5f;by=b.y+b.h*.5f;
        if(key==XE_KEY_RIGHT){p=bx-ax;s=fabsf(by-ay);}else if(key==XE_KEY_LEFT){p=ax-bx;s=fabsf(by-ay);}
        else if(key==XE_KEY_DOWN){p=by-ay;s=fabsf(bx-ax);}else{p=ay-by;s=fabsf(bx-ax);}
        if(p<=.5f)continue;score=p+s*2.4f;
        if((key==XE_KEY_LEFT||key==XE_KEY_RIGHT)&&s<=(a.h+b.h)*.5f)score*=.45f;
        if((key==XE_KEY_UP||key==XE_KEY_DOWN)&&s<=(a.w+b.w)*.5f)score*=.45f;
        if(score<bestscore){bestscore=score;best=i;}
    }
    return best>=0?prev[best].id:id;
}
static ScrollView *scroll_at(float x,float y){
    int i;for(i=MAX_SCROLLS-1;i>=0;i--)if(scrolls[i].used&&scrolls[i].offset&&contains(scrolls[i].rect,x,y))return &scrolls[i];return NULL;
}
static void apply_scroll(ScrollView *s,float delta){
    float view,maxoff;if(!s||!s->offset)return;view=s->vertical?s->rect.h:s->rect.w;maxoff=s->extent-view;if(maxoff<0)maxoff=0;
    *s->offset+=delta;if(*s->offset<0)*s->offset=0;if(*s->offset>maxoff)*s->offset=maxoff;
}
void xe_ui_begin(double dt,const XeEvent *events,int n){
    int i;frame_dt=dt<0?0:dt;if(frame_dt>.05)frame_dt=.05;evs=events;nev=(events&&n>0)?n:0;nc=0;na=0;scroll_delta=0;
    for(i=0;i<nev;i++){
        const XeEvent *e=&evs[i];
        if(e->type==XE_EV_KEY_DOWN){
            if(e->key==XE_KEY_LEFT||e->key==XE_KEY_RIGHT||e->key==XE_KEY_UP||e->key==XE_KEY_DOWN){if(focus)focus=navigate(focus,e->key);}
            else if(e->key==XE_KEY_OK&&focus&&na<MAX_TARGETS)activated[na++]=focus;
        }else if(e->type==XE_EV_POINTER_DOWN){pointer_down=1;dragging=0;px0=px=e->x;py0=py=e->y;}
        else if(e->type==XE_EV_POINTER_MOVE&&pointer_down){
            float dx=e->x-px,dy=e->y-py;if(fabsf(e->x-px0)+fabsf(e->y-py0)>DRAG_THRESHOLD)dragging=1;
            if(dragging){ScrollView *s=scroll_at(px0,py0);if(s){float d=s->vertical?-dy:-dx;apply_scroll(s,d);scroll_delta+=d;}}
            px=e->x;py=e->y;
        }else if(e->type==XE_EV_POINTER_UP){
            if(pointer_down&&!dragging){int j;for(j=np-1;j>=0;j--)if(contains(prev[j].r,e->x,e->y)){focus=prev[j].id;if(na<MAX_TARGETS)activated[na++]=focus;break;}}
            pointer_down=0;dragging=0;
        }else if(e->type==XE_EV_SCROLL){ScrollView *s=scroll_at(e->x,e->y);if(s){float d=s->vertical?e->dy:e->dx;apply_scroll(s,d);scroll_delta+=d;}}
    }
    for(i=0;i<MAX_ANIMS;i++)if(anims[i].used){anims[i].spring.target=anims[i].id==focus?1:0;xe_spring_step(&anims[i].spring,frame_dt);}
    if(!focus&&np)focus=prev[0].id;
}
int xe_ui_focusable(XeId id,XeRect r){int k;if(!id||nc>=MAX_TARGETS)return 0;k=find_target(cur,nc,id);if(k<0)k=nc++;
    cur[k].id=id;cur[k].r=r;(void)get_anim(id,1);if(!focus)focus=id;return focus==id;}
int xe_ui_activated(XeId id){int i;for(i=0;i<na;i++)if(activated[i]==id)return 1;return 0;}
void xe_ui_set_focus(XeId id){focus=id;} XeId xe_ui_focus(void){return focus;}
float xe_ui_focus_anim(XeId id){FocusAnim *a=get_anim(id,0);return a?a->spring.value:(id==focus?1:0);}
void xe_ui_set_scroll_view(XeId id,XeRect rect,float *offset,float extent,int vertical){
    int i,slot=-1;for(i=0;i<MAX_SCROLLS;i++){if(scrolls[i].used&&scrolls[i].id==id){slot=i;break;}if(!scrolls[i].used&&slot<0)slot=i;}
    if(slot<0)return;scrolls[slot].used=1;scrolls[slot].id=id;scrolls[slot].rect=rect;scrolls[slot].offset=offset;
    scrolls[slot].extent=extent>0?extent:0;scrolls[slot].vertical=vertical!=0;
}
float xe_ui_pointer_scroll_delta(void){return scroll_delta;}
static float scroll_target(float off,int index,int count,float item,float gap,float view){
    float start,end,maxoff;if(count<=0||item<=0)return 0;if(index<0)index=0;if(index>=count)index=count-1;
    start=index*(item+gap);end=start+item;maxoff=count*item+(count-1)*gap-view;if(maxoff<0)maxoff=0;
    if(start<off)off=start;else if(end>off+view)off=end-view;if(off<0)off=0;if(off>maxoff)off=maxoff;return off;
}
void xe_row_scroll_to(XeRow *r,int index,float view_w){float target;if(!r)return;target=scroll_target(r->offset,index,r->count,r->item_w,r->gap,view_w);r->spring.target=target;xe_spring_step(&r->spring,frame_dt);r->offset=r->spring.value;}
void xe_column_scroll_to(XeColumn *c,int index,float view_h){float target;if(!c)return;target=scroll_target(c->offset,index,c->count,c->item_h,c->gap,view_h);c->spring.target=target;xe_spring_step(&c->spring,frame_dt);c->offset=c->spring.value;}
void xe_ui_end(void){int i;np=nc;for(i=0;i<nc;i++)prev[i]=cur[i];
    /* focus left behind by another screen: move it to this screen's first target */
    if(nc>0&&find_target(cur,nc,focus)<0)focus=cur[0].id;
any_running=0;
    for(i=0;i<MAX_ANIMS;i++)if(anims[i].used&&!xe_spring_resting(&anims[i].spring)){any_running=1;break;}
    xe_anim_internal_set_running(any_running);
}
