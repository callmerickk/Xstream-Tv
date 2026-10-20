#ifndef XE_ANDROID_H
#define XE_ANDROID_H
#include <jni.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Reads an APK asset into malloc-owned memory. Caller must free(*data). */
int xe_asset_read(const char *path, void **data, size_t *len);
/* The process JavaVM, set in JNI_OnLoad (xe_jni.c). */
JavaVM *xe_android_vm(void);
/* Supported callback signature: (Ljava/lang/String;)V; pass a const char* argument. */
void xe_android_call_void(const char *method_name, const char *signature, ...);
#ifdef __cplusplus
}
#endif
#endif
