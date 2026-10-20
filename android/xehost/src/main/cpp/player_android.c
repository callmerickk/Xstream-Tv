#include "xe/player.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <GLES2/gl2.h>

typedef struct {
    double pos, dur;
    int buffering, paused, active;
    char error[192];
    char *tracks[3][64];
    int ntracks[3];
} PlayerSnapshot;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static PlayerSnapshot g_state;
#include "xe_android.h"
#define g_vm (xe_android_vm())
static int g_next_handle = 1;
static jclass g_bridge;
static jmethodID g_open, g_toggle, g_seek, g_close, g_select, g_preview;

static void clear_tracks_locked(void) {
    int k, i;
    for (k = 0; k < 3; ++k) {
        for (i = 0; i < g_state.ntracks[k]; ++i) free(g_state.tracks[k][i]);
        g_state.ntracks[k] = 0;
    }
}
static char *copy_utf8(JNIEnv *env, jstring value) {
    const char *s;
    char *out;
    if (!value) return NULL;
    s = (*env)->GetStringUTFChars(env, value, NULL);
    if (!s) return NULL;
    out = strdup(s);
    (*env)->ReleaseStringUTFChars(env, value, s);
    return out;
}
static JNIEnv *get_env(int *attached) {
    JNIEnv *env = NULL;
    jint r;
    *attached = 0;
    if (!g_vm) return NULL;
    r = (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6);
    if (r == JNI_EDETACHED) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return NULL;
        *attached = 1;
    } else if (r != JNI_OK) return NULL;
    return env;
}

