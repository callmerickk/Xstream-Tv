#if defined(XE_RENDER_NO_GL)
#include "xe/render.h"
#include <stdlib.h>
static size_t xe_stub_tex_bytes;
void xe_r_init(void){} void xe_r_shutdown(void){} void xe_r_begin(int w,int h){(void)w;(void)h;} void xe_r_end(void){} void xe_r_set_output(int w,int h){(void)w;(void)h;} void xe_r_clear(XeColor c){(void)c;} void xe_r_output_size(int *w,int *h){if(w)*w=1280;if(h)*h=720;}
void xe_r_rect(XeRect r,float q,XeColor c){(void)r;(void)q;(void)c;} void xe_r_rect_vgrad(XeRect r,float q,XeColor a,XeColor b){(void)r;(void)q;(void)a;(void)b;} void xe_r_rect_hgrad(XeRect r,float q,XeColor a,XeColor b){(void)r;(void)q;(void)a;(void)b;}
void xe_r_border(XeRect r,float q,float w,XeColor c){(void)r;(void)q;(void)w;(void)c;} void xe_r_border_progress(XeRect r,float q,float w,float p,XeColor c){(void)r;(void)q;(void)w;(void)p;(void)c;} void xe_r_glow(XeRect r,float q,float s,XeColor c){(void)r;(void)q;(void)s;(void)c;}
void xe_r_image(const XeTexture*t,XeRect r,float q,float a,int c){(void)t;(void)r;(void)q;(void)a;(void)c;} void xe_r_image_blurred(const XeTexture*t,XeRect r,float s,float a){(void)t;(void)r;(void)s;(void)a;} void xe_r_image_tint(const XeTexture*t,XeRect r,float q,XeColor c,float a,float b,float d,float e){(void)t;(void)r;(void)q;(void)c;(void)a;(void)b;(void)d;(void)e;} void xe_r_line(float a,float b,float c,float d,float e,XeColor f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;} void xe_r_arc(float a,float b,float c,float d,float e,float g,XeColor f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)g;(void)f;}
void xe_r_push_clip(XeRect r,float q){(void)r;(void)q;} void xe_r_pop_clip(void){} static float stub_scale=1; void xe_r_push_layer(float x,float y,float s,float a){(void)x;(void)y;(void)a;stub_scale=s;} void xe_r_pop_layer(void){stub_scale=1;} float xe_r_current_scale(void){return stub_scale;}
XeTexture *xe_tex_create(int w,int h,const void*p){if(w<=0||h<=0)return NULL;XeTexture*t=(XeTexture*)calloc(1,sizeof(*t));if(t){t->w=w;t->h=h;t->id=1;xe_stub_tex_bytes+=(size_t)w*h*4;}(void)p;return t;} void xe_tex_update(XeTexture*t,const void*p){(void)t;(void)p;} void xe_tex_update_rows(XeTexture*t,int a,int b,const void*p){(void)t;(void)a;(void)b;(void)p;} void xe_tex_destroy(XeTexture*t){if(t){xe_stub_tex_bytes-=(size_t)t->w*t->h*4;free(t);}} size_t xe_tex_bytes_in_use(void){return xe_stub_tex_bytes;}
#else
#include "xe/render.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(XE_GLES) || defined(__ANDROID__)
#include <GLES2/gl2.h>
#define XE_GL_ES 1
#else
#define XE_GL_ES 0
/* SDL's desktop host supplies a GL 3.3 context. Load modern entry points at runtime. */
#if defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_TEXTURE0 0x84C0
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA8 0x8058
#endif
#endif

