#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "port_scanner.h"

int main(void) {
    char name[64];

    int is_common = get_port_service_name(22, name, sizeof(name));
    assert(strcmp(name, "SSH") == 0);
    assert(is_common == 1);
    assert(is_port_suspicious(22, name) == 0);

    get_port_service_name(53, name, sizeof(name));
    assert(strcmp(name, "DNS") == 0);
    assert(is_port_suspicious(53, name) == 0);

    get_port_service_name(110, name, sizeof(name));
    assert(strcmp(name, "POP3") == 0);

    get_port_service_name(143, name, sizeof(name));
    assert(strcmp(name, "IMAP") == 0);

    get_port_service_name(23, name, sizeof(name));
    assert(strcmp(name, "Telnet") == 0);
    assert(is_port_suspicious(23, name) == 1);

    get_port_service_name(31337, name, sizeof(name));
    assert(strcmp(name, "Elite/Backdoor") == 0);
    assert(is_port_suspicious(31337, name) == 1);

    int is_common_unknown = get_port_service_name(50000, name, sizeof(name));
    assert(strcmp(name, "Unknown") == 0);
    assert(is_common_unknown == 0);
    assert(is_port_suspicious(50000, name) == 1);  // puerto alto sin justificación

    printf("test_port_classification: OK\n");
    return 0;
}
