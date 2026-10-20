#include "xe/net.h"
#include "xe/platform.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BODY_LIMIT (32u * 1024u * 1024u)
extern JavaVM *xe_net_android_vm(void);
extern jclass xe_net_android_class(void);
extern jmethodID xe_net_android_async(void);
extern jmethodID xe_net_android_sync(void);
extern jmethodID xe_net_android_cancel(void);
extern jmethodID xe_net_android_set_ua(void);

typedef struct Req { int id; XeHttpCb cb; void *user; struct Req *next; } Req;
typedef struct Done {
    int id, status; char *body, *url, *type, *error; size_t len; double ms; struct Done *next;
} Done;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Req *reqs;
static Done *head, *tail;
static int initialized, next_id = 1;
static char *user_agent;

static double now_sec(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static char *dupstr(const char *s) {
    if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n);
    if (p) memcpy(p, s, n); return p;
}
static void free_done(Done *d) {
    if (!d) return; free(d->body); free(d->url); free(d->type); free(d->error); free(d);
}
static void queue_done(Done *d) {
    pthread_mutex_lock(&lock);
    if (tail) tail->next = d; else head = d;
    tail = d;
    pthread_mutex_unlock(&lock);
}
static Req *find_req(int id) {
    Req *r; for (r = reqs; r; r = r->next) if (r->id == id) return r; return NULL;
}
static void remove_req(int id) {
    Req **p = &reqs;
    while (*p) { if ((*p)->id == id) { Req *r = *p; *p = r->next; free(r); return; } p = &(*p)->next; }
}
static JNIEnv *get_env(int *attached) {
    JavaVM *vm = xe_net_android_vm(); JNIEnv *e = NULL; *attached = 0;
    if (!vm) return NULL;
    jint rc = (*vm)->GetEnv(vm, (void **)&e, JNI_VERSION_1_6);
    if (rc == JNI_OK) return e;
    if (rc != JNI_EDETACHED || (*vm)->AttachCurrentThread(vm, &e, NULL) != JNI_OK) return NULL;
    *attached = 1; return e;
}
static void put_env(int attached) {
    if (attached) { JavaVM *vm = xe_net_android_vm(); if (vm) (*vm)->DetachCurrentThread(vm); }
}
static void clear_exc(JNIEnv *e) { if ((*e)->ExceptionCheck(e)) (*e)->ExceptionClear(e); }
static char *from_jstr(JNIEnv *e, jstring s) {
    if (!s) return NULL; const char *p = (*e)->GetStringUTFChars(e, s, NULL);
    if (!p) return NULL; char *v = dupstr(p); (*e)->ReleaseStringUTFChars(e, s, p); return v;
}
static jobjectArray to_headers(JNIEnv *e, const char *const *h) {
    jclass sc = (*e)->FindClass(e, "java/lang/String"); if (!sc) return NULL;
    int n = 0; while (h && h[n]) ++n;
    jobjectArray a = (*e)->NewObjectArray(e, n, sc, NULL);
    for (int i = 0; a && i < n; ++i) {
        jstring s = (*e)->NewStringUTF(e, h[i]); if (s) { (*e)->SetObjectArrayElement(e, a, i, s); (*e)->DeleteLocalRef(e, s); }
    }
    (*e)->DeleteLocalRef(e, sc); return a;
}

/* Completion threads only copy and enqueue. The user callback runs in xe_net_pump. */
JNIEXPORT void JNICALL Java_com_xe_xehost_XeNet_nativeComplete
(JNIEnv *e, jclass c, jint id, jint status, jbyteArray bytes, jstring url,
 jstring type, jstring error, jlong ms) {
    (void)c;
    Done *d = calloc(1, sizeof(*d)); if (!d) return;
    d->id = id; d->status = status; d->ms = (double)ms;
    d->url = from_jstr(e, url); d->type = from_jstr(e, type); d->error = from_jstr(e, error);
    jsize n = bytes ? (*e)->GetArrayLength(e, bytes) : 0;
    if ((size_t)n > BODY_LIMIT) { d->error = dupstr("response body exceeds 32 MiB safety limit"); n = 0; }
    d->body = malloc((size_t)n + 1);
    if (d->body) {
        if (n) (*e)->GetByteArrayRegion(e, bytes, 0, n, (jbyte *)d->body);
        if ((*e)->ExceptionCheck(e)) { clear_exc(e); n = 0; free(d->body); d->body = malloc(1); }
        if (d->body) d->body[n] = 0;
    }
    d->len = d->body ? (size_t)n : 0;
    pthread_mutex_lock(&lock);
    /* Keep the request until pump has captured callback/user. */
    pthread_mutex_unlock(&lock);
    queue_done(d);
}

