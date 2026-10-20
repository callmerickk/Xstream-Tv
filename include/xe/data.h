#ifndef XE_DATA_H
#define XE_DATA_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *id, *type, *name, *poster, *background, *logo, *description;
    char *release, *imdb_rating, *runtime;
    char **genres;
    int n_genres;
} XeMeta;
typedef struct { char *title; XeMeta *items; int n; } XeShelf;
typedef struct {
    char *addon, *url, *name, *title, *filename;
    long long size;
    char **headers;
    int n_headers;
    int height;
} XeStream;
typedef struct { char *id, *title, *thumb; int season, episode; } XeEpisode;

void xe_data_init(const char *const *addon_manifest_urls, int n, const char *tmdb_key);
void xe_data_shutdown(void);
void xe_data_pump(void); /* convenience: call xe_net_pump() then internal pump */
void xe_data_home(void (*cb)(XeShelf *shelves, int n, void *u), void *u);
void xe_data_meta(const char *type, const char *id,
                  void (*cb)(XeMeta *m, XeEpisode *eps, int n_eps, void *u), void *u);
void xe_data_search(const char *q, void (*cb)(XeMeta *res, int n, void *u), void *u);
void xe_data_streams(const char *type, const char *id,
                     void (*cb)(XeStream *s, int n, int finished, void *u), void *u);
void xe_data_free_meta(XeMeta *m);
void xe_data_free_metas(XeMeta *items, int n);
void xe_data_free_shelves(XeShelf *shelves, int n);
void xe_data_free_streams(XeStream *streams, int n);
void xe_data_free_episodes(XeEpisode *eps, int n);
#ifdef __cplusplus
}
#endif
#endif