/* The Java bridge posts all player commands to the main looper. */
static int bridge_ready(JNIEnv *env) {
    if (g_bridge) return 1;
    jclass local = (*env)->FindClass(env, "com/xe/xehost/PlayerBridge");
    if (!local) { (*env)->ExceptionClear(env); return 0; }
    g_bridge = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    if (!g_bridge) return 0;
    g_open = (*env)->GetStaticMethodID(env, g_bridge, "open", "(Ljava/lang/String;[Ljava/lang/String;D)V");
    g_toggle = (*env)->GetStaticMethodID(env, g_bridge, "toggle", "()V");
    g_seek = (*env)->GetStaticMethodID(env, g_bridge, "seek", "(D)V");
    g_close = (*env)->GetStaticMethodID(env, g_bridge, "close", "()V");
    g_select = (*env)->GetStaticMethodID(env, g_bridge, "select", "(II)V");
    g_preview = (*env)->GetStaticMethodID(env, g_bridge, "requestPreview", "(DII)V");
    if (!g_preview) (*env)->ExceptionClear(env);
    if (!g_open || !g_toggle || !g_seek || !g_close || !g_select) {
        (*env)->ExceptionClear(env); return 0;
    }
    return 1;
}
void xe_player_open(const char *url, const char *const *headers, double start_sec) {
    int attached = 0, i, count = 0;
    JNIEnv *env = get_env(&attached);
    jstring jurl = NULL;
    jobjectArray arr = NULL;
    jclass strcls;
    if (!env || !url) { if (attached) (*g_vm)->DetachCurrentThread(g_vm); return; }
    pthread_mutex_lock(&g_mu);
    g_state.active = 1; g_state.pos = 0; g_state.dur = 0; g_state.buffering = 1;
    g_state.paused = 0; g_state.error[0] = 0; clear_tracks_locked();
    pthread_mutex_unlock(&g_mu);
    if (bridge_ready(env)) {
        if (headers) while (headers[count] && headers[count + 1] && count < 128) count += 2;
        strcls = (*env)->FindClass(env, "java/lang/String");
        arr = (*env)->NewObjectArray(env, count, strcls, NULL);
        if (arr) for (i = 0; i < count; ++i) {
            jstring s = (*env)->NewStringUTF(env, headers[i]);
            if (s) { (*env)->SetObjectArrayElement(env, arr, i, s); (*env)->DeleteLocalRef(env, s); }
        }
        jurl = (*env)->NewStringUTF(env, url);
        if (jurl && arr) (*env)->CallStaticVoidMethod(env, g_bridge, g_open, jurl, arr, start_sec);
        if (jurl) (*env)->DeleteLocalRef(env, jurl);
        if (arr) (*env)->DeleteLocalRef(env, arr);
        if (strcls) (*env)->DeleteLocalRef(env, strcls);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}
static void call_simple(jmethodID method, double value, int is_double) {
    int attached = 0; JNIEnv *env = get_env(&attached);
    if (!env) return;
    if (bridge_ready(env)) {
        if (is_double) (*env)->CallStaticVoidMethod(env, g_bridge, method, (jdouble)value);
        else (*env)->CallStaticVoidMethod(env, g_bridge, method);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}
void xe_player_toggle(void) { call_simple(g_toggle, 0, 0); }
void xe_player_seek(double sec) { call_simple(g_seek, sec, 1); }
static void preview_reset(void);
void xe_player_close(void) {
    preview_reset();
    pthread_mutex_lock(&g_mu);
    g_state.active = 0; g_state.buffering = 0; g_state.paused = 0;
    g_state.pos = g_state.dur = 0; g_state.error[0] = 0; clear_tracks_locked();
    pthread_mutex_unlock(&g_mu);
    call_simple(g_close, 0, 0);
}
int xe_player_active(void) { int v; pthread_mutex_lock(&g_mu); v=g_state.active; pthread_mutex_unlock(&g_mu); return v; }
const char *xe_player_error(void) {
    static _Thread_local char copy[192];
    pthread_mutex_lock(&g_mu); snprintf(copy,sizeof copy,"%s",g_state.error); pthread_mutex_unlock(&g_mu);
    return copy[0] ? copy : NULL;
}
int xe_player_paused(void) { int v; pthread_mutex_lock(&g_mu); v=g_state.paused; pthread_mutex_unlock(&g_mu); return v; }
double xe_player_pos(void) { double v; pthread_mutex_lock(&g_mu); v=g_state.pos; pthread_mutex_unlock(&g_mu); return v; }
double xe_player_dur(void) { double v; pthread_mutex_lock(&g_mu); v=g_state.dur; pthread_mutex_unlock(&g_mu); return v; }
int xe_player_buffering(void) { int v; pthread_mutex_lock(&g_mu); v=g_state.buffering; pthread_mutex_unlock(&g_mu); return v; }
int xe_player_tracks(int kind, char names[][64], int max) {
    int i, n;
    if (kind<0 || kind>2 || max<=0 || !names) return 0;
    pthread_mutex_lock(&g_mu); n=g_state.ntracks[kind]; if(n>max)n=max;
    for(i=0;i<n;i++) snprintf(names[i],64,"%s",g_state.tracks[kind][i]?g_state.tracks[kind][i]:"");
    pthread_mutex_unlock(&g_mu); return n;
}
void xe_player_select(int kind, int index) {
    int attached=0; JNIEnv *env=get_env(&attached);
    if(!env)return;
    if(bridge_ready(env)) (*env)->CallStaticVoidMethod(env,g_bridge,g_select,(jint)kind,(jint)index);
    if((*env)->ExceptionCheck(env))(*env)->ExceptionClear(env);
    if(attached)(*g_vm)->DetachCurrentThread(g_vm);
}
void xe_player_render_video(int w, int h) {
    /* Video is a separate SurfaceView; clear transparent so the GL overlay reveals it. */
    (void)w; (void)h;
    glClearColor(0.f,0.f,0.f,0.f);
    glClear(GL_COLOR_BUFFER_BIT);
}
JNIEXPORT void JNICALL Java_com_xe_xehost_XePlayer_nativePushSnapshot(
    JNIEnv *env, jobject self, jlong handle, jdouble pos, jdouble dur,
    jint buffering, jint paused, jstring error, jstring subtitle) {
    char *err=copy_utf8(env,error);
    (void)self; (void)handle; (void)subtitle;
    pthread_mutex_lock(&g_mu);
    g_state.pos=pos<0?0:pos; g_state.dur=dur<0?0:dur;
    g_state.buffering=buffering!=0; g_state.paused=paused!=0;
    snprintf(g_state.error,sizeof g_state.error,"%s",err?err:"");
    pthread_mutex_unlock(&g_mu); free(err);
}
JNIEXPORT void JNICALL Java_com_xe_xehost_XePlayer_nativePushTracks(
    JNIEnv *env, jobject self, jlong handle, jint kind, jobjectArray names) {
    jsize n, i; char *items[64]={0};
    (void)self; (void)handle;
    if(kind<0||kind>2||!names)return;
    n=(*env)->GetArrayLength(env,names); if(n>64)n=64;
    for(i=0;i<n;i++){jstring s=(jstring)(*env)->GetObjectArrayElement(env,names,i);items[i]=copy_utf8(env,s);if(s)(*env)->DeleteLocalRef(env,s);}
    pthread_mutex_lock(&g_mu);
    for(i=0;i<g_state.ntracks[kind];i++)free(g_state.tracks[kind][i]);
    g_state.ntracks[kind]=0;
    for(i=0;i<n;i++){g_state.tracks[kind][i]=items[i];items[i]=NULL;if(g_state.tracks[kind][i])g_state.ntracks[kind]++;}
    pthread_mutex_unlock(&g_mu);
    for(i=0;i<n;i++)free(items[i]);
}

/* ---------------------------------------------------------------- seek previews
 * Kotlin (XePlayer.requestPreview) decodes one small frame on a worker thread and hands the RGBA bytes over;
 * the texture is created here, on the render thread, the next time the overlay asks. One request at a time;
 * a generation number drops frames that arrive after the player was closed. */
#define PV_BUCKET 10.0
static pthread_mutex_t pv_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { int bucket, w, h, pending_bucket; double shown; unsigned char *rgba; int gen; } pv = { -1, 0, 0, -1, 0, NULL, 1 };
static XeTexture *pv_tex; static int pv_tex_bucket = -1; static double pv_tex_shown;
static void preview_reset(void) {
    pthread_mutex_lock(&pv_mu);
    free(pv.rgba); pv.rgba = NULL; pv.bucket = -1; pv.pending_bucket = -1; pv.gen++;
    pthread_mutex_unlock(&pv_mu);
    /* The texture itself is freed on the render thread (next xe_player_preview call). */
    pv_tex_bucket = -2;
}
JNIEXPORT void JNICALL Java_com_xe_xehost_XePlayer_nativePushPreview(JNIEnv *env, jobject self, jint bucket, jdouble shown,
    jint w, jint h, jbyteArray pixels, jint gen) {
    (void)self;
    if (!pixels || w < 1 || h < 1 || w > 640 || h > 640) return;
    jsize n = (*env)->GetArrayLength(env, pixels);
    if ((size_t)n != (size_t)w * (size_t)h * 4u) return;
    unsigned char *copy = (unsigned char *)malloc((size_t)n);
    if (!copy) return;
    (*env)->GetByteArrayRegion(env, pixels, 0, n, (jbyte *)copy);
    pthread_mutex_lock(&pv_mu);
    if (gen != pv.gen) { pthread_mutex_unlock(&pv_mu); free(copy); return; }
    free(pv.rgba); pv.rgba = copy; pv.bucket = bucket; pv.w = w; pv.h = h; pv.shown = shown;
    if (pv.pending_bucket == bucket) pv.pending_bucket = -1;
    pthread_mutex_unlock(&pv_mu);
}
JNIEXPORT void JNICALL Java_com_xe_xehost_XePlayer_nativePreviewFailed(JNIEnv *env, jobject self, jint gen) {
    (void)env; (void)self;
    pthread_mutex_lock(&pv_mu); if (gen == pv.gen) pv.pending_bucket = -1; pthread_mutex_unlock(&pv_mu);
}
const XeTexture *xe_player_preview(double sec, double *shown_sec) {
    if (shown_sec) *shown_sec = 0;
    if (pv_tex_bucket == -2) { if (pv_tex) { xe_tex_destroy(pv_tex); pv_tex = NULL; } pv_tex_bucket = -1; }
    if (!isfinite(sec) || sec < 0) return NULL;
    int bucket = (int)(sec / PV_BUCKET), ask = 0, gen = 0;
    pthread_mutex_lock(&pv_mu);
    if (pv.rgba && pv.bucket != pv_tex_bucket) {
        if (pv_tex && (pv_tex->w != pv.w || pv_tex->h != pv.h)) { xe_tex_destroy(pv_tex); pv_tex = NULL; }
        if (!pv_tex) pv_tex = xe_tex_create(pv.w, pv.h, pv.rgba); else xe_tex_update(pv_tex, pv.rgba);
        pv_tex_bucket = pv.bucket; pv_tex_shown = pv.shown;
    }
    if (pv_tex_bucket != bucket && pv.pending_bucket < 0) { pv.pending_bucket = bucket; ask = 1; gen = pv.gen; }
    pthread_mutex_unlock(&pv_mu);
    if (ask) {
        int attached = 0, sent = 0; JNIEnv *env = get_env(&attached);
        if (env && bridge_ready(env) && g_preview) {
            (*env)->CallStaticVoidMethod(env, g_bridge, g_preview, (jdouble)(bucket * PV_BUCKET), (jint)bucket, (jint)gen);
            sent = !(*env)->ExceptionCheck(env);
            (*env)->ExceptionClear(env);
        }
        if (attached) (*g_vm)->DetachCurrentThread(g_vm);
        if (!sent) { pthread_mutex_lock(&pv_mu); pv.pending_bucket = -1; pthread_mutex_unlock(&pv_mu); }
    }
    if (!pv_tex) return NULL;
    if (shown_sec) *shown_sec = pv_tex_shown;
    return pv_tex;
}
