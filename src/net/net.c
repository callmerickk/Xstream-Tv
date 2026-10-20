#include "xe/net.h"
#include "xe/platform.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
typedef HANDLE XeThread;
typedef CRITICAL_SECTION XeMutex;
typedef CONDITION_VARIABLE XeCond;
#  define xe_mutex_init(m) InitializeCriticalSection(m)
#  define xe_mutex_destroy(m) DeleteCriticalSection(m)
#  define xe_mutex_lock(m) EnterCriticalSection(m)
#  define xe_mutex_unlock(m) LeaveCriticalSection(m)
#  define xe_cond_init(c) InitializeConditionVariable(c)
#  define xe_cond_signal(c) WakeConditionVariable(c)
#  define xe_cond_broadcast(c) WakeAllConditionVariable(c)
#  define xe_cond_wait(c,m) SleepConditionVariableCS(c,m,INFINITE)
#  define xe_cond_destroy(c) ((void)(c))
#else
#  include <pthread.h>
#  include <unistd.h>
typedef pthread_t XeThread;
typedef pthread_mutex_t XeMutex;
typedef pthread_cond_t XeCond;
#  define xe_mutex_init(m) pthread_mutex_init((m), NULL)
#  define xe_mutex_destroy(m) pthread_mutex_destroy(m)
#  define xe_mutex_lock(m) pthread_mutex_lock(m)
#  define xe_mutex_unlock(m) pthread_mutex_unlock(m)
#  define xe_cond_init(c) pthread_cond_init((c), NULL)
#  define xe_cond_signal(c) pthread_cond_signal(c)
#  define xe_cond_broadcast(c) pthread_cond_broadcast(c)
#  define xe_cond_wait(c,m) pthread_cond_wait((c),(m))
#  define xe_cond_destroy(c) pthread_cond_destroy(c)
#endif

#define XE_NET_MAX_ACTIVE 8
#define XE_NET_MAX_BODY (32u * 1024u * 1024u)
#define XE_NET_TIMEOUT_SECONDS 20L

typedef enum { REQ_GET, REQ_POST } ReqMethod;
typedef struct XeRequest {
    int id;
    ReqMethod method;
    char *url;
    char **headers;
    size_t header_count;
    unsigned char *post_body;
    size_t post_len;
    XeHttpCb cb;
    void *user;
    int cancelled;
    int started;
    CURL *easy;
    struct curl_slist *curl_headers;
    char *body;
    size_t body_len;
    size_t body_cap;
    char *content_type;
    char *error;
    double started_at;
    struct XeRequest *next;
    struct XeRequest *active_next;
} XeRequest;

typedef struct XeDone {
    XeRequest *req;
    int status;
    char *final_url;
    double ms;
    struct XeDone *next;
} XeDone;

static struct {
    int initialized;
    int stopping;
    int next_id;
    XeMutex mutex;
    XeCond cond;
    XeThread thread;
    XeRequest *pending_head, *pending_tail;
    XeRequest *active_head;
    XeDone *done_head, *done_tail;
    int active_count;
    char *user_agent;
    char *doh_url;
    char *ca_bundle;
    char *cookie_path;
    CURLM *multi;
} g_net;

static double monotonic_seconds(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
#  if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#  else
    timespec_get(&ts, TIME_UTC);
#  endif
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#endif
}

