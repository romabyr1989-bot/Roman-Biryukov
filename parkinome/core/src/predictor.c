#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cjson/cJSON.h>

#include "predict.h"
#include "model.h"

int predictor_init_model(const char *model_path) {
    const char *path = model_path ? model_path : PARKINOME_MODEL_FILE;
    if (model_load_active(path) == 0) {
        fprintf(stderr, "[predictor] loaded model: %s\n", path);
        return 0;
    }
    fprintf(stderr, "[predictor] model file not found, using built-in defaults\n");
    return 1;
}

static const char *risk_category_str(int category) {
    return (category == 0) ? "LOW" :
           (category == 1) ? "INTERMEDIATE" :
                             "HIGH";
}

static const char *level_str(int level) {
    return (level == 0) ? "LOW" :
           (level == 1) ? "INTERMEDIATE" :
                          "HIGH";
}

static void append_driver_if_relevant(char *buf, size_t size, const char *name, double value, int *first) {
    if (!buf || size == 0 || !name || !first) return;
    if (fabs(value) < 0.25) return;
    if (!(*first)) strncat(buf, ", ", size - strlen(buf) - 1);
    strncat(buf, name, size - strlen(buf) - 1);
    *first = 0;
}

static void build_interpretation(const parkinome_output_t *out, char *buf, size_t size) {
    const char *base;
    char drivers[160] = {0};
    int first = 1;
    if (!out || !buf || size == 0) return;

    if (out->category == 2) base = "High-risk profile; prioritize clinical review and mitigation.";
    else if (out->category == 1) base = "Intermediate profile; monitor trend and key drivers.";
    else base = "Lower risk profile; continue observation.";

    append_driver_if_relevant(drivers, sizeof(drivers), "inflammation", out->breakdown_inflammation, &first);
    append_driver_if_relevant(drivers, sizeof(drivers), "clinical", out->breakdown_clinical, &first);
    append_driver_if_relevant(drivers, sizeof(drivers), "mitochondrial", out->breakdown_mitochondrial, &first);

    if (drivers[0] != '\0') {
        snprintf(buf, size, "%s Risk %.0f%%. Key drivers: %s. Confidence %.2f.",
                 base, out->risk_probability * 100.0, drivers, out->confidence);
    } else {
        snprintf(buf, size, "%s Risk %.0f%%. Confidence %.2f.",
                 base, out->risk_probability * 100.0, out->confidence);
    }
}

