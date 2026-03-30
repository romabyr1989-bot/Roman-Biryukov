#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#include <cjson/cJSON.h>

#include "train.h"
#include "model.h"
#include "json_io.h"

#define TRAIN_DEFAULT_LR 0.01
#define TRAIN_DEFAULT_EPOCHS 1200
#define TRAIN_DEFAULT_L2 0.001

typedef struct {
    double x[PARKINOME_FEATURE_COUNT];
    int y;
    char *json_key;
    char *patient_id;
} sample_t;

typedef struct {
    int tp;
    int fp;
    int tn;
    int fn;
} confusion_t;

typedef struct {
    double prob;
    int y;
} scored_sample_t;

static const char *k_feature_names[PARKINOME_FEATURE_COUNT] = {
    "age", "updrs_iii", "moca", "scopa_aut", "hoehn_yahr",
    "ndufa4l2", "ndufs2", "pink1", "ppargc1a",
    "nlrp3", "il1b", "s100a8", "cxcl8"
};

static char* trim_copy(const char *s) {
    size_t start = 0;
    size_t end;
    size_t len;
    char *out;
    if (!s) return NULL;
    len = strlen(s);
    while (start < len && isspace((unsigned char)s[start])) start++;
    if (start == len) return NULL;
    end = len - 1;
    while (end > start && isspace((unsigned char)s[end])) end--;
    out = (char*)malloc(end - start + 2);
    if (!out) return NULL;
    memcpy(out, s + start, end - start + 1);
    out[end - start + 1] = '\0';
    return out;
}

static char* normalize_patient_id(const cJSON *obj) {
    cJSON *pid;
    char buf[128];
    char *trimmed;
    size_t i;

    if (!obj || !cJSON_IsObject(obj)) return NULL;
    pid = cJSON_GetObjectItem((cJSON*)obj, "patient_id");
    if (!pid) return NULL;

    buf[0] = '\0';
    if (cJSON_IsString(pid) && pid->valuestring) {
        snprintf(buf, sizeof(buf), "%s", pid->valuestring);
    } else if (cJSON_IsNumber(pid)) {
        snprintf(buf, sizeof(buf), "%.0f", pid->valuedouble);
    } else {
        return NULL;
    }

    trimmed = trim_copy(buf);
    if (!trimmed) return NULL;

    for (i = 0; trimmed[i] != '\0'; i++) {
        trimmed[i] = (char)toupper((unsigned char)trimmed[i]);
    }

    return trimmed;
}

static int parse_target_value(cJSON *obj, int *y_out) {
    const char *keys[] = {"target", "label", "outcome", "class", "y", "risk"};
    size_t i;
    cJSON *v = NULL;
    if (!obj || !y_out) return 1;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        v = cJSON_GetObjectItem(obj, keys[i]);
        if (!v) continue;
        if (cJSON_IsNumber(v)) {
            *y_out = (v->valuedouble >= 0.5) ? 1 : 0;
            return 0;
        }
        if (cJSON_IsBool(v)) {
            *y_out = cJSON_IsTrue(v) ? 1 : 0;
            return 0;
        }
        if (cJSON_IsString(v) && v->valuestring) {
            char tmp[32];
            size_t j;
            snprintf(tmp, sizeof(tmp), "%s", v->valuestring);
            for (j = 0; tmp[j]; j++) tmp[j] = (char)tolower((unsigned char)tmp[j]);
            if (!strcmp(tmp, "1") || !strcmp(tmp, "high") || !strcmp(tmp, "positive") || !strcmp(tmp, "yes")) { *y_out = 1; return 0; }
            if (!strcmp(tmp, "0") || !strcmp(tmp, "low") || !strcmp(tmp, "negative") || !strcmp(tmp, "no")) { *y_out = 0; return 0; }
        }
    }

    return 1;
}

static void extract_features_from_json(cJSON *obj, double x[PARKINOME_FEATURE_COUNT]) {
    int i;
    for (i = 0; i < PARKINOME_FEATURE_COUNT; i++) {
        cJSON *v = cJSON_GetObjectItem(obj, k_feature_names[i]);
        x[i] = (v && cJSON_IsNumber(v)) ? v->valuedouble : 0.0;
    }
}

