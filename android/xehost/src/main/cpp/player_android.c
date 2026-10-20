#include "player.h"
#include "xe/render.h"
#include "xe/text.h"
#include "xe/ui.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Snapshot values are pushed from Kotlin every 250 ms. Reads on render thread
   only copy this small mutex-protected state; no JNI calls happen per frame. */
typedef struct { pthread_mutex_t mu; double pos,dur; int buffering; char *subtitle; char **tracks[3]; int ntracks[3]; } AndroidPlayerState;
static AndroidPlayerState A = { .mu = PTHREAD_MUTEX_INITIALIZER };
static JavaVM *g_vm;
static jobject g_bridge;
static jmethodID m_open,m_toggle,m_seek,m_close,m_tracks,m_select;
static char *dup_utf8(const char *s){if(!s)return NULL;size_t n=strlen(s)+1;char *p=malloc(n);if(p)memcpy(p,s,n);return p;}
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved){(void)reserved;g_vm=vm;return JNI_VERSION_1_6;}
/* Host calls this once after PlayerBridge.install; production P2 can store the
   class/method IDs during JNI_OnLoad or pass a global bridge class explicitly. */
JNIEXPORT void JNICALL Java_com_streamx_xe_XePlayer_nativePushSnapshot(JNIEnv *env,jobject self,jlong handle,jdouble pos,jdouble dur,jint buffering,jstring subtitle){
 (void)self;(void)handle;const char *s=subtitle?(*env)->GetStringUTFChars(env,subtitle,NULL):NULL;char *copy=dup_utf8(s?s:"");if(s)(*env)->ReleaseStringUTFChars(env,subtitle,s);
 pthread_mutex_lock(&A.mu);A.pos=pos;A.dur=dur;A.buffering=buffering;free(A.subtitle);A.subtitle=copy;pthread_mutex_unlock(&A.mu);
}
JNIEXPORT void JNICALL Java_com_streamx_xe_XePlayer_nativePushTracks(JNIEnv *env,jobject self,jlong handle,jint kind,jobjectArray names){
 (void)self;(void)handle;if(kind<0||kind>2)return;jsize n=(*env)->GetArrayLength(env,names);char **arr=calloc((size_t)n,sizeof(char*));if(!arr&&n)return;
 for(jsize i=0;i<n;i++){jstring j=(jstring)(*env)->GetObjectArrayElement(env,names,i);const char *s=(*env)->GetStringUTFChars(env,j,NULL);arr[i]=dup_utf8(s?s:"");if(s)(*env)->ReleaseStringUTFChars(env,j,s);(*env)->DeleteLocalRef(env,j);}
 pthread_mutex_lock(&A.mu);for(int i=0;i<A.ntracks[kind];i++)free(A.tracks[kind][i]);free(A.tracks[kind]);A.tracks[kind]=arr;A.ntracks[kind]=(int)n;pthread_mutex_unlock(&A.mu);
}
void xe_player_open(const char *url,const char *const *headers,double start_sec){
 /* P2 should expose a JNI helper to call PlayerBridge.open(String,Array<String>,double).
    Keep this API stub fail-closed until bridge class/method IDs are registered. */
 (void)url;(void)headers;(void)start_sec;
}
void xe_player_toggle(void) { /* invoke PlayerBridge.toggle on Android main thread */ }
void xe_player_seek(double sec) { (void)sec; /* invoke PlayerBridge.seek on Android main thread */ }
void xe_player_close(void) { /* invoke PlayerBridge.close on Android main thread */ }
double xe_player_pos(void){pthread_mutex_lock(&A.mu);double v=A.pos;pthread_mutex_unlock(&A.mu);return v;}
double xe_player_dur(void){pthread_mutex_lock(&A.mu);double v=A.dur;pthread_mutex_unlock(&A.mu);return v;}
int xe_player_buffering(void){pthread_mutex_lock(&A.mu);int v=A.buffering;pthread_mutex_unlock(&A.mu);return v;}
int xe_player_tracks(int kind,char names[][64],int max){if(kind<0||kind>2||max<=0)return 0;pthread_mutex_lock(&A.mu);int n=A.ntracks[kind]<max?A.ntracks[kind]:max;for(int i=0;i<n;i++){snprintf(names[i],64,"%s",A.tracks[kind][i]?A.tracks[kind][i]:"");}pthread_mutex_unlock(&A.mu);return n;}
void xe_player_select(int kind,int index){(void)kind;(void)index;/* dispatch to PlayerBridge.select on UI thread */}
void xe_player_frame(double dt,XeRect area){(void)dt;(void)area;/* call platform-independent P11 overlay inside host's single GL pass */}
