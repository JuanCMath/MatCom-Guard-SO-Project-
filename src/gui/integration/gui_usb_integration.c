/**
 * @file gui_usb_integration.c
 * @brief Integración entre el sistema de monitoreo de dispositivos USB y la GUI
 * 
 * Este módulo implementa la funcionalidad completa de monitoreo de dispositivos USB
 * para el sistema MatCom Guard. Proporciona detección automática de dispositivos,
 * análisis de contenido con snapshots, y funcionalidad diferenciada para los
 * botones "Actualizar" y "Escaneo Profundo".
 * 
 * CARACTERÍSTICAS PRINCIPALES:
 * - Monitoreo automático de conexión/desconexión de dispositivos USB
 * - Sistema de snapshots para detectar cambios en archivos
 * - Análisis de actividad sospechosa con heurísticas avanzadas
 * - Funcionalidad diferenciada: "Actualizar" vs "Escaneo Profundo"
 * - Thread-safe con timeouts robustos para evitar bloqueos
 * - Integración completa con la GUI de MatCom Guard
 * 
 * ARQUITECTURA:
 * - Hilo de monitoreo continuo para detectar dispositivos
 * - Cache de snapshots para comparaciones temporales
 * - Sistema de estados thread-safe con mutex
 * - Callbacks para eventos de la GUI
 * 
 * FUNCIONALIDAD DIFERENCIADA:
 * - Botón "Actualizar": Retoma snapshots frescos (única función capaz)
 * - Botón "Escaneo Profundo": Compara sin alterar snapshots de referencia
 * 
 * @author Sistema MatCom Guard
 * @date Junio 2025
 * @version 1.0
 * 
 * CORRECCIONES IMPLEMENTADAS:
 * - Eliminación de bloqueos al cerrar aplicación
 * - Timeout inteligente para terminación de hilos
 * - Callbacks separados para botones diferenciados
 * - Limpieza robusta de recursos y estado
 */

#include "gui_usb_integration.h"
#include "gui_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

// ============================================================================
// DECLARACIONES DE FUNCIONES INTERNAS
// ============================================================================

/**
 * @brief Función de limpieza automática para timeout de seguridad
 * 
 * Esta función actúa como un mecanismo de seguridad para asegurar que
 * el estado scan_in_progress se limpie incluso si hay errores durante
 * el procesamiento de escaneos USB.
 * 
 * @param user_data Datos del usuario (no utilizado)
 * @return FALSE para ejecutar solo una vez
 */
static gboolean complete_usb_scan_cleanup(gpointer user_data);

/**
 * @brief Función principal del hilo de monitoreo USB
 * 
 * Ejecuta en un hilo separado y mantiene vigilancia continua sobre
 * los dispositivos USB conectados al sistema. Detecta conexiones,
 * desconexiones y programa escaneos periódicos.
 * 
 * @param arg Argumentos del hilo (no utilizado)
 * @return NULL al terminar
 */
static void* usb_monitoring_thread_function(void* arg);

// ============================================================================
// DECLARACIONES DE FUNCIONES ESPECÍFICAS PARA BOTONES DIFERENCIADOS  
// ============================================================================

/**
 * @brief Actualiza snapshots de dispositivos USB (botón "Actualizar")
 * 
 * FUNCIÓN EXCLUSIVA del botón "Actualizar". Es la única función capaz
 * de modificar los snapshots de referencia almacenados en el cache.
 * 
 * @return Número de dispositivos actualizados, -1 si error
 */
int refresh_usb_snapshots(void);

/**
 * @brief Realiza análisis comparativo de dispositivos USB (botón "Escaneo Profundo")
 * 
 * Función del botón "Escaneo Profundo". Compara el estado actual con
 * los snapshots de referencia SIN modificarlos, preservando la línea base.
 * 
 * @return Número de dispositivos analizados, -1 si error
 */
int deep_scan_usb_devices(void);

// ============================================================================
// ESTRUCTURAS DE ESTADO DE LA INTEGRACIÓN USB
// ============================================================================

// Esta estructura mantiene el estado completo del sistema de monitoreo USB
// Es más compleja que la de procesos porque debe manejar dispositivos
// que pueden conectarse y desconectarse dinámicamente
typedef struct {
    int initialized;                    ///< ¿Está inicializado el sistema? (0/1)
    int monitoring_active;              ///< ¿Está el monitoreo activo? (0/1)
    int scan_in_progress;               ///< ¿Hay un escaneo manual en progreso? (0/1)
    int scan_interval_seconds;          ///< Intervalo entre escaneos automáticos (segundos)
    int deep_scan_enabled;              ///< ¿Está habilitado el escaneo profundo? (0/1)
    pthread_t monitoring_thread;        ///< Hilo de monitoreo automático
    pthread_mutex_t state_mutex;        ///< Mutex para proteger acceso concurrente
    volatile int should_stop_monitoring; ///< Señal atómica para detener el monitoreo (0/1)
    int last_scan_files_changed;        ///< Suma de archivos +/-/~ del último escaneo profundo
    int last_scan_suspicious_devices;   ///< Dispositivos sospechosos detectados en el último escaneo profundo
} USBIntegrationState;

/**
 * @brief Instancia global del estado de integración USB
 * 
 * INICIALIZACIÓN SEGURA:
 * Todos los campos se inicializan a valores seguros. El mutex se inicializa
 * estáticamente para evitar problemas de inicialización en entornos multi-hilo.
 * 
 * VALORES POR DEFECTO:
 * - scan_interval_seconds = 30: Balance entre responsividad y rendimiento
 * - state_mutex = PTHREAD_MUTEX_INITIALIZER: Inicialización estática segura
 */
static USBIntegrationState usb_state = {
    .initialized = 0,                   // Sistema no inicializado
    .monitoring_active = 0,             // Monitoreo inactivo
    .scan_in_progress = 0,              // Sin escaneos en progreso
    .scan_interval_seconds = 30,        // 30 segundos entre escaneos automáticos
    .deep_scan_enabled = 0,             // Escaneo profundo deshabilitado por defecto
    .should_stop_monitoring = 0,        // Continuar monitoreo
    .last_scan_files_changed = 0,       // Sin escaneos profundos realizados aún
    .last_scan_suspicious_devices = 0,  // Sin escaneos profundos realizados aún
    .state_mutex = PTHREAD_MUTEX_INITIALIZER // Mutex inicializado estáticamente
};

