// _POSIX_C_SOURCE se necesita para exponer clock_gettime()/CLOCK_MONOTONIC/
// struct timespec bajo `-std=c99` estricto (glibc los oculta si no se pide
// explícitamente soporte POSIX cuando se compila en modo ISO C99 puro) —
// ver el mismo fix aplicado en tests/unit/benchmark_port_scan.c (Task 4).
#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "process_monitor.h"

static double elapsed_seconds(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int main(void) {
    load_config();

    int rc = start_monitoring();
    assert(rc == 0);

    sleep(1);  // dejar que el hilo entre en su primera espera

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    stop_monitoring();
    clock_gettime(CLOCK_MONOTONIC, &end);

    double shutdown_time = elapsed_seconds(start, end);
    printf("test_process_shutdown: apagado en %.3fs\n", shutdown_time);
    assert(shutdown_time < 0.5);

    cleanup_monitoring();

    printf("test_process_shutdown: OK\n");
    return 0;
}
