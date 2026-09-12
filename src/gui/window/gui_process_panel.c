#include "gui_internal.h"
#include "gui.h"
#include "gui_process_integration.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Variables del panel de procesos
static GtkWidget *process_tree_view = NULL;
static GtkListStore *process_list_store = NULL;
static GtkWidget *process_info_label = NULL;
static GtkWidget *scan_process_button = NULL;
static GtkWidget *kill_process_button = NULL;
static GtkWidget *process_panel_container = NULL;
static GtkWidget *cpu_threshold_spin = NULL;
static GtkWidget *mem_threshold_spin = NULL;

// Declaraciones de funciones auxiliares
static void format_cpu_column(GtkTreeViewColumn *column,
                             GtkCellRenderer *renderer,
                             GtkTreeModel *model,
                             GtkTreeIter *iter,
                             gpointer data);
static void format_mem_column(GtkTreeViewColumn *column,
                             GtkCellRenderer *renderer,
                             GtkTreeModel *model,
                             GtkTreeIter *iter,
                             gpointer data);
                             
// Columnas del TreeView
enum {
    COL_PROC_ICON,
    COL_PROC_PID,
    COL_PROC_NAME,
    COL_PROC_CPU_USAGE,
    COL_PROC_MEM_USAGE,
    COL_PROC_STATUS,
    COL_PROC_STATUS_COLOR,
    COL_PROC_CPU_COLOR,
    COL_PROC_MEM_COLOR,
    NUM_PROC_COLS
};

// Función para determinar el color según el porcentaje de uso
static const char* get_usage_color(float usage, float threshold) {
    if (usage > threshold) {
        return "#F44336"; // Rojo - Alto
    } else if (usage > threshold * 0.7) {
        return "#FF9800"; // Naranja - Medio-alto
    } else if (usage > threshold * 0.5) {
        return "#FFC107"; // Amarillo - Medio
    } else {
        return "#4CAF50"; // Verde - Bajo
    }
}

// Función para determinar el icono según el estado del proceso
// MODIFICACIÓN: Prioridad absoluta para procesos whitelisted
static const char* get_process_icon(float cpu_usage, float mem_usage, gboolean is_suspicious, gboolean is_whitelisted) {
    // CAMBIO PRINCIPAL: Prioridad absoluta para whitelist - estos procesos SIEMPRE muestran ✅
    // Esto evita que procesos como gnome-shell, systemd, etc. muestren iconos de alerta
    if (is_whitelisted) {
        return "✅"; // Proceso de confianza - SIEMPRE tiene prioridad
    } 
    // Solo si NO está whitelisted, aplicar iconos de alerta/warning
    else if (is_suspicious) {
        return "🚨"; // Alerta máxima
    } else if (cpu_usage > 80.0 || mem_usage > 80.0) {
        return "⚠️"; // Advertencia
    } else if (cpu_usage > 50.0 || mem_usage > 50.0) {
        return "⚡"; // Alto consumo
    } else {
        return "✓"; // Normal
    }
}

// Callback para cuando se selecciona un proceso
static void on_process_selection_changed(GtkTreeSelection *selection, gpointer data __attribute__((unused))) {
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gint pid;
        gchar *name, *status;
        gfloat cpu_usage, mem_usage;
        
        gtk_tree_model_get(model, &iter,
                          COL_PROC_PID, &pid,
                          COL_PROC_NAME, &name,
                          COL_PROC_CPU_USAGE, &cpu_usage,
                          COL_PROC_MEM_USAGE, &mem_usage,
                          COL_PROC_STATUS, &status,
                          -1);
        
        // Actualizar el label de información
        char info_text[512];
        snprintf(info_text, sizeof(info_text),
                "<b>Proceso:</b> %s\n"
                "<b>PID:</b> %d\n"
                "<b>CPU:</b> %.1f%%\n"
                "<b>Memoria:</b> %.1f%%\n"
                "<b>Estado:</b> %s\n\n"
                "<i>Seleccionado para monitoreo detallado</i>",
                name, pid, cpu_usage, mem_usage, status);
        
        gtk_label_set_markup(GTK_LABEL(process_info_label), info_text);
        
        // Habilitar el botón de terminar proceso
        gtk_widget_set_sensitive(kill_process_button, TRUE);
        
        g_free(name);
        g_free(status);
    } else {
        gtk_widget_set_sensitive(kill_process_button, FALSE);
    }
}