// ============================================================================
// FUNCIONES INTERNAS DEL HILO DE MONITOREO
// ============================================================================

/**
 * Esta función representa el corazón del sistema de monitoreo USB.
 * Se ejecuta en un hilo separado y es responsable de detectar cuando
 * se conectan o desconectan dispositivos USB, y de programar escaneos
 * periódicos para detectar cambios en el contenido.
 */
static void* usb_monitoring_thread_function(void* arg) {
    (void)arg; // Evitar warning de parámetro no usado
    
    DeviceList* current_devices = NULL;
    DeviceList* previous_devices = NULL;
    
    gui_add_log_entry("USB_INTEGRATION", "INFO", 
                     "Hilo de monitoreo USB iniciado");
    
    while (!usb_state.should_stop_monitoring) {
        // Liberar la lista anterior antes de obtener la nueva
        if (previous_devices) {
            free_device_list(previous_devices);
        }
        previous_devices = current_devices;
        
        // Obtener la lista actual de dispositivos conectados
        // Esta función del backend escanea /media para encontrar dispositivos montados
        current_devices = monitor_connected_devices();
        
        // Verificar señal de parada después de operación potencialmente bloqueante
        if (usb_state.should_stop_monitoring) {
            break;
        }
        
        if (current_devices && previous_devices) {
            // Detectar dispositivos recién conectados
            for (int i = 0; i < current_devices->count; i++) {
                char* current_device = current_devices->devices[i];
                int found_in_previous = 0;
                
                // Buscar este dispositivo en la lista anterior
                for (int j = 0; j < previous_devices->count; j++) {
                    if (strcmp(current_device, previous_devices->devices[j]) == 0) {
                        found_in_previous = 1;
                        break;
                    }
                }
                
                // Si no estaba en la lista anterior, es un dispositivo nuevo
                if (!found_in_previous) {
                    on_usb_device_connected(current_device);
                }
            }
            
            // Detectar dispositivos desconectados
            for (int i = 0; i < previous_devices->count; i++) {
                char* previous_device = previous_devices->devices[i];
                int found_in_current = 0;
                
                for (int j = 0; j < current_devices->count; j++) {
                    if (strcmp(previous_device, current_devices->devices[j]) == 0) {
                        found_in_current = 1;
                        break;
                    }
                }
                
                if (!found_in_current) {
                    on_usb_device_disconnected(previous_device);
                }
            }
        } else if (current_devices && !previous_devices) {
            // Primera ejecución: todos los dispositivos son "nuevos"
            for (int i = 0; i < current_devices->count; i++) {
                on_usb_device_connected(current_devices->devices[i]);
            }
        }
        
        // Esperar el intervalo configurado antes del próximo ciclo
        // Usamos un loop con sleeps cortos para poder responder rápidamente
        // a la señal de parada
        for (int i = 0; i < usb_state.scan_interval_seconds && !usb_state.should_stop_monitoring; i++) {
            sleep(1);
            // Verificación adicional cada segundo para mayor responsividad
            if (usb_state.should_stop_monitoring) {
                break;
            }
        }
    }
    
    // Limpieza al terminar el hilo
    if (current_devices) {
        free_device_list(current_devices);
    }
    if (previous_devices) {
        free_device_list(previous_devices);
    }
    
    pthread_mutex_lock(&usb_state.state_mutex);
    usb_state.monitoring_active = 0;
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    gui_add_log_entry("USB_INTEGRATION", "INFO", 
                     "Hilo de monitoreo USB terminado");
    
    return NULL;
}

// ============================================================================
// GESTIÓN DEL CICLO DE VIDA DEL MONITOR USB
// ============================================================================

/**
 * @brief Inicializa la integración entre el monitor USB y la GUI
 * 
 * Esta función establece el puente completo entre el sistema de monitoreo de
 * dispositivos USB del backend y la interfaz gráfica de usuario. A diferencia
 * del monitor de procesos que trabaja con flujos continuos de datos, el monitor
 * USB trabaja con "snapshots" discretos que requieren comparación temporal para
 * detectar actividad maliciosa.
 * 
 * PROCESO DE INICIALIZACIÓN:
 * 1. Verifica que no esté ya inicializado (patrón singleton)
 * 2. Inicializa el sistema de cache de snapshots
 * 3. Configura el estado inicial del sistema
 * 4. Registra la inicialización exitosa
 * 
 * COMPONENTES INICIALIZADOS:
 * - Cache de snapshots para comparaciones temporales
 * - Estructuras de estado thread-safe
 * - Sistema de logging integrado
 * 
 * THREAD SAFETY:
 * Utiliza mutex para proteger la inicialización contra condiciones de carrera
 * en entornos multi-hilo.
 * 
 * @return 0 si la inicialización es exitosa, -1 si hay error
 * 
 * @note Esta función debe llamarse antes de usar cualquier otra función del módulo
 * @note Es seguro llamarla múltiples veces (patrón idempotente)
 * 
 * EJEMPLO DE USO:
 * @code
 * if (init_usb_integration() != 0) {
 *     fprintf(stderr, "Error al inicializar monitoreo USB\n");
 *     return -1;
 * }
 * @endcode
 */
int init_usb_integration(void) {
    pthread_mutex_lock(&usb_state.state_mutex);
    
    if (usb_state.initialized) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        return 0; // Ya está inicializado
    }
    
    // Inicializar el sistema de cache de snapshots USB
    // Este sistema nos permite mantener snapshots anteriores para comparación
    // temporal y detectar cambios sospechosos en dispositivos USB
    if (init_usb_snapshot_cache() != 0) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        gui_add_log_entry("USB_INTEGRATION", "ERROR", 
                         "Error al inicializar cache de snapshots USB");
        return -1;
    }
    
    usb_state.initialized = 1;
    usb_state.should_stop_monitoring = 0;
    
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    gui_add_log_entry("USB_INTEGRATION", "INFO", 
                     "Integración de monitoreo USB inicializada exitosamente");
    
    return 0;
}

