#include "gui_internal.h"
#include "gui.h"
#include "gui_ports_integration.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Variables del panel de puertos
static GtkWidget *ports_tree_view = NULL;
static GtkListStore *ports_list_store = NULL;
static GtkWidget *ports_info_label = NULL;
static GtkWidget *scan_ports_button = NULL;
static GtkWidget *ports_panel_container = NULL;
static GtkWidget *start_port_spin = NULL;
static GtkWidget *end_port_spin = NULL;
static GtkWidget *quick_scan_button = NULL;
static GtkWidget *full_scan_button = NULL;

// Columnas del TreeView
enum {
    COL_PORT_ICON,
    COL_PORT_NUMBER,
    COL_PORT_STATE,
    COL_PORT_SERVICE,
    COL_PORT_PROTOCOL,
    COL_PORT_STATUS,
    COL_PORT_STATE_COLOR,
    NUM_PORT_COLS
};

// Estructura para definir servicios conocidos
typedef struct {
    int port;
    const char *service;
    const char *description;
} KnownService;

// Lista de servicios conocidos más comunes
static KnownService known_services[] = {
    {20, "FTP-DATA", "FTP Data Transfer"},
    {21, "FTP", "File Transfer Protocol"},
    {22, "SSH", "Secure Shell"},
    {23, "TELNET", "Telnet (inseguro)"},
    {25, "SMTP", "Simple Mail Transfer"},
    {53, "DNS", "Domain Name System"},
    {80, "HTTP", "Web Server"},
    {110, "POP3", "Post Office Protocol"},
    {143, "IMAP", "Internet Message Access"},
    {443, "HTTPS", "Secure Web Server"},
    {445, "SMB", "Server Message Block"},
    {3306, "MySQL", "MySQL Database"},
    {5432, "PostgreSQL", "PostgreSQL Database"},
    {8080, "HTTP-ALT", "Alternative HTTP"},
    {8443, "HTTPS-ALT", "Alternative HTTPS"},
    {3389, "RDP", "Remote Desktop"},
    {5900, "VNC", "Virtual Network Computing"},
    {6379, "Redis", "Redis Database"},
    {27017, "MongoDB", "MongoDB Database"},
    // Puertos potencialmente sospechosos
    {31337, "BACKDOOR", "Elite - Backdoor común"},
    {4444, "BACKDOOR", "Metasploit default"},
    {6666, "IRC", "IRC - A menudo usado por botnets"},
    {6667, "IRC", "IRC - A menudo usado por botnets"},
    {12345, "BACKDOOR", "NetBus backdoor"},
    {0, NULL, NULL} // Terminador
};

// Función para obtener información del servicio por puerto
static const char* gui_get_service_name(int port) {
    for (int i = 0; known_services[i].service != NULL; i++) {
        if (known_services[i].port == port) {
            return known_services[i].service;
        }
    }
    return "Desconocido";
}

// Función para determinar si un puerto es sospechoso
static gboolean is_suspicious_port(int port, const char *state) {
    // Puerto abierto sin servicio conocido
    if (strcmp(state, "Abierto") == 0) {
        const char *service = gui_get_service_name(port);
        if (strcmp(service, "Desconocido") == 0 && port > 1024) {
            return TRUE;
        }
        // Verificar si es un backdoor conocido
        if (strstr(service, "BACKDOOR") != NULL) {
            return TRUE;
        }
        // Servicios inseguros
        if (port == 23 || port == 21) { // Telnet o FTP sin cifrar
            return TRUE;
        }
    }
    return FALSE;
}

// Función para determinar el icono según el estado del puerto
static const char* get_port_icon(int port, const char *state) {
    if (strcmp(state, "Cerrado") == 0) {
        return "🔒"; // Puerto cerrado
    } else if (is_suspicious_port(port, state)) {
        return "🚨"; // Puerto sospechoso
    } else if (port == 22 || port == 443 || port == 3389) {
        return "🔐"; // Servicios seguros
    } else if (port == 80 || port == 8080) {
        return "🌐"; // Servicios web
    } else {
        return "🔓"; // Puerto abierto genérico
    }
}

