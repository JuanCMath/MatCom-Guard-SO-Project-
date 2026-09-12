/**
 * @file gui_backend_adapters.c
 * @brief Adaptadores entre el backend y la GUI de MatCom Guard
 * 
 * Este archivo contiene las funciones que actúan como puente entre las estructuras
 * de datos del backend (ProcessInfo, DeviceSnapshot, PortInfo) y las estructuras
 * simplificadas que maneja la interfaz gráfica (GUIProcess, GUIUSBDevice, GUIPort).
 * 
 * MEJORAS IMPLEMENTADAS EN EXPORTACIÓN DE REPORTES:
 * - Filtrado automático de emojis y caracteres especiales para compatibilidad PDF
 * - Ajuste automático de líneas largas con división inteligente de palabras
 * - Paginación mejorada con control de límites de página
 * - Conversión automática de caracteres acentuados a ASCII
 * - Soporte robusto para exportación tanto en PDF como TXT
 * 
 * @author MatCom Guard Team #4
 * @date 2025-06-23
 * @version 2.0 - Versión mejorada con sistema de exportación avanzado
 */

#define _GNU_SOURCE  // Para strnlen y funciones GNU adicionales
#include "gui_backend_adapters.h"
#include "gui_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <cairo.h>      // Librería para generación de PDFs
#include <cairo-pdf.h>  // Extensión específica para PDFs

// ============================================================================
// CACHE DE SNAPSHOTS USB
// ============================================================================

// Estructura para mantener cache de snapshots USB
typedef struct USBSnapshotCache {
    char device_name[256];
    DeviceSnapshot *snapshot;
    struct USBSnapshotCache *next;
} USBSnapshotCache;

static USBSnapshotCache *cache_head = NULL;
// Protege la lista enlazada del cache: el escaneo USB corre en su propio
// hilo mientras el coordinador la consulta desde otro, sin este mutex la
// lista puede corromperse por escrituras/lecturas concurrentes.
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// ADAPTADORES DE ESTRUCTURAS DE DATOS
// ============================================================================

int adapt_process_info_to_gui(const ProcessInfo *backend_info, GUIProcess *gui_process) {
    if (!backend_info || !gui_process) {
        return -1;
    }
    
    // Copia directa de campos compatibles
    gui_process->pid = backend_info->pid;
    strncpy(gui_process->name, backend_info->name, sizeof(gui_process->name) - 1);
    gui_process->name[sizeof(gui_process->name) - 1] = '\0';
    gui_process->cpu_usage = backend_info->cpu_usage;
    gui_process->mem_usage = backend_info->mem_usage;
    
    // MODIFICACIÓN PRINCIPAL: Añadido soporte para campo is_whitelisted
    // Este campo permite que la GUI muestre correctamente el estado de procesos whitelisted
    // y evite generar alertas o warnings para estos procesos.
    gui_process->is_whitelisted = backend_info->is_whitelisted;  
    
    // CAMBIO: Lógica mejorada para determinar procesos sospechosos
    // Ahora considera el estado de whitelist para evitar falsos positivos
    gui_process->is_suspicious = FALSE;
    
    // Si el proceso está whitelisted, NUNCA debe marcarse como sospechoso
    if (backend_info->is_whitelisted) {
        gui_process->is_suspicious = FALSE;
    } else {
        // Solo para procesos NO whitelisted, aplicar detección de comportamiento sospechoso
        if (backend_info->alerta_activa) {
            gui_process->is_suspicious = TRUE;
        } else if (backend_info->exceeds_thresholds) {
            gui_process->is_suspicious = TRUE;
        } else if (backend_info->cpu_usage > 95.0 || backend_info->mem_usage > 90.0) {
            gui_process->is_suspicious = TRUE;
        }
    }
    
    return 0;
}

