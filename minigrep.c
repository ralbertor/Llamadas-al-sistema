#define _POSIX_C_SOURCE 200809L
// Bibliotecas necesarias
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <regex.h>
#include <unistd.h>
#include <string.h>

// Definiciones de constantes
#define DEFAULT_BUFSIZE 1024
#define MIN_BUFSIZE 1
#define MAX_BUFSIZE 1048576
#define MAX_LINE_SIZE 4096

// Función para imprimir el uso del programa
void printUsage(int exit_code) {
    fprintf(stderr, "Uso: ./minigrep -r REGEX [-s BUFSIZE] [-v] [-c] [-h]\n"
                    "\t-r REGEX Expresión regular.\n"
                    "\t-s BUFSIZE Tamaño de los buffers de lectura y escritura en bytes (por defecto, 1024).\n"
                    "\t-v Acepta las líneas que NO sean reconocidas por la expresión regular (por defecto, falso).\n"
                    "\t-c Muestra el número total de líneas aceptadas (por defecto, falso).\n\n");
    exit(exit_code); // Sale con el código de salida proporcionado
}

// Función para verificar si los argumentos están en un rango válido
void verificarArgumentos(int bufsize, int regex_flag) {
    // Si el bufsize no está entre el rango de MIN-MAX debe dar un error
    if (bufsize < MIN_BUFSIZE || bufsize > MAX_BUFSIZE) {
        fprintf(stderr, "ERROR: BUFSIZE debe ser mayor que 0 y menor que o igual a 1 MB\n");
        exit(EXIT_FAILURE);
    }
    // Si no nos han pasado ninguna expresión regular debemos dar un error
    if (!regex_flag) {
        fprintf(stderr, "ERROR: REGEX vacía\n");
        exit(EXIT_FAILURE);
    }
}

// Función para procesar los argumentos de la línea de comandos
void procesarArgumentos(int argc, char *argv[], regex_t *regex, int *bufsize, int *regex_flag, int *count_flag) {
    int opt;
    while ((opt = getopt(argc, argv, "r:s:vhc")) != -1) {
        switch (opt) {
        case 'r':
            // Comprobar si la expresión regular está bien construida
            if (regcomp(regex, optarg, REG_EXTENDED | REG_NEWLINE) != 0) {
                fprintf(stderr, "ERROR: REGEX mal construida\n");
                exit(EXIT_FAILURE);
            }
            *regex_flag = 1; // Marcar que se ha proporcionado la expresión regular
            break;
        case 's':
            *bufsize = atoi(optarg);
            break;
        case 'v':
            // Modo inverso: mostrar las líneas que NO coinciden con la expresión regular
            *regex_flag = -1;
            break;
        case 'c':
            *count_flag = 1;
            break;
        case 'h':
            // Opciones de ayuda
            printUsage(EXIT_SUCCESS); // Muestra cómo usar el programa y sale con éxito
        // Cualquier otro argumento
        default:
            printUsage(EXIT_FAILURE); // Muestra cómo usar el programa y sale con error
        }
    }
}

// Función para agregar datos leídos al buffer temporal
void addlectura(char *temp_buffer, ssize_t *btemp_len, char *read_buffer, ssize_t bytes_read) {
    // Copiamos lo que leemos al buffer temporal
    for (ssize_t i = 0; i < bytes_read; ++i) {
        temp_buffer[*btemp_len + i] = read_buffer[i];
    }
    *btemp_len += bytes_read;
}

// Función para ajustar el buffer temporal eliminando las líneas ya procesadas
void ajustarbuffer(char *temp_buffer, ssize_t *btemp_len, char *line_start, ssize_t temp_buffer_size) {
    // Calcular la cantidad de caracteres no procesados
    ssize_t remaining_chars = *btemp_len - (line_start - temp_buffer);

    // Mover los caracteres no procesados hacia el final del buffer temporal
    for (ssize_t i = 0; i < remaining_chars; ++i) {
        temp_buffer[i] = line_start[i];
    }

    // Actualizar la longitud del buffer temporal
    *btemp_len = remaining_chars;

}