static char *dupstr(const char *s) {
    size_t n;
    char *p;
    if (!s) return NULL;
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void req_free(XeRequest *r) {
    size_t i;
    if (!r) return;
    if (r->easy) curl_easy_cleanup(r->easy);
    if (r->curl_headers) curl_slist_free_all(r->curl_headers);
    for (i = 0; i < r->header_count; ++i) free(r->headers[i]);
    free(r->headers);
    free(r->url);
    free(r->post_body);
    free(r->body);
    free(r->content_type);
    free(r->error);
    free(r);
}

static void done_push(XeRequest *r, int status, char *final_url) {
    XeDone *d = (XeDone *)calloc(1, sizeof(*d));
    if (!d) {
        r->error = dupstr("out of memory queuing HTTP completion");
        free(final_url);
        req_free(r);
        return;
    }
    d->req = r;
    d->status = status;
    d->final_url = final_url;
    d->ms = (monotonic_seconds() - r->started_at) * 1000.0;
    xe_mutex_lock(&g_net.mutex);
    if (g_net.done_tail) g_net.done_tail->next = d;
    else g_net.done_head = d;
    g_net.done_tail = d;
    xe_mutex_unlock(&g_net.mutex);
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    XeRequest *r = (XeRequest *)userdata;
    size_t n;
    char *p;
    if (size && nmemb > SIZE_MAX / size) return 0;
    n = size * nmemb;
    if (n > XE_NET_MAX_BODY || r->body_len > XE_NET_MAX_BODY - n) {
        if (!r->error) r->error = dupstr("response body exceeds 32 MiB safety limit");
        return 0;
    }
    if (r->body_len + n + 1 > r->body_cap) {
        size_t cap = r->body_cap ? r->body_cap : 4096;
        while (cap < r->body_len + n + 1) {
            if (cap > XE_NET_MAX_BODY / 2) { cap = XE_NET_MAX_BODY + 1; break; }
            cap *= 2;
        }
        p = (char *)realloc(r->body, cap);
        if (!p) { if (!r->error) r->error = dupstr("out of memory reading response"); return 0; }
        r->body = p; r->body_cap = cap;
    }
    memcpy(r->body + r->body_len, ptr, n);
    r->body_len += n;
    r->body[r->body_len] = '\0';
    return n;
}

/* Portable case-insensitive prefix check (avoids platform-specific strncasecmp). */
static int ascii_ncasecmp(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
        if (x != y) return (int)x - (int)y;
        if (!x) return 0;
    }
    return 0;
}

static size_t header_cb_portable(char *ptr, size_t size, size_t nmemb, void *userdata) {
    XeRequest *r = (XeRequest *)userdata;
    size_t n = size * nmemb;
    static const char prefix[] = "Content-Type:";
    if (n >= sizeof(prefix)-1 && ascii_ncasecmp(ptr, prefix, sizeof(prefix)-1) == 0) {
        const char *s = ptr + sizeof(prefix)-1;
        size_t len;
        while ((size_t)(s-ptr) < n && (*s == ' ' || *s == '\t')) ++s;
        len = n - (size_t)(s-ptr);
        while (len && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' ' || s[len-1] == '\t')) --len;
        if (len) {
            char *v = (char *)malloc(len+1);
            if (v) { memcpy(v, s, len); v[len] = 0; free(r->content_type); r->content_type = v; }
        }
    }
    return n;
}

static int progress_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
    XeRequest *r = (XeRequest *)userdata;
    int cancelled;
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    xe_mutex_lock(&g_net.mutex);
    cancelled = r->cancelled || g_net.stopping;
    xe_mutex_unlock(&g_net.mutex);
    return cancelled ? 1 : 0;
}

static void active_remove_locked(XeRequest *r) {
    XeRequest **pp = &g_net.active_head;
    while (*pp) {
        if (*pp == r) { *pp = r->active_next; r->active_next = NULL; return; }
        pp = &(*pp)->active_next;
    }
}