// Callback para cuando se selecciona un puerto
static void on_port_selection_changed(GtkTreeSelection *selection, gpointer data __attribute__((unused))) {
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gint port;
        gchar *state, *service, *protocol, *status;
        
        gtk_tree_model_get(model, &iter,
                          COL_PORT_NUMBER, &port,
                          COL_PORT_STATE, &state,
                          COL_PORT_SERVICE, &service,
                          COL_PORT_PROTOCOL, &protocol,
                          COL_PORT_STATUS, &status,
                          -1);
        
        // Obtener descripción detallada del servicio
        const char *description = "Sin descripción disponible";
        for (int i = 0; known_services[i].service != NULL; i++) {
            if (known_services[i].port == port) {
                description = known_services[i].description;
                break;
            }
        }
        
        // Actualizar el label de información
        char info_text[1024];
        snprintf(info_text, sizeof(info_text),
                "<b>Puerto:</b> %d\n"
                "<b>Estado:</b> %s\n"
                "<b>Servicio:</b> %s\n"
                "<b>Protocolo:</b> %s\n"
                "<b>Descripción:</b> %s\n\n",
                port, state, service, protocol, description);
        
        // Agregar información adicional según el estado
        if (strcmp(status, "SOSPECHOSO") == 0) {
            strcat(info_text, "<span color='red'><b>⚠️ ADVERTENCIA:</b>\n"
                             "Este puerto podría estar asociado\n"
                             "con actividad maliciosa.</span>");
        } else if (strcmp(state, "Abierto") == 0) {
            strcat(info_text, "<i>Puerto accesible desde la red.</i>");
        } else {
            strcat(info_text, "<i>Puerto no accesible.</i>");
        }
        
        gtk_label_set_markup(GTK_LABEL(ports_info_label), info_text);
        
        g_free(state);
        g_free(service);
        g_free(protocol);
        g_free(status);
    }
}

// Callback para el botón de escaneo rápido
static void on_quick_scan_clicked(GtkButton *button __attribute__((unused)), gpointer data __attribute__((unused))) {
    gui_add_log_entry("PORT_SCANNER", "INFO", "Iniciando escaneo rápido de puertos comunes");
    
    // Establecer rango para puertos comunes
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(start_port_spin), 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(end_port_spin), 1024.0);
    
    // Iniciar escaneo
    on_scan_ports_clicked(GTK_BUTTON(scan_ports_button), NULL);
}

// Callback para el botón de escaneo completo
static void on_full_scan_clicked(GtkButton *button __attribute__((unused)), gpointer data __attribute__((unused))) {
    // Crear diálogo de advertencia
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_WARNING,
                                              GTK_BUTTONS_YES_NO,
                                              "El escaneo completo de 65535 puertos puede tomar mucho tiempo.\n"
                                              "¿Está seguro de que desea continuar?");
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        gui_add_log_entry("PORT_SCANNER", "INFO", "Iniciando escaneo completo de todos los puertos");
        
        // Establecer rango completo
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(start_port_spin), 1.0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(end_port_spin), 65535.0);
        
        // Iniciar escaneo
        on_scan_ports_clicked(GTK_BUTTON(scan_ports_button), NULL);
    }
}

// Callback para re-habilitar botones después del escaneo
static gboolean re_enable_port_buttons(gpointer data __attribute__((unused))) {
    static gboolean cleanup_in_progress = FALSE;
    
    // Evitar múltiples ejecuciones simultáneas
    if (cleanup_in_progress) {
        return G_SOURCE_CONTINUE;
    }
    
    if (!is_gui_port_scan_in_progress()) {
        cleanup_in_progress = TRUE;
        
        gtk_widget_set_sensitive(scan_ports_button, TRUE);
        gtk_widget_set_sensitive(quick_scan_button, TRUE);
        gtk_widget_set_sensitive(full_scan_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(scan_ports_button), "🔍 Escanear Rango");
        
        gui_add_log_entry("GUI_PORTS", "INFO", "✅ Botones de puertos re-habilitados");
        
        cleanup_in_progress = FALSE;
        return G_SOURCE_REMOVE; // Detener el timeout
    }
    
    // Continuar verificando cada segundo
    return G_SOURCE_CONTINUE;
}