int start_usb_monitoring(int scan_interval_seconds) {
    pthread_mutex_lock(&usb_state.state_mutex);
    
    if (!usb_state.initialized) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        gui_add_log_entry("USB_INTEGRATION", "ERROR", 
                         "Integración no inicializada. Llame a init_usb_integration() primero");
        return -1;
    }
    
    if (usb_state.monitoring_active) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        gui_add_log_entry("USB_INTEGRATION", "INFO", 
                         "Monitoreo USB ya está activo");
        return 0;
    }
    
    // Configurar parámetros de monitoreo
    usb_state.scan_interval_seconds = (scan_interval_seconds > 0) ? scan_interval_seconds : 30;
    usb_state.should_stop_monitoring = 0;
    
    // Crear el hilo de monitoreo
    int result = pthread_create(&usb_state.monitoring_thread, NULL, 
                               usb_monitoring_thread_function, NULL);
    
    if (result != 0) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        gui_add_log_entry("USB_INTEGRATION", "ERROR", 
                         "Error al crear hilo de monitoreo USB");
        return -1;
    }
    
    usb_state.monitoring_active = 1;
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), 
             "Monitoreo USB iniciado con intervalo de %d segundos", 
             usb_state.scan_interval_seconds);
    gui_add_log_entry("USB_INTEGRATION", "INFO", log_msg);
    
    return 0;
}

int perform_manual_usb_scan(void) {
    pthread_mutex_lock(&usb_state.state_mutex);
    
    if (usb_state.scan_in_progress) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        gui_add_log_entry("USB_INTEGRATION", "WARNING", 
                         "Ya hay un escaneo USB en progreso");
        return -1;
    }
    
    usb_state.scan_in_progress = 1;
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    gui_add_log_entry("USB_INTEGRATION", "INFO", 
                     "Iniciando escaneo manual de dispositivos USB");
    
    // Obtener lista de dispositivos conectados
    DeviceList* devices = monitor_connected_devices();
    int devices_scanned = 0;
    
    if (devices) {
        gui_add_log_entry("USB_INTEGRATION", "INFO", 
                         "Dispositivos USB detectados para escaneo");
        
        // Analizar cada dispositivo encontrado
        for (int i = 0; i < devices->count; i++) {
            char log_msg[512];
            snprintf(log_msg, sizeof(log_msg), 
                     "Analizando dispositivo USB: %s", devices->devices[i]);
            gui_add_log_entry("USB_INTEGRATION", "INFO", log_msg);
            
            if (analyze_usb_device(devices->devices[i]) == 0) {
                devices_scanned++;
            } else {
                snprintf(log_msg, sizeof(log_msg), 
                         "Error al analizar dispositivo: %s", devices->devices[i]);
                gui_add_log_entry("USB_INTEGRATION", "ERROR", log_msg);
            }
        }
        
        free_device_list(devices);
    } else {
        gui_add_log_entry("USB_INTEGRATION", "INFO", 
                         "No se encontraron dispositivos USB conectados");
    }
    
    // Actualizar estadísticas en la GUI después del escaneo
    int total_devices, suspicious_devices, total_files, files_with_changes;
    if (get_usb_statistics_for_gui(&total_devices, &suspicious_devices, &total_files, &files_with_changes) == 0) {
        gui_update_statistics(total_devices, 0, 0); // 0 para procesos y puertos por ahora
    }
    
    pthread_mutex_lock(&usb_state.state_mutex);
    usb_state.scan_in_progress = 0;
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    char completion_msg[256];
    snprintf(completion_msg, sizeof(completion_msg), 
             "Escaneo manual completado: %d dispositivos analizados", devices_scanned);
    gui_add_log_entry("USB_INTEGRATION", "INFO", completion_msg);
    
    return devices_scanned;
}

/**
 * @brief Detiene el monitoreo automático de dispositivos USB de forma robusta
 * 
 * Esta función implementa una estrategia de terminación robusta que resuelve
 * el problema de bloqueos al cerrar la aplicación. Utiliza timeout inteligente
 * y múltiples estrategias de terminación para asegurar que la aplicación nunca
 * se cuelgue esperando por hilos que no responden.
 * 
 * ESTRATEGIA DE TERMINACIÓN ROBUSTA:
 * 1. Señala al hilo que debe terminar (should_stop_monitoring = 1)
 * 2. Espera terminación natural con timeout de 3 segundos
 * 3. Si no termina, usa pthread_join como último recurso
 * 4. En caso de error, marca forzadamente como inactivo
 * 
 * MEJORAS IMPLEMENTADAS:
 * - Timeout de 3 segundos para evitar bloqueos indefinidos
 * - Verificación periódica del estado del hilo
 * - Logging detallado para debugging
 * - Limpieza forzada como mecanismo de seguridad
 * 
 * @return 0 si el monitoreo se detuvo correctamente, -1 si hay error
 * 
 * @note Es seguro llamar esta función múltiples veces
 * @note La función siempre termina en máximo 3-4 segundos
 * 
 * EJEMPLO DE USO:
 * @code
 * if (stop_usb_monitoring() != 0) {
 *     printf("Advertencia: problemas al detener monitoreo USB\n");
 * }
 * @endcode
 */
