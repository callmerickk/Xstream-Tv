#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <GLES2/gl2.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "xe_android.h"
#include "xe/platform.h"

static JavaVM *g_vm;
static jobject g_asset_manager_obj;
static AAssetManager *g_asset_manager;
static int g_initialized;

extern int xe_net_android_jni_init(JNIEnv *env, JavaVM *vm);
/* The only JNI_OnLoad in libxe_android: caches the VM and the OkHttp bridge (net_android_jni.c). */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved; g_vm = vm;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    if (!xe_net_android_jni_init(env, vm)) __android_log_print(ANDROID_LOG_ERROR, "xe", "XeNet bridge unavailable");
    return JNI_VERSION_1_6;
}
JavaVM *xe_android_vm(void) { return g_vm; }

int xe_asset_read(const char *path, void **data, size_t *len)
{
    if (!path || !data || !len || !g_asset_manager) return 0;
    *data = NULL; *len = 0;
    AAsset *asset = AAssetManager_open(g_asset_manager, path, AASSET_MODE_BUFFER);
    if (!asset) return 0;
    off_t size = AAsset_getLength(asset);
    if (size < 0) { AAsset_close(asset); return 0; }
    void *buf = malloc((size_t)size ? (size_t)size : 1u);
    if (!buf) { AAsset_close(asset); return 0; }
    int got = AAsset_read(asset, buf, (size_t)size);
    AAsset_close(asset);
    if (got < 0 || got != size) { free(buf); return 0; }
    *data = buf; *len = (size_t)size; return 1;
}

void xe_android_call_void(const char *method_name, const char *signature, ...)
{
    if (!g_vm || !method_name || !signature) return;
    JNIEnv *env = NULL; int attached = 0;
    jint status = (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return;
        attached = 1;
    } else if (status != JNI_OK) return;

    jclass bridge = (*env)->FindClass(env, "com/xe/xehost/XeBridge");
    if (bridge) {
        jmethodID method = (*env)->GetStaticMethodID(env, bridge, method_name, signature);
        if (method) {
            va_list args; va_start(args, signature);
            const char *text_arg = va_arg(args, const char *);
            va_end(args);
            jstring value = (*env)->NewStringUTF(env, text_arg ? text_arg : "");
            if (value) { (*env)->CallStaticVoidMethod(env, bridge, method, value); (*env)->DeleteLocalRef(env, value); }
        }
        (*env)->DeleteLocalRef(env, bridge);
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

static void push_simple_event(XeEventType type, XeKey key, int repeat)
{
    XeEvent ev; memset(&ev, 0, sizeof(ev)); ev.type = type; ev.key = key; ev.repeat = repeat;
    xe_platform_push_event(&ev);
}

JNIEXPORT void JNICALL Java_com_xe_xehost_XeActivity_nativeInit(JNIEnv *env, jobject thiz,
    jstring data_dir, jstring cache_dir, jint width, jint height, jfloat density, jobject assets)
{
    const char *data = (*env)->GetStringUTFChars(env, data_dir, NULL);
    const char *cache = (*env)->GetStringUTFChars(env, cache_dir, NULL);
    xe_platform_set_dirs(data, cache);
    xe_platform_set_size(width, height, density);
    (*env)->ReleaseStringUTFChars(env, data_dir, data);
    (*env)->ReleaseStringUTFChars(env, cache_dir, cache);
    if (g_asset_manager_obj) (*env)->DeleteGlobalRef(env, g_asset_manager_obj);
    g_asset_manager_obj = (*env)->NewGlobalRef(env, assets);
    g_asset_manager = AAssetManager_fromJava(env, assets);
    /* Configure the shared OkHttp client (cache dir) before the first request. */
    jclass net = (*env)->FindClass(env, "com/xe/xehost/XeNet");
    if (net) {
        jmethodID m = (*env)->GetStaticMethodID(env, net, "configure", "(Landroid/content/Context;)V");
        if (m) (*env)->CallStaticVoidMethod(env, net, m, thiz);
        (*env)->DeleteLocalRef(env, net);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    g_initialized = 1;
    if (xe_app_init() != 0) xe_log(3, "xe_app_init reported failure");
    XeEvent resize; memset(&resize, 0, sizeof(resize)); resize.type = XE_EV_RESIZE; resize.w = width; resize.h = height; xe_platform_push_event(&resize);
}

JNIEXPORT void JNICALL Java_com_xe_xehost_XeView_nativeFrame(JNIEnv *env, jobject thiz, jdouble dt)
{
    (void)env; (void)thiz;
    if (!g_initialized) return;
    xe_app_frame(dt);
}
JNIEXPORT void JNICALL Java_com_xe_xehost_XeView_nativeResize(JNIEnv *env, jobject thiz, jint w, jint h, jfloat density)
{
    (void)env; (void)thiz;
    xe_platform_set_size(w, h, density);
    XeEvent ev; memset(&ev, 0, sizeof(ev)); ev.type = XE_EV_RESIZE; ev.w = w; ev.h = h; xe_platform_push_event(&ev);
    glViewport(0, 0, w > 0 ? w : 1, h > 0 ? h : 1);
}
JNIEXPORT void JNICALL Java_com_xe_xehost_XeView_nativeKey(JNIEnv *env, jobject thiz, jint type, jint key, jint repeat)
{
    (void)env; (void)thiz;
    if (type == XE_EV_PAUSE || type == XE_EV_RESUME) { push_simple_event((XeEventType)type, XE_KEY_NONE, 0); return; }
    if (type < XE_EV_KEY_DOWN || type > XE_EV_KEY_UP) return;
    push_simple_event((XeEventType)type, (XeKey)key, repeat);
}
JNIEXPORT void JNICALL Java_com_xe_xehost_XeView_nativePointer(JNIEnv *env, jobject thiz, jint type, jfloat x, jfloat y)
{
    (void)env; (void)thiz;
    if (type < XE_EV_POINTER_DOWN || type > XE_EV_SCROLL) return;
    XeEvent ev; memset(&ev, 0, sizeof(ev)); ev.type = (XeEventType)type; ev.x = x; ev.y = y; xe_platform_push_event(&ev);
}
JNIEXPORT jboolean JNICALL Java_com_xe_xehost_XeView_nativeWantsFrames(JNIEnv *env, jobject thiz)
{ (void)env; (void)thiz; return xe_platform_wants_frames() ? JNI_TRUE : JNI_FALSE; }
JNIEXPORT void JNICALL Java_com_xe_xehost_XeActivity_nativeShutdown(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    if (g_initialized) { xe_app_shutdown(); g_initialized = 0; }
    if (g_asset_manager_obj) { (*env)->DeleteGlobalRef(env, g_asset_manager_obj); g_asset_manager_obj = NULL; }
    g_asset_manager = NULL;
}