int adapt_device_snapshot_to_gui(const DeviceSnapshot *snapshot,                                 const DeviceSnapshot *previous_snapshot,
                                 GUIUSBDevice *gui_device) {
    if (!snapshot || !gui_device) {
        fprintf(stderr, "Error: snapshot o gui_device es NULL\n");
        return -1;
    }
    
    // Inicializar estructura GUI
    memset(gui_device, 0, sizeof(GUIUSBDevice));
    
    // Validar que device_name no sea NULL antes de usarlo
    if (!snapshot->device_name) {
        fprintf(stderr, "Error: snapshot->device_name es NULL\n");
        return -1;
    }

    // Validar que device_name sea una cadena válida (máximo 255 caracteres)
    size_t name_len = strnlen(snapshot->device_name, 256);
    if (name_len >= 256) {
        fprintf(stderr, "Error: snapshot->device_name demasiado largo o inválido\n");
        return -1;
    }
    
    printf("Device name: %s (length: %zu)\n", snapshot->device_name, name_len);
    
    // Copiar nombre del dispositivo de forma segura
    strncpy(gui_device->device_name, snapshot->device_name, sizeof(gui_device->device_name)-1);
    gui_device->device_name[sizeof(gui_device->device_name)-1] = '\0'; // Asegurar terminación

    printf("GUI device name: %s\n", gui_device->device_name);
    
    // Generar punto de montaje basado en el nombre del dispositivo
    snprintf(gui_device->mount_point, sizeof(gui_device->mount_point),
             "/media/%s", snapshot->device_name);
    
    // Establecer total de archivos
    gui_device->total_files = snapshot->file_count;
    
    // Establecer tiempo del último escaneo
    gui_device->last_scan = snapshot->snapshot_time;
    
    // Detectar cambios si hay snapshot anterior
    int files_added = 0, files_modified = 0, files_deleted = 0;
    
    if (previous_snapshot) {
        detect_usb_changes(previous_snapshot, snapshot, 
                          &files_added, &files_modified, &files_deleted);
        gui_device->files_changed = files_added + files_modified + files_deleted;
    } else {
        // Primera vez que se escanea el dispositivo
        gui_device->files_changed = 0;
    }
    
    // Evaluar si el dispositivo es sospechoso
    gui_device->is_suspicious = evaluate_usb_suspicion(files_added, files_modified,
                                                       files_deleted, snapshot->file_count);
    
    // Generar cadena de estado
    generate_usb_status_string(gui_device->files_changed, gui_device->is_suspicious,
                              FALSE, gui_device->status, sizeof(gui_device->status));
    
    return 0;
}

int adapt_port_info_to_gui(const PortInfo *backend_port, GUIPort *gui_port) {
    if (!backend_port || !gui_port) {
        return -1;
    }
    
    // Copia directa de campos compatibles
    gui_port->port = backend_port->port;
    gui_port->is_suspicious = backend_port->is_suspicious;
    
    // Copiar nombre del servicio
    strncpy(gui_port->service, backend_port->service_name, 
            sizeof(gui_port->service) - 1);
    gui_port->service[sizeof(gui_port->service) - 1] = '\0';
    
    // Convertir estado binario a cadena descriptiva
    generate_port_status_string(backend_port->is_open, 
                               gui_port->status, sizeof(gui_port->status));
    
    return 0;
}

int aggregate_port_statistics(const PortInfo *ports, int port_count,
                             int *total_open, int *total_suspicious) {
    if (!ports || !total_open || !total_suspicious) {
        return -1;
    }
    
    *total_open = 0;
    *total_suspicious = 0;
    
    for (int i = 0; i < port_count; i++) {
        if (ports[i].is_open) {
            (*total_open)++;
        }
        if (ports[i].is_suspicious) {
            (*total_suspicious)++;
        }
    }
    
    return 0;
}

// ============================================================================
// FUNCIONES DE DETECCIÓN DE CAMBIOS
// ============================================================================

int detect_usb_changes(const DeviceSnapshot *old_snapshot,
                      const DeviceSnapshot *new_snapshot,
                      int *files_added, int *files_modified, int *files_deleted) {
    if (!old_snapshot || !new_snapshot || !files_added || !files_modified || !files_deleted) {
        return -1;
    }
    
    *files_added = 0;
    *files_modified = 0;
    *files_deleted = 0;
    
    // Buscar archivos nuevos y modificados
    for (int i = 0; i < new_snapshot->file_count; i++) {
        FileInfo *new_file = new_snapshot->files[i];
        int found = 0;
        
        // Buscar este archivo en el snapshot anterior
        for (int j = 0; j < old_snapshot->file_count; j++) {
            FileInfo *old_file = old_snapshot->files[j];
            
            // Comparar por ruta completa
            if (strcmp(new_file->path, old_file->path) == 0) {
                found = 1;
                
                // Verificar si fue modificado comparando hash SHA-256
                if (strcmp(new_file->sha256_hash, old_file->sha256_hash) != 0) {
                    (*files_modified)++;
                }
                break;
            }
        }
        
        // Si no se encontró en el snapshot anterior, es un archivo nuevo
        if (!found) {
            (*files_added)++;
        }
    }
    
    // Buscar archivos eliminados (presentes en old pero no en new)
    for (int i = 0; i < old_snapshot->file_count; i++) {
        FileInfo *old_file = old_snapshot->files[i];
        int found = 0;
        
        for (int j = 0; j < new_snapshot->file_count; j++) {
            FileInfo *new_file = new_snapshot->files[j];
            
            if (strcmp(old_file->path, new_file->path) == 0) {
                found = 1;
                break;
            }
        }
        
        if (!found) {
            (*files_deleted)++;
        }
    }
    
    return 0;
}

