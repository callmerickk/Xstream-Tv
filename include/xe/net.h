#ifndef XE_NET_H
#define XE_NET_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int status;             /* HTTP status; 0 if no HTTP response was received */
    char *body;             /* NUL-terminated, may contain embedded NULs; use len */
    size_t len;
    char *final_url;
    char *content_type;
    char *error;            /* NULL on success; owned by the response until callback returns */
    double ms;              /* total transfer time in milliseconds */
} XeHttpResponse;

typedef void (*XeHttpCb)(const XeHttpResponse *res, void *user);

void xe_net_init(void);
void xe_net_shutdown(void);
void xe_net_pump(void); /* call on the render/main thread; delivers completed callbacks */
int xe_http_get(const char *url, const char *const *headers, XeHttpCb cb, void *user);
int xe_http_post(const char *url, const char *const *headers,
                 const void *body, size_t len, XeHttpCb cb, void *user);
void xe_http_cancel(int id);
/* Blocking GET for worker threads (images). Returns 1 when a response arrived;
 * always call xe_http_response_free(out) afterwards. */
int xe_http_get_sync(const char *url, const char *const *headers, XeHttpResponse *out);
void xe_http_response_free(XeHttpResponse *r);
void xe_net_set_user_agent(const char *ua);
void xe_net_set_dns_over_https(const char *doh_url_or_null);
void xe_net_set_ca_bundle(const char *path_or_null); /* Android: point at a readable PEM bundle */

#ifdef __cplusplus
}
#endif
#endif
