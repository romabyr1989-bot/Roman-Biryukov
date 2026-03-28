#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_NAME "patients.json"

/* ===== SAVE ===== */
int save_patient(const char *json) {

    FILE *f = fopen(FILE_NAME, "a");
    if (!f) return 1;

    fprintf(f, "%s\n", json);

    fclose(f);
    return 0;
}

/* ===== READ ===== */
char* get_patients() {

    FILE *f = fopen(FILE_NAME, "r");
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