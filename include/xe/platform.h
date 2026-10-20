#ifndef XE_PLATFORM_H
#define XE_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define XE_WEAK __attribute__((weak))
#define XE_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define XE_WEAK
#define XE_PRINTF(fmt_idx, arg_idx)
#endif

typedef enum {
    XE_KEY_NONE, XE_KEY_UP, XE_KEY_DOWN, XE_KEY_LEFT, XE_KEY_RIGHT, XE_KEY_OK, XE_KEY_BACK,
    XE_KEY_MENU, XE_KEY_PLAYPAUSE, XE_KEY_FF, XE_KEY_RW, XE_KEY_SEARCH,
    XE_KEY_DELETE /* Backspace on a keyboard: deletes text in fields, acts as Back elsewhere */
} XeKey;

typedef enum {
    XE_EV_NONE, XE_EV_KEY_DOWN, XE_EV_KEY_UP, XE_EV_TEXT, XE_EV_POINTER_DOWN, XE_EV_POINTER_MOVE,
    XE_EV_POINTER_UP, XE_EV_SCROLL, XE_EV_RESIZE, XE_EV_PAUSE, XE_EV_RESUME, XE_EV_QUIT
} XeEventType;

/*
 * Field use per event type:
 *   KEY_DOWN/KEY_UP : key, repeat (non-zero for auto-repeat)
 *   TEXT            : codepoint (Unicode scalar value)
 *   POINTER_*       : x, y in drawable pixels; POINTER_MOVE also dx, dy
 *   SCROLL          : dx, dy (dy > 0 = wheel up / content moves down), x, y = pointer position
 *   RESIZE          : w, h in drawable pixels
 */
typedef struct {
    XeEventType type;
    XeKey key;
    int repeat;
    unsigned codepoint;
    float x, y, dx, dy;
    int w, h;
} XeEvent;

/* Hosts call this from any thread. Thread-safe ring buffer; if full, the oldest
 * event is dropped (a new POINTER_MOVE is dropped instead). */
void   xe_platform_push_event(const XeEvent *ev);
/* Engine calls this each frame. Returns 1 and fills *out, or 0 when empty. */
int    xe_platform_poll(XeEvent *out);
/* Seconds, monotonic, arbitrary epoch. */
double xe_time_now(void);

/* Size is the drawable size in pixels; density = device pixels per logical pixel (1.0 = 96 dpi). */
void   xe_platform_set_size(int w, int h, float density);
void   xe_platform_size(int *w, int *h, float *density);

/* Call once at startup, before xe_app_init. Strings are copied. Defaults to ".". */
void   xe_platform_set_dirs(const char *data_dir, const char *cache_dir);
const char *xe_platform_data_dir(void);
const char *xe_platform_cache_dir(void);

/* level: 0 debug, 1 info, 2 warn, 3 error. Android -> logcat tag "xe". */
void   xe_log(int level, const char *fmt, ...) XE_PRINTF(2, 3);

/* Hosts that render on demand (Android) keep drawing while this is 1. */
int    xe_platform_wants_frames(void);
void   xe_platform_set_wants_frames(int wants_frames);

/* Reads a bundled asset ("fonts/NotoSans.ttf") into malloc'd memory; caller frees.
 * Android reads from the APK; desktop reads from <exe dir>/assets/. Returns 1 on success. */
int    xe_asset_read(const char *path, void **data, size_t *len);
/* Desktop hosts set the folder that holds bundled assets. */
void   xe_platform_set_asset_dir(const char *dir);

/* The app (written later) implements these; hosts call them.
 * xe_app_init returns 0 on success, non-zero on failure. A GL context is current. */
int  xe_app_init(void);
void xe_app_frame(double dt);
void xe_app_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* XE_PLATFORM_H */