/* ===== ПАРСЕР ===== */
static int parse_patient(cJSON *json, parkinome_input_t *in) {

    if (!json || !in) return PARKINOME_NULL_POINTER;

    memset(in, 0, sizeof(*in));

    cJSON *pid = cJSON_GetObjectItem(json, "patient_id");
    if (pid && cJSON_IsString(pid) && pid->valuestring) {
        snprintf(in->patient_id, sizeof(in->patient_id), "%s", pid->valuestring);
        in->has_patient_id = 1;
    } else if (pid && cJSON_IsNumber(pid)) {
        snprintf(in->patient_id, sizeof(in->patient_id), "%.0f", pid->valuedouble);
        in->has_patient_id = 1;
    }

    /* Копируем только поля, которые есть в JSON, и выставляем флаги has_* для модели. */
    #define SET_FIELD(name) { \
        cJSON *item = cJSON_GetObjectItem(json, #name); \
        if (item && cJSON_IsNumber(item)) { \
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
    #undef SET_FIELD

    return PARKINOME_OK;
}

static int append_prediction(cJSON *patient, cJSON *patients_out) {
    parkinome_input_t in = {0};
    parkinome_output_t out = {0};
    cJSON *row = NULL;
    const char *cat = NULL;
    cJSON *breakdown = NULL;
    cJSON *explainability = NULL;
    cJSON *indices = NULL;
    cJSON *levels = NULL;
    cJSON *genes = NULL;
    char interpretation[320] = {0};

    if (parse_patient(patient, &in) != 0) {
        return 1;
    }

    if (parkinome_predict(&in, &out) != PARKINOME_OK) {
        return 1;
    }

    cat = risk_category_str(out.category);

    row = cJSON_CreateObject();
    if (!row) return 1;

    if (in.has_patient_id) {
        cJSON_AddStringToObject(row, "patient_id", in.patient_id);
    }
    cJSON_AddNumberToObject(row, "isp", out.isp);
    cJSON_AddNumberToObject(row, "risk_probability", out.risk_probability);
    cJSON_AddStringToObject(row, "category", cat);
    cJSON_AddNumberToObject(row, "confidence", out.confidence);
    /* Новые биологические индексы для downstream-аналитики/UI. */
    cJSON_AddNumberToObject(row, "mito_score", out.mito_score);
    cJSON_AddNumberToObject(row, "inflam_score", out.inflam_score);
    cJSON_AddNumberToObject(row, "imbalance", out.imbalance);
    build_interpretation(&out, interpretation, sizeof(interpretation));
    cJSON_AddStringToObject(row, "interpretation", interpretation);

    /* UI-friendly structured blocks for interpretability. */
    breakdown = cJSON_CreateObject();
    explainability = cJSON_CreateObject();
    indices = cJSON_CreateObject();
    levels = cJSON_CreateObject();
    genes = cJSON_CreateObject();
    if (!breakdown || !explainability || !indices || !levels || !genes) {
        if (breakdown) cJSON_Delete(breakdown);
        if (explainability) cJSON_Delete(explainability);
        if (indices) cJSON_Delete(indices);
        if (levels) cJSON_Delete(levels);
        if (genes) cJSON_Delete(genes);
        cJSON_Delete(row);
        return 1;
    }

    cJSON_AddNumberToObject(breakdown, "clinical", out.breakdown_clinical);
    cJSON_AddNumberToObject(breakdown, "cognitive", out.breakdown_cognitive);
    cJSON_AddNumberToObject(breakdown, "inflammation", out.breakdown_inflammation);
    cJSON_AddNumberToObject(breakdown, "mitochondrial", out.breakdown_mitochondrial);
    cJSON_AddNumberToObject(breakdown, "imbalance", out.breakdown_imbalance);
    cJSON_AddItemToObject(row, "breakdown", breakdown);
    cJSON_AddNumberToObject(explainability, "clinical", out.breakdown_clinical);
    cJSON_AddNumberToObject(explainability, "inflammation", out.breakdown_inflammation);
    cJSON_AddNumberToObject(explainability, "mitochondrial", out.breakdown_mitochondrial);
    cJSON_AddItemToObject(row, "explainability", explainability);

    cJSON_AddNumberToObject(indices, "mito", out.mito_index);
    cJSON_AddNumberToObject(indices, "inflammation", out.inflam_index);
    cJSON_AddNumberToObject(indices, "stress", out.stress_index);
    cJSON_AddItemToObject(row, "indices", indices);

    cJSON_AddStringToObject(levels, "mitochondrial", level_str(out.mito_level));
    cJSON_AddStringToObject(levels, "inflammation", level_str(out.inflam_level));
    cJSON_AddItemToObject(row, "levels", levels);

    if (in.has_ndufa4l2) cJSON_AddNumberToObject(genes, "ndufa4l2", in.ndufa4l2);
    if (in.has_ndufs2) cJSON_AddNumberToObject(genes, "ndufs2", in.ndufs2);
    if (in.has_pink1) cJSON_AddNumberToObject(genes, "pink1", in.pink1);
    if (in.has_ppargc1a) cJSON_AddNumberToObject(genes, "ppargc1a", in.ppargc1a);
    if (in.has_nlrp3) cJSON_AddNumberToObject(genes, "nlrp3", in.nlrp3);
    if (in.has_il1b) cJSON_AddNumberToObject(genes, "il1b", in.il1b);
    if (in.has_s100a8) cJSON_AddNumberToObject(genes, "s100a8", in.s100a8);
    if (in.has_cxcl8) cJSON_AddNumberToObject(genes, "cxcl8", in.cxcl8);
    cJSON_AddItemToObject(row, "genes", genes);

    cJSON_AddItemToArray(patients_out, row);
    return 0;
}

/* ===== ДИНАМИЧЕСКИЙ ВХОД ===== */
/* Принимает:
 * - объект пациента;
 * - массив пациентов;
 * - объект вида { "patients": [...] }.
 * Всегда возвращает единый JSON-формат:
 * { "count": N, "patients": [ ... ] } */
int run_prediction(const char *body, char *result, size_t size) {
    cJSON *root = NULL;
    cJSON *patients_in = NULL;
    cJSON *out = NULL;
    cJSON *patients_out = NULL;
    char *serialized = NULL;
    int count = 0;

    if (!body || !result || size == 0) return 1;

    root = cJSON_Parse(body);
    if (!root) return 1;

    if (cJSON_IsObject(root)) {
        patients_in = cJSON_GetObjectItem(root, "patients");
        if (patients_in && !cJSON_IsArray(patients_in)) {
            cJSON_Delete(root);
            return 1;
        }
    } else if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return 1;
    }

    out = cJSON_CreateObject();
    patients_out = cJSON_CreateArray();
    if (!out || !patients_out) {
        if (out) cJSON_Delete(out);
        if (patients_out) cJSON_Delete(patients_out);
        cJSON_Delete(root);
        return 1;
    }

    cJSON_AddItemToObject(out, "patients", patients_out);

    if (!patients_in && cJSON_IsObject(root)) {
        /* Одиночный объект пациента */
        if (append_prediction(root, patients_out) != 0) {
            cJSON_Delete(out);
            cJSON_Delete(root);
            return 1;
        }
    } else {
        /* Массив пациентов: считаем прогноз для каждого элемента. */
        cJSON *source = patients_in ? patients_in : root;
        int n = cJSON_GetArraySize(source);
        if (n <= 0) {
            cJSON_Delete(out);
            cJSON_Delete(root);
            return 1;
        }
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(source, i);
            if (!item || !cJSON_IsObject(item) || append_prediction(item, patients_out) != 0) {
                cJSON_Delete(out);
                cJSON_Delete(root);
                return 1;
            }
        }
    }

    count = cJSON_GetArraySize(patients_out);
    cJSON_AddNumberToObject(out, "count", count);

    serialized = cJSON_PrintUnformatted(out);
    if (!serialized) {
        cJSON_Delete(out);
        cJSON_Delete(root);
        return 1;
    }

    if (strlen(serialized) >= size) {
        free(serialized);
        cJSON_Delete(out);
        cJSON_Delete(root);
        return 1;
    }

    strncpy(result, serialized, size);
    result[size - 1] = '\0';

    free(serialized);
    cJSON_Delete(out);
    cJSON_Delete(root);
    return 0;
}