gboolean evaluate_usb_suspicion(int files_added, int files_modified, 
                                int files_deleted, int total_files) {
    // Heurísticas para determinar comportamiento sospechoso
    
    // Criterio 1: Muchos archivos eliminados (posible ataque de cifrado/borrado)
    if (files_deleted > (total_files * 0.1)) {  // Más del 10% eliminados
        return TRUE;
    }
    
    // Criterio 2: Muchos archivos modificados (posible cifrado masivo)
    if (files_modified > (total_files * 0.2)) {  // Más del 20% modificados
        return TRUE;
    }
    
    // Criterio 3: Actividad muy alta de cambios
    int total_changes = files_added + files_modified + files_deleted;
    if (total_changes > (total_files * 0.3)) {  // Más del 30% de actividad
        return TRUE;
    }
    
    // Criterio 4: Muchos archivos nuevos en dispositivo pequeño (posible inyección)
    if (total_files < 50 && files_added > 10) {
        return TRUE;
    }
    
    return FALSE;
}

// ============================================================================
// GESTIÓN DE ESTADO Y CACHE
// ============================================================================

int init_usb_snapshot_cache(void) {
    // El cache se inicializa vacío (cache_head = NULL ya establecido)
    return 0;
}

int store_usb_snapshot(const char *device_name, const DeviceSnapshot *snapshot) {
    if (!device_name || !snapshot) {
        return -1;
    }

    pthread_mutex_lock(&cache_mutex);

    // Buscar si ya existe una entrada para este dispositivo
    USBSnapshotCache *current = cache_head;
    while (current) {
        if (strcmp(current->device_name, device_name) == 0) {
            // Reemplazar snapshot existente
            if (current->snapshot) {
                free_device_snapshot(current->snapshot);
            }
            current->snapshot = (DeviceSnapshot*)snapshot;  // Cast para eliminar const
            pthread_mutex_unlock(&cache_mutex);
            return 0;
        }
        current = current->next;
    }

    // Crear nueva entrada en el cache
    USBSnapshotCache *new_entry = malloc(sizeof(USBSnapshotCache));
    if (!new_entry) {
        pthread_mutex_unlock(&cache_mutex);
        return -1;
    }

    strncpy(new_entry->device_name, device_name, sizeof(new_entry->device_name) - 1);
    new_entry->device_name[sizeof(new_entry->device_name) - 1] = '\0';
    new_entry->snapshot = (DeviceSnapshot*)snapshot;  // Cast para eliminar const
    new_entry->next = cache_head;
    cache_head = new_entry;

    pthread_mutex_unlock(&cache_mutex);
    return 0;
}

DeviceSnapshot* get_cached_usb_snapshot(const char *device_name) {
    if (!device_name) {
        return NULL;
    }

    pthread_mutex_lock(&cache_mutex);

    USBSnapshotCache *current = cache_head;
    while (current) {
        if (strcmp(current->device_name, device_name) == 0) {
            DeviceSnapshot *snapshot = current->snapshot;
            pthread_mutex_unlock(&cache_mutex);
            return snapshot;
        }
        current = current->next;
    }

    pthread_mutex_unlock(&cache_mutex);
    return NULL;
}

void cleanup_usb_snapshot_cache(void) {
    pthread_mutex_lock(&cache_mutex);

    USBSnapshotCache *current = cache_head;
    while (current) {
        USBSnapshotCache *next = current->next;
        if (current->snapshot) {
            free_device_snapshot(current->snapshot);
        }
        free(current);
        current = next;
    }
    cache_head = NULL;

    pthread_mutex_unlock(&cache_mutex);
}

// ============================================================================
// UTILIDADES DE CONVERSIÓN
// ============================================================================

