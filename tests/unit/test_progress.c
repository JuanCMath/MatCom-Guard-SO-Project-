#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "progress.h"

static int callback_invocations = 0;
static char last_phase[128];
static int last_current = -1;
static int last_total = -1;

static void record_progress(const ProgressUpdate *update, void *user_data) {
    int *counter = (int *)user_data;
    (*counter)++;
    callback_invocations++;
    strncpy(last_phase, update->phase, sizeof(last_phase) - 1);
    last_phase[sizeof(last_phase) - 1] = '\0';
    last_current = update->current;
    last_total = update->total;
}

int main(void) {
    int user_counter = 0;
    ProgressCallback cb = record_progress;

    ProgressUpdate update1 = { .phase = "Escaneando puertos", .current = 10, .total = 100 };
    cb(&update1, &user_counter);

    assert(callback_invocations == 1);
    assert(user_counter == 1);
    assert(strcmp(last_phase, "Escaneando puertos") == 0);
    assert(last_current == 10);
    assert(last_total == 100);

    ProgressUpdate update2 = { .phase = "Monitoreando...", .current = 0, .total = 0 };
    cb(&update2, &user_counter);

    assert(callback_invocations == 2);
    assert(last_total == 0);  // 0 = indeterminado

    printf("test_progress: OK\n");
    return 0;
}
