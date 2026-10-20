#include "xe/platform.h"
#include "xe/render.h"
#include <stdio.h>
#include <math.h>
#if defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#elif defined(__ANDROID__)
#include <GLES2/gl2.h>
#endif

static int fbw=1280,fbh=720; static float hue=0.58f,angle=0.0f; static double last_print=0; static unsigned frame_count=0;
static XeColor rgb(float h,float s,float v){float x=h*6.0f;int i=(int)x;float f=x-i,p=v*(1-s),q=v*(1-f*s),t=v*(1-(1-f)*s);switch(i%6){case 0:return(XeColor){v,t,p,1};case 1:return(XeColor){q,v,p,1};case 2:return(XeColor){p,v,t,1};case 3:return(XeColor){p,q,v,1};case 4:return(XeColor){t,p,v,1};default:return(XeColor){v,p,q,1};}}
int xe_app_init(void){xe_log(1,"P3 renderer test: 200 cards");xe_r_init();return 1;}
void xe_app_frame(double dt){XeEvent ev;while(xe_platform_poll(&ev)){if(ev.type==XE_EV_RESIZE){fbw=ev.w;fbh=ev.h;}if(ev.type==XE_EV_KEY_DOWN){if(ev.key==XE_KEY_LEFT)hue-=0.025f;if(ev.key==XE_KEY_RIGHT)hue+=0.025f;if(ev.key==XE_KEY_UP)angle+=0.08f;if(ev.key==XE_KEY_DOWN)angle-=0.08f;}}
if(hue<0)hue+=1;if(hue>1)hue-=1;angle+= (float)dt*0.7f;XeColor bg={0.035f,0.045f,0.075f,1};glClearColor(bg.r,bg.g,bg.b,1);glClear(GL_COLOR_BUFFER_BIT);xe_r_begin(fbw,fbh);
float gap=12.0f, margin=22.0f, cw=(fbw-2*margin-gap*9)/10.0f, ch=(fbh-2*margin-gap*9)/10.0f;if(cw<12)cw=12;if(ch<12)ch=12;
for(int i=0;i<200;i++){int col=i%10,row=i/10;float x=margin+col*(cw+gap),y=margin+row*(ch+gap);float h=hue+(float)i*0.017f;h-=floorf(h);XeColor a=rgb(h,0.62f,0.98f),b=rgb(h+0.08f-floorf(h+0.08f),0.78f,0.46f);XeRect r={x,y,cw,ch};xe_r_glow(r,10,3,(XeColor){a.r,a.g,a.b,0.25f});xe_r_rect_vgrad(r,10,a,b);xe_r_border(r,10,1.3f,(XeColor){1,1,1,0.48f});}
float p=(sinf(angle)+1)*0.5f;xe_r_border_progress((XeRect){fbw*.5f-90,fbh*.5f-26,180,52},18,4,p,(XeColor){1,1,1,1});xe_r_end();frame_count++;if(last_print==0)last_print=xe_time_now();double now=xe_time_now();if(now-last_print>=1.0){char msg[128];snprintf(msg,sizeof(msg),"P3: %.1f FPS | %u frames",frame_count/(now-last_print),frame_count);xe_log(1,"%s",msg);printf("%s\n",msg);frame_count=0;last_print=now;}}
void xe_app_shutdown(void){xe_r_shutdown();}