int format_timestamp_for_gui(time_t timestamp, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
    if (timestamp == 0) {
        strncpy(buffer, "Nunca", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return 0;
    }
    
    time_t now = time(NULL);
    int diff = (int)(now - timestamp);
    
    if (diff < 60) {
        snprintf(buffer, buffer_size, "Hace %d seg", diff);
    } else if (diff < 3600) {
        snprintf(buffer, buffer_size, "Hace %d min", diff / 60);
    } else if (diff < 86400) {
        snprintf(buffer, buffer_size, "Hace %d horas", diff / 3600);
    } else {
        snprintf(buffer, buffer_size, "Hace %d días", diff / 86400);
    }
    
    return 0;
}

int generate_usb_status_string(int files_changed, gboolean is_suspicious, 
                              gboolean is_scanning, char *status_buffer, 
                              size_t buffer_size) {
    if (!status_buffer || buffer_size == 0) {
        return -1;
    }
    
    if (is_scanning) {
        strncpy(status_buffer, "ESCANEANDO", buffer_size - 1);
    } else if (is_suspicious) {
        strncpy(status_buffer, "SOSPECHOSO", buffer_size - 1);
    } else if (files_changed > 0) {
        strncpy(status_buffer, "CAMBIOS DETECTADOS", buffer_size - 1);
    } else {
        strncpy(status_buffer, "LIMPIO", buffer_size - 1);
    }
    
    status_buffer[buffer_size - 1] = '\0';
    return 0;
}

int generate_port_status_string(int is_open, char *status_buffer, size_t buffer_size) {
    if (!status_buffer || buffer_size == 0) {
        return -1;
    }
    
    if (is_open) {
        strncpy(status_buffer, "Abierto", buffer_size - 1);
    } else {
        strncpy(status_buffer, "Cerrado", buffer_size - 1);
    }
    
    status_buffer[buffer_size - 1] = '\0';
    return 0;
}

// ============================================================================
// EXPORTACIÓN DE LOGS Y REPORTES
// ============================================================================

// Función para obtener el contenido actual del log
static char* get_log_content() {
    extern GtkTextBuffer *log_buffer;
    if (!log_buffer) {
        return NULL;
    }
    
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(log_buffer, &start);
    gtk_text_buffer_get_end_iter(log_buffer, &end);
    
    return gtk_text_buffer_get_text(log_buffer, &start, &end, FALSE);
}

// Función para generar estadísticas del sistema
static void generate_system_stats(char *stats_buffer, size_t buffer_size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Obtener estadísticas de las variables globales del sistema
    extern GtkWidget *stats_usb_count;
    extern GtkWidget *stats_process_count;
    extern GtkWidget *stats_ports_open;
    extern GtkWidget *stats_system_status;
    
    const char *usb_count = "N/A";
    const char *process_count = "N/A";
    const char *ports_count = "N/A";
    const char *system_status = "N/A";
    
    if (stats_usb_count) {
        usb_count = gtk_label_get_text(GTK_LABEL(stats_usb_count));
    }
    if (stats_process_count) {
        process_count = gtk_label_get_text(GTK_LABEL(stats_process_count));
    }
    if (stats_ports_open) {
        ports_count = gtk_label_get_text(GTK_LABEL(stats_ports_open));
    }
    if (stats_system_status) {
        system_status = gtk_label_get_text(GTK_LABEL(stats_system_status));
    }
    
    snprintf(stats_buffer, buffer_size,
        "=== REPORTE DE SEGURIDAD MATCOM GUARD ===\n"
        "Fecha y Hora: %s\n"
        "Estado del Sistema: %s\n"
        "Dispositivos USB Conectados: %s\n"
        "Procesos Monitoreados: %s\n"
        "Puertos Abiertos: %s\n"
        "Generado por: MatCom Guard v1.0\n\n",
        timestamp,
        system_status ? system_status : "Desconocido",
        usb_count ? usb_count : "0",
        process_count ? process_count : "0", 
        ports_count ? ports_count : "0"    );
}

/**
 * @brief Función principal de exportación de reportes de seguridad
 * 
 * FUNCIONALIDAD MEJORADA (v2.0):
 * Esta función ha sido completamente rediseñada para solucionar los problemas
 * de exportación identificados por el usuario:
 * 
 * PROBLEMAS RESUELTOS:
 * 1. Líneas largas que no cabían en la página del PDF
 * 2. Emojis que causaban errores en la generación de PDFs
 * 3. Pérdida de información por recorte incorrecto
 * 4. Formato inconsistente entre PDF y TXT
 * 
 * MEJORAS IMPLEMENTADAS:
 * - Filtrado automático de emojis y caracteres especiales
 * - División inteligente de líneas largas respetando palabras completas
 * - Paginación automática cuando se alcanza el límite de página
 * - Conversión de caracteres acentuados a ASCII para compatibilidad
 * - Uso de buffers estáticos para evitar problemas de memoria
 * - Mejor control de errores y validación de entrada
 * 
 * @param filename Ruta del archivo a generar (.pdf o .txt)
 * 
 * FORMATOS SOPORTADOS:
 * - .pdf: Genera PDF usando Cairo con formato profesional
 * - .txt: Genera archivo de texto plano filtrado
 * 
 * @note Esta función reemplaza la implementación anterior que tenía
 *       problemas con el manejo de texto largo y caracteres especiales
 */
void gui_export_report_to_pdf(const char *filename) {
    if (!filename) {
        printf("Error: Nombre de archivo no válido para exportar PDF\n");
        return;
    }
    
    // Verificar extensión del archivo para determinar formato de salida
    const char *ext = strrchr(filename, '.');
    if (!ext || (strcmp(ext, ".pdf") != 0 && strcmp(ext, ".txt") != 0)) {
        printf("Error: Formato de archivo no soportado. Use .pdf o .txt\n");
        return;
    }
    
    char *log_content = get_log_content();
    if (!log_content) {
        printf("Error: No se pudo obtener el contenido del log\n");
        return;
    }
    
    // Generar estadísticas del sistema
    char stats_buffer[2048];
    generate_system_stats(stats_buffer, sizeof(stats_buffer));    if (strcmp(ext, ".txt") == 0) {
        // ================================================================
        // EXPORTACIÓN TXT MEJORADA
        // ================================================================
        // MEJORA: Ahora incluye filtrado de emojis también en TXT
        // para mantener consistencia entre formatos y evitar problemas
        // con editores de texto que no soportan UTF-8 completo
        
        FILE *file = fopen(filename, "w");
        if (!file) {
            printf("Error: No se pudo crear el archivo %s\n", filename);
            g_free(log_content);
            return;
        }
        
        // Usar buffers estáticos en lugar de malloc para mayor estabilidad
        // Tamaños calculados para manejar logs grandes sin problemas de memoria
        char filtered_stats[4096];   // Buffer para estadísticas filtradas
        char filtered_log[32768];    // Buffer para log filtrado (32KB)
        
        // Aplicar filtrado de caracteres especiales y emojis
        filter_emoji_and_special_chars(stats_buffer, filtered_stats, sizeof(filtered_stats));
        filter_emoji_and_special_chars(log_content, filtered_log, sizeof(filtered_log));
        
        // Escribir contenido filtrado al archivo
        fprintf(file, "%s", filtered_stats);
        fprintf(file, "=== LOG DE EVENTOS ===\n\n");
        fprintf(file, "%s", filtered_log);
        
        fclose(file);
        printf("✅ Reporte exportado exitosamente a: %s\n", filename);
        
    } else {
        // ================================================================
        // EXPORTACIÓN PDF AVANZADA CON CAIRO
        // ================================================================
        // Implementación completamente rediseñada para
        // solucionar todos los problemas reportados por el usuario
        
        // Crear superficie PDF con dimensiones A4 estándar (595x842 puntos)
        cairo_surface_t *surface = cairo_pdf_surface_create(filename, 595, 842);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            printf("Error: No se pudo crear la superficie PDF\n");
            cairo_surface_destroy(surface);  // Incluso en fallo, Cairo devuelve un objeto "nil" que hay que liberar
            g_free(log_content);
            return;
        }
        // Crear contexto de dibujo Cairo con validación de errores
        cairo_t *cr = cairo_create(surface);
        if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
            printf("Error: No se pudo crear el contexto Cairo\n");
            cairo_destroy(cr);  // Liberar contexto incluso si falló
            cairo_surface_destroy(surface);
            g_free(log_content);
            return;
        }
        
        // ================================================================
        // CONFIGURACIÓN DE FUENTE Y ESTILO
        // ================================================================
        // Usar fuente monospace para mejor legibilidad y cálculos precisos
        // DejaVu Sans Mono es una fuente estándar disponible en la mayoría de sistemas
        cairo_select_font_face(cr, "DejaVu Sans Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10);  // Tamaño legible pero compacto
        cairo_set_source_rgb(cr, 0, 0, 0);  // Color negro para texto
        
        // ================================================================
        // VARIABLES DE PAGINACIÓN MEJORADAS
        // ================================================================
        // Parámetros calculados para optimizar el uso del espacio
        double x = 50, y = 50;              // Márgenes izquierdo y superior
        double line_height = 12;             // Espacio entre líneas (ajustado para fuente 10pt)
        double page_height = 792;            // Altura total de página A4 en puntos
        double margin_bottom = 50;           // Margen inferior para evitar cortes
        int max_chars_per_line = 85;         // SOLUCION: Límite de caracteres por línea
                                            // calculado empíricamente para evitar desbordamiento
        
        // ================================================================
        // ENCABEZADO DEL DOCUMENTO
        // ================================================================
        cairo_set_font_size(cr, 16);  // Fuente más grande para título
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, "REPORTE DE SEGURIDAD MATCOM GUARD");
        y += line_height * 2;
        
        cairo_set_font_size(cr, 10);  // Restaurar tamaño normal para contenido
        
        // ================================================================
        // PROCESAMIENTO DE TEXTO AVANZADO
        // ================================================================
        // Filtrado de caracteres especiales y emojis
        // Esto resuelve el problema reportado de emojis que causaban errores en PDFs
        
        char filtered_stats[4096];   // Buffer para estadísticas filtradas
        char filtered_log[32768];    // Buffer grande para log filtrado
        
        // Aplicar filtrado de emojis y caracteres problemáticos
        filter_emoji_and_special_chars(stats_buffer, filtered_stats, sizeof(filtered_stats));
        filter_emoji_and_special_chars(log_content, filtered_log, sizeof(filtered_log));
        
        // ================================================================
        // AJUSTE AUTOMÁTICO DE LÍNEAS LARGAS
        // ================================================================
        // SOLUCIÓN PRINCIPAL: Word wrapping inteligente para líneas que
        // exceden el ancho de página. Esto resuelve el problema de pérdida
        // de información reportado por el usuario.
        
        char wrapped_stats[8192];  // Buffer ampliado para texto con saltos de línea
        wrap_text_for_pdf(filtered_stats, wrapped_stats, sizeof(wrapped_stats), max_chars_per_line);
        
        // ================================================================
        // ESCRITURA DE ESTADÍSTICAS CON PAGINACIÓN AUTOMÁTICA
        // ================================================================        // Control preciso de paginación para evitar cortes abruptos
        
        char *stats_copy = strdup(wrapped_stats);  // Copia para strtok (modifica original)
        if (!stats_copy) {
            printf("Error: No se pudo asignar memoria para stats_copy\n");
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            g_free(log_content);
            return;
        }
        
        char *stats_line = strtok(stats_copy, "\n");
        while (stats_line != NULL) {
            // Verificar si necesitamos nueva página antes de escribir
            if (y > page_height - margin_bottom) {
                cairo_show_page(cr);  // Crear nueva página
                y = 50;               // Reiniciar posición vertical
            }
            
            // Escribir línea en posición actual
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, stats_line);
            y += line_height;
            stats_line = strtok(NULL, "\n");
        }
        free(stats_copy);  // Liberar memoria de la copia
        y += line_height;  // Espacio entre secciones
        
        // ================================================================
        // SEPARADOR VISUAL PARA LOG DE EVENTOS
        // ================================================================
        if (y > page_height - margin_bottom) {
            cairo_show_page(cr);
            y = 50;
        }
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, "=== LOG DE EVENTOS ===");
        y += line_height * 1.5;  // Espacio extra después del separador
        
        // ================================================================
        // PROCESAMIENTO AVANZADO DEL LOG
        // ================================================================
        // El log puede contener líneas extremadamente largas
        // que necesitan ser divididas inteligentemente para evitar pérdida
        // de información (problema principal reportado por el usuario)
        
        char wrapped_log[65536];  // Buffer grande para log con saltos de línea (64KB)
        wrap_text_for_pdf(filtered_log, wrapped_log, sizeof(wrapped_log), max_chars_per_line);
          // ================================================================
        // ESCRITURA DEL LOG CON CONTROL DE PAGINACIÓN
        // ================================================================
        // Escritura línea por línea con verificación automática
        // de límites de página para asegurar que todo el contenido se incluya
        
        char *log_copy = strdup(wrapped_log);  // Copia para preservar original
        if (!log_copy) {
            printf("Error: No se pudo asignar memoria para log_copy\n");
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            g_free(log_content);
            return;
        }
        
        char *log_line = strtok(log_copy, "\n");
        while (log_line != NULL) {
            // Control automático de paginación
            if (y > page_height - margin_bottom) {
                cairo_show_page(cr);  // Nueva página cuando sea necesario
                y = 50;               // Reiniciar posición
            }
            
            // Escribir línea del log
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, log_line);
            y += line_height;
            log_line = strtok(NULL, "\n");
        }
        free(log_copy);  // Limpiar memoria
          // ================================================================
        // FINALIZACIÓN Y LIMPIEZA
        // ================================================================
        cairo_destroy(cr);                    // Liberar contexto Cairo
        cairo_surface_finish(surface);       // Finalizar escritura a archivo
        cairo_surface_destroy(surface);      // Liberar superficie
        
        printf("✅ Reporte PDF exportado exitosamente a: %s\n", filename);
    }
    
    g_free(log_content);
    
    // Agregar entrada al log sobre la exportación
    char export_message[512];
    snprintf(export_message, sizeof(export_message), 
             "Reporte de seguridad exportado a: %s", filename);
    gui_add_log_entry("EXPORT", "INFO", export_message);
}

