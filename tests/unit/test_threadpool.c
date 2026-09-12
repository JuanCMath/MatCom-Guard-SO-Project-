#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include "threadpool.h"

static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
static int counter = 0;

static void increment_task(void *arg) {
    (void)arg;
    pthread_mutex_lock(&counter_lock);
    counter++;
    pthread_mutex_unlock(&counter_lock);
}

int main(void) {
    assert(threadpool_create(0) == NULL);

    ThreadPool *pool = threadpool_create(4);
    assert(pool != NULL);

    const int NUM_TASKS = 200;
    for (int i = 0; i < NUM_TASKS; i++) {
        int rc = threadpool_submit(pool, increment_task, NULL);
        assert(rc == 0);
    }

    threadpool_wait(pool);
    assert(counter == NUM_TASKS);

    threadpool_destroy(pool);

    printf("test_threadpool: OK (%d tareas ejecutadas)\n", counter);
    return 0;
}