#if !XE_GL_ES && defined(_WIN32)
#define GLPROC(ret,name,args) typedef ret (APIENTRY *PFN##name) args; static PFN##name p##name
GLPROC(GLuint,CreateShader,(GLenum)); GLPROC(void,ShaderSource,(GLuint,GLsizei,const GLchar *const*,const GLint*)); GLPROC(void,CompileShader,(GLuint)); GLPROC(void,GetShaderiv,(GLuint,GLenum,GLint*)); GLPROC(void,GetShaderInfoLog,(GLuint,GLsizei,GLsizei*,GLchar*)); GLPROC(GLuint,CreateProgram,(void)); GLPROC(void,AttachShader,(GLuint,GLuint)); GLPROC(void,LinkProgram,(GLuint)); GLPROC(void,GetProgramiv,(GLuint,GLenum,GLint*)); GLPROC(void,GetProgramInfoLog,(GLuint,GLsizei,GLsizei*,GLchar*)); GLPROC(void,UseProgram,(GLuint)); GLPROC(GLint,GetUniformLocation,(GLuint,const GLchar*)); GLPROC(void,Uniform2f,(GLint,GLfloat,GLfloat)); GLPROC(void,Uniform1i,(GLint,GLint)); GLPROC(void,Uniform1f,(GLint,GLfloat)); GLPROC(void,Uniform4f,(GLint,GLfloat,GLfloat,GLfloat,GLfloat)); GLPROC(void,GenBuffers,(GLsizei,GLuint*)); GLPROC(void,BindBuffer,(GLenum,GLuint)); GLPROC(void,BufferData,(GLenum,GLsizeiptr,const void*,GLenum)); GLPROC(void,BufferSubData,(GLenum,ptrdiff_t,GLsizeiptr,const void*)); GLPROC(void,EnableVertexAttribArray,(GLuint)); GLPROC(void,VertexAttribPointer,(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*)); GLPROC(void,ActiveTexture,(GLenum)); GLPROC(void,DeleteShader,(GLuint)); GLPROC(void,DeleteProgram,(GLuint)); GLPROC(void,DeleteBuffers,(GLsizei,const GLuint*)); GLPROC(void,BlendFuncSeparate,(GLenum,GLenum,GLenum,GLenum)); GLPROC(void,GenerateMipmap,(GLenum)); GLPROC(void,GenVertexArrays,(GLsizei,GLuint*)); GLPROC(void,BindVertexArray,(GLuint)); GLPROC(void,DeleteVertexArrays,(GLsizei,const GLuint*));
static void load_gl(void) {
#define LOAD(n) p##n=(PFN##n)wglGetProcAddress("gl" #n)
LOAD(CreateShader);LOAD(ShaderSource);LOAD(CompileShader);LOAD(GetShaderiv);LOAD(GetShaderInfoLog);LOAD(CreateProgram);LOAD(AttachShader);LOAD(LinkProgram);LOAD(GetProgramiv);LOAD(GetProgramInfoLog);LOAD(UseProgram);LOAD(GetUniformLocation);LOAD(Uniform2f);LOAD(Uniform1i);LOAD(Uniform1f);LOAD(Uniform4f);LOAD(GenBuffers);LOAD(BindBuffer);LOAD(BufferData);LOAD(BufferSubData);LOAD(EnableVertexAttribArray);LOAD(VertexAttribPointer);LOAD(ActiveTexture);LOAD(DeleteShader);LOAD(DeleteProgram);LOAD(DeleteBuffers);LOAD(BlendFuncSeparate);LOAD(GenerateMipmap);LOAD(GenVertexArrays);LOAD(BindVertexArray);LOAD(DeleteVertexArrays);
#undef LOAD
}
#define glCreateShader pCreateShader
#define glShaderSource pShaderSource
#define glCompileShader pCompileShader
#define glGetShaderiv pGetShaderiv
#define glGetShaderInfoLog pGetShaderInfoLog
#define glCreateProgram pCreateProgram
#define glAttachShader pAttachShader
#define glLinkProgram pLinkProgram
#define glGetProgramiv pGetProgramiv
#define glGetProgramInfoLog pGetProgramInfoLog
#define glUseProgram pUseProgram
#define glGetUniformLocation pGetUniformLocation
#define glUniform2f pUniform2f
#define glUniform1i pUniform1i
#define glUniform1f pUniform1f
#define glUniform4f pUniform4f
#define glGenBuffers pGenBuffers
#define glBindBuffer pBindBuffer
#define glBufferData pBufferData
#define glBufferSubData pBufferSubData
#define glEnableVertexAttribArray pEnableVertexAttribArray
#define glVertexAttribPointer pVertexAttribPointer
#define glActiveTexture pActiveTexture
#define glDeleteShader pDeleteShader
#define glDeleteProgram pDeleteProgram
#define glDeleteBuffers pDeleteBuffers
#define glBlendFuncSeparate pBlendFuncSeparate
#define glGenerateMipmap pGenerateMipmap
#define glGenVertexArrays pGenVertexArrays
#define glBindVertexArray pBindVertexArray
#define glDeleteVertexArrays pDeleteVertexArrays
#elif !XE_GL_ES
static void load_gl(void) {}
#endif