static int same_features(const double a[PARKINOME_FEATURE_COUNT], const double b[PARKINOME_FEATURE_COUNT]) {
    int i;
    for (i = 0; i < PARKINOME_FEATURE_COUNT; i++) {
        if (fabs(a[i] - b[i]) > 1e-12) return 0;
    }
    return 1;
}

static int parse_samples(const char *json_text, sample_t **out_samples, int *out_count) {
    cJSON *root = NULL;
    cJSON *arr = NULL;
    int n;
    int i;
    sample_t *samples = NULL;
    int count = 0;

    if (!json_text || !out_samples || !out_count) return 1;

    *out_samples = NULL;
    *out_count = 0;

    root = cJSON_Parse(json_text);
    if (!root) return 1;

    if (cJSON_IsArray(root)) {
        arr = root;
    } else if (cJSON_IsObject(root)) {
        cJSON *patients = cJSON_GetObjectItem(root, "patients");
        arr = (patients && cJSON_IsArray(patients)) ? patients : NULL;
    }

    if (!arr) {
        cJSON_Delete(root);
        return 1;
    }

    n = cJSON_GetArraySize(arr);
    if (n <= 0) {
        cJSON_Delete(root);
        return 1;
    }

    samples = (sample_t*)calloc((size_t)n, sizeof(sample_t));
    if (!samples) {
        cJSON_Delete(root);
        return 1;
    }

    for (i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        char *serialized = NULL;
        int y = 0;
        int dup = 0;
        int k;

        if (!item || !cJSON_IsObject(item)) continue;
        if (parse_target_value(item, &y) != 0) continue;

        samples[count].patient_id = normalize_patient_id(item);
        if (samples[count].patient_id) {
            cJSON_ReplaceItemInObject(item, "patient_id", cJSON_CreateString(samples[count].patient_id));
        }

        extract_features_from_json(item, samples[count].x);
        samples[count].y = y;

        serialized = cJSON_PrintUnformatted(item);
        if (!serialized) continue;
        samples[count].json_key = serialized;

        for (k = 0; k < count; k++) {
            if ((samples[k].json_key && !strcmp(samples[k].json_key, samples[count].json_key)) ||
                same_features(samples[k].x, samples[count].x)) {
                dup = 1;
                break;
            }
        }

        if (dup) {
            free(samples[count].json_key);
            samples[count].json_key = NULL;
            free(samples[count].patient_id);
            samples[count].patient_id = NULL;
            continue;
        }

        count++;
    }

    if (count <= 0) {
        free(samples);
        cJSON_Delete(root);
        return 1;
    }

    *out_samples = samples;
    *out_count = count;

    cJSON_Delete(root);
    return 0;
}

static void free_samples(sample_t *samples, int count) {
    int i;
    if (!samples) return;
    for (i = 0; i < count; i++) {
        free(samples[i].json_key);
        free(samples[i].patient_id);
    }
    free(samples);
}

static void shuffle_indices(int *idx, int n) {
    int i;
    for (i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
    }
}

static void fit_logreg(const sample_t *samples, const int *idx, int n, const train_config_t *cfg, logistic_model_t *model) {
    int e;
    int j;
    double b = 0.0;
    double w[PARKINOME_FEATURE_COUNT] = {0};

    for (e = 0; e < cfg->epochs; e++) {
        double gb = 0.0;
        double gw[PARKINOME_FEATURE_COUNT] = {0};
        int i;

        for (i = 0; i < n; i++) {
            const sample_t *s = &samples[idx[i]];
            double z = b;
            double p;
            double err;

            for (j = 0; j < PARKINOME_FEATURE_COUNT; j++) {
                z += w[j] * s->x[j];
            }

            p = model_sigmoid(z);
            err = p - (double)s->y;
            gb += err;

            for (j = 0; j < PARKINOME_FEATURE_COUNT; j++) {
                gw[j] += err * s->x[j] + cfg->l2 * w[j];
            }
        }

        b -= cfg->learning_rate * (gb / (double)n);
        for (j = 0; j < PARKINOME_FEATURE_COUNT; j++) {
            w[j] -= cfg->learning_rate * (gw[j] / (double)n);
        }

        if ((e + 1) % 200 == 0 || e == 0 || e + 1 == cfg->epochs) {
            fprintf(stderr, "[train] epoch=%d/%d\n", e + 1, cfg->epochs);
        }
    }

    model->intercept = b;
    for (j = 0; j < PARKINOME_FEATURE_COUNT; j++) model->weights[j] = w[j];
}

