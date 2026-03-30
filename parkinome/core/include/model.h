#ifndef MODEL_H
#define MODEL_H

#include "predict.h"

#define PARKINOME_FEATURE_COUNT 13
#define PARKINOME_MODEL_FILE "model.dat"

typedef struct {
    double intercept;
    double weights[PARKINOME_FEATURE_COUNT];
} logistic_model_t;

int model_save(const char *path, const logistic_model_t *model);
int model_load(const char *path, logistic_model_t *model);
int model_set_active(const logistic_model_t *model);
int model_load_active(const char *path);
const logistic_model_t* model_get_active(void);

void model_fill_features(const parkinome_input_t *in, double x[PARKINOME_FEATURE_COUNT], int present[PARKINOME_FEATURE_COUNT]);
double model_sigmoid(double z);
double model_predict_probability_from_features(const logistic_model_t *model, const double x[PARKINOME_FEATURE_COUNT]);

#endif
