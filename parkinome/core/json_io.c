#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include "predict.h"

/* =========================
   Read file
   ========================= */

char* read_file(const char* filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *data = malloc(size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, size, f) != size) {
        fclose(f);
        free(data);
        return NULL;
    }

    data[size] = '\0';
    fclose(f);
    return data;
}

/* =========================
   Parse JSON → struct
   ========================= */

int parse_patient(cJSON *json, parkinome_input_t *in) {

    #define GET(name) \
        cJSON *item_##name = cJSON_GetObjectItem(json, #name); \
        if (!item_##name || !cJSON_IsNumber(item_##name)) return 1; \
        in->name = item_##name->valuedouble;

    GET(age)
    GET(updrs_iii)
    GET(moca)
    GET(scopa_aut)
    GET(hoehn_yahr)

    GET(ndufa4l2)
    GET(ndufs2)
    GET(pink1)
    GET(ppargc1a)
    GET(nlrp3)
    GET(il1b)
    GET(s100a8)
    GET(cxcl8)

    #undef GET

    return 0;
}

/* =========================
   Print JSON (single)
   ========================= */

void print_output_json(const parkinome_output_t *out) {

    const char *cat =
        (out->category == 0) ? "LOW" :
        (out->category == 1) ? "INTERMEDIATE" :
                              "HIGH";

    printf("{\n");
    printf("  \"isp\": %.3f,\n", out->isp);
    printf("  \"risk_probability\": %.3f,\n", out->risk_probability);
    printf("  \"category\": \"%s\"\n", cat);
    printf("}");
}

/* =========================
   Batch processing
   ========================= */

void process_batch(const char *json_str) {

    cJSON *root = cJSON_Parse(json_str);
    if (!root || !cJSON_IsArray(root)) {
        fprintf(stderr, "Error: expected JSON array\n");
        return;
    }

    int n = cJSON_GetArraySize(root);

    printf("[\n");

    for (int i = 0; i < n; i++) {

        cJSON *item = cJSON_GetArrayItem(root, i);

        parkinome_input_t in = {0};
        parkinome_output_t out = {0};

        if (parse_patient(item, &in) != 0) {
            printf("{\"error\": \"invalid input\"}");
        } else {

            int status = parkinome_predict(&in, &out);

            if (status != PARKINOME_OK) {
                printf("{\"error\": %d}", status);
            } else {
                print_output_json(&out);
            }
        }

        if (i < n - 1) printf(",\n");
    }

    printf("\n]\n");

    cJSON_Delete(root);
}