// Callback para el botón de escaneo de puertos
void on_scan_ports_clicked(GtkButton *button, gpointer data __attribute__((unused))) {
    if (!ports_callback) {
        gui_add_log_entry("PORT_SCANNER", "WARNING", "No hay callback de escaneo de puertos configurado");
        return;
    }
    
    if (is_gui_port_scan_in_progress()) {
        gui_add_log_entry("PORT_SCANNER", "WARNING", "Escaneo de puertos ya en progreso");
        return;
    }
    
    int start = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(start_port_spin));
    int end = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(end_port_spin));
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Escaneando puertos %d-%d", start, end);
    gui_add_log_entry("PORT_SCANNER", "INFO", log_msg);
    
    gui_set_scanning_status(TRUE);
    
    // Deshabilitar botones durante el escaneo
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    gtk_widget_set_sensitive(quick_scan_button, FALSE);
    gtk_widget_set_sensitive(full_scan_button, FALSE);
    gtk_button_set_label(button, "🔄 Escaneando...");
      // Limpiar lista actual
    gtk_list_store_clear(ports_list_store);
    gui_add_log_entry("GUI_PORTS", "INFO", "🧹 Tabla de puertos limpiada antes del escaneo");
    
    // Iniciar escaneo en hilo separado
    gui_compatible_scan_ports();
    
    // Verificar periódicamente si terminó el escaneo
    g_timeout_add_seconds(1, re_enable_port_buttons, NULL);
}

