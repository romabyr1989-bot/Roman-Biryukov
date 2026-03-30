#include <stdio.h>
#include <string.h>
#include <math.h>

#include "model.h"

#define LOGIT_CLAMP 30.0

static logistic_model_t g_active_model = {
    .intercept = -0.35,
    .weights = {
        0.03, 0.045, -0.04, 0.25, -0.06,
        -0.10, -0.08, -0.09, -0.07,
        0.11, 0.10, 0.10, 0.09
    }
};

static double clamp(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static double normalize_01(double x, double min, double max) {
    if (max <= min) return 0.0;
    return (clamp(x, min, max) - min) / (max - min);
}

static int level_from_index(double x01) {
    if (x01 < 0.33) return 0;
    if (x01 < 0.66) return 1;
    return 2;
}

static double mean4(double a, int ha, double b, int hb, double c, int hc, double d, int hd, int *used) {
    double sum = 0.0;
    int n = 0;
    if (ha) { sum += a; n++; }
    if (hb) { sum += b; n++; }
    if (hc) { sum += c; n++; }
    if (hd) { sum += d; n++; }
    if (used) *used = (n > 0);
    return (n > 0) ? (sum / (double)n) : 0.0;
}

void model_fill_features(const parkinome_input_t *in, double x[PARKINOME_FEATURE_COUNT], int present[PARKINOME_FEATURE_COUNT]) {
    if (!in || !x || !present) return;

    x[0] = in->has_age ? in->age : 0.0; present[0] = in->has_age;
    x[1] = in->has_updrs_iii ? in->updrs_iii : 0.0; present[1] = in->has_updrs_iii;
    x[2] = in->has_moca ? in->moca : 0.0; present[2] = in->has_moca;
    x[3] = in->has_scopa_aut ? in->scopa_aut : 0.0; present[3] = in->has_scopa_aut;
    x[4] = in->has_hoehn_yahr ? in->hoehn_yahr : 0.0; present[4] = in->has_hoehn_yahr;

    x[5] = in->has_ndufa4l2 ? in->ndufa4l2 : 0.0; present[5] = in->has_ndufa4l2;
    x[6] = in->has_ndufs2 ? in->ndufs2 : 0.0; present[6] = in->has_ndufs2;
    x[7] = in->has_pink1 ? in->pink1 : 0.0; present[7] = in->has_pink1;
    x[8] = in->has_ppargc1a ? in->ppargc1a : 0.0; present[8] = in->has_ppargc1a;

    x[9] = in->has_nlrp3 ? in->nlrp3 : 0.0; present[9] = in->has_nlrp3;
    x[10] = in->has_il1b ? in->il1b : 0.0; present[10] = in->has_il1b;
    x[11] = in->has_s100a8 ? in->s100a8 : 0.0; present[11] = in->has_s100a8;
    x[12] = in->has_cxcl8 ? in->cxcl8 : 0.0; present[12] = in->has_cxcl8;
}

double model_sigmoid(double z) {
    double x = clamp(z, -LOGIT_CLAMP, LOGIT_CLAMP);
    return 1.0 / (1.0 + exp(-x));
}

double model_predict_probability_from_features(const logistic_model_t *model, const double x[PARKINOME_FEATURE_COUNT]) {
    int i;
    double z = 0.0;

    if (!model || !x) return 0.5;

    z = model->intercept;
    for (i = 0; i < PARKINOME_FEATURE_COUNT; i++) {
        z += model->weights[i] * x[i];
    }

    return model_sigmoid(z);
}

int model_save(const char *path, const logistic_model_t *model) {
    int i;
    FILE *fp;

    if (!path || !model) return 1;

    fp = fopen(path, "w");
    if (!fp) return 1;

    if (fprintf(fp, "%.17g\n", model->intercept) < 0) {
        fclose(fp);
        return 1;
    }

    for (i = 0; i < PARKINOME_FEATURE_COUNT; i++) {
        if (fprintf(fp, "%.17g%c", model->weights[i], (i + 1 == PARKINOME_FEATURE_COUNT) ? '\n' : ' ') < 0) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

int model_load(const char *path, logistic_model_t *model) {
    int i;
    FILE *fp;

    if (!path || !model) return 1;

    fp = fopen(path, "r");
    if (!fp) return 1;

    if (fscanf(fp, "%lf", &model->intercept) != 1) {
        fclose(fp);
        return 1;
    }

    for (i = 0; i < PARKINOME_FEATURE_COUNT; i++) {
        if (fscanf(fp, "%lf", &model->weights[i]) != 1) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

int model_set_active(const logistic_model_t *model) {
    if (!model) return 1;
    g_active_model = *model;
    return 0;
}

int model_load_active(const char *path) {
    logistic_model_t m;
    if (model_load(path, &m) != 0) return 1;
    g_active_model = m;
    return 0;
}

const logistic_model_t* model_get_active(void) {
    return &g_active_model;
}

int parkinome_predict(parkinome_input_t *in, parkinome_output_t *out) {
    const logistic_model_t *model;
    double x[PARKINOME_FEATURE_COUNT];
    int present[PARKINOME_FEATURE_COUNT];
    int i;
    int used = 0;
    int mito_used = 0;
    int inflam_used = 0;
    double margin;
    double p;

    if (!in || !out) return PARKINOME_NULL_POINTER;

    memset(out, 0, sizeof(*out));

    model_fill_features(in, x, present);
    for (i = 0; i < PARKINOME_FEATURE_COUNT; i++) {
        if (present[i]) used++;
    }
    if (used == 0) return PARKINOME_ERR_INVALID_INPUT;

    model = model_get_active();
    p = model_predict_probability_from_features(model, x);

    out->risk_probability = p;
    out->isp = p;
    out->category = (p < 0.33) ? 0 : ((p < 0.66) ? 1 : 2);

    margin = fabs(p - 0.5) * 2.0;
    out->confidence = 0.7 * ((double)used / (double)PARKINOME_FEATURE_COUNT) + 0.3 * margin;

    out->mito_score = mean4(x[5], present[5], x[6], present[6], x[7], present[7], x[8], present[8], &mito_used);
    out->inflam_score = mean4(x[9], present[9], x[10], present[10], x[11], present[11], x[12], present[12], &inflam_used);
    out->imbalance = (mito_used && inflam_used) ? (out->inflam_score - out->mito_score) : 0.0;

    out->breakdown_clinical =
        model->weights[0] * x[0] + model->weights[1] * x[1] + model->weights[3] * x[3] + model->weights[4] * x[4];
    out->breakdown_cognitive = model->weights[2] * x[2];
    out->breakdown_mitochondrial =
        model->weights[5] * x[5] + model->weights[6] * x[6] + model->weights[7] * x[7] + model->weights[8] * x[8];
    out->breakdown_inflammation =
        model->weights[9] * x[9] + model->weights[10] * x[10] + model->weights[11] * x[11] + model->weights[12] * x[12];
    out->breakdown_imbalance = out->breakdown_inflammation - out->breakdown_mitochondrial;

    out->mito_index = mito_used ? normalize_01(out->mito_score, -5.0, 5.0) : 0.5;
    out->inflam_index = inflam_used ? normalize_01(out->inflam_score, -5.0, 5.0) : 0.5;
    out->stress_index = (mito_used && inflam_used) ? normalize_01(out->imbalance, -5.0, 5.0) : 0.5;

    out->mito_level = level_from_index(out->mito_index);
    out->inflam_level = level_from_index(out->inflam_index);

    return PARKINOME_OK;
}