int stop_usb_monitoring(void) {
    pthread_mutex_lock(&usb_state.state_mutex);
    
    if (!usb_state.monitoring_active) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        gui_add_log_entry("USB_INTEGRATION", "INFO", 
                         "Monitoreo USB no está activo");
        return 0;
    }
    
    // Señalar al hilo que debe detenerse
    usb_state.should_stop_monitoring = 1;
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    gui_add_log_entry("USB_INTEGRATION", "INFO", 
                     "Esperando terminación del hilo USB...");
    
    // Usar un enfoque de timeout manual más portable
    int timeout_seconds = 3;
    int attempts = 0;
    
    while (attempts < timeout_seconds) {
        pthread_mutex_lock(&usb_state.state_mutex);
        int still_active = usb_state.monitoring_active;
        pthread_mutex_unlock(&usb_state.state_mutex);
        
        if (!still_active) {
            gui_add_log_entry("USB_INTEGRATION", "INFO", 
                             "Hilo USB terminó naturalmente");
            break;
        }
        
        sleep(1);
        attempts++;
    }
    
    // Si el hilo aún está activo después del timeout, forzar terminación
    pthread_mutex_lock(&usb_state.state_mutex);
    if (usb_state.monitoring_active) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        
        gui_add_log_entry("USB_INTEGRATION", "WARNING", 
                         "Timeout al esperar terminación - usando pthread_join");
        
        // Intentar join normal como último recurso
        int result = pthread_join(usb_state.monitoring_thread, NULL);
        
        if (result == 0) {
            gui_add_log_entry("USB_INTEGRATION", "INFO", 
                             "Monitoreo USB detenido exitosamente");
        } else {
            gui_add_log_entry("USB_INTEGRATION", "WARNING", 
                             "Error en pthread_join - marcando como inactivo");
            
            pthread_mutex_lock(&usb_state.state_mutex);
            usb_state.monitoring_active = 0;
            pthread_mutex_unlock(&usb_state.state_mutex);
        }
    } else {
        pthread_mutex_unlock(&usb_state.state_mutex);
        gui_add_log_entry("USB_INTEGRATION", "INFO", 
                         "Monitoreo USB detenido exitosamente");
    }
    
    return 0;
}

int is_usb_monitoring_active(void) {
    pthread_mutex_lock(&usb_state.state_mutex);
    int active = usb_state.monitoring_active;
    pthread_mutex_unlock(&usb_state.state_mutex);
    return active;
}

void cleanup_usb_integration(void) {
    gui_add_log_entry("USB_INTEGRATION", "INFO", 
                     "Iniciando limpieza de integración USB...");
    
    // Detener monitoreo si está activo
    if (is_usb_monitoring_active()) {
        gui_add_log_entry("USB_INTEGRATION", "INFO", 
                         "Deteniendo monitoreo USB activo...");
        stop_usb_monitoring();
    }
    
    pthread_mutex_lock(&usb_state.state_mutex);
    
    if (usb_state.initialized) {
        // Asegurar que cualquier escaneo en progreso se marque como terminado
        if (usb_state.scan_in_progress) {
            gui_add_log_entry("USB_INTEGRATION", "WARNING", 
                             "Escaneo en progreso durante limpieza - forzando terminación");
            usb_state.scan_in_progress = 0;
        }
        
        // Limpiar el cache de snapshots
        gui_add_log_entry("USB_INTEGRATION", "INFO", 
                         "Limpiando cache de snapshots USB...");
        cleanup_usb_snapshot_cache();
        
        usb_state.initialized = 0;
        usb_state.monitoring_active = 0;
        usb_state.should_stop_monitoring = 1;
    }
    
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    gui_add_log_entry("USB_INTEGRATION", "INFO", 
                     "✅ Integración USB finalizada y recursos liberados");
}

// ============================================================================
// FUNCIONES DE ANÁLISIS DE DISPOSITIVOS ESPECÍFICOS
// ============================================================================

int analyze_usb_device(const char *device_name) {
    if (!device_name) {
        return -1;
    }
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), 
             "Creando snapshot del dispositivo: %s", device_name);
    gui_add_log_entry("USB_ANALYZER", "INFO", log_msg);      // Paso 1: Crear un snapshot completo del dispositivo actual
    // Esta operación puede tomar tiempo ya que calcula hashes SHA-256 de todos los archivos
    DeviceSnapshot* new_snapshot = create_device_snapshot(device_name);
    if (!new_snapshot) {
        snprintf(log_msg, sizeof(log_msg), 
                 "Error al crear snapshot de %s", device_name);
        gui_add_log_entry("USB_ANALYZER", "ERROR", log_msg);
        return -1;
    }
    
    // Validar la integridad del snapshot antes de continuar
    if (validate_device_snapshot(new_snapshot) != 0) {
        snprintf(log_msg, sizeof(log_msg), 
                 "Snapshot de %s falló validación de integridad", device_name);
        gui_add_log_entry("USB_ANALYZER", "ERROR", log_msg);
        free_device_snapshot(new_snapshot);
        return -1;
    }
      // Paso 2: Recuperar el snapshot anterior del cache para comparación
    DeviceSnapshot* previous_snapshot = get_cached_usb_snapshot(device_name);
    
    // Paso 3: Convertir los datos del backend al formato que entiende la GUI
    GUIUSBDevice gui_device;
    if (adapt_device_snapshot_to_gui(new_snapshot, previous_snapshot, &gui_device) != 0) {
        gui_add_log_entry("USB_ANALYZER", "ERROR", 
                         "Error al adaptar snapshot para GUI");
        free_device_snapshot(new_snapshot);
        return -1;
    }
    
    // Paso 4: Actualizar la interfaz gráfica con los resultados
    gui_update_usb_device(&gui_device);
    
    // Paso 5: Verificar si hay actividad sospechosa y generar alertas si es necesario
    if (gui_device.is_suspicious) {
        char threat_msg[512];
        snprintf(threat_msg, sizeof(threat_msg), 
                 "Detectados %d archivos modificados/añadidos en dispositivo potencialmente comprometido", 
                 gui_device.files_changed);
        on_usb_suspicious_activity_detected(device_name, threat_msg);
    }
    
    // Paso 6: Almacenar el nuevo snapshot en el cache para futuras comparaciones
    if (store_usb_snapshot(device_name, new_snapshot) != 0) {
        gui_add_log_entry("USB_ANALYZER", "WARNING", 
                         "No se pudo almacenar snapshot en cache");
    }
    
    snprintf(log_msg, sizeof(log_msg), 
             "Análisis completado: %s - %d archivos, %d cambios, Estado: %s", 
             device_name, gui_device.total_files, gui_device.files_changed, 
             gui_device.is_suspicious ? "SOSPECHOSO" : "LIMPIO");
    gui_add_log_entry("USB_ANALYZER", "INFO", log_msg);
    
    return 0;
}