// Callback para re-habilitar botón después del escaneo
static gboolean re_enable_process_button(gpointer data) {
    static gboolean cleanup_in_progress = FALSE;
    GtkButton *button = GTK_BUTTON(data);
    
    printf(">>> Timer ejecutándose - verificando estado del escaneo\n");
    
    // Evitar múltiples ejecuciones simultáneas
    if (cleanup_in_progress) {
        printf(">>> Limpieza ya en progreso - esperando\n");
        return G_SOURCE_CONTINUE;
    }
    
    if (!is_gui_process_scan_in_progress()) {
        printf(">>> Escaneo completado - rehabilitando botón\n");
        cleanup_in_progress = TRUE;
        
        gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
        gtk_button_set_label(button, "🔍 Escanear Procesos");
        
        gui_add_log_entry("GUI_PROCESSES", "INFO", "✅ Botón de procesos re-habilitado");
        printf(">>> Botón rehabilitado exitosamente\n");
        
        cleanup_in_progress = FALSE;
        return G_SOURCE_REMOVE; // Detener el timer
    } else {
        printf(">>> Escaneo aún en progreso - continuando timer\n");
        return G_SOURCE_CONTINUE; // Continuar verificando
    }
}

// Callback para el botón de escaneo de procesos
static void on_scan_processes_clicked(GtkButton *button, gpointer data __attribute__((unused))) {
    printf("=== DIAGNÓSTICO PROCESO ESCANEO ===\n");
    printf("1. Botón presionado\n");
    
    if (!processes_callback) {
        printf("ERROR: processes_callback es NULL\n");
        gui_add_log_entry("PROCESS_SCANNER", "WARNING", "No hay callback de escaneo de procesos configurado");
        return;
    }
    printf("2. Callback verificado - OK\n");
    
    if (is_gui_process_scan_in_progress()) {
        printf("WARNING: Escaneo ya en progreso\n");
        gui_add_log_entry("PROCESS_SCANNER", "WARNING", "Escaneo de procesos ya en progreso");
        return;
    }
    printf("3. Estado verificado - no hay escaneo previo\n");
    
    gui_add_log_entry("PROCESS_SCANNER", "INFO", "Iniciando escaneo de procesos del sistema");
    gui_set_scanning_status(TRUE);
    
    // Deshabilitar el botón durante el escaneo
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    gtk_button_set_label(button, "🔄 Escaneando...");
    printf("4. Botón deshabilitado y etiqueta cambiada\n");
    
    // Iniciar escaneo en hilo separado
    printf("5. Llamando a gui_compatible_scan_processes()\n");
    gui_compatible_scan_processes();
    printf("6. gui_compatible_scan_processes() retornó\n");
    
    // Verificar periódicamente si terminó el escaneo
    printf("7. Configurando timer de verificación\n");
    g_timeout_add_seconds(1, re_enable_process_button, button);
    printf("=== FIN DIAGNÓSTICO - Timer configurado ===\n");
}

// Callback para terminar un proceso
static void on_kill_process_clicked(GtkButton *button __attribute__((unused)), gpointer data __attribute__((unused))) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(process_tree_view));
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gint pid;
        gchar *name;
        gtk_tree_model_get(model, &iter,
                          COL_PROC_PID, &pid,
                          COL_PROC_NAME, &name,
                          -1);
        
        // Crear diálogo de confirmación
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_WARNING,
                                                  GTK_BUTTONS_YES_NO,
                                                  "¿Está seguro de que desea terminar el proceso '%s' (PID: %d)?",
                                                  name, pid);
        
        int response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_YES) {
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "Intentando terminar proceso '%s' (PID: %d)", name, pid);
            gui_add_log_entry("PROCESS_MANAGER", "WARNING", log_msg);
            
            // Aquí iría el código real para terminar el proceso
            // Por ahora, solo simulamos
            gtk_list_store_remove(process_list_store, &iter);
            
            snprintf(log_msg, sizeof(log_msg), "Proceso '%s' terminado exitosamente", name);
            gui_add_log_entry("PROCESS_MANAGER", "INFO", log_msg);
        }
        
        gtk_widget_destroy(dialog);
        g_free(name);
    }
}

// Callback para cambios en los umbrales
static void on_threshold_changed(GtkSpinButton *spin_button, gpointer data) {
    const char *type = (const char *)data;
    gdouble value = gtk_spin_button_get_value(spin_button);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Umbral de %s cambiado a %.0f%%", type, value);
    gui_add_log_entry("CONFIG", "INFO", log_msg);
    
    // Aquí se actualizaría la configuración real del backend
}

