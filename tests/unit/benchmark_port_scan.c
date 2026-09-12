// _POSIX_C_SOURCE se necesita para exponer clock_gettime()/CLOCK_MONOTONIC/
// struct timespec bajo `-std=c99` estricto (glibc los oculta si no se pide
// explícitamente soporte POSIX cuando se compila en modo ISO C99 puro).
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "port_scanner.h"

static double elapsed_seconds(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int main(void) {
    ScanResult result;
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    scan_ports_range(1, 20000, 1, NULL, NULL, NULL, &result);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("1 hilo:   %.3fs (%d abiertos)\n", elapsed_seconds(start, end), result.open_ports);
    free(result.ports);

    clock_gettime(CLOCK_MONOTONIC, &start);
    scan_ports_range(1, 20000, 50, NULL, NULL, NULL, &result);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("50 hilos: %.3fs (%d abiertos)\n", elapsed_seconds(start, end), result.open_ports);
    free(result.ports);

    return 0;
}