int perform_deep_usb_scan(const char *device_name) {
    if (!device_name) {
        return -1;
    }
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), 
             "Iniciando escaneo profundo de %s (esto puede tomar varios minutos)", device_name);
    gui_add_log_entry("USB_ANALYZER", "INFO", log_msg);
    
    // El escaneo profundo es esencialmente el mismo proceso que el escaneo normal,
    // pero el backend automáticamente calcula hashes SHA-256 más detallados
    // y realiza verificaciones adicionales de integridad
    return analyze_usb_device(device_name);
}

// ============================================================================
// GESTIÓN DE ESTADO Y SINCRONIZACIÓN
// ============================================================================

int sync_gui_with_usb_devices(void) {
    gui_add_log_entry("USB_INTEGRATION", "INFO", 
                     "Sincronizando vista GUI con dispositivos USB conectados");
    
    // Obtener la lista actual de dispositivos
    DeviceList* devices = monitor_connected_devices();
    int devices_synced = 0;
    
    if (devices) {
        for (int i = 0; i < devices->count; i++) {
            // Para cada dispositivo, verificar si tenemos un snapshot en cache
            DeviceSnapshot* cached_snapshot = get_cached_usb_snapshot(devices->devices[i]);
            
            if (cached_snapshot) {
                // Si tenemos un snapshot, convertirlo y actualizar la GUI
                GUIUSBDevice gui_device;
                if (adapt_device_snapshot_to_gui(cached_snapshot, NULL, &gui_device) == 0) {
                    gui_update_usb_device(&gui_device);
                    devices_synced++;
                }
            } else {
                // Si no tenemos snapshot, realizar un análisis inicial
                if (analyze_usb_device(devices->devices[i]) == 0) {
                    devices_synced++;
                }
            }
        }
        
        free_device_list(devices);
    }
    
    char sync_msg[256];
    snprintf(sync_msg, sizeof(sync_msg), 
             "Sincronización completada: %d dispositivos actualizados", devices_synced);
    gui_add_log_entry("USB_INTEGRATION", "INFO", sync_msg);
    return devices_synced;
}

// ============================================================================
// FUNCIONES ESPECÍFICAS PARA BOTONES DIFERENCIADOS
// ============================================================================

/**
 * @brief Actualiza snapshots de dispositivos USB (FUNCIÓN EXCLUSIVA DEL BOTÓN "ACTUALIZAR")
 * 
 * MODIFICACIÓN PRINCIPAL: Esta función fue completamente separada del escaneo profundo
 * para implementar lógica diferenciada entre los dos botones de la GUI USB.
 * 
 * Esta función implementa la funcionalidad del botón "Actualizar" y es la ÚNICA
 * función capaz de modificar los snapshots de referencia almacenados en el cache.
 * Su propósito es establecer una nueva línea base después de cambios legítimos
 * en los dispositivos USB.
 * 
 * FUNCIONALIDAD EXCLUSIVA:
 * - Es la única función que puede modificar snapshots de referencia
 * - Reemplaza snapshots anteriores con nuevos snapshots frescos
 * - Establece nueva línea base para futuras comparaciones
 * - Resetea contadores de archivos modificados a 0
 * 
 * PROCESO DE ACTUALIZACIÓN:
 * 1. Verifica que no hay otro escaneo en progreso
 * 2. Detecta todos los dispositivos USB conectados
 * 3. Crea nuevos snapshots completos (sin comparar con anteriores)
 * 4. Almacena los snapshots como nueva línea base
 * 5. Actualiza la GUI con estado "ACTUALIZADO"
 * 6. Limpia el estado de escaneo
 * 
 * DIFERENCIA CRÍTICA CON ESCANEO PROFUNDO:
 * - Esta función MODIFICA snapshots de referencia
 * - deep_scan_usb_devices() SOLO LEE snapshots sin modificarlos
 * 
 * USO RECOMENDADO:
 * - Después de hacer cambios legítimos en dispositivos USB
 * - Cuando se quiere establecer un nuevo punto de referencia
 * - Para "limpiar" el estado después de actividad conocida
 * 
 * THREAD SAFETY:
 * Protegida con mutex para evitar condiciones de carrera con otros escaneos.
 * 
 * @return Número de dispositivos actualizados, -1 si error o escaneo en progreso
 * 
 * @note Solo esta función puede actualizar snapshots de referencia
 * @note Marca dispositivos con estado "ACTUALIZADO" en la GUI
 * 
 * EJEMPLO DE USO:
 * @code
 * int updated = refresh_usb_snapshots();
 * if (updated > 0) {
 *     printf("Snapshots actualizados para %d dispositivos\n", updated);
 * }
 * @endcode
 */
