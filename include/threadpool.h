#ifndef THREADPOOL_H
#define THREADPOOL_H

typedef struct ThreadPool ThreadPool;

/** Crea un pool con `num_threads` trabajadores. NULL si num_threads <= 0 o falla la reserva. */
ThreadPool *threadpool_create(int num_threads);

/** Encola una tarea; `arg` se pasa tal cual a `task`. 0 si se encoló, -1 si falló. */
int threadpool_submit(ThreadPool *pool, void (*task)(void *arg), void *arg);

/** Bloquea hasta que todas las tareas encoladas hasta el momento terminen. */
void threadpool_wait(ThreadPool *pool);

/** Espera las tareas pendientes, detiene los hilos y libera el pool. */
void threadpool_destroy(ThreadPool *pool);

#endif // THREADPOOL_H