static confusion_t evaluate(const sample_t *samples, const int *idx, int n, const logistic_model_t *model) {
    confusion_t c = {0,0,0,0};
    int i;

    for (i = 0; i < n; i++) {
        const sample_t *s = &samples[idx[i]];
        double p = model_predict_probability_from_features(model, s->x);
        int pred = (p >= 0.5) ? 1 : 0;
        if (pred == 1 && s->y == 1) c.tp++;
        else if (pred == 1 && s->y == 0) c.fp++;
        else if (pred == 0 && s->y == 0) c.tn++;
        else c.fn++;
    }

    return c;
}

static void confusion_to_metrics(confusion_t c, training_result_t *r) {
    double total = (double)(c.tp + c.fp + c.tn + c.fn);
    r->accuracy = (total > 0) ? ((double)(c.tp + c.tn) / total) : 0.0;
    r->precision = (c.tp + c.fp > 0) ? ((double)c.tp / (double)(c.tp + c.fp)) : 0.0;
    r->recall = (c.tp + c.fn > 0) ? ((double)c.tp / (double)(c.tp + c.fn)) : 0.0;
    r->specificity = (c.tn + c.fp > 0) ? ((double)c.tn / (double)(c.tn + c.fp)) : 0.0;
}

static int compare_scored_desc(const void *a, const void *b) {
    const scored_sample_t *sa = (const scored_sample_t*)a;
    const scored_sample_t *sb = (const scored_sample_t*)b;
    if (sa->prob < sb->prob) return 1;
    if (sa->prob > sb->prob) return -1;
    return 0;
}

/* Строит ROC на тестовой части:
   сортируем вероятности по убыванию и двигаем порог сверху вниз. */
static void compute_roc_from_test(
    const sample_t *samples,
    const int *idx,
    int n,
    const logistic_model_t *model,
    training_result_t *result
) {
    scored_sample_t *scored = NULL;
    int positives = 0;
    int negatives = 0;
    int i;
    int points = 0;
    double prev_fpr = 0.0;
    double prev_tpr = 0.0;
    double auc = 0.0;
    int step;
    int tp, fp, tn, fn;

    if (!samples || !idx || !model || !result || n <= 0) {
        if (result) {
            result->roc_points = 2;
            result->roc_fpr[0] = 0.0; result->roc_tpr[0] = 0.0;
            result->roc_fpr[1] = 1.0; result->roc_tpr[1] = 1.0;
            result->auc = 0.0;
        }
        return;
    }

    scored = (scored_sample_t*)malloc(sizeof(scored_sample_t) * (size_t)n);
    if (!scored) {
        result->roc_points = 2;
        result->roc_fpr[0] = 0.0; result->roc_tpr[0] = 0.0;
        result->roc_fpr[1] = 1.0; result->roc_tpr[1] = 1.0;
        result->auc = 0.0;
        return;
    }

    for (i = 0; i < n; i++) {
        const sample_t *s = &samples[idx[i]];
        scored[i].prob = model_predict_probability_from_features(model, s->x);
        scored[i].y = s->y;
        if (s->y == 1) positives++;
        else negatives++;
    }

    /* Сортировка нужна для корректного threshold sweep. */
    qsort(scored, (size_t)n, sizeof(scored_sample_t), compare_scored_desc);

    if (positives == 0 || negatives == 0) {
        /* В тесте только один класс: полноценная ROC не определена. */
        result->roc_points = 2;
        result->roc_fpr[0] = 0.0; result->roc_tpr[0] = 0.0;
        result->roc_fpr[1] = 1.0; result->roc_tpr[1] = 1.0;
        result->auc = 0.5;
        free(scored);
        return;
    }

    /* Прореживаем точки, если выборка большая, чтобы не отдавать слишком длинный массив. */
    step = (n > TRAIN_ROC_MAX_POINTS - 2) ? (n / (TRAIN_ROC_MAX_POINTS - 2) + 1) : 1;
    tp = 0; fp = 0; tn = negatives; fn = positives;

    result->roc_fpr[points] = 0.0;
    result->roc_tpr[points] = 0.0;
    points++;

    for (i = 0; i < n; i++) {
        if (scored[i].y == 1) { tp++; fn--; }
        else { fp++; tn--; }

        if (((i + 1) % step == 0) || i + 1 == n) {
            double tpr = (tp + fn > 0) ? ((double)tp / (double)(tp + fn)) : 0.0;
            double fpr = (fp + tn > 0) ? ((double)fp / (double)(fp + tn)) : 0.0;

            if (points < TRAIN_ROC_MAX_POINTS - 1) {
                result->roc_fpr[points] = fpr;
                result->roc_tpr[points] = tpr;
                points++;
            }
        }
    }

    result->roc_fpr[points] = 1.0;
    result->roc_tpr[points] = 1.0;
    points++;

    /* AUC считаем по правилу трапеций в пространстве FPR/TPR. */
    for (i = 1; i < points; i++) {
        double x = result->roc_fpr[i];
        double y = result->roc_tpr[i];
        auc += (x - prev_fpr) * (y + prev_tpr) * 0.5;
        prev_fpr = x;
        prev_tpr = y;
    }

    result->roc_points = points;
    result->auc = auc;
    free(scored);
}

