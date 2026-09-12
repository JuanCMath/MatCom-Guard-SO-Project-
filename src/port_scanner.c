#include "../include/port_scanner.h"

// ============================================================================
// MAPEO DE SERVICIOS COMUNES (DATOS ESTÁTICOS)
// ============================================================================

/**
 * Tabla de servicios comunes y sus puertos asociados
 */
static const ServiceMapping common_services[] = {
    {21, "FTP", 1},
    {22, "SSH", 1},
    {23, "Telnet", 0},           // Sospechoso por inseguro
    {25, "SMTP", 1},
    {53, "DNS", 1},
    {80, "HTTP", 1},
    {110, "POP3", 1},
    {143, "IMAP", 1},
    {443, "HTTPS", 1},
    {993, "IMAPS", 1},
    {995, "POP3S", 1},
    {3389, "RDP", 0},            // Sospechoso si está expuesto
    {4444, "Metasploit", 0},     // Puerto común para backdoors
    {5900, "VNC", 0},            // Sospechoso si está expuesto
    {6667, "IRC", 0},            // Sospechoso para backdoors
    {8080, "HTTP-Alt", 1},       // Común para servidores web alternativos
    {31337, "Elite/Backdoor", 0}, // Puerto típico de backdoors
    {0, NULL, 0}                 // Terminador
};

// ============================================================================
// FUNCIONES DE DETECCIÓN DE SERVICIOS
// ============================================================================

/**
 * Obtiene el nombre del servicio asociado a un puerto
 *
 * @param port: Número del puerto
 * @param service_name: Buffer donde se almacenará el nombre del servicio
 * @param buffer_size: Tamaño del buffer
 * @return int: 1 si es un servicio común, 0 si es sospechoso o desconocido
 */
int get_port_service_name(int port, char *service_name, size_t buffer_size) {
    for (int i = 0; common_services[i].service != NULL; i++) {
        if (common_services[i].port == port) {
            snprintf(service_name, buffer_size, "%s", common_services[i].service);
            return common_services[i].is_common;
        }
    }
    
    // Puerto desconocido
    snprintf(service_name, buffer_size, "Unknown");
    return 0;
}

/**
 * Determina si un puerto es sospechoso basado en criterios de seguridad
 *
 * @param port: Número del puerto
 * @param service_name: Nombre del servicio asociado
 * @return int: 1 si es sospechoso, 0 si es normal
 */
int is_port_suspicious(int port, const char *service_name) {
    // Puertos conocidos como backdoors
    if (port == 31337 || port == 4444 || port == 6667) {
        return 1;
    }
    
    // Servicios inseguros
    if (strcmp(service_name, "Telnet") == 0 || 
        strcmp(service_name, "RDP") == 0 || 
        strcmp(service_name, "VNC") == 0) {
        return 1;
    }
    
    // Puertos altos sin justificación común
    if (port > 1024 && strcmp(service_name, "Unknown") == 0) {
        return 1;
    }
    
    return 0;
}

// ============================================================================
// FUNCIONES DE ESCANEO DE PUERTOS
// ============================================================================

/**
 * Intenta establecer conexión TCP a un puerto específico
 * 
 * @param port: Puerto a escanear
 * @return int: 1 si el puerto está abierto, 0 si está cerrado
 */
static int scan_single_port(int port) {
    int sock;
    struct sockaddr_in target;
    int result;
    
    // Crear socket TCP
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0;
    }
    
    // Configurar timeout para evitar bloqueos largos
    struct timeval timeout;
    timeout.tv_sec = 1;  // 1 segundo timeout
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    
    // Configurar dirección de destino (localhost)
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // Intentar conexión
    result = connect(sock, (struct sockaddr *)&target, sizeof(target));
    
    close(sock);
    
    return (result == 0) ? 1 : 0;
}

/**
 * Escanea un rango de puertos y genera información detallada
 * 
 * @param start_port: Puerto inicial del rango
 * @param end_port: Puerto final del rango
 * @param result: Estructura donde se almacenarán los resultados
 * @return int: 0 si es exitoso, -1 si hay error
 */
static int scan_port_range(int start_port, int end_port, ScanResult *result) {
    if (!result || start_port < 1 || end_port > 65535 || start_port > end_port) {
        return -1;
    }
    
    int total_ports = end_port - start_port + 1;
    result->ports = malloc(total_ports * sizeof(PortInfo));
    if (!result->ports) {
        return -1;
    }
    
    result->total_ports = total_ports;
    result->open_ports = 0;
    result->suspicious_ports = 0;
    
    printf("Iniciando escaneo de puertos %d-%d...\n", start_port, end_port);
    
    for (int port = start_port; port <= end_port; port++) {
        int index = port - start_port;
        PortInfo *port_info = &result->ports[index];
        
        // Inicializar información del puerto
        port_info->port = port;
        port_info->is_open = scan_single_port(port);
        
        if (port_info->is_open) {
            result->open_ports++;
            
            // Obtener información del servicio
            get_port_service_name(port, port_info->service_name,
                           sizeof(port_info->service_name));
            
            // Determinar si es sospechoso
            port_info->is_suspicious = is_port_suspicious(port, port_info->service_name);
            
            if (port_info->is_suspicious) {
                result->suspicious_ports++;
            }
            
            // Mostrar resultado inmediatamente
            if (port_info->is_suspicious) {
                printf("[ALERTA] Puerto %d/tcp abierto (%s) - SOSPECHOSO\n", 
                       port, port_info->service_name);
            } else {
                printf("[OK] Puerto %d/tcp (%s) abierto (esperado)\n", 
                       port, port_info->service_name);
            }
        }
        
        // Mostrar progreso cada 100 puertos
        if ((port - start_port + 1) % 100 == 0) {
            printf("Progreso: %d/%d puertos escaneados\n", 
                   port - start_port + 1, total_ports);
        }
    }
    
    return 0;
}