#define XE_BATCH_QUADS 4096
#define XE_CLIP_MAX 16
#define XE_LAYER_MAX 16
/* shape_*: position in the shape's own frame (pixels); mode 0 fill, 1 ring (ring = width), 2 arc (half_w = radius,
 * half_h = half stroke, sc = sin/cos of half the opening). */
typedef struct { float x,y,u,v; float r,g,b,a; float shape_x,shape_y,half_w,half_h,corner; float mode,ring,sc_s,sc_c; } Vertex;
typedef struct { XeRect r; float radius; } Clip;
typedef struct { float tx,ty,scale,alpha; } Layer;
static Vertex verts[XE_BATCH_QUADS*6]; static int vcount; static float g_ring;
static GLuint program,vbo,vao; static GLint u_view,u_tex,u_use_tex,u_clip;
static GLuint bound_tex; static int fbw=1,fbh=1; static size_t tex_bytes;
static Clip clips[XE_CLIP_MAX]; static int clip_n; static Layer layers[XE_LAYER_MAX]; static int layer_n;
static XeRect current_clip; static int has_clip;
static const char *vshader_es="attribute vec2 a_pos; attribute vec2 a_uv; attribute vec4 a_col; attribute vec4 a_shape; attribute float a_radius; attribute vec4 a_extra; uniform vec2 u_view; varying vec2 v_uv; varying vec4 v_col; varying vec4 v_shape; varying float v_radius; varying vec4 v_extra; void main(){ vec2 p=vec2(a_pos.x/u_view.x*2.0-1.0,1.0-a_pos.y/u_view.y*2.0); gl_Position=vec4(p,0.0,1.0); v_uv=a_uv; v_col=a_col; v_shape=a_shape; v_radius=a_radius; v_extra=a_extra; }";
static const char *fshader_es="#extension GL_OES_standard_derivatives : enable\nprecision mediump float; varying vec2 v_uv; varying vec4 v_col; varying vec4 v_shape; uniform sampler2D u_tex; uniform int u_use_tex; uniform vec4 u_clip; varying float v_radius; varying vec4 v_extra; float sdRound(vec2 p,vec2 b,float r){vec2 q=abs(p)-b+vec2(r);return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r;} float shapeDist(){ if(v_extra.x>1.5){ vec2 p=v_shape.xy; p.x=abs(p.x); vec2 sc=v_extra.zw; return ((sc.y*p.x>sc.x*p.y)?length(p-sc*v_shape.z):abs(length(p)-v_shape.z))-v_shape.w; } float d=sdRound(v_shape.xy, v_shape.zw, v_radius); if(v_extra.x>0.5) d=abs(d+v_extra.y*0.5)-v_extra.y*0.5; return d; } void main(){ float d=shapeDist(); float aa=max(fwidth(d),0.7); float cov=1.0-smoothstep(-aa,aa,d); vec4 c=v_col; if(u_use_tex==1){vec4 t=texture2D(u_tex,v_uv);c*=vec4(t.rgb*t.a,t.a);} c*=cov; gl_FragColor=c; }";
#if !XE_GL_ES
static const char *vshader_gl="#version 330 core\n#define XE_DESKTOP_GL 1\nlayout(location=0) in vec2 a_pos; layout(location=1) in vec2 a_uv; layout(location=2) in vec4 a_col; layout(location=3) in vec4 a_shape; layout(location=4) in float a_radius; layout(location=5) in vec4 a_extra; uniform vec2 u_view; out vec2 v_uv; out vec4 v_col; out vec4 v_shape; out float v_radius; out vec4 v_extra; void main(){vec2 p=vec2(a_pos.x/u_view.x*2.0-1.0,1.0-a_pos.y/u_view.y*2.0);gl_Position=vec4(p,0,1);v_uv=a_uv;v_col=a_col;v_shape=a_shape;v_radius=a_radius;v_extra=a_extra;}";
static const char *fshader_gl="#version 330 core\n#define XE_DESKTOP_GL 1\nin vec2 v_uv; in vec4 v_col; in vec4 v_shape; in float v_radius; in vec4 v_extra; uniform sampler2D u_tex; uniform int u_use_tex; uniform vec4 u_clip; out vec4 frag; float sdRound(vec2 p,vec2 b,float r){vec2 q=abs(p)-b+vec2(r);return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r;} float shapeDist(){ if(v_extra.x>1.5){ vec2 p=v_shape.xy; p.x=abs(p.x); vec2 sc=v_extra.zw; return ((sc.y*p.x>sc.x*p.y)?length(p-sc*v_shape.z):abs(length(p)-v_shape.z))-v_shape.w; } float d=sdRound(v_shape.xy,v_shape.zw,v_radius); if(v_extra.x>0.5) d=abs(d+v_extra.y*0.5)-v_extra.y*0.5; return d; } void main(){float d=shapeDist();float aa=max(fwidth(d),0.7);float cov=1.0-smoothstep(-aa,aa,d);vec4 c=v_col;if(u_use_tex==1){vec4 t=texture(u_tex,v_uv);c*=vec4(t.rgb*t.a,t.a);}c*=cov;frag=c;}";
#endif
static GLuint compile_shader(GLenum type,const char *src){GLuint s=glCreateShader(type);glShaderSource(s,1,&src,0);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[1024];GLsizei n=0;glGetShaderInfoLog(s,sizeof(log),&n,log);(void)log;glDeleteShader(s);return 0;}return s;}
static void flush(void){if(!vcount||!program)return;
#if !XE_GL_ES
glBindVertexArray(vao);
#endif
glUseProgram(program);glUniform2f(u_view,(float)fbw,(float)fbh);glUniform1i(u_tex,0);glUniform1i(u_use_tex,bound_tex?1:0);if(bound_tex){glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,bound_tex);}glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)(vcount*sizeof(Vertex)),verts);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);glEnableVertexAttribArray(3);glEnableVertexAttribArray(4);glEnableVertexAttribArray(5);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)0);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(2*sizeof(float)));glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(4*sizeof(float)));glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(8*sizeof(float)));glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(12*sizeof(float)));glVertexAttribPointer(5,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(13*sizeof(float)));glDrawArrays(GL_TRIANGLES,0,vcount);vcount=0;}
static void bind_texture(GLuint t){if(bound_tex!=t){flush();bound_tex=t;}}
static XeColor premul(XeColor c,float a){c.a*=a;c.r*=c.a;c.g*=c.a;c.b*=c.a;return c;}
static void quad(XeRect r,float radius,XeColor c,XeColor c2,int grad,int kind,float extra0,float extra1,float u0,float v0,float u1,float v1){if(vcount+6>XE_BATCH_QUADS*6)flush();Layer l={0,0,1,1};if(layer_n)l=layers[layer_n-1];r.x=r.x*l.scale+l.tx;r.y=r.y*l.scale+l.ty;r.w*=l.scale;r.h*=l.scale;c=premul(c,l.alpha);c2=premul(c2,l.alpha);float rad=fmaxf(0,fminf(radius,fminf(r.w,r.h)*.5f));float x0=r.x,y0=r.y,x1=r.x+r.w,y1=r.y+r.h;float px[6][2]={{x0,y0},{x1,y0},{x1,y1},{x0,y0},{x1,y1},{x0,y1}};float uv[6][2]={{u0,v0},{u1,v0},{u1,v1},{u0,v0},{u1,v1},{u0,v1}};for(int i=0;i<6;i++){Vertex *v=&verts[vcount++];v->x=px[i][0];v->y=px[i][1];v->u=uv[i][0];v->v=uv[i][1];float t=grad==1?(px[i][1]-y0)/(r.h?r.h:1):grad==2?(px[i][0]-x0)/(r.w?r.w:1):0;XeColor cc=grad? (XeColor){c.r+(c2.r-c.r)*t,c.g+(c2.g-c.g)*t,c.b+(c2.b-c.b)*t,c.a+(c2.a-c.a)*t}:c;v->r=cc.r;v->g=cc.g;v->b=cc.b;v->a=cc.a;v->shape_x=px[i][0]-(x0+x1)*0.5f;v->shape_y=px[i][1]-(y0+y1)*0.5f;v->half_w=r.w*0.5f;v->half_h=r.h*0.5f;v->corner=rad;v->mode=g_ring>0?1.0f:0.0f;v->ring=g_ring*l.scale;v->sc_s=v->sc_c=0;}}
void xe_r_init(void){
#if !XE_GL_ES
load_gl();
#endif
#if XE_GL_ES
const char *vs=vshader_es,*fs=fshader_es;
#else
const char *vs=vshader_gl,*fs=fshader_gl;
#endif
GLuint v=compile_shader(GL_VERTEX_SHADER,vs),f=compile_shader(GL_FRAGMENT_SHADER,fs);program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);
#if XE_GL_ES
glBindAttribLocation(program,0,"a_pos");glBindAttribLocation(program,1,"a_uv");glBindAttribLocation(program,2,"a_col");glBindAttribLocation(program,3,"a_shape");glBindAttribLocation(program,4,"a_radius");glBindAttribLocation(program,5,"a_extra");
#endif
glLinkProgram(program);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);glDeleteShader(v);glDeleteShader(f);if(!ok){program=0;return;}u_view=glGetUniformLocation(program,"u_view");u_tex=glGetUniformLocation(program,"u_tex");u_use_tex=glGetUniformLocation(program,"u_use_tex");u_clip=glGetUniformLocation(program,"u_clip");
#if !XE_GL_ES
glGenVertexArrays(1,&vao);glBindVertexArray(vao);
#endif
glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(verts),0,GL_DYNAMIC_DRAW);glEnable(GL_BLEND);glBlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);}
void xe_r_shutdown(void){flush();if(vbo)glDeleteBuffers(1,&vbo);if(program)glDeleteProgram(program);
#if !XE_GL_ES
if(vao)glDeleteVertexArrays(1,&vao);vao=0;
#endif
vbo=program=0;}
static int out_w,out_h;
void xe_r_clear(XeColor c){glClearColor(c.r,c.g,c.b,c.a);glClear(GL_COLOR_BUFFER_BIT);}
void xe_r_set_output(int w,int h){out_w=w;out_h=h;}
void xe_r_output_size(int *w,int *h){if(w)*w=out_w>0?out_w:fbw;if(h)*h=out_h>0?out_h:fbh;}
void xe_r_begin(int w,int h){float base=1.0f;if(w<=0)w=1;if(h<=0)h=1;if(out_w>0&&out_h>0){base=(float)out_h/(float)h;w=out_w;h=out_h;}fbw=w;fbh=h;vcount=0;bound_tex=0;clip_n=layer_n=0;has_clip=0;if(base!=1.0f){layers[0]=(Layer){0,0,base,1};layer_n=1;}glViewport(0,0,fbw,fbh);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);}
void xe_r_end(void){flush();}
void xe_r_rect(XeRect r,float rad,XeColor c){bind_texture(0);quad(r,rad,c,c,0,0,0,0,0,0,1,1);}
void xe_r_rect_vgrad(XeRect r,float rad,XeColor a,XeColor b){bind_texture(0);quad(r,rad,a,b,1,0,0,0,0,0,1,1);}
void xe_r_rect_hgrad(XeRect r,float rad,XeColor a,XeColor b){bind_texture(0);quad(r,rad,a,b,2,0,0,0,0,0,1,1);}
void xe_r_border(XeRect r,float rad,float width,XeColor c){if(width<=0)return;bind_texture(0);g_ring=width;quad(r,rad,c,c,0,0,0,0,0,0,1,1);g_ring=0;}
/* Rotated shape: a quad of half size (hw,hh) around (cx,cy), rotated by (ca,sa), with SDF params in its own frame. */
static void shape_quad(float cx,float cy,float hw,float hh,float ca,float sa,float mode,float p0,float p1,float corner,float sc_s,float sc_c,XeColor c){
 Layer l={0,0,1,1};int i;if(layer_n)l=layers[layer_n-1];
 cx=cx*l.scale+l.tx;cy=cy*l.scale+l.ty;hw*=l.scale;hh*=l.scale;p0*=l.scale;p1*=l.scale;corner*=l.scale;c=premul(c,l.alpha);
 hw+=1.5f;hh+=1.5f;   /* room for anti-aliasing */
 {float lc[6][2]={{-hw,-hh},{hw,-hh},{hw,hh},{-hw,-hh},{hw,hh},{-hw,hh}};
 if(vcount+6>XE_BATCH_QUADS*6)flush();
 for(i=0;i<6;i++){Vertex *v=&verts[vcount++];float lx=lc[i][0],ly=lc[i][1];v->x=cx+lx*ca-ly*sa;v->y=cy+lx*sa+ly*ca;v->u=v->v=0;v->r=c.r;v->g=c.g;v->b=c.b;v->a=c.a;v->shape_x=lx;v->shape_y=ly;v->half_w=p0;v->half_h=p1;v->corner=corner;v->mode=mode;v->ring=0;v->sc_s=sc_s;v->sc_c=sc_c;}}
}
void xe_r_line(float x0,float y0,float x1,float y1,float width,XeColor c){float dx=x1-x0,dy=y1-y0,len=sqrtf(dx*dx+dy*dy);if(width<=0)return;bind_texture(0);if(len<0.001f){dx=1;dy=0;len=0;}else{dx/=len;dy/=len;}
 /* a capsule: rounded ends */
 shape_quad((x0+x1)*.5f,(y0+y1)*.5f,len*.5f+width*.5f,width*.5f,dx,dy,0,len*.5f+width*.5f,width*.5f,width*.5f,0,0,c);}
