#ifndef XE_JSON_H
#define XE_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse returns a cJSON object/array/value as void*. Release with xe_json_delete(). */
void *xe_json_parse(const char *text, size_t len);
void xe_json_delete(void *value);

/* Path is dot-separated object keys, e.g. "meta.name". Returns a borrowed string. */
const char *xe_json_str(const void *obj, const char *path);
int xe_json_int(const void *obj, const char *path, int fallback);
int xe_json_arr_len(const void *arr);
void *xe_json_at(const void *arr, int index); /* borrowed child; do not delete */
void *xe_json_obj_get(const void *obj, const char *key); /* borrowed child; do not delete */

/* Small JSON object builder. Keys and values are copied by cJSON.
   The returned builder is opaque. Nested builder APIs are intentionally omitted. */
typedef struct XeJsonBuilder XeJsonBuilder;
XeJsonBuilder *xe_json_builder_new(void);
int xe_json_builder_string(XeJsonBuilder *b, const char *key, const char *value);
int xe_json_builder_int(XeJsonBuilder *b, const char *key, int value);
int xe_json_builder_double(XeJsonBuilder *b, const char *key, double value);
int xe_json_builder_bool(XeJsonBuilder *b, const char *key, int value);
int xe_json_builder_null(XeJsonBuilder *b, const char *key);
char *xe_json_builder_finish(XeJsonBuilder *b); /* malloc-owned UTF-8 string; free() it */
void xe_json_builder_free(XeJsonBuilder *b);

#ifdef __cplusplus
}
#endif
#endif
