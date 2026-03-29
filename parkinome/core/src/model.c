#include <math.h>
#include "predict.h"
#include "model_params.h"

#define LOGIT_CLAMP 20.0
#define Z_CLAMP_MIN -5.0
#define Z_CLAMP_MAX  5.0

typedef struct {
    double value;
    int used;
} score_t;

static double clamp(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static double normalize_01(double x, double min, double max) {
    if (max <= min) return 0.0;
    return (clamp(x, min, max) - min) / (max - min);
}

static double logistic(double z) {
    double x = clamp(z, -LOGIT_CLAMP, LOGIT_CLAMP);
    return 1.0 / (1.0 + exp(-x));
}

static int level_from_index(double x01) {
    if (x01 < 0.33) return 0;
    if (x01 < 0.66) return 1;
    return 2;
}

static score_t mean4(double a, int ha, double b, int hb, double c, int hc, double d, int hd) {
    score_t s = {0.0, 0};
    double sum = 0.0;
    int n = 0;
    if (ha) { sum += a; n++; }
    if (hb) { sum += b; n++; }
    if (hc) { sum += c; n++; }
    if (hd) { sum += d; n++; }
    if (n > 0) { s.value = sum / (double)n; s.used = 1; }
    return s;
}

/* Формирует единый вектор фич для trainable линейной модели.
   Вектор фиксированный и полностью C-совместимый. */
static void build_feature_vector(const parkinome_input_t *in, double x[MODEL_FEATURE_COUNT], int present[MODEL_FEATURE_COUNT]) {
    score_t mito, inflam;
    int i;

    for (i = 0; i < MODEL_FEATURE_COUNT; i++) {
        x[i] = 0.0;
        present[i] = 0;
    }

    if (in->has_age)       { x[FEAT_AGE] = normalize_01(in->age, 30.0, 90.0); present[FEAT_AGE] = 1; }
    if (in->has_updrs_iii) { x[FEAT_UPDRS_III] = normalize_01(in->updrs_iii, 0.0, 132.0); present[FEAT_UPDRS_III] = 1; }
    if (in->has_scopa_aut) { x[FEAT_SCOPA_AUT] = normalize_01(in->scopa_aut, 0.0, 69.0); present[FEAT_SCOPA_AUT] = 1; }
    if (in->has_hoehn_yahr){ x[FEAT_HOEHN_YAHR] = normalize_01(in->hoehn_yahr, 1.0, 5.0); present[FEAT_HOEHN_YAHR] = 1; }
    if (in->has_moca)      { x[FEAT_MOCA_DEFICIT] = normalize_01(30.0 - in->moca, 0.0, 30.0); present[FEAT_MOCA_DEFICIT] = 1; }

    if (in->has_ndufa4l2) { x[FEAT_NDUFA4L2] = clamp(in->ndufa4l2, Z_CLAMP_MIN, Z_CLAMP_MAX); present[FEAT_NDUFA4L2] = 1; }
    if (in->has_ndufs2)   { x[FEAT_NDUFS2]   = clamp(in->ndufs2,   Z_CLAMP_MIN, Z_CLAMP_MAX); present[FEAT_NDUFS2] = 1; }
    if (in->has_pink1)    { x[FEAT_PINK1]    = clamp(in->pink1,    Z_CLAMP_MIN, Z_CLAMP_MAX); present[FEAT_PINK1] = 1; }
    if (in->has_ppargc1a) { x[FEAT_PPARGC1A] = clamp(in->ppargc1a, Z_CLAMP_MIN, Z_CLAMP_MAX); present[FEAT_PPARGC1A] = 1; }

    if (in->has_nlrp3)  { x[FEAT_NLRP3]  = clamp(in->nlrp3,  Z_CLAMP_MIN, Z_CLAMP_MAX); present[FEAT_NLRP3] = 1; }
    if (in->has_il1b)   { x[FEAT_IL1B]   = clamp(in->il1b,   Z_CLAMP_MIN, Z_CLAMP_MAX); present[FEAT_IL1B] = 1; }
    if (in->has_s100a8) { x[FEAT_S100A8] = clamp(in->s100a8, Z_CLAMP_MIN, Z_CLAMP_MAX); present[FEAT_S100A8] = 1; }
    if (in->has_cxcl8)  { x[FEAT_CXCL8]  = clamp(in->cxcl8,  Z_CLAMP_MIN, Z_CLAMP_MAX); present[FEAT_CXCL8] = 1; }

    mito = mean4(
        x[FEAT_NDUFA4L2], present[FEAT_NDUFA4L2],
        x[FEAT_NDUFS2],   present[FEAT_NDUFS2],
        x[FEAT_PINK1],    present[FEAT_PINK1],
        x[FEAT_PPARGC1A], present[FEAT_PPARGC1A]
    );
    inflam = mean4(
        x[FEAT_NLRP3],  present[FEAT_NLRP3],
        x[FEAT_IL1B],   present[FEAT_IL1B],
        x[FEAT_S100A8], present[FEAT_S100A8],
        x[FEAT_CXCL8],  present[FEAT_CXCL8]
    );

    if (mito.used) {
        x[FEAT_MITO_SCORE] = mito.value;
        present[FEAT_MITO_SCORE] = 1;
    }
    if (inflam.used) {
        x[FEAT_INFLAM_SCORE] = inflam.value;
        present[FEAT_INFLAM_SCORE] = 1;
    }
    if (mito.used && inflam.used) {
        x[FEAT_IMBALANCE] = inflam.value - mito.value;
        present[FEAT_IMBALANCE] = 1;
    }

    /* Missing flags. */
    x[FEAT_MISS_AGE] = present[FEAT_AGE] ? 0.0 : 1.0;
    x[FEAT_MISS_UPDRS_III] = present[FEAT_UPDRS_III] ? 0.0 : 1.0;
    x[FEAT_MISS_SCOPA_AUT] = present[FEAT_SCOPA_AUT] ? 0.0 : 1.0;
    x[FEAT_MISS_HOEHN_YAHR] = present[FEAT_HOEHN_YAHR] ? 0.0 : 1.0;
    x[FEAT_MISS_MOCA_DEFICIT] = present[FEAT_MOCA_DEFICIT] ? 0.0 : 1.0;
    x[FEAT_MISS_NDUFA4L2] = present[FEAT_NDUFA4L2] ? 0.0 : 1.0;
    x[FEAT_MISS_NDUFS2] = present[FEAT_NDUFS2] ? 0.0 : 1.0;
    x[FEAT_MISS_PINK1] = present[FEAT_PINK1] ? 0.0 : 1.0;
    x[FEAT_MISS_PPARGC1A] = present[FEAT_PPARGC1A] ? 0.0 : 1.0;
    x[FEAT_MISS_NLRP3] = present[FEAT_NLRP3] ? 0.0 : 1.0;
    x[FEAT_MISS_IL1B] = present[FEAT_IL1B] ? 0.0 : 1.0;
    x[FEAT_MISS_S100A8] = present[FEAT_S100A8] ? 0.0 : 1.0;
    x[FEAT_MISS_CXCL8] = present[FEAT_CXCL8] ? 0.0 : 1.0;
    x[FEAT_MISS_MITO_SCORE] = present[FEAT_MITO_SCORE] ? 0.0 : 1.0;
    x[FEAT_MISS_INFLAM_SCORE] = present[FEAT_INFLAM_SCORE] ? 0.0 : 1.0;
    x[FEAT_MISS_IMBALANCE] = present[FEAT_IMBALANCE] ? 0.0 : 1.0;

    present[FEAT_MISS_AGE] = 1;
    present[FEAT_MISS_UPDRS_III] = 1;
    present[FEAT_MISS_SCOPA_AUT] = 1;
    present[FEAT_MISS_HOEHN_YAHR] = 1;
    present[FEAT_MISS_MOCA_DEFICIT] = 1;
    present[FEAT_MISS_NDUFA4L2] = 1;
    present[FEAT_MISS_NDUFS2] = 1;
    present[FEAT_MISS_PINK1] = 1;
    present[FEAT_MISS_PPARGC1A] = 1;
    present[FEAT_MISS_NLRP3] = 1;
    present[FEAT_MISS_IL1B] = 1;
    present[FEAT_MISS_S100A8] = 1;
    present[FEAT_MISS_CXCL8] = 1;
    present[FEAT_MISS_MITO_SCORE] = 1;
    present[FEAT_MISS_INFLAM_SCORE] = 1;
    present[FEAT_MISS_IMBALANCE] = 1;
}

int parkinome_predict(parkinome_input_t *in, parkinome_output_t *out) {
    double x[MODEL_FEATURE_COUNT];
    double xz[MODEL_FEATURE_COUNT];
    int present[MODEL_FEATURE_COUNT];
    int i;
    int base_present = 0;
    double raw = MODEL_INTERCEPT;
    double contrib[MODEL_FEATURE_COUNT];

    if (!in || !out) return PARKINOME_NULL_POINTER;

    build_feature_vector(in, x, present);

    /* Проверка: есть ли хоть одна реальная (не missing flag) группа данных. */
    for (i = FEAT_AGE; i <= FEAT_IMBALANCE; i++) {
        if (present[i]) { base_present = 1; break; }
    }
    if (!base_present) return PARKINOME_ERR_INVALID_INPUT;

    for (i = 0; i < MODEL_FEATURE_COUNT; i++) {
        double v = present[i] ? x[i] : MODEL_IMPUTER_MEDIAN[i];
        double denom = (fabs(MODEL_SCALER_SCALE[i]) > 1e-12) ? MODEL_SCALER_SCALE[i] : 1.0;
        xz[i] = (v - MODEL_SCALER_MEAN[i]) / denom;
        contrib[i] = MODEL_COEF[i] * xz[i];
        raw += contrib[i];
    }

    out->risk_probability = logistic(raw);
    out->isp = out->risk_probability;
    out->category = (out->risk_probability < MODEL_THR_LOW) ? 0 :
                    (out->risk_probability < MODEL_THR_HIGH) ? 1 : 2;

    /* Confidence: сочетание coverage и distance-from-0.5 (чем дальше, тем увереннее). */
    {
        int used = 0;
        double margin = fabs(out->risk_probability - 0.5) * 2.0;
        if (present[FEAT_AGE]) used++;
        if (present[FEAT_UPDRS_III]) used++;
        if (present[FEAT_SCOPA_AUT]) used++;
        if (present[FEAT_HOEHN_YAHR]) used++;
        if (present[FEAT_MOCA_DEFICIT]) used++;
        if (present[FEAT_NDUFA4L2]) used++;
        if (present[FEAT_NDUFS2]) used++;
        if (present[FEAT_PINK1]) used++;
        if (present[FEAT_PPARGC1A]) used++;
        if (present[FEAT_NLRP3]) used++;
        if (present[FEAT_IL1B]) used++;
        if (present[FEAT_S100A8]) used++;
        if (present[FEAT_CXCL8]) used++;
        out->confidence = 0.7 * ((double)used / 13.0) + 0.3 * margin;
    }

    out->mito_score = present[FEAT_MITO_SCORE] ? x[FEAT_MITO_SCORE] : 0.0;
    out->inflam_score = present[FEAT_INFLAM_SCORE] ? x[FEAT_INFLAM_SCORE] : 0.0;
    out->imbalance = present[FEAT_IMBALANCE] ? x[FEAT_IMBALANCE] : 0.0;

    /* Breakdown для interpretability UI: вклад каждой биологической группы в logit. */
    out->breakdown_clinical =
        contrib[FEAT_AGE] + contrib[FEAT_UPDRS_III] + contrib[FEAT_SCOPA_AUT] + contrib[FEAT_HOEHN_YAHR];
    out->breakdown_cognitive = contrib[FEAT_MOCA_DEFICIT];
    out->breakdown_mitochondrial =
        contrib[FEAT_NDUFA4L2] + contrib[FEAT_NDUFS2] + contrib[FEAT_PINK1] + contrib[FEAT_PPARGC1A] + contrib[FEAT_MITO_SCORE];
    out->breakdown_inflammation =
        contrib[FEAT_NLRP3] + contrib[FEAT_IL1B] + contrib[FEAT_S100A8] + contrib[FEAT_CXCL8] + contrib[FEAT_INFLAM_SCORE];
    out->breakdown_imbalance = contrib[FEAT_IMBALANCE];

    /* UI indices [0..1]:
       - mito: выше = лучше митохондриальный профиль;
       - inflam/stress: выше = выше воспалительный/дисбалансный стресс. */
    if (present[FEAT_MITO_SCORE]) out->mito_index = normalize_01(out->mito_score, -3.0, 3.0);
    else out->mito_index = 0.5;
    if (present[FEAT_INFLAM_SCORE]) out->inflam_index = normalize_01(out->inflam_score, -3.0, 3.0);
    else out->inflam_index = 0.5;
    if (present[FEAT_IMBALANCE]) out->stress_index = normalize_01(out->imbalance, -3.0, 3.0);
    else out->stress_index = 0.5;

    out->mito_level = level_from_index(out->mito_index);
    out->inflam_level = level_from_index(out->inflam_index);

    return PARKINOME_OK;
}