void xe_net_init(void) {
    pthread_mutex_lock(&lock);
    if (!initialized) { initialized = 1; next_id = 1; user_agent = dupstr("XE/1.0"); }
    pthread_mutex_unlock(&lock);
    xe_log(1, "Android OkHttp network backend initialized");
}
void xe_net_shutdown(void) {
    pthread_mutex_lock(&lock);
    Req *r = reqs; while (r) { Req *n = r->next; free(r); r = n; } reqs = NULL;
    Done *d = head; head = tail = NULL; initialized = 0;
    pthread_mutex_unlock(&lock);
    while (d) { Done *n = d->next; free_done(d); d = n; }
    free(user_agent); user_agent = NULL;
}
static int request(const char *url, const char *const *headers, const void *body,
                   size_t len, int post, XeHttpCb cb, void *user) {
    if (!initialized || !url || !*url || !cb || (post && len && !body) || len > BODY_LIMIT) return 0;
    int attached = 0; JNIEnv *e = get_env(&attached); if (!e) { put_env(attached); return 0; }
    Req *r = calloc(1, sizeof(*r)); if (!r) { put_env(attached); return 0; }
    pthread_mutex_lock(&lock);
    int id = next_id++; if (next_id <= 0) next_id = 1;
    r->id = id; r->cb = cb; r->user = user; r->next = reqs; reqs = r;
    pthread_mutex_unlock(&lock);
    jstring ju = (*e)->NewStringUTF(e, url), jm = (*e)->NewStringUTF(e, post ? "POST" : "GET");
    jobjectArray jh = to_headers(e, headers); jbyteArray jb = NULL;
    if (post && len) { jb = (*e)->NewByteArray(e, (jsize)len); if (jb) (*e)->SetByteArrayRegion(e, jb, 0, (jsize)len, (const jbyte *)body); }
    if (!ju || !jm || !jh || (post && len && !jb) || (*e)->ExceptionCheck(e)) {
        clear_exc(e); pthread_mutex_lock(&lock); remove_req(id); pthread_mutex_unlock(&lock); id = 0;
    } else {
        (*e)->CallStaticVoidMethod(e, xe_net_android_class(), xe_net_android_async(), ju, jh, jm, jb, (jint)id);
        if ((*e)->ExceptionCheck(e)) { clear_exc(e); pthread_mutex_lock(&lock); remove_req(id); pthread_mutex_unlock(&lock); id = 0; }
    }
    if (ju) (*e)->DeleteLocalRef(e, ju); if (jm) (*e)->DeleteLocalRef(e, jm);
    if (jh) (*e)->DeleteLocalRef(e, jh); if (jb) (*e)->DeleteLocalRef(e, jb);
    put_env(attached); return id;
}
int xe_http_get(const char *u, const char *const *h, XeHttpCb cb, void *v) { return request(u,h,NULL,0,0,cb,v); }
int xe_http_post(const char *u, const char *const *h, const void *b, size_t n, XeHttpCb cb, void *v) { return request(u,h,b,n,1,cb,v); }
void xe_http_cancel(int id) {
    if (id <= 0) return;
    pthread_mutex_lock(&lock); int exists = find_req(id) != NULL; pthread_mutex_unlock(&lock);
    if (!exists) return;
    int attached = 0; JNIEnv *e = get_env(&attached);
    if (e) { (*e)->CallStaticVoidMethod(e, xe_net_android_class(), xe_net_android_cancel(), (jint)id); clear_exc(e); }
    put_env(attached);
    Done *d = calloc(1, sizeof(*d)); if (!d) return;
    d->id = id; d->error = dupstr("request cancelled");
    pthread_mutex_lock(&lock);
    if (find_req(id)) { remove_req(id); pthread_mutex_unlock(&lock); queue_done(d); }
    else { pthread_mutex_unlock(&lock); free_done(d); }
}
void xe_net_set_user_agent(const char *ua) {
    char *s = dupstr(ua && *ua ? ua : "XE/1.0"); if (!s) return;
    pthread_mutex_lock(&lock); free(user_agent); user_agent = s; pthread_mutex_unlock(&lock);
    int attached = 0; JNIEnv *e = get_env(&attached);
    if (e) { jstring js = (*e)->NewStringUTF(e, s); if (js) { (*e)->CallStaticVoidMethod(e, xe_net_android_class(), xe_net_android_set_ua(), js); (*e)->DeleteLocalRef(e, js); } clear_exc(e); }
    put_env(attached);
}
void xe_net_set_dns_over_https(const char *u) { if (u && *u) xe_log(2, "Custom DNS-over-HTTPS is unsupported by Android backend"); }
void xe_net_set_ca_bundle(const char *p) { if (p && *p) xe_log(2, "Custom CA bundle ignored; Android system trust store is used"); }