// ============================================================================
// FUNCIONES AUXILIARES PARA EXPORTACIÓN
// ============================================================================

// ============================================================================
// UTILIDADES DE FORMATEO DE TEXTO PARA PDF
// ============================================================================

/**
 * @brief Filtra emojis y caracteres especiales de texto para compatibilidad PDF
 * 
 * PROPÓSITO:
 * Esta función resuelve el problema crítico reportado por el usuario donde
 * los emojis y caracteres especiales causaban errores en la generación de PDFs.
 * 
 * FUNCIONAMIENTO:
 * - Itera carácter por carácter del texto de entrada
 * - Mantiene solo caracteres ASCII imprimibles (32-126)
 * - Preserva saltos de línea (\n) y tabulaciones (\t) importantes
 * - Convierte caracteres acentuados comunes (á, é, í, ó, ú, ñ) a ASCII
 * - Ignora completamente emojis y otros caracteres UTF-8 complejos
 * - Usa buffers estáticos en lugar de malloc (más estable)
 * - Control de límites mejorado para evitar buffer overflow
 * 
 * @param input Texto original que puede contener emojis/caracteres especiales
 * @param output Buffer de salida para texto filtrado (debe estar pre-asignado)
 * @param output_size Tamaño del buffer de salida (incluye terminador nulo)
 * 
 * EJEMPLO DE USO:
 * char original[] = "Hola 👋 ¿cómo estás? 😊";
 * char filtrado[256];
 * filter_emoji_and_special_chars(original, filtrado, sizeof(filtrado));
 * // Resultado: "Hola  como estas? "
 */