// Crear el panel principal de puertos
GtkWidget* create_ports_panel() {
    // Contenedor principal
    ports_panel_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(ports_panel_container, 10);
    gtk_widget_set_margin_end(ports_panel_container, 10);
    gtk_widget_set_margin_top(ports_panel_container, 10);
    gtk_widget_set_margin_bottom(ports_panel_container, 10);
    
    // Barra de herramientas
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), 
        "<span size='large' weight='bold'>🔌 Escáner de Puertos de Red</span>");
    gtk_box_pack_start(GTK_BOX(toolbar), title, FALSE, FALSE, 0);
    
    // Espaciador
    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(toolbar), spacer, TRUE, TRUE, 0);
    
    // Botón de escaneo
    scan_ports_button = gtk_button_new_with_label("🔍 Escanear Rango");
    gtk_widget_set_tooltip_text(scan_ports_button, "Escanear el rango de puertos especificado");
    g_signal_connect(scan_ports_button, "clicked", G_CALLBACK(on_scan_ports_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(toolbar), scan_ports_button, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(ports_panel_container), toolbar, FALSE, FALSE, 0);
    
    // Separador
    GtkWidget *separator1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(ports_panel_container), separator1, FALSE, FALSE, 5);
    
    // Panel de configuración de rango y botones rápidos
    GtkWidget *config_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_halign(config_box, GTK_ALIGN_CENTER);
    
    // Configuración de rango
    GtkWidget *range_label = gtk_label_new("Rango de puertos:");
    gtk_box_pack_start(GTK_BOX(config_box), range_label, FALSE, FALSE, 0);
    
    start_port_spin = gtk_spin_button_new_with_range(1.0, 65535.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(start_port_spin), 1.0);
    gtk_widget_set_tooltip_text(start_port_spin, "Puerto inicial del rango a escanear");
    gtk_box_pack_start(GTK_BOX(config_box), start_port_spin, FALSE, FALSE, 0);
    
    GtkWidget *dash_label = gtk_label_new(" - ");
    gtk_box_pack_start(GTK_BOX(config_box), dash_label, FALSE, FALSE, 0);
    
    end_port_spin = gtk_spin_button_new_with_range(1.0, 65535.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(end_port_spin), 1024.0);
    gtk_widget_set_tooltip_text(end_port_spin, "Puerto final del rango a escanear");
    gtk_box_pack_start(GTK_BOX(config_box), end_port_spin, FALSE, FALSE, 0);
    
    // Separador vertical
    GtkWidget *vsep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(config_box), vsep, FALSE, FALSE, 0);
    
    // Botones de escaneo rápido
    quick_scan_button = gtk_button_new_with_label("⚡ Escaneo Rápido (1-1024)");
    gtk_widget_set_tooltip_text(quick_scan_button, "Escanear solo los puertos más comunes");
    g_signal_connect(quick_scan_button, "clicked", G_CALLBACK(on_quick_scan_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(config_box), quick_scan_button, FALSE, FALSE, 0);
      full_scan_button = gtk_button_new_with_label("🔍 Escaneo Completo (1-65535)");    gtk_widget_set_tooltip_text(full_scan_button, "Escanear todos los puertos posibles (lento)");
    g_signal_connect(full_scan_button, "clicked", G_CALLBACK(on_full_scan_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(config_box), full_scan_button, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(ports_panel_container), config_box, FALSE, FALSE, 0);
    
    // Separador
    GtkWidget *separator2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(ports_panel_container), separator2, FALSE, FALSE, 5);
    
    // Contenedor horizontal para la lista y la información
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    
    // Crear el TreeView con scrolling
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, 600, 300);
      // Crear el modelo de datos
    ports_list_store = gtk_list_store_new(NUM_PORT_COLS,
                                         G_TYPE_STRING,  // Icon
                                         G_TYPE_INT,     // Port number
                                         G_TYPE_STRING,  // State
                                         G_TYPE_STRING,  // Service
                                         G_TYPE_STRING,  // Protocol
                                         G_TYPE_STRING,  // Status
                                         G_TYPE_STRING); // State color
    
    // Log de inicialización
    gui_add_log_entry("GUI_PORTS", "INFO", "🏗️ ports_list_store inicializado correctamente");
      ports_tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ports_list_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ports_tree_view), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(ports_tree_view), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(ports_tree_view), COL_PORT_NUMBER);
    
    // Establecer un tamaño mínimo para evitar warnings de dimensiones
    gtk_widget_set_size_request(ports_tree_view, 400, 200);
    
    // Log de verificación del TreeView
    gui_add_log_entry("GUI_PORTS", "INFO", "🏗️ ports_tree_view creado y configurado");
    
    // Crear las columnas
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    
    // Columna de icono
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer,
                                                     "text", COL_PORT_ICON,
                                                     NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(ports_tree_view), column);
    
    // Columna de número de puerto
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Puerto", renderer,
                                                     "text", COL_PORT_NUMBER,
                                                     NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_PORT_NUMBER);
    gtk_tree_view_append_column(GTK_TREE_VIEW(ports_tree_view), column);
    
    // Columna de estado con color
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Estado", renderer,
                                                     "text", COL_PORT_STATE,
                                                     "foreground", COL_PORT_STATE_COLOR,
                                                     NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(ports_tree_view), column);
    
    // Columna de servicio
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Servicio", renderer,
                                                     "text", COL_PORT_SERVICE,
                                                     NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_min_width(column, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(ports_tree_view), column);
    
    // Columna de protocolo
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Protocolo", renderer,
                                                     "text", COL_PORT_PROTOCOL,
                                                     NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(ports_tree_view), column);
    
    // Columna de estado de seguridad
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Evaluación", renderer,
                                                     "text", COL_PORT_STATUS,
                                                     NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(ports_tree_view), column);
    
    // Configurar selección
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ports_tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_port_selection_changed), NULL);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window), ports_tree_view);
    gtk_box_pack_start(GTK_BOX(content_box), scrolled_window, TRUE, TRUE, 0);
    
    // Panel de información detallada
    GtkWidget *info_frame = gtk_frame_new("Información del Puerto");
    gtk_widget_set_size_request(info_frame, 280, -1);
    
    ports_info_label = gtk_label_new("Seleccione un puerto para ver detalles");
    gtk_label_set_line_wrap(GTK_LABEL(ports_info_label), TRUE);
    gtk_widget_set_margin_start(ports_info_label, 10);
    gtk_widget_set_margin_end(ports_info_label, 10);
    gtk_widget_set_margin_top(ports_info_label, 10);
    gtk_widget_set_margin_bottom(ports_info_label, 10);
    gtk_label_set_xalign(GTK_LABEL(ports_info_label), 0.0);
    
    gtk_container_add(GTK_CONTAINER(info_frame), ports_info_label);
    gtk_box_pack_start(GTK_BOX(content_box), info_frame, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(ports_panel_container), content_box, TRUE, TRUE, 0);
    
    // Barra de estado del panel
    GtkWidget *ports_status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(ports_status_bar, 5);
    
    GtkWidget *status_icon = gtk_label_new("ℹ️");
    gtk_box_pack_start(GTK_BOX(ports_status_bar), status_icon, FALSE, FALSE, 0);
    
    GtkWidget *status_text = gtk_label_new("Los puertos marcados con 🚨 requieren atención inmediata");
    gtk_widget_set_halign(status_text, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(ports_status_bar), status_text, TRUE, TRUE, 0);
    
    gtk_box_pack_end(GTK_BOX(ports_panel_container), ports_status_bar, FALSE, FALSE, 0);
      return ports_panel_container;
}

/**
 * Actualiza la tabla de puertos en la GUI con la información de un puerto detectado.
 * Esta función debe ser llamada desde el hilo principal de GTK.
 * 
 * @param port Estructura GUIPort con la información del puerto a mostrar
 */