void train_default_config(train_config_t *cfg) {
    if (!cfg) return;
    cfg->learning_rate = TRAIN_DEFAULT_LR;
    cfg->epochs = TRAIN_DEFAULT_EPOCHS;
    cfg->l2 = TRAIN_DEFAULT_L2;
    cfg->seed = (unsigned int)time(NULL);
}

int train_and_save_model_from_json(const char *json_text, const train_config_t *cfg_in, const char *model_path, training_result_t *result) {
    train_config_t cfg;
    sample_t *samples = NULL;
    int n = 0;
    int *idx = NULL;
    int train_n;
    logistic_model_t model;
    confusion_t cm;

    if (!json_text || !result) return 1;

    train_default_config(&cfg);
    if (cfg_in) {
        if (cfg_in->learning_rate > 0.0) cfg.learning_rate = cfg_in->learning_rate;
        if (cfg_in->epochs > 0) cfg.epochs = cfg_in->epochs;
        if (cfg_in->l2 >= 0.0) cfg.l2 = cfg_in->l2;
        if (cfg_in->seed != 0) cfg.seed = cfg_in->seed;
    }

    if (parse_samples(json_text, &samples, &n) != 0 || n < 2) {
        free_samples(samples, n);
        return 1;
    }

    idx = (int*)malloc(sizeof(int) * (size_t)n);
    if (!idx) {
        free_samples(samples, n);
        return 1;
    }

    for (int i = 0; i < n; i++) idx[i] = i;

    srand(cfg.seed);
    shuffle_indices(idx, n);

    train_n = (int)((double)n * 0.8);
    if (train_n < 1) train_n = 1;
    if (train_n >= n) train_n = n - 1;

    fit_logreg(samples, idx, train_n, &cfg, &model);

    cm = evaluate(samples, idx + train_n, n - train_n, &model);
    memset(result, 0, sizeof(*result));
    confusion_to_metrics(cm, result);
    result->samples = n;
    result->train_size = train_n;
    result->test_size = n - train_n;
    result->intercept = model.intercept;
    compute_roc_from_test(samples, idx + train_n, n - train_n, &model, result);

    if (model_save(model_path ? model_path : PARKINOME_MODEL_FILE, &model) != 0) {
        free(idx);
        free_samples(samples, n);
        return 1;
    }

    model_set_active(&model);

    free(idx);
    free_samples(samples, n);
    return 0;
}

static int parse_cfg_from_body(cJSON *root, train_config_t *cfg) {
    cJSON *v;
    if (!root || !cfg) return 1;
    v = cJSON_GetObjectItem(root, "learning_rate");
    if (v && cJSON_IsNumber(v) && v->valuedouble > 0) cfg->learning_rate = v->valuedouble;
    v = cJSON_GetObjectItem(root, "epochs");
    if (v && cJSON_IsNumber(v) && v->valuedouble > 0) cfg->epochs = (int)v->valuedouble;
    v = cJSON_GetObjectItem(root, "l2");
    if (v && cJSON_IsNumber(v) && v->valuedouble >= 0) cfg->l2 = v->valuedouble;
    v = cJSON_GetObjectItem(root, "seed");
    if (v && cJSON_IsNumber(v) && v->valuedouble > 0) cfg->seed = (unsigned int)v->valuedouble;
    return 0;
}

