#ifndef GUI_PROCESS_INTEGRATION_H
#define GUI_PROCESS_INTEGRATION_H

#include "gui.h"
#include "process_monitor.h"
#include "gui_backend_adapters.h"

// ============================================================================
// GESTIÓN DEL CICLO DE VIDA DEL MONITOR DE PROCESOS
// ============================================================================

/**
 * @brief Inicializa la integración entre el monitor de procesos y la GUI
 * 
 * Esta función establece la conexión entre el backend de monitoreo de procesos
 * y la interfaz gráfica. Configura los callbacks necesarios y carga la 
 * configuración inicial del sistema.
 * 
 * Internamente, esta función:
 * 1. Carga la configuración del backend desde /etc/matcomguard.conf
 * 2. Registra los callbacks que conectan eventos del backend con la GUI
 * 3. Inicializa las estructuras de datos necesarias para la sincronización
 * 
 * @return int 0 si la inicialización es exitosa, -1 si hay error
 */
int init_process_integration(void);

/**
 * @brief Inicia el monitoreo automático de procesos en segundo plano
 * 
 * Esta función actúa como puente entre el botón "Escanear Procesos" de la GUI
 * y el sistema de monitoreo continuo del backend. Cuando la GUI solicita un
 * escaneo, esta función inicia el hilo de monitoreo del backend si no está
 * ya corriendo.
 * 
 * @return int 0 si el monitoreo se inició correctamente, 1 si ya estaba activo, -1 si error
 */
int start_process_monitoring(void);

/**
 * @brief Detiene el monitoreo de procesos de forma segura
 * 
 * Esta función asegura que el hilo de monitoreo del backend se detenga
 * ordenadamente y que todos los recursos sean liberados correctamente.
 * 
 * @return int 0 si se detuvo correctamente, 1 si no estaba activo, -1 si error
 */
int stop_process_monitoring(void);

/**
 * @brief Verifica si el monitoreo de procesos está actualmente activo
 * 
 * @return int 1 si está activo, 0 si no está activo
 */
int is_process_monitoring_active(void);

/**
 * @brief Limpia recursos y finaliza la integración de procesos
 * 
 * Esta función debe llamarse al cerrar la aplicación para asegurar
 * que todos los recursos del backend sean liberados correctamente.
 */
void cleanup_process_integration(void);

// ============================================================================
// INTERFAZ DE CALLBACKS PARA LA GUI
// ============================================================================

/**
 * @brief Función callback que se ejecuta cuando se detecta un proceso nuevo
 * 
 * Esta función actúa como puente entre el evento "on_new_process" del backend
 * y la función "gui_update_process" del frontend. Convierte la estructura
 * ProcessInfo del backend en una GUIProcess y actualiza la interfaz gráfica.
 * 
 * @param info Información del nuevo proceso detectado por el backend
 */
void on_gui_process_new(ProcessInfo *info);

/**
 * @brief Función callback que se ejecuta cuando un proceso termina
 * 
 * Esta función maneja la limpieza en la GUI cuando un proceso que estaba
 * siendo monitoreado termina su ejecución.
 * 
 * @param pid PID del proceso que terminó
 * @param name Nombre del proceso que terminó
 */
void on_gui_process_terminated(pid_t pid, const char *name);

/**
 * @brief Función callback para alertas de alto uso de CPU
 * 
 * Se ejecuta cuando un proceso excede los umbrales de CPU configurados.
 * Esta función no solo actualiza la GUI sino que también puede disparar
 * notificaciones visuales o sonoras si están habilitadas.
 * 
 * @param info Información del proceso que causó la alerta
 */
void on_gui_high_cpu_alert(ProcessInfo *info);

/**
 * @brief Función callback para alertas de alto uso de memoria
 * 
 * Se ejecuta cuando un proceso excede los umbrales de memoria configurados.
 * 
 * @param info Información del proceso que causó la alerta
 */
void on_gui_high_memory_alert(ProcessInfo *info);

/**
 * @brief Función callback cuando una alerta se despeja
 * 
 * Se ejecuta cuando un proceso que tenía una alerta activa vuelve a
 * valores normales de uso de recursos.
 * 
 * @param info Información del proceso cuya alerta se despejó
 */
void on_gui_alert_cleared(ProcessInfo *info);

// ============================================================================
// FUNCIONES DE SINCRONIZACIÓN Y ESTADO
// ============================================================================

/**
 * @brief Sincroniza la vista de la GUI con el estado actual del backend
 * 
 * Esta función es útil cuando la GUI necesita refrescar su vista con todos
 * los procesos que está monitoreando actualmente el backend. Por ejemplo,
 * cuando el usuario cambia de pestaña o cuando se inicia la aplicación.
 * 
 * @return int Número de procesos sincronizados, -1 si error
 */
int sync_gui_with_backend_processes(void);

/**
 * @brief Obtiene estadísticas del sistema de monitoreo para la GUI
 * 
 * Esta función actúa como adaptador entre las estadísticas detalladas
 * del backend y los números simples que necesita la GUI para mostrar
 * en el panel de estadísticas principales.
 * 
 * @param total_processes Puntero donde almacenar el total de procesos monitoreados
 * @param high_cpu_count Puntero donde almacenar el número de procesos con alta CPU
 * @param high_memory_count Puntero donde almacenar el número de procesos con alta memoria
 * @param suspicious_count Puntero donde almacenar el número de procesos con alerta activa
 * @return int 0 si es exitoso, -1 si error
 */
int get_process_statistics_for_gui(int *total_processes, int *high_cpu_count,
                                   int *high_memory_count, int *suspicious_count);

/**
 * @brief Actualiza la configuración del monitor de procesos desde la GUI
 * 
 * Cuando el usuario cambia configuraciones como umbrales de CPU o memoria
 * desde el diálogo de configuración de la GUI, esta función aplica esos
 * cambios al backend sin necesidad de reiniciar el monitoreo.
 * 
 * @param cpu_threshold Nuevo umbral de CPU (porcentaje)
 * @param memory_threshold Nuevo umbral de memoria (porcentaje)
 * @param check_interval Nuevo intervalo de verificación (segundos)
 * @return int 0 si es exitoso, -1 si error
 */
int update_process_monitoring_config(float cpu_threshold, float memory_threshold, int check_interval);

// ============================================================================
// FUNCIONES DE COMPATIBILIDAD CON GUI EXISTENTE
// ============================================================================

/**
 * @brief Función adaptadora para el callback ScanProcessesCallback de la GUI
 *
 * Esta función permite que el sistema de callbacks existente de la GUI
 * funcione con el nuevo backend sin modificar el código de la interfaz.
 * Actúa como un wrapper que traduce las llamadas simples de la GUI
 * a las operaciones más complejas del backend.
 */
void gui_compatible_scan_processes(void);

/**
 * @brief Verifica si hay un escaneo de procesos en progreso
 * 
 * Esta función complementa el sistema de threading existente de la GUI
 * permitiendo que los botones se deshabiliten correctamente durante
 * las operaciones de escaneo.
 * 
 * @return int 1 si hay escaneo en progreso, 0 si no
 */
int is_gui_process_scan_in_progress(void);

#endif // GUI_PROCESS_INTEGRATION_H