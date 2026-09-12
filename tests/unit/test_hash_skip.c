// _GNU_SOURCE se necesita para exponer mkdtemp()/utime() bajo -std=c99
// estricto, y es el mismo patrón que usa src/device_monitor.c para strdup()
// (glibc oculta estas funciones si no se pide soporte extendido cuando se
// compila en modo ISO C99 puro).
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include "device_monitor.h"
#include "threadpool.h"

static DeviceSnapshot *make_empty_snapshot(void) {
    DeviceSnapshot *snap = malloc(sizeof(DeviceSnapshot));
    assert(snap != NULL);

    const char *name = "test_hash_skip_integration";
    snap->device_name = malloc(strlen(name) + 1);
    assert(snap->device_name != NULL);
    strcpy(snap->device_name, name);

    snap->capacity = 16;
    snap->files = malloc((size_t)snap->capacity * sizeof(FileInfo *));
    assert(snap->files != NULL);

    snap->file_count = 0;
    snap->snapshot_time = time(NULL);
    return snap;
}

/* 64 caracteres hexadecimales y no el centinela de error. */
static int is_valid_hash(const char *hash) {
    if (hash == NULL || strcmp(hash, "ERROR_CALCULATING_HASH") == 0) {
        return 0;
    }
    size_t len = strlen(hash);
    if (len != 64) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        char c = hash[i];
        int is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!is_hex) {
            return 0;
        }
    }
    return 1;
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    size_t len = strlen(content);
    assert(fwrite(content, 1, len, f) == len);
    fclose(f);
}

/**
 * Prueba de integración del camino paralelo de hashing: ejercita
 * scan_directory_recursive() con un ThreadPool real y un previous_snapshot
 * real, en vez de sólo las funciones puras probadas arriba. Verifica que:
 *   - un archivo sin cambios (mismo tamaño y mtime) reutiliza el hash
 *     anterior tal cual, sin recalcularlo;
 *   - un archivo modificado recibe un hash nuevo y válido, calculado por
 *     una tarea del pool (camino hash_pool != NULL en device_monitor.c).
 */
static void test_parallel_hash_skip_integration(void) {
    char tmpl[] = "/tmp/matcomguard_hashtest_XXXXXX";
    char *tmp_dir = mkdtemp(tmpl);
    assert(tmp_dir != NULL);

    char unchanged_path[1200];
    char changed_path[1200];
    snprintf(unchanged_path, sizeof(unchanged_path), "%s/unchanged.txt", tmp_dir);
    snprintf(changed_path, sizeof(changed_path), "%s/changed.txt", tmp_dir);

    write_file(unchanged_path, "contenido original sin cambios\n");
    write_file(changed_path, "contenido original que sera modificado\n");

    int total_files = count_files_recursive(tmp_dir);
    assert(total_files == 2);

    // 1) Baseline: escaneo síncrono completo (hash_pool = NULL, sin
    //    previous_snapshot) para tener un snapshot "anterior" con hashes
    //    reales conocidos.
    DeviceSnapshot *baseline = make_empty_snapshot();
    assert(scan_directory_recursive(baseline, tmp_dir, NULL, total_files,
                                     NULL, NULL, NULL, NULL) == 0);
    assert(baseline->file_count == 2);

    const FileInfo *base_unchanged = device_monitor_find_file(baseline, unchanged_path);
    const FileInfo *base_changed = device_monitor_find_file(baseline, changed_path);
    assert(base_unchanged != NULL);
    assert(base_changed != NULL);
    assert(is_valid_hash(base_unchanged->sha256_hash));
    assert(is_valid_hash(base_changed->sha256_hash));

    char base_unchanged_hash[65];
    char base_changed_hash[65];
    strcpy(base_unchanged_hash, base_unchanged->sha256_hash);
    strcpy(base_changed_hash, base_changed->sha256_hash);

    // 2) Modificar "changed.txt" (contenido y tamaño distintos) y forzar un
    //    mtime explícitamente distinto, para no depender de la resolución
    //    de reloj del sistema de archivos. "unchanged.txt" se deja intacto.
    write_file(changed_path, "contenido totalmente distinto y mas largo para variar el tamano tambien\n");
    struct utimbuf new_times;
    new_times.actime = time(NULL) + 1000;
    new_times.modtime = time(NULL) + 1000;
    assert(utime(changed_path, &new_times) == 0);

    // 3) Segundo escaneo, ahora con un ThreadPool real y el baseline como
    //    previous_snapshot: ejercita el camino de hash paralelo/reuso.
    ThreadPool *pool = threadpool_create(2);
    assert(pool != NULL);

    DeviceSnapshot *result = make_empty_snapshot();
    total_files = count_files_recursive(tmp_dir);
    assert(scan_directory_recursive(result, tmp_dir, baseline, total_files,
                                     pool, NULL, NULL, NULL) == 0);
    threadpool_wait(pool);

    assert(result->file_count == 2);

    const FileInfo *res_unchanged = device_monitor_find_file(result, unchanged_path);
    const FileInfo *res_changed = device_monitor_find_file(result, changed_path);
    assert(res_unchanged != NULL);
    assert(res_changed != NULL);

    // El archivo sin cambios debe reutilizar exactamente el hash anterior
    // (no se encoló ninguna tarea de hash para él).
    assert(is_valid_hash(res_unchanged->sha256_hash));
    assert(strcmp(res_unchanged->sha256_hash, base_unchanged_hash) == 0);

    // El archivo modificado debe tener un hash nuevo y válido, calculado
    // por una tarea del pool.
    assert(is_valid_hash(res_changed->sha256_hash));
    assert(strcmp(res_changed->sha256_hash, base_changed_hash) != 0);

    threadpool_destroy(pool);
    free_device_snapshot(baseline);
    free_device_snapshot(result);

    unlink(unchanged_path);
    unlink(changed_path);
    rmdir(tmp_dir);

    printf("test_parallel_hash_skip_integration: OK\n");
}

int main(void) {
    FileInfo previous;
    previous.size = 1024;
    previous.last_modified = 1700000000;
    strcpy(previous.sha256_hash, "abc123");

    assert(device_monitor_file_unchanged(&previous, 1024, 1700000000) == 1);
    assert(device_monitor_file_unchanged(&previous, 2048, 1700000000) == 0);
    assert(device_monitor_file_unchanged(&previous, 1024, 1700000001) == 0);
    assert(device_monitor_file_unchanged(NULL, 1024, 1700000000) == 0);

    FileInfo file_a = previous;
    file_a.path = "/media/test/a.txt";
    FileInfo *files[1] = { &file_a };

    DeviceSnapshot snapshot;
    snapshot.device_name = "test";
    snapshot.files = files;
    snapshot.file_count = 1;
    snapshot.capacity = 1;

    const FileInfo *found = device_monitor_find_file(&snapshot, "/media/test/a.txt");
    assert(found == &file_a);

    const FileInfo *missing = device_monitor_find_file(&snapshot, "/media/test/b.txt");
    assert(missing == NULL);

    assert(device_monitor_find_file(NULL, "/media/test/a.txt") == NULL);

    printf("test_hash_skip: OK\n");

    test_parallel_hash_skip_integration();

    return 0;
}
