#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "device_monitor.h"

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
    return 0;
}