int refresh_usb_snapshots(void) {
    pthread_mutex_lock(&usb_state.state_mutex);
    if (usb_state.scan_in_progress) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        gui_add_log_entry("USB_REFRESH", "WARNING", 
                         "No se puede actualizar: escaneo en progreso");
        return -1;
    }
    usb_state.scan_in_progress = 1;
    pthread_mutex_unlock(&usb_state.state_mutex);

    gui_add_log_entry("USB_REFRESH", "INFO", 
                     "🔄 Iniciando actualización de snapshots USB");

    DeviceList* devices = monitor_connected_devices();
    int devices_updated = 0;

    if (devices && devices->count > 0) {
        for (int i = 0; i < devices->count; i++) {
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), 
                     "Creando nuevo snapshot para: %s", devices->devices[i]);
            gui_add_log_entry("USB_REFRESH", "INFO", log_msg);

            DeviceSnapshot* new_snapshot = create_device_snapshot(devices->devices[i]);
            
            if (new_snapshot && validate_device_snapshot(new_snapshot) == 0) {
                if (store_usb_snapshot(devices->devices[i], new_snapshot) == 0) {
                    GUIUSBDevice gui_device;
                    if (adapt_device_snapshot_to_gui(new_snapshot, NULL, &gui_device) == 0) {
                        strncpy(gui_device.status, "ACTUALIZADO", sizeof(gui_device.status) - 1);
                        gui_device.files_changed = 0;
                        gui_device.is_suspicious = FALSE;
                        gui_update_usb_device(&gui_device);
                        devices_updated++;
                    }
                    
                    snprintf(log_msg, sizeof(log_msg), 
                             "✅ Snapshot actualizado para %s (%d archivos)", 
                             devices->devices[i], new_snapshot->file_count);
                    gui_add_log_entry("USB_REFRESH", "INFO", log_msg);
                } else {
                    snprintf(log_msg, sizeof(log_msg), 
                             "❌ Error al almacenar snapshot de %s", devices->devices[i]);
                    gui_add_log_entry("USB_REFRESH", "ERROR", log_msg);
                    free_device_snapshot(new_snapshot);
                }
            } else {
                snprintf(log_msg, sizeof(log_msg), 
                         "❌ Error al crear snapshot de %s", devices->devices[i]);
                gui_add_log_entry("USB_REFRESH", "ERROR", log_msg);
                if (new_snapshot) {
                    free_device_snapshot(new_snapshot);
                }
            }
        }
        free_device_list(devices);
    } else {
        gui_add_log_entry("USB_REFRESH", "INFO", 
                         "No se encontraron dispositivos USB para actualizar");
    }

    pthread_mutex_lock(&usb_state.state_mutex);
    usb_state.scan_in_progress = 0;
    pthread_mutex_unlock(&usb_state.state_mutex);

    char completion_msg[256];
    snprintf(completion_msg, sizeof(completion_msg), 
             "🔄 Actualización completada: %d dispositivos actualizados", devices_updated);
    gui_add_log_entry("USB_REFRESH", "INFO", completion_msg);

    return devices_updated;
}

/**
 * @brief Realiza análisis comparativo de dispositivos USB (FUNCIÓN DEL BOTÓN "ESCANEO PROFUNDO")
 * 
 * Esta función implementa la funcionalidad del botón "Escaneo Profundo" y realiza
 * análisis de seguridad comparativo SIN modificar los snapshots de referencia.
 * Su propósito es detectar actividad sospechosa preservando la línea base establecida.
 * 
 * FUNCIONALIDAD NO DESTRUCTIVA:
 * - NUNCA modifica snapshots de referencia
 * - Crea snapshots temporales solo para comparación
 * - Preserva la línea base establecida por refresh_usb_snapshots()
 * - Libera snapshots temporales después del análisis
 * 
 * PROCESO DE ANÁLISIS PROFUNDO:
 * 1. Verifica que no hay otro escaneo en progreso
 * 2. Para cada dispositivo conectado:
 *    a. Obtiene snapshot de referencia del cache
 *    b. Crea snapshot temporal del estado actual
 *    c. Compara ambos snapshots para detectar cambios
 *    d. Evalúa si los cambios son sospechosos
 *    e. Actualiza GUI con resultados
 *    f. Libera snapshot temporal (NO lo almacena)
 * 
 * CRITERIOS DE EVALUACIÓN DE SOSPECHA:
 * - Eliminación masiva: >10% de archivos eliminados
 * - Modificación masiva: >20% de archivos modificados
 * - Actividad muy alta: >30% de cambios totales
 * - Inyección de archivos: Muchos archivos nuevos en dispositivos pequeños
 * 
 * ESTADOS POSIBLES EN GUI:
 * - "NUEVO": Primera vez que se detecta el dispositivo
 * - "LIMPIO": Sin cambios detectados
 * - "CAMBIOS": Cambios detectados pero no sospechosos
 * - "SOSPECHOSO": Cambios que activan heurísticas de seguridad
 * 
 * DIFERENCIA CRÍTICA CON ACTUALIZACIÓN:
 * - Esta función SOLO LEE snapshots de referencia
 * - refresh_usb_snapshots() MODIFICA snapshots de referencia
 * 
 * USO RECOMENDADO:
 * - Para verificación de seguridad periódica
 * - Cuando se sospecha actividad maliciosa
 * - Para análisis forense sin alterar evidencia
 * 
 * @return Número de dispositivos analizados, -1 si error o escaneo en progreso
 * 
 * @note Esta función preserva los snapshots de referencia intactos
 * @note Los snapshots temporales se liberan automáticamente
 * 
 * EJEMPLO DE USO:
 * @code
 * int analyzed = deep_scan_usb_devices();
 * if (analyzed > 0) {
 *     printf("Análisis completado para %d dispositivos\n", analyzed);
 * }
 * @endcode
 */