/* Arc of a circle centred at (cx,cy): from angle a0 to a1 (radians, 0 = +x, clockwise on screen), stroke width w. */
void xe_r_arc(float cx,float cy,float radius,float a0,float a1,float width,XeColor c){float half,mid,ext;if(width<=0||radius<=0)return;if(a1<a0){float s=a0;a0=a1;a1=s;}half=(a1-a0)*.5f;if(half<=0)return;if(half>3.14159265f)half=3.14159265f;mid=(a0+a1)*.5f;bind_texture(0);ext=radius+width;
 /* The shader's arc opens around its local +y axis; rotate local +y onto the middle direction. */
 shape_quad(cx,cy,ext,ext,cosf(mid-1.5707963f),sinf(mid-1.5707963f),2,radius,width*.5f,0,sinf(half),cosf(half),c);}
/* Progress around a rounded rectangle, clockwise from the top centre. */
void xe_r_border_progress(XeRect r,float rad,float width,float progress,XeColor c){
 float x0,y0,x1,y1,seg[8],total=0,left;int i;if(progress<=0||width<=0)return;if(progress>1)progress=1;
 rad=fmaxf(0,fminf(rad,fminf(r.w,r.h)*.5f));
 x0=r.x+width*.5f;y0=r.y+width*.5f;x1=r.x+r.w-width*.5f;y1=r.y+r.h-width*.5f;rad=fmaxf(0,rad-width*.5f);
 {float sw=(x1-x0)-2*rad,sh=(y1-y0)-2*rad,q=1.5707963f*rad;seg[0]=sw*.5f;seg[1]=q;seg[2]=sh;seg[3]=q;seg[4]=sw;seg[5]=q;seg[6]=sh;seg[7]=q;}
 for(i=0;i<8;i++)total+=seg[i];left=progress*total;
 for(i=0;i<8&&left>0;i++){float n=fminf(left,seg[i]),f=seg[i]>0?n/seg[i]:0;left-=n;if(n<=0)continue;
  switch(i){
  case 0:{float sx=(x0+x1)*.5f;xe_r_line(sx,y0,sx+n,y0,width,c);}break;
  case 1:xe_r_arc(x1-rad,y0+rad,rad,-1.5707963f,-1.5707963f+1.5707963f*f,width,c);break;
  case 2:xe_r_line(x1,y0+rad,x1,y0+rad+n,width,c);break;
  case 3:xe_r_arc(x1-rad,y1-rad,rad,0,1.5707963f*f,width,c);break;
  case 4:xe_r_line(x1-rad,y1,x1-rad-n,y1,width,c);break;
  case 5:xe_r_arc(x0+rad,y1-rad,rad,1.5707963f,1.5707963f+1.5707963f*f,width,c);break;
  case 6:xe_r_line(x0,y1-rad,x0,y1-rad-n,width,c);break;
  case 7:xe_r_arc(x0+rad,y0+rad,rad,3.14159265f,3.14159265f+1.5707963f*f,width,c);break;
  }}
 /* the last stretch of the top edge, from the top-left corner back to the centre */
 if(left>0){float n=fminf(left,(x1-x0)*.5f-rad);xe_r_line(x0+rad,y0,x0+rad+n,y0,width,c);}
}
void xe_r_glow(XeRect r,float rad,float spread,XeColor c){if(spread<=0){xe_r_rect(r,rad,c);return;}/* Layered translucent shells; no offscreen blur pass. */for(int i=5;i>=1;i--){float t=(float)i/5.0f;XeRect q={r.x-spread*t,r.y-spread*t,r.w+2*spread*t,r.h+2*spread*t};XeColor cc=c;cc.a*=0.045f*(6-i);xe_r_rect(q,rad+spread*t,cc);}xe_r_rect(r,rad,c);}
void xe_r_image(const XeTexture *t,XeRect dst,float rad,float alpha,int contain){if(!t||!t->id)return;bind_texture(t->id);float u0=0,v0=0,u1=1,v1=1;if(contain){float ar=(float)t->w/(float)(t->h?t->h:1),dr=dst.w/(dst.h?dst.h:1);if(ar>dr){float hh=dr/ar;v0=(1-hh)*.5f;v1=1-v0;}else{float ww=ar/dr;u0=(1-ww)*.5f;u1=1-u0;}}XeColor c={1,1,1,alpha};quad(dst,rad,c,c,0,1,0,0,u0,v0,u1,v1);}
void xe_r_image_tint(const XeTexture *t,XeRect dst,float rad,XeColor tint,float u0,float v0,float u1,float v1){if(!t||!t->id)return;bind_texture(t->id);quad(dst,rad,tint,tint,0,1,0,0,u0,v0,u1,v1);}
void xe_r_image_blurred(const XeTexture *t,XeRect dst,float strength,float alpha){(void)strength;/* safe low-cost fallback until an offscreen framebuffer cache is available */xe_r_image(t,dst,0,alpha,0);}
float xe_r_current_scale(void){return layer_n?layers[layer_n-1].scale:1.0f;}
void xe_r_push_clip(XeRect r,float radius){(void)radius;if(clip_n>=XE_CLIP_MAX)return;if(layer_n){Layer l=layers[layer_n-1];r.x=r.x*l.scale+l.tx;r.y=r.y*l.scale+l.ty;r.w*=l.scale;r.h*=l.scale;}flush();clips[clip_n++]=(Clip){r,radius};if(clip_n==1){current_clip=r;has_clip=1;}else{float x=fmaxf(current_clip.x,r.x),y=fmaxf(current_clip.y,r.y),right=fminf(current_clip.x+current_clip.w,r.x+r.w),bottom=fminf(current_clip.y+current_clip.h,r.y+r.h);current_clip=(XeRect){x,y,fmaxf(0,right-x),fmaxf(0,bottom-y)};}glEnable(GL_SCISSOR_TEST);glScissor((int)current_clip.x,(int)(fbh-current_clip.y-current_clip.h),(int)current_clip.w,(int)current_clip.h);}
void xe_r_pop_clip(void){if(!clip_n)return;flush();clip_n--;has_clip=clip_n>0;if(!has_clip)glDisable(GL_SCISSOR_TEST);else{current_clip=clips[0].r;for(int i=1;i<clip_n;i++){XeRect r=clips[i].r;float x=fmaxf(current_clip.x,r.x),y=fmaxf(current_clip.y,r.y),rr=fminf(current_clip.x+current_clip.w,r.x+r.w),bb=fminf(current_clip.y+current_clip.h,r.y+r.h);current_clip=(XeRect){x,y,fmaxf(0,rr-x),fmaxf(0,bb-y)};}glScissor((int)current_clip.x,(int)(fbh-current_clip.y-current_clip.h),(int)current_clip.w,(int)current_clip.h);}}
void xe_r_push_layer(float tx,float ty,float scale,float alpha){if(layer_n>=XE_LAYER_MAX)return;Layer p={tx,ty,scale,alpha};if(layer_n){Layer a=layers[layer_n-1];p.tx=a.tx+tx*a.scale;p.ty=a.ty+ty*a.scale;p.scale=a.scale*scale;p.alpha=a.alpha*alpha;}layers[layer_n++]=p;}
void xe_r_pop_layer(void){if(layer_n>0)layer_n--;}
XeTexture *xe_tex_create(int w,int h,const void *rgba){if(w<=0||h<=0)return 0;XeTexture *t=(XeTexture*)calloc(1,sizeof(*t));if(!t)return 0;glGenTextures(1,&t->id);glBindTexture(GL_TEXTURE_2D,t->id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);t->w=w;t->h=h;t->external=0;tex_bytes+=(size_t)w*(size_t)h*4;return t;}
void xe_tex_update(XeTexture *t,const void *rgba){if(!t||!t->id)return;glBindTexture(GL_TEXTURE_2D,t->id);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,t->w,t->h,GL_RGBA,GL_UNSIGNED_BYTE,rgba);}
void xe_tex_update_rows(XeTexture *t,int y0,int rows,const void *px){if(!t||!t->id||rows<=0||y0<0||y0+rows>t->h)return;flush();glBindTexture(GL_TEXTURE_2D,t->id);glTexSubImage2D(GL_TEXTURE_2D,0,0,y0,t->w,rows,GL_RGBA,GL_UNSIGNED_BYTE,px);if(bound_tex)glBindTexture(GL_TEXTURE_2D,bound_tex);}
void xe_tex_destroy(XeTexture *t){if(!t)return;if(t->id&&!t->external)glDeleteTextures(1,&t->id);if(!t->external)tex_bytes-=(size_t)t->w*(size_t)t->h*4;free(t);}
size_t xe_tex_bytes_in_use(void){return tex_bytes;}

#endif /* XE_RENDER_NO_GL */
