#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

#include "predict.h"

/* ===== PARSER ===== */
int parse_patient(cJSON *json, parkinome_input_t *in) {

    if (!json || !in) return PARKINOME_NULL_POINTER;

    memset(in, 0, sizeof(*in));

    #define SET_FIELD(name) { \
        cJSON *item = cJSON_GetObjectItem(json, #name); \
        if (item) { \
            in->name = item->valuedouble; \
            in->has_##name = 1; \
        } \
    }

    SET_FIELD(age);
    SET_FIELD(updrs_iii);
    SET_FIELD(moca);
    SET_FIELD(scopa_aut);
    SET_FIELD(hoehn_yahr);

    SET_FIELD(ndufa4l2);
    SET_FIELD(ndufs2);
    SET_FIELD(pink1);
    SET_FIELD(ppargc1a);
    SET_FIELD(nlrp3);
    SET_FIELD(il1b);
    SET_FIELD(s100a8);
    SET_FIELD(cxcl8);

    return PARKINOME_OK;
}

/* ===== SINGLE ===== */
int run_prediction(const char *body, char *result, size_t size) {

    cJSON *json = cJSON_Parse(body);
    if (!json) return 1;

    parkinome_input_t in = {0};
    parkinome_output_t out = {0};

    if (parse_patient(json, &in) != 0) {
        cJSON_Delete(json);
        return 1;
    }

    cJSON_Delete(json);

    if (parkinome_predict(&in, &out) != PARKINOME_OK) {
        return 1;
    }

    const char *cat =
        (out.category == 0) ? "LOW" :
        (out.category == 1) ? "INTERMEDIATE" :
                              "HIGH";

    snprintf(result, size,
        "{ \"isp\": %.3f, \"risk_probability\": %.3f, \"category\": \"%s\", \"confidence\": %.2f }",
        out.isp,
        out.risk_probability,
        cat,
        out.confidence
    );

    return 0;
}

/* ===== BATCH ===== */
int run_batch(const char *body, char *result, size_t size) {

    cJSON *array = cJSON_Parse(body);
    if (!array || !cJSON_IsArray(array)) return 1;

    char buffer[4096] = "[";
    int first = 1;

    for (int i = 0; i < cJSON_GetArraySize(array); i++) {

        cJSON *item = cJSON_GetArrayItem(array, i);

        char res[512];

        char *str = cJSON_PrintUnformatted(item);
        if (!str) continue;

        if (run_prediction(str, res, sizeof(res)) != 0) {
            free(str);
            continue;
        }

        free(str);

        if (!first) strcat(buffer, ",");
        strcat(buffer, res);

        first = 0;
    }

    strcat(buffer, "]");

    strncpy(result, buffer, size);

    cJSON_Delete(array);
    return 0;
}