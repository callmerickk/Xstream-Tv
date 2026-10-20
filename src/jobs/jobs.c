#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "xe/jobs.h"
#include "xe/platform.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION XeMutex; typedef CONDITION_VARIABLE XeCond; typedef HANDLE XeThread;
#define M_INIT(m) InitializeCriticalSection(m)
#define M_LOCK(m) EnterCriticalSection(m)
#define M_UNLOCK(m) LeaveCriticalSection(m)
#define M_DESTROY(m) DeleteCriticalSection(m)
#define C_INIT(c) InitializeConditionVariable(c)
#define C_WAIT(c,m) SleepConditionVariableCS(c,m,INFINITE)
#define C_SIGNAL(c) WakeConditionVariable(c)
#define C_BROADCAST(c) WakeAllConditionVariable(c)
static DWORD WINAPI xe_worker(void *p);
#else
#include <pthread.h>
#include <errno.h>
typedef pthread_mutex_t XeMutex; typedef pthread_cond_t XeCond; typedef pthread_t XeThread;
#define M_INIT(m) pthread_mutex_init(m,NULL)
#define M_LOCK(m) pthread_mutex_lock(m)
#define M_UNLOCK(m) pthread_mutex_unlock(m)
#define M_DESTROY(m) pthread_mutex_destroy(m)
#define C_INIT(c) pthread_cond_init(c,NULL)
#define C_WAIT(c,m) pthread_cond_wait(c,m)
#define C_SIGNAL(c) pthread_cond_signal(c)
#define C_BROADCAST(c) pthread_cond_broadcast(c)
static void *xe_worker(void *p);
#endif

typedef struct XeJob { void (*work)(void*); void (*done)(void*); void *arg; int priority; unsigned long long seq; struct XeJob *next; } XeJob;
static struct { XeMutex mu; XeCond cv; XeThread *threads; int nthreads, initialized, stopping; XeJob *queued, *completed; unsigned long long seq; } g;
static void queue_insert(XeJob **head, XeJob *j, int by_priority) {
    XeJob **p=head;
    if (by_priority) while (*p && ((*p)->priority > j->priority || ((*p)->priority==j->priority && (*p)->seq<j->seq))) p=&(*p)->next;
    else { while (*p) p=&(*p)->next; }
    j->next=*p; *p=j;
}
#ifdef _WIN32
static DWORD WINAPI xe_worker(void *unused)
#else
static void *xe_worker(void *unused)
#endif
{
    (void)unused;
    for (;;) {
        M_LOCK(&g.mu);
        while (!g.queued && !g.stopping) C_WAIT(&g.cv,&g.mu);
        if (!g.queued && g.stopping) { M_UNLOCK(&g.mu); break; }
        XeJob *j=g.queued; g.queued=j->next; j->next=NULL;
        M_UNLOCK(&g.mu);
        j->work(j->arg);
        M_LOCK(&g.mu); queue_insert(&g.completed,j,0); M_UNLOCK(&g.mu);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
void xe_jobs_init(int threads) {
    if (g.initialized) return;
    memset(&g,0,sizeof(g)); M_INIT(&g.mu); C_INIT(&g.cv);
    if (threads<1) threads=2;
    if (threads>8) threads=8;
    g.threads=(XeThread*)calloc((size_t)threads,sizeof(XeThread));
    if (!g.threads) { M_DESTROY(&g.mu); return; }
    g.initialized=1;
    for (int i=0;i<threads;i++) {
#ifdef _WIN32
        g.threads[i]=CreateThread(NULL,0,xe_worker,NULL,0,NULL);
        if (!g.threads[i]) break;
#else
        if (pthread_create(&g.threads[i],NULL,xe_worker,NULL)!=0) break;
#endif
        g.nthreads++;
    }
    if (!g.nthreads) { free(g.threads); g.threads=NULL; g.initialized=0; M_DESTROY(&g.mu); }
}
void xe_jobs_submit(void (*work)(void*), void (*done)(void*), void *arg, int priority) {
    if (!work) return;
    if (!g.initialized) xe_jobs_init(2);
    if (!g.initialized) { xe_log(3,"xe_jobs_submit: worker pool unavailable; dropping job"); return; }
    XeJob *j=(XeJob*)calloc(1,sizeof(*j));
    if (!j) { xe_log(3,"xe_jobs_submit: out of memory; dropping job"); return; }
    j->work=work; j->done=done; j->arg=arg; j->priority=priority;
    M_LOCK(&g.mu); j->seq=++g.seq;
    if (g.stopping) { M_UNLOCK(&g.mu); free(j); return; }
    queue_insert(&g.queued,j,1); C_SIGNAL(&g.cv); M_UNLOCK(&g.mu);
}
static double now_ms(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq; LARGE_INTEGER n; if(!freq.QuadPart) QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&n); return 1000.0*(double)n.QuadPart/(double)freq.QuadPart;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (double)ts.tv_sec*1000.0+(double)ts.tv_nsec/1000000.0;
#endif
}
void xe_jobs_pump(void) {
    if (!g.initialized) return;
    double until=now_ms()+2.0;
    do { M_LOCK(&g.mu); XeJob *j=g.completed; if(j) {g.completed=j->next;} M_UNLOCK(&g.mu); if(!j) break; if(j->done) j->done(j->arg); free(j); } while(now_ms()<until);
}
void xe_jobs_shutdown(void) {
    if(!g.initialized) return;
    M_LOCK(&g.mu); g.stopping=1; C_BROADCAST(&g.cv); M_UNLOCK(&g.mu);
    for(int i=0;i<g.nthreads;i++) {
#ifdef _WIN32
        WaitForSingleObject(g.threads[i],INFINITE); CloseHandle(g.threads[i]);
#else
        pthread_join(g.threads[i],NULL);
#endif
    }
    free(g.threads); g.threads=NULL;
    /* Shutdown is a lifecycle boundary: deliver all completions on the caller. */
    for(;;) { M_LOCK(&g.mu); XeJob *j=g.completed; if(j) g.completed=j->next; M_UNLOCK(&g.mu); if(!j) break; if(j->done) j->done(j->arg); free(j); }
    while(g.queued) { XeJob *j=g.queued; g.queued=j->next; free(j); }
    M_DESTROY(&g.mu); memset(&g,0,sizeof(g));
}
