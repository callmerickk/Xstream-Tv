#ifndef XE_JOBS_H
#define XE_JOBS_H
#ifdef __cplusplus
extern "C" {
#endif
/* Initializes a bounded worker pool. Repeated init calls are ignored. */
void xe_jobs_init(int threads);
/* Stops accepting work, completes queued work, joins workers and runs pending callbacks. */
void xe_jobs_shutdown(void);
/* Higher priority jobs are selected first. work runs on a worker; done_on_main runs in pump. */
void xe_jobs_submit(void (*work)(void *arg), void (*done_on_main)(void *arg), void *arg, int priority);
/* Run completion callbacks on the calling/render thread for at most about 2 ms. */
void xe_jobs_pump(void);
#ifdef __cplusplus
}
#endif
#endif
