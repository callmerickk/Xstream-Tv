/* Temporary Android network layer: every request fails cleanly.
 * Part R1 replaces this file with net_android.c (OkHttp through JNI). */
#include "xe/net.h"
#include <string.h>
#include <stdlib.h>
void xe_net_init(void) {}
void xe_net_shutdown(void) {}
void xe_net_pump(void) {}
int xe_http_get(const char *url, const char *const *headers, XeHttpCb cb, void *user) { (void)url; (void)headers; (void)cb; (void)user; return 0; }
int xe_http_post(const char *url, const char *const *headers, const void *body, size_t len, XeHttpCb cb, void *user) { (void)url; (void)headers; (void)body; (void)len; (void)cb; (void)user; return 0; }
void xe_http_cancel(int id) { (void)id; }
void xe_net_set_user_agent(const char *ua) { (void)ua; }
void xe_net_set_dns_over_https(const char *u) { (void)u; }
void xe_net_set_ca_bundle(const char *p) { (void)p; }
int xe_http_get_sync(const char *url, const char *const *headers, XeHttpResponse *out) { (void)url; (void)headers; if (out) memset(out, 0, sizeof *out); return 0; }
void xe_http_response_free(XeHttpResponse *r) { if (r) { free(r->body); free(r->final_url); free(r->content_type); free(r->error); memset(r, 0, sizeof *r); } }