// Función para ajustar los trozos de datos en el buffer temporal
void ajustarTrozos(char *temp_buffer, ssize_t *btemp_len, char *line_start, ssize_t bytes_read, char *read_buffer) {
    // Comprobar si quedan caracteres sin procesar para meterlos al temp_buffer
    if (line_start < read_buffer + bytes_read) {
        // Calcular la cantidad de caracteres sin procesar
        ssize_t remaining_chars = read_buffer + bytes_read - line_start;

        // Copiar los caracteres no procesados al temp_buffer
        for (ssize_t i = 0; i < remaining_chars; ++i) {
            temp_buffer[*btemp_len + i] = line_start[i];
        }
        // Actualizar la longitud del buffer temporal
        *btemp_len += remaining_chars;
    }
}

// Función para escribir en el flujo de salida
ssize_t escribir(int fd, char *buf, size_t size) {
    ssize_t num_written, size_left = size;
    char *buf_left = buf;
    while (size_left > 0 && (num_written = write(fd, buf_left, size_left)) != -1) {
        size_left -= num_written;
        buf_left += num_written;
    }
    // Verificar errores durante la escritura
    return num_written == -1 ? -1 : size;
}

// Función para procesar las líneas en el buffer (combinada)
void procesarLineaBuffer(regex_t *regex, int regex_flag, char *line_start, char *buffer, ssize_t buffer_size, ssize_t bytes_read, char *temp_buffer, ssize_t *btemp_len, char *writeBuffer, ssize_t temp_buffer_size, int *match_count, int count_flag, int is_temp_buffer) {

    char *line_end = strchr(line_start, '\n');
    
    while (line_end != NULL) {
        *line_end = '\0'; // Apunto al final de la cadena
        int match = regexec(regex, line_start, 0, NULL, 0); // Compruebo si hay coincidencia
        
        if ((match == 0 && regex_flag != -1) || (match != 0 && regex_flag == -1)) {
            (*match_count)++;
            if (!count_flag) {
                // Copiar la cadena coincidente al buffer de escritura
                char *write_ptr = writeBuffer;
                char *read_ptr = line_start;
                
                while (*read_ptr != '\0' && *read_ptr != '\n') {
                    *write_ptr++ = *read_ptr++;
                }
                *write_ptr = '\n'; // Añadir un carácter de nueva línea después de la cadena coincidente
                ssize_t num_written = escribir(STDOUT_FILENO, writeBuffer, write_ptr - writeBuffer + 1); // Escribir
                if (num_written < 0) {
                    fprintf(stderr, "ERROR: write()\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
        
        line_start = line_end + 1; // Ajustar índices para comprobar la siguiente cadena
        line_end = strchr(line_start, '\n');
    }
    
    if (is_temp_buffer) {
        if (*btemp_len > MAX_LINE_SIZE) {
            fprintf(stderr, "ERROR: Línea demasiado larga\n");
            exit(EXIT_FAILURE);
        }
        // Borrar las cadenas que ya han sido procesadas
        ajustarbuffer(temp_buffer, btemp_len, line_start, temp_buffer_size);
    } else {
        // Ajustar los trozos en el buffer temporal
        ajustarTrozos(temp_buffer, btemp_len, line_start, bytes_read, buffer);
    }
}

// Función para procesar la cadena final en el buffer temporal
void procesarCadenaFinal(regex_t *regex, int regex_flag, char *line_start, char *temp_buffer, ssize_t *btemp_len, char *writeBuffer, int *match_count, int count_flag) {

    line_start = temp_buffer;
    temp_buffer[*btemp_len] = '\n';  // Añadir un carácter de nueva línea al final del buffer temporal
    (*btemp_len)++;
    if (*btemp_len > MAX_LINE_SIZE) {
        fprintf(stderr, "ERROR: Línea demasiado larga\n");
        exit(EXIT_FAILURE);
    }

    int match = regexec(regex, line_start, 0, NULL, 0); // Comprobar si hay coincidencia en el buffer temporal
    
    if ((match == 0 && regex_flag != -1) || (match != 0 && regex_flag == -1)) {
        (*match_count)++;
        if (!count_flag) {
            // Copiar la cadena coincidente al buffer de escritura
            char *write_ptr = writeBuffer;
            char *read_ptr = line_start;
            
            while (*read_ptr != '\0' && *read_ptr != '\n') {
                *write_ptr++ = *read_ptr++;
            }
            
            *write_ptr = '\n'; // Añadir un carácter de nueva línea después de la cadena coincidente
            ssize_t num_written = escribir(STDOUT_FILENO, writeBuffer, write_ptr - writeBuffer + 1); // Escribir 
            if (num_written < 0) {
                fprintf(stderr, "ERROR: write()\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}

// Función principal que ejecuta la lógica principal del programa
void minigrep(regex_t *regex, int regex_flag, int bufsize, int count_flag) {
    // Variables
    ssize_t bytes_read;
    ssize_t btemp_len = 0;
    ssize_t temp_buffer_size = MAX_LINE_SIZE;
    int match_count = 0;

    // Inicializo buffers
    char *read_buffer = (char *)malloc(bufsize * sizeof(char));
    if (read_buffer == NULL) {
        fprintf(stderr, "ERROR: malloc()\n");
        exit(EXIT_FAILURE);
    }
    char *writeBuffer = (char *)malloc(bufsize * sizeof(char));
    if (writeBuffer == NULL) {
        fprintf(stderr, "ERROR: malloc()\n");
        free(read_buffer);
        exit(EXIT_FAILURE);
    }
    char *temp_buffer = (char *)malloc(temp_buffer_size * sizeof(char));
    if (temp_buffer == NULL) {
        fprintf(stderr, "ERROR: malloc()\n");
        free(read_buffer);
        free(writeBuffer);
        exit(EXIT_FAILURE);
    }

    // Punteros que marcaran el principio y final de las lineas
    char *line_start;
    
    // While(que no haya leido todo)
    while ((bytes_read = read(STDIN_FILENO, read_buffer, bufsize)) > 0) {
        if(btemp_len > 0) { // Caso para cuando tenemos trozos en el buffer temporal
            // Añado lo leido al buffer temporal 
            addlectura(temp_buffer, &btemp_len, read_buffer, bytes_read);

            // Proceso el buffer temporal
            line_start = temp_buffer;
            procesarLineaBuffer(regex, regex_flag, line_start, temp_buffer, bufsize, bytes_read, temp_buffer, &btemp_len, writeBuffer, temp_buffer_size, &match_count, count_flag, 1);
        } else { // Caso para cuando tengo todo en el buffer de lectura y no necesito el buffer temporal
            line_start = read_buffer;
            // Proceso el buffer de lectura
            procesarLineaBuffer(regex, regex_flag, line_start, read_buffer, bufsize, bytes_read, temp_buffer, &btemp_len, writeBuffer, temp_buffer_size, &match_count, count_flag, 0);
        }
    }

    // Comprobar si hubo un error de lectura
    if (bytes_read == -1) {
        fprintf(stderr, "ERROR: read()\n");
        free(read_buffer);
        free(writeBuffer);
        free(temp_buffer);
        exit(EXIT_FAILURE);
    }

    // Comprobar el caso de que queden cadenas por procesar sin /n al final
    if (btemp_len > 0) {
        procesarCadenaFinal(regex, regex_flag, line_start, temp_buffer, &btemp_len, writeBuffer, &match_count, count_flag);
    }
    if (count_flag) {
        printf("%d\n", match_count);
    }

    // Liberar la memoria de los buffers
    free(read_buffer);
    free(writeBuffer);
}

// Función principal del programa
int main(int argc, char *argv[]) {
    int regex_flag = 0;
    int count_flag = 0;
    regex_t regex;                 // Variables para la expresion regular
    int bufsize = DEFAULT_BUFSIZE; // Tamaño del buffer por defecto que usaremos para leer y escribir

    procesarArgumentos(argc, argv, &regex, &bufsize, &regex_flag, &count_flag);  // Coger parametros con getopt
    verificarArgumentos(bufsize, regex_flag);                                   // Comprobar si los parametros estan en nuestro rango
    minigrep(&regex, regex_flag, bufsize, count_flag);                          // Procesar lineas
    return EXIT_SUCCESS;
}
