#include <assert.h>
#include <signal.h>
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

    // Cancelación: el flag ya está en 1 antes de arrancar, así que ningún
    // hilo llega a tomar un puerto. `scan_ports_range` debe seguir
    // devolviendo éxito, con total_ports correcto y cada slot de PortInfo
    // determinísticamente en cero (gracias a calloc) en vez de memoria
    // indeterminada — nada de puertos "abiertos" fantasma ni valores de
    // basura que un caller como generate_scan_report pudiera leer.
    volatile sig_atomic_t cancel = 1;
    rc = scan_ports_range(1, 500, 4, NULL, NULL, &cancel, &result);
    assert(rc == 0);
    assert(result.total_ports == 500);
    assert(result.open_ports == 0);
    assert(result.suspicious_ports == 0);
    for (int i = 0; i < result.total_ports; i++) {
        assert(result.ports[i].is_open == 0);
        assert(result.ports[i].is_suspicious == 0);
    }
    free(result.ports);

    printf("test_port_scan_range: OK\n");
    return 0;
}
