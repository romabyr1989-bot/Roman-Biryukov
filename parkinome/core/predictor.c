#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include "json_io.h"
#include "predict.h"
#include "json_io.h"

/*
 Service layer:
 вход → JSON строка
 выход → JSON строка результата
*/

int run_prediction(const char *input_json, char *output_json, size_t out_size) {

    /* ===== parse ===== */
    cJSON *json = cJSON_Parse(input_json);
    if (!json) return 1;

    parkinome_input_t in = {0};
    parkinome_output_t out = {0};

    if (parse_patient(json, &in) != 0) {
        cJSON_Delete(json);
        return 2;
    }

    cJSON_Delete(json);

    /* ===== core ===== */
    int status = parkinome_predict(&in, &out);
    if (status != PARKINOME_OK) return 3;

    /* ===== format output ===== */
    const char *cat =
        (out.category == 0) ? "LOW" :
        (out.category == 1) ? "INTERMEDIATE" :
                              "HIGH";

    snprintf(output_json, out_size,
        "{ \"isp\": %.3f, \"risk_probability\": %.3f, \"category\": \"%s\" }",
        out.isp,
        out.risk_probability,
        cat
    );

    return 0;
}
int run_batch(const char *input_json, char *output_json, size_t out_size) {

    cJSON *array = cJSON_Parse(input_json);
    if (!array || !cJSON_IsArray(array)) return 1;

    cJSON *results = cJSON_CreateArray();

    int size = cJSON_GetArraySize(array);

    for (int i = 0; i < size; i++) {

        cJSON *item = cJSON_GetArrayItem(array, i);

        parkinome_input_t in = {0};
        parkinome_output_t out = {0};

        if (parse_patient(item, &in) != 0)
            continue;

        if (parkinome_predict(&in, &out) != PARKINOME_OK)
            continue;

        const char *cat =
            (out.category == 0) ? "LOW" :
            (out.category == 1) ? "INTERMEDIATE" :
                                  "HIGH";

        cJSON *res = cJSON_CreateObject();

        cJSON_AddNumberToObject(res, "isp", out.isp);
        cJSON_AddNumberToObject(res, "risk_probability", out.risk_probability);
        cJSON_AddStringToObject(res, "category", cat);

        cJSON_AddItemToArray(results, res);
    }

    char *str = cJSON_PrintUnformatted(results);

    strncpy(output_json, str, out_size);

    free(str);
    cJSON_Delete(results);
    cJSON_Delete(array);

    return 0;
}