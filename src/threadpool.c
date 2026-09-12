#include <stdlib.h>
#include <pthread.h>
#include "threadpool.h"

typedef struct Task {
    void (*func)(void *arg);
    void *arg;
    struct Task *next;
} Task;

struct ThreadPool {
    pthread_t *workers;
    int num_threads;

    Task *queue_head;
    Task *queue_tail;

    pthread_mutex_t lock;
    pthread_cond_t task_available;
    pthread_cond_t all_done;

    int pending_tasks;  // tareas encoladas o en ejecución, no terminadas
    int shutdown;
};

static void *worker_loop(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;

    for (;;) {
        pthread_mutex_lock(&pool->lock);
        while (pool->queue_head == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->task_available, &pool->lock);
        }
        if (pool->queue_head == NULL && pool->shutdown) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        Task *task = pool->queue_head;
        pool->queue_head = task->next;
        if (pool->queue_head == NULL) {
            pool->queue_tail = NULL;
        }
        pthread_mutex_unlock(&pool->lock);

        task->func(task->arg);
        free(task);

        pthread_mutex_lock(&pool->lock);
        pool->pending_tasks--;
        if (pool->pending_tasks == 0) {
            pthread_cond_broadcast(&pool->all_done);
        }
        pthread_mutex_unlock(&pool->lock);
    }

    return NULL;
}

ThreadPool *threadpool_create(int num_threads) {
    if (num_threads <= 0) {
        return NULL;
    }

    ThreadPool *pool = malloc(sizeof(ThreadPool));
    if (!pool) {
        return NULL;
    }

    pool->workers = malloc(num_threads * sizeof(pthread_t));
    if (!pool->workers) {
        free(pool);
        return NULL;
    }

    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->pending_tasks = 0;
    pool->shutdown = 0;
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->task_available, NULL);
    pthread_cond_init(&pool->all_done, NULL);

    int created = 0;
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->workers[i], NULL, worker_loop, pool) != 0) {
            break;  // degradar: seguir con los hilos que sí se crearon
        }
        created++;
    }

    if (created == 0) {
        pthread_mutex_destroy(&pool->lock);
        pthread_cond_destroy(&pool->task_available);
        pthread_cond_destroy(&pool->all_done);
        free(pool->workers);
        free(pool);
        return NULL;
    }

    pool->num_threads = created;
    return pool;
}

int threadpool_submit(ThreadPool *pool, void (*task_func)(void *arg), void *arg) {
    if (!pool || !task_func) {
        return -1;
    }

    Task *task = malloc(sizeof(Task));
    if (!task) {
        return -1;
    }

    task->func = task_func;
    task->arg = arg;
    task->next = NULL;

    pthread_mutex_lock(&pool->lock);
    if (pool->queue_tail == NULL) {
        pool->queue_head = task;
        pool->queue_tail = task;
    } else {
        pool->queue_tail->next = task;
        pool->queue_tail = task;
    }
    pool->pending_tasks++;
    pthread_cond_signal(&pool->task_available);
    pthread_mutex_unlock(&pool->lock);

    return 0;
}

void threadpool_wait(ThreadPool *pool) {
    if (!pool) {
        return;
    }
    pthread_mutex_lock(&pool->lock);
    while (pool->pending_tasks > 0) {
        pthread_cond_wait(&pool->all_done, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
}

void threadpool_destroy(ThreadPool *pool) {
    if (!pool) {
        return;
    }

    threadpool_wait(pool);

    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->task_available);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->workers[i], NULL);
    }

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->task_available);
    pthread_cond_destroy(&pool->all_done);
    free(pool->workers);
    free(pool);
}