// Crear el panel principal de procesos
GtkWidget* create_process_panel() {
    // Contenedor principal
    process_panel_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(process_panel_container, 10);
    gtk_widget_set_margin_end(process_panel_container, 10);
    gtk_widget_set_margin_top(process_panel_container, 10);
    gtk_widget_set_margin_bottom(process_panel_container, 10);
    
    // Barra de herramientas
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
        "<span size='large' weight='bold'>⚡ Monitor de Procesos del Sistema</span>");
    gtk_box_pack_start(GTK_BOX(toolbar), title, FALSE, FALSE, 0);
    
    // Espaciador
    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(toolbar), spacer, TRUE, TRUE, 0);
    
    // Botón de terminar proceso
    kill_process_button = gtk_button_new_with_label("❌ Terminar Proceso");
    gtk_widget_set_tooltip_text(kill_process_button, "Terminar el proceso seleccionado");
    gtk_widget_set_sensitive(kill_process_button, FALSE);
    g_signal_connect(kill_process_button, "clicked", G_CALLBACK(on_kill_process_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(toolbar), kill_process_button, FALSE, FALSE, 0);
    
    // Botón de escaneo
    scan_process_button = gtk_button_new_with_label("🔍 Escanear Procesos");
    gtk_widget_set_tooltip_text(scan_process_button, "Actualizar lista de procesos");
    g_signal_connect(scan_process_button, "clicked", G_CALLBACK(on_scan_processes_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(toolbar), scan_process_button, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(process_panel_container), toolbar, FALSE, FALSE, 0);
    
    // Separador
    GtkWidget *separator1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(process_panel_container), separator1, FALSE, FALSE, 5);
    
    // Panel de configuración de umbrales
    GtkWidget *config_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_halign(config_box, GTK_ALIGN_CENTER);
    
    // Umbral de CPU
    GtkWidget *cpu_label = gtk_label_new("Umbral CPU (%):");
    gtk_box_pack_start(GTK_BOX(config_box), cpu_label, FALSE, FALSE, 0);
    
    cpu_threshold_spin = gtk_spin_button_new_with_range(10.0, 100.0, 5.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(cpu_threshold_spin), 70.0);
    gtk_widget_set_tooltip_text(cpu_threshold_spin, "Procesos que excedan este % de CPU serán marcados");
    g_signal_connect(cpu_threshold_spin, "value-changed", G_CALLBACK(on_threshold_changed), "CPU");
    gtk_box_pack_start(GTK_BOX(config_box), cpu_threshold_spin, FALSE, FALSE, 0);
    
    // Umbral de Memoria
    GtkWidget *mem_label = gtk_label_new("Umbral Memoria (%):");
    gtk_box_pack_start(GTK_BOX(config_box), mem_label, FALSE, FALSE, 0);
    
    mem_threshold_spin = gtk_spin_button_new_with_range(10.0, 100.0, 5.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(mem_threshold_spin), 50.0);
    gtk_widget_set_tooltip_text(mem_threshold_spin, "Procesos que excedan este % de RAM serán marcados");
    g_signal_connect(mem_threshold_spin, "value-changed", G_CALLBACK(on_threshold_changed), "Memoria");
    gtk_box_pack_start(GTK_BOX(config_box), mem_threshold_spin, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(process_panel_container), config_box, FALSE, FALSE, 0);
    
    // Separador
    GtkWidget *separator2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(process_panel_container), separator2, FALSE, FALSE, 5);
    
    // Contenedor horizontal para la lista y la información
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    // Crear el TreeView con scrolling
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, 650, 300);
    
    // Crear el modelo de datos
    process_list_store = gtk_list_store_new(NUM_PROC_COLS,
                                           G_TYPE_STRING,  // Icon
                                           G_TYPE_INT,     // PID
                                           G_TYPE_STRING,  // Name
                                           G_TYPE_FLOAT,   // CPU usage
                                           G_TYPE_FLOAT,   // Memory usage
                                           G_TYPE_STRING,  // Status
                                           G_TYPE_STRING,  // Status color
                                           G_TYPE_STRING,  // CPU color
                                           G_TYPE_STRING); // Memory color
    
    process_tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(process_list_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(process_tree_view), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(process_tree_view), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(process_tree_view), COL_PROC_NAME);
    
    // Crear las columnas
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    
    // Columna de icono
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer,
                                                     "text", COL_PROC_ICON,
                                                     NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(process_tree_view), column);
    
    // Columna de PID
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("PID", renderer,
                                                     "text", COL_PROC_PID,
                                                     NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_PROC_PID);
    gtk_tree_view_append_column(GTK_TREE_VIEW(process_tree_view), column);
    
    // Columna de nombre del proceso
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Proceso", renderer,
                                                     "text", COL_PROC_NAME,
                                                     NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_min_width(column, 200);
    gtk_tree_view_column_set_sort_column_id(column, COL_PROC_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(process_tree_view), column);
    
    // Columna de uso de CPU con color
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("CPU %", renderer,
                                                     "text", COL_PROC_CPU_USAGE,
                                                     "foreground", COL_PROC_CPU_COLOR,
                                                     NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
        (GtkTreeCellDataFunc)format_cpu_column, NULL, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_PROC_CPU_USAGE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(process_tree_view), column);
    
    // Columna de uso de memoria con color
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Memoria %", renderer,
                                                     "text", COL_PROC_MEM_USAGE,
                                                     "foreground", COL_PROC_MEM_COLOR,
                                                     NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
        (GtkTreeCellDataFunc)format_mem_column, NULL, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_PROC_MEM_USAGE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(process_tree_view), column);
    
    // Columna de estado con color
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Estado", renderer,
                                                     "text", COL_PROC_STATUS,
                                                     "foreground", COL_PROC_STATUS_COLOR,
                                                     NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(process_tree_view), column);
    
    // Configurar selección
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(process_tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_process_selection_changed), NULL);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window), process_tree_view);
    gtk_box_pack_start(GTK_BOX(content_box), scrolled_window, TRUE, TRUE, 0);
    
    // Panel de información detallada
    GtkWidget *info_frame = gtk_frame_new("Información del Proceso");
    gtk_widget_set_size_request(info_frame, 250, -1);
    
    process_info_label = gtk_label_new("Seleccione un proceso para ver detalles");
    gtk_label_set_line_wrap(GTK_LABEL(process_info_label), TRUE);
    gtk_widget_set_margin_start(process_info_label, 10);
    gtk_widget_set_margin_end(process_info_label, 10);
    gtk_widget_set_margin_top(process_info_label, 10);
    gtk_widget_set_margin_bottom(process_info_label, 10);
    gtk_label_set_xalign(GTK_LABEL(process_info_label), 0.0);
    
    gtk_container_add(GTK_CONTAINER(info_frame), process_info_label);
    gtk_box_pack_start(GTK_BOX(content_box), info_frame, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(process_panel_container), content_box, TRUE, TRUE, 0);
    
    // Barra de estado del panel
    GtkWidget *process_status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(process_status_bar, 5);
    
    GtkWidget *status_icon = gtk_label_new("ℹ️");
    gtk_box_pack_start(GTK_BOX(process_status_bar), status_icon, FALSE, FALSE, 0);
    
    GtkWidget *status_text = gtk_label_new("Los procesos marcados con 🚨 superan los umbrales configurados");
    gtk_widget_set_halign(status_text, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(process_status_bar), status_text, TRUE, TRUE, 0);
    
    gtk_box_pack_end(GTK_BOX(process_panel_container), process_status_bar, FALSE, FALSE, 0);
    
    return process_panel_container;
}

