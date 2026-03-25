#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include "predict.h"

/* JSON API */
char* read_file(const char* filename);
int parse_patient(cJSON *json, parkinome_input_t *in);
void print_output_json(const parkinome_output_t *out);
void process_batch(const char *json_str);

int main(int argc, char **argv) {

    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.json\n", argv[0]);
        return 1;
    }

    /* Read file */
    char *json_str = read_file(argv[1]);
    if (!json_str) {
        fprintf(stderr, "Error: cannot read file\n");
        return 1;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        fprintf(stderr, "Error: invalid JSON\n");
        free(json_str);
        return 1;
    }

    /* ===== Batch ===== */
    if (cJSON_IsArray(root)) {
        process_batch(json_str);
        cJSON_Delete(root);
        free(json_str);
        return 0;
    }

    /* ===== Single ===== */
    parkinome_input_t input = {0};
    parkinome_output_t output = {0};

    if (parse_patient(root, &input) != 0) {
        fprintf(stderr, "Error: invalid input fields\n");
        cJSON_Delete(root);
        free(json_str);
        return 1;
    }

    int status = parkinome_predict(&input, &output);

    if (status != PARKINOME_OK) {
        fprintf(stderr, "Error: model failed (%d)\n", status);
        cJSON_Delete(root);
        free(json_str);
        return 1;
    }

    print_output_json(&output);
    printf("\n");

    cJSON_Delete(root);
    free(json_str);

    return 0;
}