void filter_emoji_and_special_chars(const char *input, char *output, size_t output_size) {    if (!input || !output || output_size == 0) {
        return;
    }
    
    size_t j = 0;  // Índice para buffer de salida
    
    for (size_t i = 0; input[i] != '\0' && j < output_size - 1; i++) {
        unsigned char c = (unsigned char)input[i];
        
        // ================================================================
        // PROCESAMIENTO DE CARACTERES ASCII BÁSICOS
        // ================================================================
        // Mantener caracteres ASCII imprimibles (32-126) y de control útiles
        if ((c >= 32 && c <= 126) || c == '\n' || c == '\t') {
            output[j++] = input[i];
        }
        // ================================================================
        // CONVERSIÓN DE CARACTERES ACENTUADOS UTF-8
        // ================================================================
        // Los caracteres acentuados en UTF-8 comienzan con 0xC3
        // seguidos de un byte específico para cada carácter
        else if (c == 0xC3) {
            if (i + 1 < strlen(input)) {
                unsigned char next = (unsigned char)input[i + 1];
                switch (next) {
                    case 0xA1: output[j++] = 'a'; break; // á → a
                    case 0xA9: output[j++] = 'e'; break; // é → e
                    case 0xAD: output[j++] = 'i'; break; // í → i
                    case 0xB3: output[j++] = 'o'; break; // ó → o
                    case 0xBA: output[j++] = 'u'; break; // ú → u
                    case 0xB1: output[j++] = 'n'; break; // ñ → n
                    case 0x81: output[j++] = 'A'; break; // Á → A
                    case 0x89: output[j++] = 'E'; break; // É → E
                    case 0x8D: output[j++] = 'I'; break; // Í → I
                    case 0x93: output[j++] = 'O'; break; // Ó → O
                    case 0x9A: output[j++] = 'U'; break; // Ú → U
                    case 0x91: output[j++] = 'N'; break; // Ñ → N
                    default: 
                        // Carácter acentuado no reconocido, usar '?'
                        if (j < output_size - 1) output[j++] = '?'; 
                        break;
                }
                i++; // Saltar el siguiente byte UTF-8
            }
        }
        // ================================================================
        // IGNORAR OTROS CARACTERES ESPECIALES Y EMOJIS
        // ================================================================
        // Todos los demás caracteres (incluyendo emojis) se ignoran
        // para asegurar compatibilidad total con PDFs
    }
    
    output[j] = '\0';  // Asegurar terminación nula
}