int run_training(const char *body, char *result_json, size_t size) {
    const char *train_json = NULL;
    char *fallback_json = NULL;
    cJSON *root = NULL;
    cJSON *dataset = NULL;
    char *dataset_text = NULL;
    train_config_t cfg;
    training_result_t res;
    cJSON *resp = NULL;
    cJSON *roc = NULL;
    cJSON *fpr = NULL;
    cJSON *tpr = NULL;
    char *serialized = NULL;
    int i;

    if (!result_json || size == 0) return 1;

    train_default_config(&cfg);

    if (body && *body) {
        root = cJSON_Parse(body);
        if (!root) return 1;
        parse_cfg_from_body(root, &cfg);

        if (cJSON_IsArray(root)) {
            dataset_text = cJSON_PrintUnformatted(root);
        } else {
            dataset = cJSON_GetObjectItem(root, "dataset");
            if (dataset) dataset_text = cJSON_PrintUnformatted(dataset);
            else if (cJSON_GetObjectItem(root, "patients")) dataset_text = cJSON_PrintUnformatted(root);
        }
    }

    if (dataset_text) train_json = dataset_text;
    if (!train_json) {
        fallback_json = read_file("data/train_example.json");
        if (!fallback_json) fallback_json = read_file("parkinome/core/data/train_example.json");
        train_json = fallback_json;
    }

    if (!train_json || train_and_save_model_from_json(train_json, &cfg, PARKINOME_MODEL_FILE, &res) != 0) {
        if (root) cJSON_Delete(root);
        free(dataset_text);
        free(fallback_json);
        return 1;
    }

    /* Формируем ответ через cJSON, так безопаснее для вложенных массивов ROC. */
    resp = cJSON_CreateObject();
    roc = cJSON_CreateObject();
    fpr = cJSON_CreateArray();
    tpr = cJSON_CreateArray();
    if (!resp || !roc || !fpr || !tpr) {
        if (resp) cJSON_Delete(resp);
        if (roc) cJSON_Delete(roc);
        if (fpr) cJSON_Delete(fpr);
        if (tpr) cJSON_Delete(tpr);
        if (root) cJSON_Delete(root);
        free(dataset_text);
        free(fallback_json);
        return 1;
    }

    cJSON_AddNumberToObject(resp, "accuracy", res.accuracy);
    cJSON_AddNumberToObject(resp, "precision", res.precision);
    cJSON_AddNumberToObject(resp, "recall", res.recall);
    cJSON_AddNumberToObject(resp, "specificity", res.specificity);
    cJSON_AddNumberToObject(resp, "samples", res.samples);
    cJSON_AddNumberToObject(resp, "train_size", res.train_size);
    cJSON_AddNumberToObject(resp, "test_size", res.test_size);
    cJSON_AddNumberToObject(resp, "intercept", res.intercept);
    cJSON_AddNumberToObject(resp, "auc", res.auc);

    /* Сериализация ROC-массивов в ответ API. */
    for (i = 0; i < res.roc_points; i++) {
        cJSON_AddItemToArray(fpr, cJSON_CreateNumber(res.roc_fpr[i]));
        cJSON_AddItemToArray(tpr, cJSON_CreateNumber(res.roc_tpr[i]));
    }

    cJSON_AddItemToObject(roc, "fpr", fpr);
    cJSON_AddItemToObject(roc, "tpr", tpr);
    cJSON_AddItemToObject(resp, "roc", roc);

    serialized = cJSON_PrintUnformatted(resp);
    if (!serialized || strlen(serialized) >= size) {
        if (serialized) free(serialized);
        cJSON_Delete(resp);
        if (root) cJSON_Delete(root);
        free(dataset_text);
        free(fallback_json);
        return 1;
    }

    strncpy(result_json, serialized, size);
    result_json[size - 1] = '\0';
    free(serialized);
    cJSON_Delete(resp);

    if (root) cJSON_Delete(root);
    free(dataset_text);
    free(fallback_json);
    return 0;
}