// ============================================================================
// FUNCIONES DE GENERACIÓN DE INFORMES
// ============================================================================

/**
 * Genera un informe detallado de los resultados del escaneo
 * 
 * @param result: Resultados del escaneo
 */
static void generate_scan_report(const ScanResult *result) {
    if (!result) {
        return;
    }
    
    printf("\n" SEPARATOR);
    printf("INFORME DE ESCANEO DE PUERTOS\n");
    printf(SEPARATOR);
    
    printf("Total de puertos escaneados: %d\n", result->total_ports);
    printf("Puertos abiertos encontrados: %d\n", result->open_ports);
    printf("Puertos sospechosos: %d\n", result->suspicious_ports);
    
    if (result->open_ports == 0) {
        printf("\n[INFO] No se encontraron puertos abiertos.\n");
        return;
    }
    
    printf("\nDETALLE DE PUERTOS ABIERTOS:\n");
    printf("Puerto\tServicio\t\tEstado\n");
    printf("------\t--------\t\t------\n");
    
    for (int i = 0; i < result->total_ports; i++) {
        const PortInfo *port = &result->ports[i];
        if (port->is_open) {
            const char *status = port->is_suspicious ? "SOSPECHOSO" : "NORMAL";
            printf("%d\t%-15s\t%s\n", port->port, port->service_name, status);
        }
    }
    
    if (result->suspicious_ports > 0) {
        printf("\n[ADVERTENCIA] Se encontraron %d puerto(s) sospechoso(s).\n", 
               result->suspicious_ports);
        printf("Se recomienda investigar estos puertos para verificar su legitimidad.\n");
    } else {
        printf("\n[OK] Todos los puertos abiertos corresponden a servicios esperados.\n");
    }
}



// ============================================================================
// FUNCIONES PÚBLICAS DE LA INTERFAZ
// ============================================================================

/**
 * Escanea puertos en un rango específico y genera informe
 * 
 * @param start_port: Puerto inicial (1-65535)
 * @param end_port: Puerto final (1-65535)
 * @return int: 0 si es exitoso, -1 si hay error
 */
int scan_ports(int start_port, int end_port) {
    ScanResult result;
    memset(&result, 0, sizeof(result));
    
    printf("=== ESCANEADOR DE PUERTOS MATCOM-GUARD ===\n");
    printf("Analizando puertos locales para detectar posibles amenazas...\n\n");
    
    // Realizar escaneo
    if (scan_port_range(start_port, end_port, &result) != 0) {
        printf("Error: Fallo en el escaneo de puertos\n");
        return -1;
    }

    // Generar informe en consola
    generate_scan_report(&result);
    
    // Liberar memoria
    free(result.ports);
    
    return 0;
}

/**
 * Escanea puertos comunes (1-1024) con análisis de seguridad
 * 
 * @return int: 0 si es exitoso, -1 si hay error
 */
int scan_common_ports(void) {
    printf("Iniciando escaneo de puertos comunes (1-1024)...\n");
    return scan_ports(1, 1024);
}

/**
 * Escanea un puerto específico y muestra información detallada
 * 
 * @param port: Puerto a escanear (1-65535)
 * @return int: 1 si está abierto, 0 si está cerrado, -1 si hay error
 */
int scan_specific_port(int port) {
    if (port < 1 || port > 65535) {
        printf("Error: Puerto inválido (%d). Debe estar entre 1 y 65535.\n", port);
        return -1;
    }
    
    printf("Escaneando puerto %d...\n", port);
    
    int is_open = scan_single_port(port);
    
    if (is_open) {
        char service_name[64];
        get_port_service_name(port, service_name, sizeof(service_name));
        int suspicious = is_port_suspicious(port, service_name);
        
        printf("Puerto %d/tcp: ABIERTO\n", port);
        printf("Servicio: %s\n", service_name);
        
        if (suspicious) {
            printf("Estado: SOSPECHOSO - Se recomienda investigar\n");
        } else {
            printf("Estado: NORMAL - Servicio esperado\n");
        }
        
        return 1;
    } else {
        printf("Puerto %d/tcp: CERRADO\n", port);
        return 0;
    }
}

// ============================================================================
// FUNCIONES AUXILIARES
// ============================================================================

/**
 * Obtiene el timestamp actual en formato legible
 * 
 * @return const char*: String con fecha y hora actual
 */
const char* get_current_timestamp(void) {
    static char timestamp[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S\n", tm_info);
    return timestamp;
}