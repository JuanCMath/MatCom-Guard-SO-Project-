#ifndef DEVICE_MONITOR_H
#define DEVICE_MONITOR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>

// Estructura para almacenar información de dispositivos conectados
typedef struct {
    char **devices;     // Array de nombres de dispositivos
    int count;          // Número de dispositivos encontrados
    int capacity;       // Capacidad actual del array
} DeviceList;

// Estructura para almacenar información de un archivo
typedef struct {
    char *path;                 // Ruta completa del archivo
    char *name;                 // Nombre del archivo
    char *extension;            // Extensión del archivo
    off_t size;                 // Tamaño del archivo
    char sha256_hash[65];       // Hash SHA-256 (64 caracteres + null terminator)
    mode_t permissions;         // Permisos del archivo
    time_t last_modified;       // Última modificación
    time_t last_accessed;       // Último acceso
} FileInfo;

// Estructura para almacenar el snapshot de un dispositivo
typedef struct {
    char *device_name;          // Nombre del dispositivo
    FileInfo **files;           // Array de archivos
    int file_count;             // Número de archivos
    int capacity;               // Capacidad del array
    time_t snapshot_time;       // Tiempo del snapshot
} DeviceSnapshot;

// Funciones para monitoreo de dispositivos
DeviceList* monitor_connected_devices(void);
void free_device_list(DeviceList *device_list);

// Funciones para manejo de archivos y snapshots
int calculate_sha256(const char *filepath, char *hash_output);
char* get_file_extension(const char *filename);
int scan_directory_recursive(DeviceSnapshot *snapshot, const char *dir_path);
DeviceSnapshot* create_device_snapshot(const char *device_name);
void free_device_snapshot(DeviceSnapshot *snapshot);
int validate_device_snapshot(const DeviceSnapshot *snapshot);

#endif // DEVICE_MONITOR_H