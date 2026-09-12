#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "port_scanner.h"

int main(void) {
    ScanResult result;

    // Rango inválido: debe fallar sin tocar `result`.
    int rc = scan_ports_range(100, 1, 4, NULL, NULL, NULL, &result);
    assert(rc == -1);

    // Rango válido, sin callback ni cancelación.
    rc = scan_ports_range(1, 200, 4, NULL, NULL, NULL, &result);
    assert(rc == 0);
    assert(result.total_ports == 200);
    assert(result.open_ports >= 0);
    assert(result.suspicious_ports >= 0);
    assert(result.suspicious_ports <= result.open_ports);
    free(result.ports);

    printf("test_port_scan_range: OK\n");
    return 0;
}
