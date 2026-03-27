#include <stdio.h>
#include <stdlib.h>

/* ===== READ FILE ===== */
char* read_file(const char* filename) {

    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *data = malloc(size + 1);
    fread(data, 1, size, f);
    data[size] = '\0';

    fclose(f);
    return data;
}