#ifndef PROGRESS_H
#define PROGRESS_H

/**
 * Reporte de avance de una operación potencialmente larga (escaneo de
 * puertos, análisis de un dispositivo USB, ciclo de monitoreo de procesos).
 *
 * `phase` describe qué está pasando ahora mismo (p.ej. "Escaneando puertos
 * altos"). Es válido solo durante la llamada al callback — si necesitás
 * conservarlo, copialo antes de que el callback retorne.
 *
 * `total == 0` significa progreso indeterminado (no hay un total conocido
 * de antemano, como en el monitoreo continuo de procesos); en ese caso
 * `current` no debe interpretarse como una fracción de `total`.
 */
typedef struct {
    const char *phase;
    int current;
    int total;
} ProgressUpdate;

/**
 * El backend puede invocar este callback desde cualquier hilo trabajador.
 * Quien lo implementa es responsable de despachar a su propio hilo principal
 * si necesita tocar una interfaz gráfica (p.ej. con g_idle_add en GTK) — el
 * backend no depende de ningún toolkit de GUI.
 */
typedef void (*ProgressCallback)(const ProgressUpdate *update, void *user_data);

#endif // PROGRESS_H