// Función auxiliar para formatear la columna de CPU
static void format_cpu_column(GtkTreeViewColumn *column __attribute__((unused)),
                             GtkCellRenderer *renderer,
                             GtkTreeModel *model,
                             GtkTreeIter *iter,
                             gpointer data __attribute__((unused))) {
    gfloat cpu_usage;
    gtk_tree_model_get(model, iter, COL_PROC_CPU_USAGE, &cpu_usage, -1);
    
    char text[32];
    snprintf(text, sizeof(text), "%.1f%%", cpu_usage);
    g_object_set(renderer, "text", text, NULL);
}

// Función auxiliar para formatear la columna de memoria
static void format_mem_column(GtkTreeViewColumn *column __attribute__((unused)),
                             GtkCellRenderer *renderer,
                             GtkTreeModel *model,
                             GtkTreeIter *iter,
                             gpointer data __attribute__((unused))) {
    gfloat mem_usage;
    gtk_tree_model_get(model, iter, COL_PROC_MEM_USAGE, &mem_usage, -1);
    
    char text[32];
    snprintf(text, sizeof(text), "%.1f%%", mem_usage);
    g_object_set(renderer, "text", text, NULL);
}

// Función pública para actualizar un proceso
void gui_update_process(GUIProcess *process) {
    if (!process_list_store || !process) return;
    
    GtkTreeIter iter;
    gboolean found = FALSE;
    
    // Buscar si el proceso ya existe por PID
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(process_list_store), &iter)) {
        do {
            gint pid;
            gtk_tree_model_get(GTK_TREE_MODEL(process_list_store), &iter,
                             COL_PROC_PID, &pid, -1);
            
            if (pid == process->pid) {
                found = TRUE;
                break;
            }
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(process_list_store), &iter));
    }
    
    // Si no existe, crear nueva entrada
    if (!found) {
        gtk_list_store_append(process_list_store, &iter);
    }
    
    // Obtener los umbrales actuales
    gdouble cpu_threshold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(cpu_threshold_spin));
    gdouble mem_threshold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(mem_threshold_spin));    // MODIFICACIÓN CRÍTICA: Lógica de estado para procesos whitelisted
    // Determinar estado y colores según prioridades
    const char *status = "Normal";
    const char *status_color = "#4CAF50";
    
    // CAMBIO PRINCIPAL: Prioridad absoluta para whitelist    // Los procesos whitelisted SIEMPRE muestran estado "Whitelisted", sin importar su uso de recursos
    if (process->is_whitelisted) {
        status = "Whitelisted";
        status_color = "#2196F3";  // Azul para procesos de confianza
    } 
    // CAMBIO: Solo si NO está whitelisted, verificar si es sospechoso
    // Esto evita que procesos como gnome-shell generen alertas aunque usen muchos recursos
    else if (process->is_suspicious) {
        status = "SOSPECHOSO";
        status_color = "#F44336";
    } else if (process->cpu_usage > cpu_threshold && process->mem_usage > mem_threshold) {
        status = "Alto CPU+RAM";
        status_color = "#F44336";
    } else if (process->cpu_usage > cpu_threshold) {
        status = "Alto CPU";
        status_color = "#FF9800";
    } else if (process->mem_usage > mem_threshold) {
        status = "Alta Memoria";
        status_color = "#FF9800";
    }
    
    const char *icon = get_process_icon(process->cpu_usage, process->mem_usage, process->is_suspicious, process->is_whitelisted);
    const char *cpu_color = get_usage_color(process->cpu_usage, cpu_threshold);
    const char *mem_color = get_usage_color(process->mem_usage, mem_threshold);
    
    // Actualizar los datos
    gtk_list_store_set(process_list_store, &iter,
                      COL_PROC_ICON, icon,
                      COL_PROC_PID, process->pid,
                      COL_PROC_NAME, process->name,
                      COL_PROC_CPU_USAGE, process->cpu_usage,
                      COL_PROC_MEM_USAGE, process->mem_usage,
                      COL_PROC_STATUS, status,
                      COL_PROC_STATUS_COLOR, status_color,
                      COL_PROC_CPU_COLOR, cpu_color,
                      COL_PROC_MEM_COLOR, mem_color,
                      -1);
      // Log del evento si es sospechoso Y NO está en whitelist
    if ((process->is_suspicious || process->cpu_usage > cpu_threshold || process->mem_usage > mem_threshold) 
        && !process->is_whitelisted) {
        char log_msg[512];
        snprintf(log_msg, sizeof(log_msg),
                "Proceso '%s' (PID: %d) - CPU: %.1f%%, RAM: %.1f%% - Estado: %s",
                process->name, process->pid, process->cpu_usage, process->mem_usage, status);
        gui_add_log_entry("PROCESS_MONITOR", 
                         process->is_suspicious ? "ALERT" : "WARNING", 
                         log_msg);
    } else if ((process->is_suspicious || process->cpu_usage > cpu_threshold || process->mem_usage > mem_threshold) 
               && process->is_whitelisted) {
        // Log informativo para procesos whitelisted con alto uso
        char log_msg[512];
        snprintf(log_msg, sizeof(log_msg),
                "✅ Proceso whitelisted '%s' (PID: %d) - CPU: %.1f%%, RAM: %.1f%% - Estado: %s",
                process->name, process->pid, process->cpu_usage, process->mem_usage, status);
        gui_add_log_entry("PROCESS_MONITOR", "INFO", log_msg);
    }
}