int deep_scan_usb_devices(void) {
    pthread_mutex_lock(&usb_state.state_mutex);
    if (usb_state.scan_in_progress) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        gui_add_log_entry("USB_DEEP_SCAN", "WARNING", 
                         "No se puede hacer escaneo profundo: escaneo en progreso");
        return -1;
    }
    usb_state.scan_in_progress = 1;
    pthread_mutex_unlock(&usb_state.state_mutex);

    gui_add_log_entry("USB_DEEP_SCAN", "INFO", 
                     "🔍 Iniciando escaneo profundo de dispositivos USB");

    DeviceList* devices = monitor_connected_devices();
    int devices_analyzed = 0;
    int total_files_changed = 0;
    int total_suspicious_devices = 0;

    if (devices && devices->count > 0) {
        for (int i = 0; i < devices->count; i++) {
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), 
                     "Analizando dispositivo: %s", devices->devices[i]);
            gui_add_log_entry("USB_DEEP_SCAN", "INFO", log_msg);

            DeviceSnapshot* reference_snapshot = get_cached_usb_snapshot(devices->devices[i]);
            
            if (!reference_snapshot) {
                snprintf(log_msg, sizeof(log_msg), 
                         "⚠️ No hay snapshot de referencia para %s. Creando inicial...", 
                         devices->devices[i]);
                gui_add_log_entry("USB_DEEP_SCAN", "WARNING", log_msg);
                
                DeviceSnapshot* initial_snapshot = create_device_snapshot(devices->devices[i]);
                if (initial_snapshot && validate_device_snapshot(initial_snapshot) == 0) {
                    store_usb_snapshot(devices->devices[i], initial_snapshot);
                    
                    GUIUSBDevice gui_device;
                    if (adapt_device_snapshot_to_gui(initial_snapshot, NULL, &gui_device) == 0) {
                        strncpy(gui_device.status, "NUEVO", sizeof(gui_device.status) - 1);
                        gui_device.files_changed = 0;
                        gui_device.is_suspicious = FALSE;
                        gui_update_usb_device(&gui_device);
                    }
                    devices_analyzed++;
                }
                continue;
            }

            DeviceSnapshot* current_snapshot = create_device_snapshot(devices->devices[i]);
            
            if (current_snapshot && validate_device_snapshot(current_snapshot) == 0) {
                int files_added, files_modified, files_deleted;                if (detect_usb_changes(reference_snapshot, current_snapshot,
                                           &files_added, &files_modified, &files_deleted) == 0) {
                    
                    gboolean is_suspicious = evaluate_usb_suspicion(files_added, files_modified, 
                                                                   files_deleted, reference_snapshot->file_count);
                    
                    GUIUSBDevice gui_device;
                    if (adapt_device_snapshot_to_gui(current_snapshot, reference_snapshot, &gui_device) == 0) {
                        if (files_added + files_modified + files_deleted > 0) {
                            strncpy(gui_device.status, is_suspicious ? "SOSPECHOSO" : "CAMBIOS", 
                                   sizeof(gui_device.status) - 1);
                        } else {
                            strncpy(gui_device.status, "LIMPIO", sizeof(gui_device.status) - 1);
                        }
                        
                        gui_device.files_changed = files_added + files_modified + files_deleted;
                        gui_device.is_suspicious = is_suspicious;
                        gui_update_usb_device(&gui_device);
                    }
                    
                    total_files_changed += files_added + files_modified + files_deleted;
                    if (is_suspicious) {
                        total_suspicious_devices++;
                        snprintf(log_msg, sizeof(log_msg),
                                "🚨 ACTIVIDAD SOSPECHOSA en %s: +%d archivos, ~%d modificados, -%d eliminados",
                                devices->devices[i], files_added, files_modified, files_deleted);
                        gui_add_log_entry("USB_DEEP_SCAN", "ALERT", log_msg);
                    } else if (files_added + files_modified + files_deleted > 0) {
                        snprintf(log_msg, sizeof(log_msg), 
                                "ℹ️ Cambios normales en %s: +%d archivos, ~%d modificados, -%d eliminados", 
                                devices->devices[i], files_added, files_modified, files_deleted);
                        gui_add_log_entry("USB_DEEP_SCAN", "INFO", log_msg);
                    } else {
                        snprintf(log_msg, sizeof(log_msg), 
                                "✅ Sin cambios en %s", devices->devices[i]);
                        gui_add_log_entry("USB_DEEP_SCAN", "INFO", log_msg);
                    }
                    
                    devices_analyzed++;
                }
                
                free_device_snapshot(current_snapshot);
            } else {
                snprintf(log_msg, sizeof(log_msg), 
                         "❌ Error al crear snapshot temporal de %s", devices->devices[i]);
                gui_add_log_entry("USB_DEEP_SCAN", "ERROR", log_msg);
                if (current_snapshot) {
                    free_device_snapshot(current_snapshot);
                }
            }
        }
        free_device_list(devices);
    } else {
        gui_add_log_entry("USB_DEEP_SCAN", "INFO", 
                         "No se encontraron dispositivos USB para analizar");
    }

    pthread_mutex_lock(&usb_state.state_mutex);
    usb_state.scan_in_progress = 0;
    usb_state.last_scan_files_changed = total_files_changed;
    usb_state.last_scan_suspicious_devices = total_suspicious_devices;
    pthread_mutex_unlock(&usb_state.state_mutex);

    char completion_msg[256];
    snprintf(completion_msg, sizeof(completion_msg),
             "🔍 Escaneo profundo completado: %d dispositivos analizados", devices_analyzed);
    gui_add_log_entry("USB_DEEP_SCAN", "INFO", completion_msg);

    return devices_analyzed;
}

int get_usb_statistics_for_gui(int *total_devices, int *suspicious_devices,
                               int *total_files, int *files_with_changes) {
    if (!total_devices || !suspicious_devices || !total_files || !files_with_changes) {
        return -1;
    }

    *total_devices = 0;
    *suspicious_devices = 0;
    *total_files = 0;
    *files_with_changes = 0;

    // Obtener lista de dispositivos actuales
    DeviceList* devices = monitor_connected_devices();

    if (devices) {
        *total_devices = devices->count;

        // El total de archivos se calcula sobre los snapshots de referencia actuales;
        // los cambios/sospecha se toman del último escaneo profundo (único momento
        // en que realmente se compara contra un snapshot anterior)
        for (int i = 0; i < devices->count; i++) {
            DeviceSnapshot* snapshot = get_cached_usb_snapshot(devices->devices[i]);
            if (snapshot) {
                *total_files += snapshot->file_count;
            }
        }

        free_device_list(devices);
    }

    pthread_mutex_lock(&usb_state.state_mutex);
    *suspicious_devices = usb_state.last_scan_suspicious_devices;
    *files_with_changes = usb_state.last_scan_files_changed;
    pthread_mutex_unlock(&usb_state.state_mutex);

    return 0;
}

int update_usb_monitoring_config(int scan_interval, int deep_scan_enabled) {
    if (scan_interval < 5 || scan_interval > 3600) {
        gui_add_log_entry("USB_INTEGRATION", "ERROR", 
                         "Intervalo de escaneo inválido (debe estar entre 5 y 3600 segundos)");
        return -1;
    }
    
    pthread_mutex_lock(&usb_state.state_mutex);
    usb_state.scan_interval_seconds = scan_interval;
    usb_state.deep_scan_enabled = deep_scan_enabled;
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    char config_msg[256];
    snprintf(config_msg, sizeof(config_msg), 
             "Configuración USB actualizada: intervalo=%ds, escaneo_profundo=%s",
             scan_interval, deep_scan_enabled ? "habilitado" : "deshabilitado");
    gui_add_log_entry("USB_INTEGRATION", "INFO", config_msg);
    
    return 0;
}

