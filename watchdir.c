#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

// Definición del tamaño máximo de una ruta si no está definido
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define DEFAULT_DIR "."
#define DEFAULT_PATH "/tmp/watchdir.log"
#define DEFAULT_INTERVAL 1
#define DEFAULT_CLEAN 0

// Declaración de variables globales
char *NombreRegistro = DEFAULT_PATH;    // Nombre del archivo de registro
char *dirNombre = DEFAULT_DIR;         // Directorio por defecto es el directorio actual
int intervalo = DEFAULT_INTERVAL;     // Intervalo de actualización predeterminado es 1 segundo
int descriptor;                      // Descriptor de archivo del archivo de registro
int limpio = DEFAULT_CLEAN;         // Indicador de si el archivo de registro ha sido limpiado

// Estructura para almacenar información sobre un archivo
struct FileInfo {
    dev_t dev;       // Identificador de dispositivo
    ino_t ino;      // Número de inodo
    char nombre[256];  // Nombre del archivo
    time_t mtime; // Tiempo de modificación
    off_t size;  // Tamaño del archivo
};

// Función para imprimir el mensaje de uso del programa y finalizar el programa 
void printUso(int exit_code) {
    fprintf(stderr, "Usage: ./watchdir [-n SECONDS] [-l LOG] [DIR]\n"
                    "\tSECONDS Refresh rate in [1..60] seconds [default: 1].\n"
                    "\tLOG     Log file.\n"
                    "\tDIR     Directory name [default: '.'].\n\n");
    exit(exit_code);
}

// Función de comparación para ordenar la estructura FileInfo por nombre de archivo
int compararFileInfo(const void *a, const void *b) {
    struct FileInfo *fileA = (struct FileInfo *)a;
    struct FileInfo *fileB = (struct FileInfo *)b;
    return strncmp(fileA->nombre, fileB->nombre, 256);
}

// Función para formatear el tiempo en el formato específico
char *formatTime(time_t mtime) {
    static char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&mtime));
    return buffer;
}

// Función para actualizar el archivo de registro con los cambios en el directorio
void actualizarArchivo(const struct FileInfo *oldEntrada, int oldContador, const struct FileInfo *newEntrada, int newContador) {
    int oldIndex = 0;
    int newIndex = 0;
    while (oldIndex < oldContador && newIndex < newContador) {
        if (oldEntrada[oldIndex].nombre[0] == '.' || newEntrada[newIndex].nombre[0] == '.') {
            if (oldEntrada[oldIndex].nombre[0] == '.') {
                oldIndex++;
            }
            if (newEntrada[newIndex].nombre[0] == '.') {
                newIndex++;
            }
            continue;
        }

        if (oldEntrada[oldIndex].ino == newEntrada[newIndex].ino &&
            oldEntrada[oldIndex].dev == newEntrada[newIndex].dev) {

            if (strncmp(oldEntrada[oldIndex].nombre, newEntrada[newIndex].nombre, 256) != 0) {
                dprintf(descriptor, "UpdateName: %s -> %s\n", oldEntrada[oldIndex].nombre, newEntrada[newIndex].nombre);
            } else if (oldEntrada[oldIndex].size != newEntrada[newIndex].size) {
                dprintf(descriptor, "UpdateSize: %s: %ld -> %ld\n", newEntrada[newIndex].nombre, (long)oldEntrada[oldIndex].size, (long)newEntrada[newIndex].size);
            } else if (oldEntrada[oldIndex].mtime != newEntrada[newIndex].mtime) {
                dprintf(descriptor, "UpdateMtim: %s: %s -> %s\n", newEntrada[newIndex].nombre, formatTime(oldEntrada[oldIndex].mtime), formatTime(newEntrada[newIndex].mtime));
            }
            oldIndex++;
            newIndex++;
        } else {
            dprintf(descriptor, "Deletion: %s\n", oldEntrada[oldIndex].nombre);
            oldIndex++;
        }
    }

    for (int i = newIndex; i < newContador; i++) {
        if (newEntrada[i].nombre[0] != '.') {
            dprintf(descriptor, "Creation: %s\n", newEntrada[i].nombre);
        }
    }

    for (int i = oldIndex; i < oldContador; i++) {
        if (oldEntrada[i].nombre[0] != '.') {
            dprintf(descriptor, "Deletion: %s\n", oldEntrada[i].nombre);
        }
    }
}

