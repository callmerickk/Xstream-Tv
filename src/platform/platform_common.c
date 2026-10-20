#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "xe/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static SRWLOCK g_lock = SRWLOCK_INIT;
static void lock(void)   { AcquireSRWLockExclusive(&g_lock); }
static void unlock(void) { ReleaseSRWLockExclusive(&g_lock); }
#else
#include <pthread.h>
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void lock(void)   { pthread_mutex_lock(&g_lock); }
static void unlock(void) { pthread_mutex_unlock(&g_lock); }
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

/* ------------------------------------------------------------------ events */

#define XE_QUEUE_CAP 512

static XeEvent  g_queue[XE_QUEUE_CAP];
static unsigned g_head;
static unsigned g_count;

void xe_platform_push_event(const XeEvent *ev)
{
    if (!ev) return;
    lock();
    if (g_count == XE_QUEUE_CAP) {
        if (ev->type == XE_EV_POINTER_MOVE) {
            unlock();
            return;
        }
        g_head = (g_head + 1) % XE_QUEUE_CAP; /* drop oldest */
        g_count--;
    }
    g_queue[(g_head + g_count) % XE_QUEUE_CAP] = *ev;
    g_count++;
    unlock();
}

int xe_platform_poll(XeEvent *out)
{
    int got = 0;
    if (!out) return 0;
    lock();
    if (g_count > 0) {
        *out = g_queue[g_head];
        g_head = (g_head + 1) % XE_QUEUE_CAP;
        g_count--;
        got = 1;
    }
    unlock();
    return got;
}

/* -------------------------------------------------------------------- time */

double xe_time_now(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

/* -------------------------------------------------------------------- size */

static int   g_w, g_h;
static float g_density = 1.0f;

void xe_platform_set_size(int w, int h, float density)
{
    lock();
    g_w = w;
    g_h = h;
    g_density = density > 0.0f ? density : 1.0f;
    unlock();
}

void xe_platform_size(int *w, int *h, float *density)
{
    lock();
    if (w) *w = g_w;
    if (h) *h = g_h;
    if (density) *density = g_density;
    unlock();
}

/* -------------------------------------------------------------------- dirs */

static char g_data_dir[1024]  = ".";
static char g_cache_dir[1024] = ".";

void xe_platform_set_dirs(const char *data_dir, const char *cache_dir)
{
    lock();
    snprintf(g_data_dir, sizeof g_data_dir, "%s", (data_dir && *data_dir) ? data_dir : ".");
    snprintf(g_cache_dir, sizeof g_cache_dir, "%s", (cache_dir && *cache_dir) ? cache_dir : ".");
    unlock();
}

/* Set once at startup; the returned pointers stay valid until the next set_dirs. */
const char *xe_platform_data_dir(void)  { return g_data_dir; }
const char *xe_platform_cache_dir(void) { return g_cache_dir; }

/* ------------------------------------------------------------- frames/assets */

static int g_wants_frames = 1;
int  xe_platform_wants_frames(void) { int v; lock(); v = g_wants_frames; unlock(); return v; }
void xe_platform_set_wants_frames(int on) { lock(); g_wants_frames = on ? 1 : 0; unlock(); }

#ifndef __ANDROID__
#include <stdlib.h>
static char g_asset_dir[1024] = "assets";
void xe_platform_set_asset_dir(const char *dir)
{
    lock();
    snprintf(g_asset_dir, sizeof g_asset_dir, "%s", (dir && *dir) ? dir : "assets");
    unlock();
}
int xe_asset_read(const char *path, void **data, size_t *len)
{
    char full[1400];
    FILE *f;
    long n;
    void *buf;
    if (!path || !data || !len) return 0;
    *data = NULL; *len = 0;
    snprintf(full, sizeof full, "%s/%s", g_asset_dir, path);
    f = fopen(full, "rb");
    if (!f) f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = malloc(n ? (size_t)n : 1u);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return 0; }
    fclose(f);
    *data = buf; *len = (size_t)n;
    return 1;
}
#else
void xe_platform_set_asset_dir(const char *dir) { (void)dir; }
#endif

/* --------------------------------------------------------------------- log */

void xe_log(int level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    if (!fmt) fmt = "";
    if (level < 0) level = 0;
    if (level > 3) level = 3;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

#ifdef __ANDROID__
    {
        static const int prio[4] = { ANDROID_LOG_DEBUG, ANDROID_LOG_INFO,
                                     ANDROID_LOG_WARN, ANDROID_LOG_ERROR };
        __android_log_write(prio[level], "xe", buf);
    }
#else
    {
        static const char tag[4] = { 'D', 'I', 'W', 'E' };
        fprintf(stderr, "[%9.3f] %c xe: %s\n", xe_time_now(), tag[level], buf);
        fflush(stderr);
#ifdef _WIN32
        {
            char line[1100];
            snprintf(line, sizeof line, "xe: %s\n", buf);
            OutputDebugStringA(line);
        }
#endif
    }
#endif
}
