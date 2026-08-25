#ifndef GUI_USB_INTEGRATION_H
#define GUI_USB_INTEGRATION_H

#include "gui.h"
#include "device_monitor.h"
#include "gui_backend_adapters.h"
#include <pthread.h>

// ============================================================================
// GESTIÓN DEL CICLO DE VIDA DEL MONITOR USB
// ============================================================================

/**
 * @brief Inicializa la integración entre el monitor USB y la GUI
 * 
 * Esta función establece el puente entre el backend de monitoreo de dispositivos
 * USB y la interfaz gráfica. A diferencia del monitor de procesos que trabaja
 * con eventos continuos, el monitor USB trabaja con "snapshots" discretos que
 * requieren comparación temporal para detectar cambios maliciosos.
 * 
 * La inicialización configura:
 * 1. El sistema de cache para mantener snapshots anteriores
 * 2. El hilo de monitoreo que detecta dispositivos conectados/desconectados
 * 3. Las estructuras de datos para rastrear el estado de cada dispositivo
 * 
 * @return int 0 si la inicialización es exitosa, -1 si hay error
 */
int init_usb_integration(void);

/**
 * @brief Inicia el monitoreo automático de dispositivos USB
 * 
 * Esta función inicia un hilo dedicado que monitorea continuamente el
 * directorio /media para detectar dispositivos USB que se conectan o
 * desconectan. Cuando detecta un dispositivo nuevo, automáticamente
 * crea un snapshot inicial de su contenido.
 * 
 * El monitoreo incluye:
 * - Detección de nuevos dispositivos conectados
 * - Creación automática de snapshots iniciales
 * - Programación de re-escaneos periódicos para detectar cambios
 * - Limpieza automática cuando se desconectan dispositivos
 * 
 * @param scan_interval_seconds Intervalo entre escaneos en segundos
 * @return int 0 si el monitoreo se inició correctamente, -1 si error
 */
int start_usb_monitoring(int scan_interval_seconds);

/**
 * @brief Realiza un escaneo manual de todos los dispositivos USB conectados
 * 
 * Esta función es llamada cuando el usuario presiona el botón "Escanear USB"
 * en la interfaz. Realiza un escaneo completo de todos los dispositivos
 * conectados, comparándolos con sus snapshots anteriores para detectar cambios.
 * 
 * El proceso incluye:
 * 1. Detectar todos los dispositivos USB conectados actualmente
 * 2. Para cada dispositivo, crear un nuevo snapshot
 * 3. Comparar con el snapshot anterior (si existe)
 * 4. Actualizar la GUI con los resultados del análisis
 * 
 * @return int Número de dispositivos escaneados, -1 si error
 */
int perform_manual_usb_scan(void);

/**
 * @brief Detiene el monitoreo automático de dispositivos USB
 * 
 * @return int 0 si se detuvo correctamente, -1 si error
 */
int stop_usb_monitoring(void);

/**
 * @brief Verifica si el monitoreo USB está actualmente activo
 * 
 * @return int 1 si está activo, 0 si no está activo
 */
int is_usb_monitoring_active(void);

/**
 * @brief Limpia recursos y finaliza la integración USB
 */
void cleanup_usb_integration(void);

// ============================================================================
// FUNCIONES DE ANÁLISIS DE DISPOSITIVOS ESPECÍFICOS
// ============================================================================

/**
 * @brief Analiza un dispositivo USB específico y actualiza la GUI
 * 
 * Esta función encapsula todo el proceso de análisis de un dispositivo:
 * desde la creación del snapshot hasta la actualización de la interfaz.
 * Es la función central que coordina todo el análisis de un dispositivo.
 * 
 * El proceso que ejecuta es:
 * 1. Crear un snapshot completo del dispositivo (análisis de todos los archivos)
 * 2. Recuperar el snapshot anterior del cache (si existe)
 * 3. Comparar ambos snapshots para detectar cambios
 * 4. Evaluar si los cambios indican actividad sospechosa
 * 5. Convertir los resultados a formato GUI y actualizar la interfaz
 * 6. Almacenar el nuevo snapshot para futuras comparaciones
 * 
 * @param device_name Nombre del dispositivo a analizar (ej: "USB_DRIVE")
 * @return int 0 si el análisis fue exitoso, -1 si error
 */
int analyze_usb_device(const char *device_name);

/**
 * @brief Realiza un escaneo profundo de un dispositivo específico
 * 
 * Esta función implementa el análisis más detallado posible de un dispositivo,
 * incluyendo verificación de hashes SHA-256 de todos los archivos. Es más
 * lenta que el escaneo regular pero proporciona máxima precisión para
 * detectar modificaciones sutiles que podrían indicar malware.
 * 
 * @param device_name Nombre del dispositivo para escaneo profundo
 * @return int 0 si el escaneo fue exitoso, -1 si error
 */
int perform_deep_usb_scan(const char *device_name);

// ============================================================================
// FUNCIONES ESPECÍFICAS PARA BOTONES DIFERENCIADOS
// ============================================================================

/**
 * @brief Actualiza snapshots de dispositivos USB (función del botón "Actualizar")
 * 
 * Esta función es ejecutada cuando el usuario presiona el botón "Actualizar".
 * Su objetivo es retomar snapshots frescos de todos los dispositivos conectados
 * sin hacer comparaciones. Es útil cuando el usuario quiere establecer un nuevo
 * punto de referencia después de hacer cambios legítimos en los dispositivos.
 * 
 * Funcionalidad:
 * 1. Detecta todos los dispositivos USB conectados
 * 2. Crea nuevos snapshots sin comparar con versiones anteriores
 * 3. Almacena estos snapshots como nueva línea base
 * 4. Actualiza la GUI con el estado "ACTUALIZADO"
 * 
 * @return int Número de dispositivos actualizados, -1 si error
 */