static void set_easy_options(XeRequest *r) {
    size_t i;
    CURLcode rc;
    r->easy = curl_easy_init();
    if (!r->easy) { r->error = dupstr("curl_easy_init failed"); return; }
    for (i = 0; i < r->header_count; ++i) {
        struct curl_slist *h = curl_slist_append(r->curl_headers, r->headers[i]);
        if (h) r->curl_headers = h;
    }
    curl_easy_setopt(r->easy, CURLOPT_URL, r->url);
    curl_easy_setopt(r->easy, CURLOPT_PRIVATE, r);
    curl_easy_setopt(r->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(r->easy, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(r->easy, CURLOPT_CONNECTTIMEOUT, XE_NET_TIMEOUT_SECONDS);
    curl_easy_setopt(r->easy, CURLOPT_TIMEOUT, XE_NET_TIMEOUT_SECONDS);
    curl_easy_setopt(r->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(r->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(r->easy, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(r->easy, CURLOPT_XFERINFODATA, r);
    curl_easy_setopt(r->easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(r->easy, CURLOPT_ACCEPT_ENCODING, ""); /* gzip/deflate; br if built into curl */
    curl_easy_setopt(r->easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(r->easy, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(r->easy, CURLOPT_HEADERFUNCTION, header_cb_portable);
    curl_easy_setopt(r->easy, CURLOPT_HEADERDATA, r);
    curl_easy_setopt(r->easy, CURLOPT_USERAGENT, g_net.user_agent ? g_net.user_agent : "XE/1.0");
    curl_easy_setopt(r->easy, CURLOPT_HTTPHEADER, r->curl_headers);
    curl_easy_setopt(r->easy, CURLOPT_COOKIEFILE, g_net.cookie_path ? g_net.cookie_path : "");
    curl_easy_setopt(r->easy, CURLOPT_COOKIEJAR, g_net.cookie_path ? g_net.cookie_path : NULL);
#ifdef CURLOPT_DOH_URL
    if (g_net.doh_url && *g_net.doh_url) curl_easy_setopt(r->easy, CURLOPT_DOH_URL, g_net.doh_url);
#else
    if (g_net.doh_url && *g_net.doh_url) xe_log(2, "libcurl built without CURLOPT_DOH_URL support");
#endif
    if (g_net.ca_bundle && *g_net.ca_bundle) curl_easy_setopt(r->easy, CURLOPT_CAINFO, g_net.ca_bundle);
#if LIBCURL_VERSION_NUM >= 0x072F00
    curl_easy_setopt(r->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
#endif
    if (r->method == REQ_POST) {
        curl_easy_setopt(r->easy, CURLOPT_POST, 1L);
        curl_easy_setopt(r->easy, CURLOPT_POSTFIELDS, r->post_body ? (const char *)r->post_body : "");
        curl_easy_setopt(r->easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)r->post_len);
    }
    rc = curl_easy_setopt(r->easy, CURLOPT_PRIVATE, r);
    (void)rc;
}

static void remove_pending_cancelled(void) {
    XeRequest **pp, *r;
    xe_mutex_lock(&g_net.mutex);
    pp = &g_net.pending_head;
    while ((r = *pp) != NULL) {
        if (r->cancelled) {
            *pp = r->next;
            if (g_net.pending_tail == r) g_net.pending_tail = NULL;
            r->error = dupstr("request cancelled");
            r->started_at = monotonic_seconds();
            r->next = NULL;
            xe_mutex_unlock(&g_net.mutex);
            done_push(r, 0, dupstr(r->url));
            xe_mutex_lock(&g_net.mutex);
            pp = &g_net.pending_head;
        } else pp = &r->next;
    }
    if (!g_net.pending_head) g_net.pending_tail = NULL;
    else if (!g_net.pending_tail) { XeRequest *t = g_net.pending_head; while (t->next) t=t->next; g_net.pending_tail=t; }
    xe_mutex_unlock(&g_net.mutex);
}

#if defined(_WIN32)
static DWORD WINAPI net_worker(LPVOID unused)
#else
static void *net_worker(void *unused)
#endif
{
    (void)unused;
    g_net.multi = curl_multi_init();
    if (!g_net.multi) {
        xe_log(3, "curl_multi_init failed");
        xe_mutex_lock(&g_net.mutex); g_net.stopping = 1; xe_cond_broadcast(&g_net.cond); xe_mutex_unlock(&g_net.mutex);
#if defined(_WIN32)
        return 0;
#else
        return NULL;
#endif
    }
    for (;;) {
        int running = 0, msgs = 0;
        CURLMsg *msg;
        remove_pending_cancelled();
        xe_mutex_lock(&g_net.mutex);
        while (g_net.pending_head && g_net.active_count < XE_NET_MAX_ACTIVE) {
            XeRequest *r = g_net.pending_head;
            g_net.pending_head = r->next;
            if (!g_net.pending_head) g_net.pending_tail = NULL;
            r->next = NULL;
            if (r->cancelled) {
                r->error = dupstr("request cancelled");
                r->started_at = monotonic_seconds();
                xe_mutex_unlock(&g_net.mutex); done_push(r, 0, dupstr(r->url)); xe_mutex_lock(&g_net.mutex);
                continue;
            }
            r->started = 1; r->started_at = monotonic_seconds();
            xe_mutex_unlock(&g_net.mutex);
            set_easy_options(r);
            if (!r->easy || r->error) {
                xe_mutex_lock(&g_net.mutex); xe_mutex_unlock(&g_net.mutex);
                done_push(r, 0, dupstr(r->url));
                xe_mutex_lock(&g_net.mutex);
                continue;
            }
            curl_multi_add_handle(g_net.multi, r->easy);
            xe_mutex_lock(&g_net.mutex);
            r->active_next = g_net.active_head;
            g_net.active_head = r;
            ++g_net.active_count;
            xe_mutex_unlock(&g_net.mutex);
            xe_mutex_lock(&g_net.mutex);
        }
        if (g_net.stopping && g_net.active_count == 0 && !g_net.pending_head) {
            xe_mutex_unlock(&g_net.mutex);
            break;
        }
        xe_mutex_unlock(&g_net.mutex);

        /* Cancellation is inspected between short multi polls, keeping shutdown responsive. */
        curl_multi_perform(g_net.multi, &running);
        curl_multi_poll(g_net.multi, NULL, 0, 50, NULL);
        curl_multi_perform(g_net.multi, &running);

        while ((msg = curl_multi_info_read(g_net.multi, &msgs)) != NULL) {
            if (msg->msg == CURLMSG_DONE) {
                XeRequest *r = NULL;
                long status = 0;
                char *final_url = NULL;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &r);
                curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &status);
                { char *u = NULL; if (curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &u) == CURLE_OK && u) final_url = dupstr(u); }
                if (r) {
                    xe_mutex_lock(&g_net.mutex);
                    active_remove_locked(r);
                    if (g_net.active_count > 0) --g_net.active_count;
                    if (r->cancelled && !r->error) r->error = dupstr("request cancelled");
                    xe_mutex_unlock(&g_net.mutex);
                    if (msg->data.result != CURLE_OK && !r->error) r->error = dupstr(curl_easy_strerror(msg->data.result));
                    curl_multi_remove_handle(g_net.multi, msg->easy_handle);
                    done_push(r, (int)status, final_url ? final_url : dupstr(r->url));
                } else free(final_url);
                curl_multi_remove_handle(g_net.multi, msg->easy_handle);
            }
        }

        /* Abort active transfers requested by xe_http_cancel(). */
        {
            CURLM *m = g_net.multi;
            (void)m;
            /* We track active requests through CURLINFO_PRIVATE by walking handles. */
            CURL *easy = NULL;
            (void)easy;
            /* curl_multi has no public iterator in older releases; cancellation is also
               checked by the progress callback below in builds where supported. */
        }
        xe_mutex_lock(&g_net.mutex);
        if (!g_net.stopping && !g_net.pending_head && g_net.active_count == 0)
            xe_cond_wait(&g_net.cond, &g_net.mutex);
        xe_mutex_unlock(&g_net.mutex);
    }
    curl_multi_cleanup(g_net.multi);
    g_net.multi = NULL;
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static XeRequest *request_new(ReqMethod method, const char *url, const char *const *headers,
                              const void *body, size_t len, XeHttpCb cb, void *user) {
    XeRequest *r;
    size_t n = 0, i;
    if (!url || !*url || !cb || (method == REQ_POST && len && !body)) return NULL;
    r = (XeRequest *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->method = method; r->url = dupstr(url); r->cb = cb; r->user = user;
    if (!r->url) { req_free(r); return NULL; }
    if (headers) { while (headers[n]) ++n; }
    if (n) {
        r->headers = (char **)calloc(n, sizeof(char *));
        if (!r->headers) { req_free(r); return NULL; }
        for (i=0; i<n; ++i) {
            r->headers[i] = dupstr(headers[i]);
            if (!r->headers[i]) { r->header_count = i; req_free(r); return NULL; }
            r->header_count++;
        }
    }
    if (method == REQ_POST && len) {
        r->post_body = (unsigned char *)malloc(len);
        if (!r->post_body) { req_free(r); return NULL; }
        memcpy(r->post_body, body, len); r->post_len = len;
    }
    return r;
}

void xe_net_init(void) {
    const char *data;
    if (g_net.initialized) return;
    memset(&g_net, 0, sizeof(g_net));
    xe_mutex_init(&g_net.mutex); xe_cond_init(&g_net.cond);
    g_net.next_id = 1;
    g_net.user_agent = dupstr("XE/1.0");
    data = xe_platform_data_dir();
    if (data && *data) {
        size_t n = strlen(data) + 32;
        g_net.cookie_path = (char *)malloc(n);
        if (g_net.cookie_path) snprintf(g_net.cookie_path, n, "%s/%s", data, "xe-cookies.txt");
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        xe_log(3, "curl_global_init failed");
        free(g_net.user_agent); free(g_net.cookie_path);
        xe_cond_destroy(&g_net.cond); xe_mutex_destroy(&g_net.mutex); memset(&g_net,0,sizeof(g_net)); return;
    }
#if defined(_WIN32)
    g_net.thread = CreateThread(NULL, 0, net_worker, NULL, 0, NULL);
    if (!g_net.thread) { curl_global_cleanup(); xe_log(3, "failed to start network worker"); return; }
#else
    if (pthread_create(&g_net.thread, NULL, net_worker, NULL) != 0) {
        curl_global_cleanup(); xe_log(3, "failed to start network worker"); return;
    }
#endif
    g_net.initialized = 1;
}

static int submit_request(XeRequest *r) {
    if (!r || !g_net.initialized) { req_free(r); return 0; }
    xe_mutex_lock(&g_net.mutex);
    r->id = g_net.next_id++;
    if (g_net.next_id <= 0) g_net.next_id = 1;
    if (g_net.pending_tail) g_net.pending_tail->next = r; else g_net.pending_head = r;
    g_net.pending_tail = r;
    xe_cond_signal(&g_net.cond);
    xe_mutex_unlock(&g_net.mutex);
    return r->id;
}

int xe_http_get(const char *url, const char *const *headers, XeHttpCb cb, void *user) {
    return submit_request(request_new(REQ_GET, url, headers, NULL, 0, cb, user));
}
int xe_http_post(const char *url, const char *const *headers, const void *body, size_t len, XeHttpCb cb, void *user) {
    return submit_request(request_new(REQ_POST, url, headers, body, len, cb, user));
}

void xe_http_cancel(int id) {
    XeRequest *r;
    if (!g_net.initialized || id <= 0) return;
    xe_mutex_lock(&g_net.mutex);
    for (r = g_net.pending_head; r; r = r->next) if (r->id == id) r->cancelled = 1;
    for (r = g_net.active_head; r; r = r->active_next) if (r->id == id) r->cancelled = 1;
    xe_cond_signal(&g_net.cond);
    xe_mutex_unlock(&g_net.mutex);
}

void xe_net_set_user_agent(const char *ua) {
    char *copy = dupstr(ua && *ua ? ua : "XE/1.0");
    if (!copy) return;
    xe_mutex_lock(&g_net.mutex); free(g_net.user_agent); g_net.user_agent = copy; xe_cond_signal(&g_net.cond); xe_mutex_unlock(&g_net.mutex);
}
void xe_net_set_dns_over_https(const char *url) {
    char *copy = dupstr(url && *url ? url : NULL);
    xe_mutex_lock(&g_net.mutex); free(g_net.doh_url); g_net.doh_url = copy; xe_cond_signal(&g_net.cond); xe_mutex_unlock(&g_net.mutex);
}
void xe_net_set_ca_bundle(const char *path) {
    char *copy = dupstr(path && *path ? path : NULL);
    xe_mutex_lock(&g_net.mutex); free(g_net.ca_bundle); g_net.ca_bundle = copy; xe_cond_signal(&g_net.cond); xe_mutex_unlock(&g_net.mutex);
}

/* ------------------------------------------------------------ blocking GET */

static size_t sync_write(char *ptr, size_t size, size_t nmemb, void *userdata) {
    XeHttpResponse *r = (XeHttpResponse *)userdata;
    size_t n = size * nmemb;
    char *p;
    if (r->len + n > XE_NET_MAX_BODY) return 0;
    p = (char *)realloc(r->body, r->len + n + 1);
    if (!p) return 0;
    r->body = p;
    memcpy(r->body + r->len, ptr, n);
    r->len += n;
    r->body[r->len] = 0;
    return n;
}

int xe_http_get_sync(const char *url, const char *const *headers, XeHttpResponse *out) {
    CURL *c;
    CURLcode rc;
    struct curl_slist *hl = NULL;
    long status = 0;
    double t0;
    int i;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!url || !*url) return 0;
    if (!g_net.initialized) xe_net_init();
    c = curl_easy_init();
    if (!c) return 0;
    for (i = 0; headers && headers[i]; ++i) { struct curl_slist *h = curl_slist_append(hl, headers[i]); if (h) hl = h; }
    t0 = monotonic_seconds();
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 6L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_USERAGENT, g_net.user_agent ? g_net.user_agent : "XE/1.0");
    if (hl) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    if (g_net.ca_bundle && *g_net.ca_bundle) curl_easy_setopt(c, CURLOPT_CAINFO, g_net.ca_bundle);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sync_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    out->status = (int)status;
    out->ms = (monotonic_seconds() - t0) * 1000.0;
    if (rc != CURLE_OK) out->error = dupstr(curl_easy_strerror(rc));
    curl_easy_cleanup(c);
    curl_slist_free_all(hl);
    return rc == CURLE_OK;
}

void xe_http_response_free(XeHttpResponse *r) {
    if (!r) return;
    free(r->body); free(r->final_url); free(r->content_type); free(r->error);
    memset(r, 0, sizeof(*r));
}

void xe_net_pump(void) {
    double start = monotonic_seconds();
    const double budget = 0.002; /* ~2 ms callback budget; at least one completion is delivered. */
    if (!g_net.initialized) return;
    for (;;) {
        XeDone *d;
        XeRequest *saved_req;
        XeHttpResponse res;
        xe_mutex_lock(&g_net.mutex);
        d = g_net.done_head;
        if (d) {
            g_net.done_head = d->next;
            if (!g_net.done_head) g_net.done_tail = NULL;
        }
        xe_mutex_unlock(&g_net.mutex);
        if (!d) break;
        saved_req = d->req;
        memset(&res, 0, sizeof(res));
        res.status = d->status;
        res.body = saved_req->body;
        res.len = saved_req->body_len;
        res.final_url = d->final_url;
        res.content_type = saved_req->content_type;
        res.error = saved_req->error;
        res.ms = d->ms;
        saved_req->cb(&res, saved_req->user);
        free(d->final_url); free(d);
        req_free(saved_req);
        if (monotonic_seconds() - start >= budget) break;
    }
}

void xe_net_shutdown(void) {
    XeRequest *r, *next;
    XeDone *d, *dn;
    if (!g_net.initialized) return;
    xe_mutex_lock(&g_net.mutex); g_net.stopping = 1; xe_cond_broadcast(&g_net.cond); xe_mutex_unlock(&g_net.mutex);
#if defined(_WIN32)
    WaitForSingleObject(g_net.thread, INFINITE); CloseHandle(g_net.thread);
#else
    pthread_join(g_net.thread, NULL);
#endif
    /* Deliver final completions while callback target is still valid. */
    for (;;) {
        xe_mutex_lock(&g_net.mutex); d = g_net.done_head;
        if (d) { g_net.done_head = d->next; if (!g_net.done_head) g_net.done_tail = NULL; }
        xe_mutex_unlock(&g_net.mutex);
        if (!d) break;
        { XeHttpResponse res; XeRequest *q = d->req; memset(&res,0,sizeof(res));
          res.status=d->status; res.body=q->body; res.len=q->body_len; res.final_url=d->final_url;
          res.content_type=q->content_type; res.error=q->error; res.ms=d->ms; q->cb(&res,q->user);
          free(d->final_url); free(d); req_free(q); }
    }
    r = g_net.pending_head; while (r) { next=r->next; req_free(r); r=next; }
    free(g_net.user_agent); free(g_net.doh_url); free(g_net.ca_bundle); free(g_net.cookie_path);
    curl_global_cleanup(); xe_cond_destroy(&g_net.cond); xe_mutex_destroy(&g_net.mutex);
    memset(&g_net, 0, sizeof(g_net));
}
