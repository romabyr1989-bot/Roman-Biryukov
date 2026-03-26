#include <stdio.h>
#include <time.h>

void log_info(const char *msg) {
    FILE *f = fopen("server.log", "a");

    time_t t = time(NULL);

    fprintf(f, "[INFO] %s - %s\n", ctime(&t), msg);

    fclose(f);
}

void log_error(const char *msg) {
    FILE *f = fopen("server.log", "a");

    time_t t = time(NULL);

    fprintf(f, "[ERROR] %s - %s\n", ctime(&t), msg);

    fclose(f);
}