int refresh_usb_snapshots(void);

/**
 * @brief Realiza análisis comparativo de dispositivos USB (función del botón "Escaneo Profundo")
 * 
 * Esta función es ejecutada cuando el usuario presiona el botón "Escaneo Profundo".
 * Su objetivo es comparar el estado actual de los dispositivos con los snapshots
 * almacenados para detectar cambios sospechosos sin alterar dichos snapshots.
 * 
 * Funcionalidad:
 * 1. Crea snapshots temporales de todos los dispositivos conectados
 * 2. Los compara con los snapshots de referencia almacenados
 * 3. Evalúa si los cambios son sospechosos
 * 4. Actualiza la GUI con resultados del análisis
 * 5. NO almacena los nuevos snapshots (mantiene línea base intacta)
 * 
 * @return int Número de dispositivos analizados, -1 si error
 */
int deep_scan_usb_devices(void);

// ============================================================================
// GESTIÓN DE ESTADO Y SINCRONIZACIÓN
// ============================================================================

/**
 * @brief Sincroniza la vista GUI con el estado actual de dispositivos USB
 * 
 * Esta función es útil cuando la GUI necesita refrescar completamente su
 * vista de dispositivos USB, por ejemplo cuando el usuario cambia de pestaña
 * o cuando se recupera de un error.
 * 
 * @return int Número de dispositivos sincronizados, -1 si error
 */
int sync_gui_with_usb_devices(void);

/**
 * @brief Obtiene estadísticas de dispositivos USB para el panel principal
 * 
 * Esta función recopila estadísticas agregadas de todos los dispositivos
 * USB monitoreados para mostrar en el panel de estadísticas principal
 * de la aplicación.
 * 
 * @param total_devices Puntero donde almacenar el total de dispositivos
 * @param suspicious_devices Puntero donde almacenar dispositivos sospechosos
 * @param total_files Puntero donde almacenar el total de archivos monitoreados
 * @param files_with_changes Puntero donde almacenar archivos con cambios en el último escaneo profundo
 * @return int 0 si es exitoso, -1 si error
 */
int get_usb_statistics_for_gui(int *total_devices, int *suspicious_devices,
                               int *total_files, int *files_with_changes);

/**
 * @brief Actualiza la configuración del monitoreo USB desde la GUI
 * 
 * Permite que los cambios de configuración realizados en el diálogo de
 * configuración de la GUI se apliquen al sistema de monitoreo USB sin
 * necesidad de reiniciar.
 * 
 * @param scan_interval Nuevo intervalo de escaneo en segundos
 * @param deep_scan_enabled Si está habilitado el escaneo profundo automático
 * @return int 0 si es exitoso, -1 si error
 */
int update_usb_monitoring_config(int scan_interval, int deep_scan_enabled);

// ============================================================================
// FUNCIONES DE COMPATIBILIDAD CON GUI EXISTENTE
// ============================================================================

/**
 * @brief Función adaptadora para el callback ScanUSBCallback de la GUI
 * 
 * Esta función permite que el sistema de callbacks existente de la GUI
 * funcione con el nuevo backend USB sin modificar el código de la interfaz.
 * Actúa como un wrapper que traduce las llamadas simples de la GUI
 * a las operaciones complejas de análisis de snapshots.
 * 
 * Cuando se llama esta función:
 * 1. Si el monitoreo no está activo, lo inicia automáticamente
 * 2. Realiza un escaneo manual de todos los dispositivos conectados
 * 3. Actualiza las estadísticas en la GUI
 * 4. Registra la actividad en el log del sistema
 */
void gui_compatible_scan_usb(void);

/**
 * @brief Verifica si hay un escaneo USB en progreso
 * 
 * Esta función complementa el sistema de threading existente de la GUI
 * permitiendo que los botones se deshabiliten correctamente durante
 * las operaciones de escaneo USB que pueden ser lentas.
 * 
 * @return int 1 si hay escaneo en progreso, 0 si no
 */
int is_gui_usb_scan_in_progress(void);

// ============================================================================
// FUNCIONES DE NOTIFICACIÓN Y EVENTOS
// ============================================================================

/**
 * @brief Callback ejecutado cuando se detecta un dispositivo USB nuevo
 * 
 * Esta función se ejecuta automáticamente cuando el sistema de monitoreo
 * detecta que se ha conectado un nuevo dispositivo USB. Inicia el proceso
 * de análisis inicial y actualiza la GUI para mostrar el nuevo dispositivo.
 * 
 * @param device_name Nombre del dispositivo recién conectado
 */
void on_usb_device_connected(const char *device_name);

/**
 * @brief Callback ejecutado cuando se desconecta un dispositivo USB
 * 
 * Esta función limpia el estado asociado con un dispositivo que se ha
 * desconectado, incluyendo la remoción de sus snapshots del cache y
 * la actualización de la GUI.
 * 
 * @param device_name Nombre del dispositivo desconectado
 */
void on_usb_device_disconnected(const char *device_name);

/**
 * @brief Callback ejecutado cuando se detecta actividad sospechosa
 * 
 * Esta función se ejecuta cuando el análisis de un dispositivo USB
 * detecta patrones que podrían indicar actividad maliciosa, como
 * modificación masiva de archivos o inyección de contenido sospechoso.
 * 
 * @param device_name Nombre del dispositivo con actividad sospechosa
 * @param threat_description Descripción del tipo de amenaza detectada
 */
void on_usb_suspicious_activity_detected(const char *device_name, const char *threat_description);

#endif // GUI_USB_INTEGRATION_H