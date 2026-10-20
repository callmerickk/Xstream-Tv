#include <jni.h>

/* xe_jni.c owns JNI_OnLoad; cache this class and its methods exactly once. */
static JavaVM *g_vm;
static jclass g_class;
static jmethodID g_async, g_sync, g_cancel, g_ua;

int xe_net_android_jni_init(JNIEnv *env, JavaVM *vm) {
    g_vm = vm;
    jclass local = (*env)->FindClass(env, "com/xe/xehost/XeNet");
    if (!local) return 0;
    g_class = (jclass)(*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    if (!g_class) return 0;
    g_async = (*env)->GetStaticMethodID(env,g_class,"requestAsync","(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;[BI)V");
    g_sync = (*env)->GetStaticMethodID(env,g_class,"requestBlocking","(Ljava/lang/String;[Ljava/lang/String;)Lcom/xe/xehost/XeNet$Result;");
    g_cancel = (*env)->GetStaticMethodID(env,g_class,"cancel","(I)V");
    g_ua = (*env)->GetStaticMethodID(env,g_class,"setUserAgent","(Ljava/lang/String;)V");
    if (!g_async || !g_sync || !g_cancel || !g_ua || (*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    return 1;
}
JavaVM *xe_net_android_vm(void) { return g_vm; }
jclass xe_net_android_class(void) { return g_class; }
jmethodID xe_net_android_async(void) { return g_async; }
jmethodID xe_net_android_sync(void) { return g_sync; }
jmethodID xe_net_android_cancel(void) { return g_cancel; }
jmethodID xe_net_android_set_ua(void) { return g_ua; }