/**
 * @brief Ajusta automáticamente líneas largas para evitar desbordamiento en PDFs
 * 
 * PROBLEMA RESUELTO:
 * Esta función soluciona el problema principal reportado por el usuario donde
 * las líneas largas se cortaban o perdían información al generar PDFs.
 * 
 * ALGORITMO INTELIGENTE:
 * 1. Analiza cada línea del texto de entrada
 * 2. Si una línea cabe en el límite (max_width), la copia tal como está
 * 3. Si una línea es muy larga:
 *    a) La divide en chunks de tamaño max_width
 *    b) Busca el último espacio antes del límite para no cortar palabras
 *    c) Si no encuentra espacio, corta en el límite (para palabras muy largas)
 *    d) Inserta saltos de línea automáticamente
 * 
 * CARACTERÍSTICAS AVANZADAS:
 * - Preserva saltos de línea originales del texto
 * - Evita cortar palabras cuando es posible
 * - Maneja líneas extremadamente largas sin perder información
 * - Control estricto de límites de buffer para evitar overflow
 * - Optimizado para fuentes monospace usadas en PDFs
 * 
 * CASOS DE USO:
 * - Líneas de log muy largas con rutas de archivos extensas
 * - Mensajes de error con trazas de stack largas  
 * - Datos JSON o XML en líneas únicas
 * - Comandos de sistema con muchos parámetros
 * 
 * @param input Texto original que puede contener líneas largas
 * @param output Buffer de salida para texto con saltos de línea añadidos
 * @param output_size Tamaño total del buffer de salida
 * @param max_width Número máximo de caracteres por línea
 * 
 * EJEMPLO:
 * Entrada: "Esta es una línea muy larga que no cabe en el ancho especificado del PDF"
 * max_width = 30
 * Salida:  "Esta es una línea muy larga\nque no cabe en el ancho\nespecificado del PDF"
 */
