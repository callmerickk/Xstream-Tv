#include "xe/json.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

struct XeJsonBuilder { cJSON *root; };

void *xe_json_parse(const char *text, size_t len) {
    const char *end;
    if (!text) return NULL;
    end = text + len;
    return cJSON_ParseWithLengthOpts(text, len, &end, 0);
}
void xe_json_delete(void *value) { cJSON_Delete((cJSON *)value); }

static const cJSON *path_get(const cJSON *obj, const char *path) {
    const cJSON *cur = obj;
    const char *p = path;
    char key[256];
    if (!path || !*path) return cur;
    while (*p && cur) {
        size_t n = 0;
        while (*p && *p != '.') {
            if (n + 1 < sizeof(key)) key[n++] = *p;
            ++p;
        }
        key[n] = '\0';
        if (*p == '.') ++p;
        if (!cJSON_IsObject(cur)) return NULL;
        cur = cJSON_GetObjectItemCaseSensitive((cJSON *)cur, key);
    }
    return cur;
}
const char *xe_json_str(const void *obj, const char *path) {
    const cJSON *v = path_get((const cJSON *)obj, path);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
int xe_json_int(const void *obj, const char *path, int fallback) {
    const cJSON *v = path_get((const cJSON *)obj, path);
    if (cJSON_IsNumber(v)) return v->valueint;
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? 1 : 0;
    return fallback;
}
int xe_json_arr_len(const void *arr) {
    return cJSON_IsArray((const cJSON *)arr) ? cJSON_GetArraySize((const cJSON *)arr) : 0;
}
void *xe_json_at(const void *arr, int index) {
    if (index < 0 || !cJSON_IsArray((const cJSON *)arr)) return NULL;
    return cJSON_GetArrayItem((cJSON *)arr, index);
}
void *xe_json_obj_get(const void *obj, const char *key) {
    if (!key || !cJSON_IsObject((const cJSON *)obj)) return NULL;
    return cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
}

XeJsonBuilder *xe_json_builder_new(void) {
    XeJsonBuilder *b = (XeJsonBuilder *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->root = cJSON_CreateObject();
    if (!b->root) { free(b); return NULL; }
    return b;
}
static int add_item(XeJsonBuilder *b, const char *key, cJSON *item) {
    if (!b || !b->root || !key || !*key || !item) { cJSON_Delete(item); return 0; }
    if (!cJSON_AddItemToObject(b->root, key, item)) { cJSON_Delete(item); return 0; }
    return 1;
}
int xe_json_builder_string(XeJsonBuilder *b, const char *key, const char *value) {
    return add_item(b, key, value ? cJSON_CreateString(value) : cJSON_CreateNull());
}
int xe_json_builder_int(XeJsonBuilder *b, const char *key, int value) {
    return add_item(b, key, cJSON_CreateNumber((double)value));
}
int xe_json_builder_double(XeJsonBuilder *b, const char *key, double value) {
    return add_item(b, key, cJSON_CreateNumber(value));
}
int xe_json_builder_bool(XeJsonBuilder *b, const char *key, int value) {
    return add_item(b, key, cJSON_CreateBool(value != 0));
}
int xe_json_builder_null(XeJsonBuilder *b, const char *key) {
    return add_item(b, key, cJSON_CreateNull());
}
char *xe_json_builder_finish(XeJsonBuilder *b) {
    char *out;
    if (!b || !b->root) return NULL;
    out = cJSON_PrintUnformatted(b->root);
    return out; /* caller frees with free() */
}
void xe_json_builder_free(XeJsonBuilder *b) {
    if (!b) return;
    cJSON_Delete(b->root); free(b);
}