void gui_update_port(GUIPort *port) {
    if (!port) {
        gui_add_log_entry("GUI_PORTS", "ERROR", "gui_update_port llamado con puerto NULL");
        return;
    }
    
    if (!ports_list_store) {
        gui_add_log_entry("GUI_PORTS", "ERROR", "❌ ports_list_store es NULL");
        return;
    }
    
    GtkTreeIter iter;
    gboolean found = FALSE;
    
    // Buscar si el puerto ya existe en la tabla
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ports_list_store), &iter)) {
        do {
            gint existing_port;
            gtk_tree_model_get(GTK_TREE_MODEL(ports_list_store), &iter,
                             COL_PORT_NUMBER, &existing_port, -1);
            
            if (existing_port == port->port) {
                found = TRUE;
                break;
            }
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(ports_list_store), &iter));
    }
    
    // Si no existe, crear nueva entrada
    if (!found) {
        gtk_list_store_append(ports_list_store, &iter);
    }
    
    // Determinar estado visual
    const char *state = strcmp(port->status, "open") == 0 ? "Abierto" : "Cerrado";
    const char *service = port->service[0] ? port->service : gui_get_service_name(port->port);
    const char *protocol = "TCP";
    
    // Determinar seguridad y color
    const char *security_status = "Normal";
    const char *color = "#4CAF50"; // Verde por defecto
    
    if (strcmp(state, "Cerrado") == 0) {
        security_status = "Cerrado";
        color = "#9E9E9E"; // Gris
    } else if (port->is_suspicious || is_suspicious_port(port->port, state)) {
        security_status = "SOSPECHOSO";
        color = "#F44336"; // Rojo
    }
    
    const char *icon = get_port_icon(port->port, state);
    
    // Insertar/actualizar datos en la tabla
    gtk_list_store_set(ports_list_store, &iter,
                      COL_PORT_ICON, icon,
                      COL_PORT_NUMBER, port->port,
                      COL_PORT_STATE, state,
                      COL_PORT_SERVICE, service,
                      COL_PORT_PROTOCOL, protocol,
                      COL_PORT_STATUS, security_status,
                      COL_PORT_STATE_COLOR, color,
                      -1);
    
    // Log apropiado según el estado del puerto
    if (strcmp(security_status, "SOSPECHOSO") == 0) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), 
                "🚨 Puerto sospechoso añadido: %d/%s (%s) - %s",
                port->port, protocol, service, security_status);
        gui_add_log_entry("GUI_PORTS", "WARNING", log_msg);
    } else if (strcmp(state, "Abierto") == 0) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), 
                "✅ Puerto abierto añadido: %d/%s (%s)",
                port->port, protocol, service);
        gui_add_log_entry("GUI_PORTS", "INFO", log_msg);
    }
    
    // Refresco visual seguro
    if (ports_tree_view && GTK_IS_WIDGET(ports_tree_view)) {
        if (gtk_widget_get_realized(ports_tree_view) && gtk_widget_get_visible(ports_tree_view)) {
            GtkAllocation allocation;
            gtk_widget_get_allocation(ports_tree_view, &allocation);
            
            // Solo refrescar si el widget tiene dimensiones válidas
            if (allocation.width > 0 && allocation.height > 0) {
                gtk_widget_queue_draw(ports_tree_view);
                gtk_tree_view_columns_autosize(GTK_TREE_VIEW(ports_tree_view));
            }
        }
    }
}

/**
 * Función wrapper para ejecutar gui_update_port en el hilo principal de GTK.
 * Usado por g_main_context_invoke para asegurar thread safety.
 * 
 * @param user_data Puntero a estructura GUIPort (será liberado automáticamente)
 * @return FALSE para ejecutar solo una vez
 */
// Función wrapper para ejecutar gui_update_port en el hilo principal
gboolean gui_update_port_main_thread_wrapper(gpointer user_data) {
    GUIPort *port = (GUIPort *)user_data;
    
    if (!port) {
        gui_add_log_entry("GUI_UPDATE", "ERROR", "gui_update_port_main_thread_wrapper: puerto NULL");
        return FALSE;
    }
    
    // Llamar a gui_update_port desde el hilo principal de GTK
    gui_update_port(port);
    
    // Liberar memoria
    free(port);
    return FALSE; // Solo ejecutar una vez
}

// ============================================================================