// Función para manejar señales (SIGALRM y SIGUSR1)
void manejadorSignal(int signal) {
    static struct FileInfo *oldEntrada = NULL;
    static int oldContador = 0;

    if (signal == SIGALRM) {
        DIR *dir = opendir(dirNombre);
        if (!dir) {
            // opendir() falló, no hacer nada y simplemente regresar.
            fprintf(stderr, "ERROR: opendir()\n");
            exit(EXIT_FAILURE);  // Cambiar exit por return para evitar bloqueo
        }

        struct FileInfo *newFileInfos = NULL;
        struct dirent *entry;
        int newCount = 0;

        while ((entry = readdir(dir)) != NULL) {
            char filePath[PATH_MAX];
            snprintf(filePath, PATH_MAX, "%s/%s", dirNombre, entry->d_name);
            struct stat fileStat;
            if (stat(filePath, &fileStat) == 0) {
                newFileInfos = realloc(newFileInfos, (newCount + 1) * sizeof(struct FileInfo));
                if (newFileInfos == NULL) {
                    perror("ERROR: realloc()");
                    closedir(dir);
                    return; // Cambiar exit por return para evitar bloqueo
                }
                newFileInfos[newCount].dev = fileStat.st_dev;
                newFileInfos[newCount].ino = entry->d_ino;
                strncpy(newFileInfos[newCount].nombre, entry->d_name, 256);
                newFileInfos[newCount].mtime = fileStat.st_mtime;
                newFileInfos[newCount].size = fileStat.st_size;
                newCount++;
            }
        }
        closedir(dir);

        qsort(newFileInfos, newCount, sizeof(struct FileInfo), compararFileInfo);

        actualizarArchivo(oldEntrada, oldContador, newFileInfos, newCount);

        free(oldEntrada);
        oldEntrada = newFileInfos;
        oldContador = newCount;

    } else if (signal == SIGUSR1) {
        if (!limpio) {
            close(descriptor);
            descriptor = open(NombreRegistro, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (descriptor == -1) {
                perror("ERROR: Cannot open log file");
                return; // Cambiar exit por return para evitar bloqueo
            }
            limpio = 1;
        }
    }
}

// Configura los manejadores de señales SIGALRM y SIGUSR1
void confSignals() {
    struct sigaction sa;
    sa.sa_handler = manejadorSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        fprintf(stderr, "ERROR: sigaction()\n");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        fprintf(stderr, "ERROR: sigaction()\n");
        exit(EXIT_FAILURE);
    }

    manejadorSignal(SIGALRM);
}

// Verifica si la ruta es un directorio
int test_isFolder(char *folder){
    struct stat sfile;

    if (stat(folder, &sfile) == -1) {
        fprintf(stderr, "ERROR: stat()\n");
        exit(EXIT_FAILURE);
    }
    if (!S_ISDIR(sfile.st_mode)) {
        fprintf(stderr, "ERROR: '%s' is not a directory.\n", folder);
        printUso(EXIT_FAILURE);
    }
    return open(folder, O_RDONLY);
}

// Obtiene el número de directorios y los valida
void test_numdir(int argc, char *argv[]) {
    if (optind < argc) {
        dirNombre = argv[optind];
        if (optind + 1 < argc) {
            fprintf(stderr, "ERROR: '%s' does not support more than one directory.\n", argv[0]);
            printUso(EXIT_FAILURE);
        }
    }
}

// Función para procesar las opciones de línea de comandos
void procesarArgumentos(int argc, char *argv[]) {
    int opt = 0;
    while ((opt = getopt(argc, argv, "hn:l:")) != -1) {
        switch (opt) {
            case 'n':
                intervalo = atoi(optarg);
                break;
            case 'l':
                NombreRegistro = optarg;
                break;
            case 'h':
                printUso(EXIT_SUCCESS);
                break;
            case '?':
                printUso(EXIT_FAILURE);
        }
    }
    test_numdir(argc, argv);
}

// Verifica el intervalo establecido
void testIntervalo(int intervalo) {
    if (intervalo < 1 || intervalo > 60) {
        fprintf(stderr, "ERROR: SECONDS must be a value in [1..60].\n");
        printUso(EXIT_FAILURE);
    }
}

// Función para abrir o crear el archivo de registro
int abrirCrearArchivoRegistro(const char *NombreRegistro) {
    int descriptor = open(NombreRegistro, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor == -1) {
        fprintf(stderr, "ERROR: Cannot open log file");
        exit(EXIT_FAILURE);
    }
    return descriptor;
}

// Función para configurar el temporizador para la señal SIGALRM
void configurarTemporizador(int intervalo) {
    struct itimerval timer;
    timer.it_value.tv_sec = intervalo;
    timer.it_value.tv_usec = 0;
    timer.it_interval = timer.it_value;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        fprintf(stderr, "ERROR: setitimer()\n");
        exit(EXIT_FAILURE);
    }
}

// Función principal del programa
int main(int argc, char *argv[]) {
    procesarArgumentos(argc, argv);
    testIntervalo(intervalo);
    int fdfolder = test_isFolder(dirNombre);
    descriptor = abrirCrearArchivoRegistro(NombreRegistro);
    confSignals();
    configurarTemporizador(intervalo);
    while (1) {
        pause();
    }
    close(descriptor);
    return 0;
}