void xe_net_pump(void) {
    double start = now_sec(); int delivered = 0;
    for (;;) {
        pthread_mutex_lock(&lock); Done *d = head;
        if (d) { head = d->next; if (!head) tail = NULL; }
        Req *r = d ? find_req(d->id) : NULL;
        XeHttpCb cb = r ? r->cb : NULL; void *user = r ? r->user : NULL;
        if (r) remove_req(d->id);
        pthread_mutex_unlock(&lock);
        if (!d) break;
        if (cb) {
            XeHttpResponse res = { d->status, d->body, d->len, d->url, d->type, d->error, d->ms };
            cb(&res, user); ++delivered;
        }
        free_done(d);
        if (delivered && now_sec() - start >= XE_NET_CALLBACK_BUDGET_SEC) break;
    }
}
int xe_http_get_sync(const char *url, const char *const *headers, XeHttpResponse *out) {
    if (!out || !url || !*url) return 0;
    memset(out, 0, sizeof(*out));
    int attached = 0; JNIEnv *e = get_env(&attached); if (!e) { put_env(attached); return 0; }
    jstring ju = (*e)->NewStringUTF(e, url); jobjectArray jh = to_headers(e, headers);
    jobject obj = NULL;
    if (ju && jh) obj = (*e)->CallStaticObjectMethod(e, xe_net_android_class(), xe_net_android_sync(), ju, jh);
    if ((*e)->ExceptionCheck(e)) clear_exc(e);
    int ok = 0;
    if (obj) {
        jclass c = (*e)->GetObjectClass(e, obj); jmethodID m;
        m = (*e)->GetMethodID(e,c,"getStatus","()I"); if (m) out->status = (*e)->CallIntMethod(e,obj,m);
        m = (*e)->GetMethodID(e,c,"getBody","()[B");
        if (m) { jbyteArray b = (jbyteArray)(*e)->CallObjectMethod(e,obj,m); if (b) {
            jsize n = (*e)->GetArrayLength(e,b);
            if ((size_t)n <= BODY_LIMIT) { out->body = malloc((size_t)n+1); if (out->body) { (*e)->GetByteArrayRegion(e,b,0,n,(jbyte *)out->body); out->body[n]=0; out->len=(size_t)n; } }
            else out->error = dupstr("response body exceeds 32 MiB safety limit");
            (*e)->DeleteLocalRef(e,b);
        }}
        m=(*e)->GetMethodID(e,c,"getFinalUrl","()Ljava/lang/String;"); if(m) out->final_url=from_jstr(e,(jstring)(*e)->CallObjectMethod(e,obj,m));
        m=(*e)->GetMethodID(e,c,"getContentType","()Ljava/lang/String;"); if(m) out->content_type=from_jstr(e,(jstring)(*e)->CallObjectMethod(e,obj,m));
        m=(*e)->GetMethodID(e,c,"getError","()Ljava/lang/String;"); if(m) out->error=from_jstr(e,(jstring)(*e)->CallObjectMethod(e,obj,m));
        m=(*e)->GetMethodID(e,c,"getMs","()J"); if(m) out->ms=(double)(*e)->CallLongMethod(e,obj,m);
        ok = out->error == NULL && out->body != NULL;
        (*e)->DeleteLocalRef(e,c); (*e)->DeleteLocalRef(e,obj);
    }
    if (ju) (*e)->DeleteLocalRef(e,ju); if (jh) (*e)->DeleteLocalRef(e,jh);
    if ((*e)->ExceptionCheck(e)) { clear_exc(e); ok = 0; }
    put_env(attached); return ok;
}
void xe_http_response_free(XeHttpResponse *r) {
    if (!r) return; free(r->body); free(r->final_url); free(r->content_type); free(r->error); memset(r,0,sizeof(*r));
}