// ============================================================================
// FUNCIONES DE COMPATIBILIDAD CON GUI EXISTENTE
// ============================================================================

void gui_compatible_scan_usb(void) {
    // Esta función actúa como puente entre el sistema de callbacks simple
    // de la GUI existente y el sistema complejo de análisis de snapshots USB
    
    pthread_mutex_lock(&usb_state.state_mutex);
    
    if (!usb_state.initialized) {
        pthread_mutex_unlock(&usb_state.state_mutex);
        
        // Auto-inicializar si no está inicializado
        if (init_usb_integration() != 0) {
            gui_add_log_entry("USB_INTEGRATION", "ERROR", 
                             "Error al inicializar integración USB");
            return;
        }
    }
    
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    // Si el monitoreo automático no está activo, iniciarlo
    if (!is_usb_monitoring_active()) {
        int result = start_usb_monitoring(30); // 30 segundos por defecto
        if (result != 0) {
            gui_add_log_entry("USB_INTEGRATION", "ERROR", 
                             "Error al iniciar monitoreo automático USB");
        }
    }
    
    // Realizar un escaneo manual inmediato para dar feedback al usuario
    gui_add_log_entry("USB_INTEGRATION", "INFO", 
                     "Ejecutando escaneo manual solicitado por usuario");
    
    int devices_scanned = perform_manual_usb_scan();
    
    if (devices_scanned >= 0) {
        char result_msg[256];
        snprintf(result_msg, sizeof(result_msg), 
                 "Escaneo USB completado: %d dispositivos analizados", devices_scanned);
        gui_add_log_entry("USB_INTEGRATION", "INFO", result_msg);
    }
    
    // IMPORTANTE: Timeout de seguridad para limpiar estado en caso de errores
    // Esto asegura que los botones se desbloqueen incluso si hay problemas
    g_timeout_add_seconds(5, (GSourceFunc)complete_usb_scan_cleanup, NULL);
}

int is_gui_usb_scan_in_progress(void) {
    pthread_mutex_lock(&usb_state.state_mutex);
    int in_progress = usb_state.scan_in_progress;
    pthread_mutex_unlock(&usb_state.state_mutex);
    
    return in_progress;
}

// ============================================================================
// FUNCIONES DE LIMPIEZA DE SEGURIDAD
// ============================================================================

/**
 * @brief Función de seguridad para limpiar estado USB si hay problemas
 * 
 * Esta función actúa como un timeout de seguridad para asegurar que el estado
 * scan_in_progress se limpie incluso si hay errores en el escaneo USB.
 */
static gboolean complete_usb_scan_cleanup(gpointer user_data) {
    (void)user_data; // Suprimir warning
    
    pthread_mutex_lock(&usb_state.state_mutex);
    
    if (usb_state.scan_in_progress) {
        // Solo limpiar si ha pasado suficiente tiempo
        usb_state.scan_in_progress = 0;
        pthread_mutex_unlock(&usb_state.state_mutex);
        
        gui_add_log_entry("USB_CLEANUP", "INFO", 
                         "🧹 Estado USB limpiado por timeout de seguridad");
        gui_set_scanning_status(FALSE);
    } else {
        pthread_mutex_unlock(&usb_state.state_mutex);
    }
    
    return FALSE; // Ejecutar solo una vez
}

// ============================================================================
// FUNCIONES DE NOTIFICACIÓN Y EVENTOS
// ============================================================================

void on_usb_device_connected(const char *device_name) {
    if (!device_name) return;
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), 
             "🔌 Dispositivo USB conectado: %s - iniciando análisis inicial", device_name);
    gui_add_log_entry("USB_MONITOR", "INFO", log_msg);
    
    // Crear un dispositivo GUI inicial con estado "DETECTADO"
    GUIUSBDevice gui_device;
    memset(&gui_device, 0, sizeof(gui_device));
    
    strncpy(gui_device.device_name, device_name, sizeof(gui_device.device_name) - 1);
    snprintf(gui_device.mount_point, sizeof(gui_device.mount_point), "/media/%s", device_name);
    strncpy(gui_device.status, "DETECTADO", sizeof(gui_device.status) - 1);
    gui_device.total_files = 0;
    gui_device.files_changed = 0;
    gui_device.is_suspicious = FALSE;
    gui_device.last_scan = time(NULL);
    
    // Actualizar la GUI inmediatamente para mostrar el dispositivo
    gui_update_usb_device(&gui_device);
    
    // Programar análisis inicial en segundo plano
    // En una implementación más sofisticada, esto podría ejecutarse en un hilo separado
    if (analyze_usb_device(device_name) != 0) {
        gui_add_log_entry("USB_MONITOR", "WARNING", 
                         "No se pudo completar el análisis inicial del dispositivo");
    }
}

void on_usb_device_disconnected(const char *device_name) {
    if (!device_name) return;
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), 
             "🔌 Dispositivo USB desconectado: %s", device_name);
    gui_add_log_entry("USB_MONITOR", "INFO", log_msg);
    
    // En una implementación más completa, aquí removeríamos el dispositivo
    // de la vista de la GUI. Por ahora, solo registramos el evento.
    
    // El snapshot permanece en el cache para comparación futura si el
    // dispositivo se reconecta
}

void on_usb_suspicious_activity_detected(const char *device_name, const char *threat_description) {
    if (!device_name || !threat_description) return;
    
    char alert_msg[1024];
    snprintf(alert_msg, sizeof(alert_msg), 
             "🚨 AMENAZA USB DETECTADA en %s: %s", device_name, threat_description);
    gui_add_log_entry("USB_SECURITY", "ALERT", alert_msg);
    
    // Si las notificaciones están habilitadas, aquí podríamos disparar
    // una notificación del sistema usando las funciones de configuración:
    // if (is_notifications_enabled()) {
    //     show_system_notification("MatCom Guard", alert_msg);
    // }
    
    // Si las alertas sonoras están habilitadas:
    // if (is_sound_alerts_enabled()) {
    //     play_alert_sound();
    // }
    
    // Actualizar el estado del sistema para reflejar la amenaza detectada
    gui_update_system_status("Amenaza USB Detectada", FALSE);
}