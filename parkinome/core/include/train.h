#ifndef TRAIN_H
#define TRAIN_H

#include <stddef.h>

/* Максимальное число точек ROC, чтобы не перегружать ответ API и UI. */
#define TRAIN_ROC_MAX_POINTS 100

typedef struct {
    double learning_rate;
    int epochs;
    double l2;
    unsigned int seed;
} train_config_t;

typedef struct {
    double accuracy;
    double precision;
    double recall;
    double specificity;
    int samples;
    int train_size;
    int test_size;
    double intercept;
    /* ROC-кривая на тестовой выборке. */
    int roc_points;
    double roc_fpr[TRAIN_ROC_MAX_POINTS];
    double roc_tpr[TRAIN_ROC_MAX_POINTS];
    /* Площадь под ROC-кривой (AUC). */
    double auc;
} training_result_t;

void train_default_config(train_config_t *cfg);
int train_and_save_model_from_json(const char *json_text, const train_config_t *cfg, const char *model_path, training_result_t *result);
int run_training(const char *body, char *result_json, size_t size);

#endif