void wrap_text_for_pdf(const char *input, char *output, size_t output_size, int max_width) {    if (!input || !output || output_size == 0 || max_width <= 0) {
        return;
    }
    
    size_t input_len = strlen(input);
    size_t output_pos = 0;    // Posición actual en buffer de salida
    size_t line_start = 0;    // Inicio de la línea actual
    
    // ================================================================
    // PROCESAMIENTO LÍNEA POR LÍNEA
    // ================================================================
    for (size_t i = 0; i <= input_len && output_pos < output_size - 1; i++) {
        // Detectar fin de línea o fin de texto
        if (input[i] == '\n' || input[i] == '\0') {
            size_t line_len = i - line_start;
            
            // ================================================================
            // CASO 1: LÍNEA CORTA (CABE EN UNA LÍNEA)
            // ================================================================
            if (line_len <= (size_t)max_width) {
                // Copiar línea completa sin modificaciones
                if (output_pos + line_len < output_size - 1) {
                    strncpy(output + output_pos, input + line_start, line_len);
                    output_pos += line_len;
                    // Preservar salto de línea original si existe
                    if (input[i] == '\n' && output_pos < output_size - 1) {
                        output[output_pos++] = '\n';
                    }
                }
            } 
            // ================================================================
            // CASO 2: LÍNEA LARGA (NECESITA DIVISIÓN)
            // ================================================================
            else {
                size_t pos = line_start;
                
                // Dividir línea en chunks manejables
                while (pos < i && output_pos < output_size - 1) {
                    size_t chunk_end = pos + max_width;
                    if (chunk_end > i) chunk_end = i;  // No exceder fin de línea
                    
                    // ================================================================
                    // BÚSQUEDA INTELIGENTE DE PUNTO DE CORTE
                    // ================================================================
                    // Buscar último espacio antes del límite para evitar cortar palabras
                    if (chunk_end < i) {  // Solo si no estamos al final
                        size_t last_space = chunk_end;
                        // Retroceder hasta encontrar espacio
                        while (last_space > pos && input[last_space] != ' ') {
                            last_space--;
                        }
                        // Si encontramos espacio válido, usarlo como punto de corte
                        if (last_space > pos) {
                            chunk_end = last_space;
                        }
                        // Si no hay espacio, cortar en límite (palabra muy larga)
                    }
                    
                    // ================================================================
                    // COPIA DEL CHUNK CON SALTO DE LÍNEA
                    // ================================================================
                    size_t chunk_len = chunk_end - pos;
                    if (output_pos + chunk_len < output_size - 1) {
                        strncpy(output + output_pos, input + pos, chunk_len);
                        output_pos += chunk_len;
                        // Añadir salto de línea después de cada chunk
                        if (output_pos < output_size - 1) {
                            output[output_pos++] = '\n';
                        }
                    }
                    
                    // Avanzar al siguiente chunk
                    pos = chunk_end;
                    if (pos < i && input[pos] == ' ') pos++; // Saltar espacio separador
                }
            }
            line_start = i + 1;  // Preparar para siguiente línea
        }
    }
    
    output[output_pos] = '\0';  // Asegurar terminación nula
}

/**
 * @brief Calcula el número total de líneas que ocupará un texto después del ajuste
 * 
 * PROPÓSITO:
 * Esta función de utilidad permite calcular previamente cuántas líneas
 * ocupará un texto después de aplicar el ajuste automático, sin necesidad
 * de procesar todo el texto. Es útil para:
 * 
 * CASOS DE USO:
 * - Calcular espacio necesario antes de escribir al PDF
 * - Determinar si cabe en la página actual o necesita nueva página
 * - Optimizar la paginación automática
 * - Estimar el tamaño final del documento
 * 
 * ALGORITMO:
 * 1. Analiza cada línea del texto original
 * 2. Si la línea cabe en max_width, cuenta como 1 línea
 * 3. Si la línea es más larga, calcula cuántas líneas necesitará:
 *    número_líneas = ceil(longitud_línea / max_width)
 * 4. Suma todas las líneas calculadas
 * 
 * VENTAJAS:
 * - Muy eficiente (no crea buffers grandes)
 * - Cálculo matemático preciso
 * - Permite planificación de paginación anticipada
 * - Compatible con el algoritmo de wrap_text_for_pdf()
 * 
 * @param text Texto a analizar
 * @param max_width Ancho máximo de línea (mismo que se usará en wrap_text_for_pdf)
 * @return Número total de líneas que ocupará el texto ajustado
 * 
 * EJEMPLO:
 * text = "Línea corta\nLínea muy muy muy muy muy larga que no cabe"
 * max_width = 20
 * Resultado: 3 (1 línea corta + 2 líneas para la larga)
 */
int count_wrapped_lines(const char *text, int max_width) {    if (!text || max_width <= 0) {
        return 0;
    }
    
    int line_count = 0;
    size_t text_len = strlen(text);
    size_t line_start = 0;
    
    // ================================================================
    // ANÁLISIS LÍNEA POR LÍNEA SIN PROCESAMIENTO COMPLETO
    // ================================================================
    for (size_t i = 0; i <= text_len; i++) {
        // Detectar final de línea o final de texto
        if (text[i] == '\n' || text[i] == '\0') {
            size_t line_len = i - line_start;
            
            // ================================================================
            // CÁLCULO MATEMÁTICO DE LÍNEAS NECESARIAS
            // ================================================================
            if (line_len <= (size_t)max_width) {
                // Línea corta: ocupa exactamente 1 línea
                line_count++;
            } else {
                // Línea larga: calcular cuántas líneas necesitará
                // Usamos división con redondeo hacia arriba: ceil(line_len / max_width)
                line_count += (line_len + max_width - 1) / max_width;
            }
            
            line_start = i + 1;  // Preparar para siguiente línea
        }
    }
      return line_count;
}

// ============================================================================