#ifdef HAVE_CONFIG_H
# include "config.h" /* for _GNU_SOURCE */
#endif

#include <assert.h>
#include <stdio.h>
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <qthread/qthread.h>
#include <qthread/qtimer.h>
#include "argparsing.h"

static aligned_t        global_scratch = 0;
static uint64_t         num_iterations = 0;
static uint64_t        *rets           = NULL;
static pthread_mutex_t *ret_sync       = NULL;

static aligned_t null_task(void *args_)
{
    aligned_t d = 0;

    for (uint64_t i = 0; i < num_iterations; i++) {
	d += (2.0 * i + 1);
    }
    rets[(uintptr_t)args_] = d;
    pthread_mutex_unlock(&ret_sync[(uintptr_t)args_]);
    return d;
}

int main(int   argc,
         char *argv[])
{
    uint64_t count = 0;

    qtimer_t timer;
    double   total_time = 0.0;

    CHECK_VERBOSE();

    NUMARG(num_iterations, "MT_NUM_ITERATIONS");
    NUMARG(count, "MT_COUNT");
    assert(0 != count);

    rets     = malloc(sizeof(uint64_t) * count);
    ret_sync = malloc(sizeof(pthread_mutex_t) * count);
    for (uint64_t i = 0; i < count; i++) {
        rets[i] = 0;
        pthread_mutex_init(&ret_sync[i], NULL);
        pthread_mutex_lock(&ret_sync[i]);
    }

    timer = qtimer_create();
    qtimer_start(timer);

    for (uint64_t i = 0; i < count; i++) {
        rets[i] = _Cilk_spawn null_task((void*)(uintptr_t)i);
    }

    for (uint64_t i = 0; i < count; i++) {
        pthread_mutex_lock(&ret_sync[i]);
        global_scratch += rets[i];
    }

    qtimer_stop(timer);

    total_time = qtimer_secs(timer);

    qtimer_destroy(timer);

    printf("%lu %lu %lu %f\n",
           (unsigned long)qthread_num_workers(),
           (unsigned long)count,
           (unsigned long)num_iterations,
           total_time);

    return 0;
}

/* vim:set expandtab */