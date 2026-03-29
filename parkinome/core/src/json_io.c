#include <stdio.h>
#include <stdlib.h>

/* ===== ЧТЕНИЕ ФАЙЛА ===== */
char* read_file(const char* filename) {

    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *data = malloc((size_t)size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t got = fread(data, 1, (size_t)size, f);
    if (got != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    data[size] = '\0';

    fclose(f);
    return data;
}
