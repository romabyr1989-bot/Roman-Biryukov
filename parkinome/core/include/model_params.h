#ifndef MODEL_PARAMS_H
#define MODEL_PARAMS_H

/* ===== TRAINED PARAMS (C EXPORT) =====
   Этот файл — C-совместимый контейнер параметров модели.
   После обучения в оффлайн-пайплайне параметры заменяются автоматически
   (коэффициенты, имputation и scaling). Runtime остается чистым C. */

enum {
    FEAT_AGE = 0,
    FEAT_UPDRS_III,
    FEAT_SCOPA_AUT,
    FEAT_HOEHN_YAHR,
    FEAT_MOCA_DEFICIT,
    FEAT_NDUFA4L2,
    FEAT_NDUFS2,
    FEAT_PINK1,
    FEAT_PPARGC1A,
    FEAT_NLRP3,
    FEAT_IL1B,
    FEAT_S100A8,
    FEAT_CXCL8,
    FEAT_MITO_SCORE,
    FEAT_INFLAM_SCORE,
    FEAT_IMBALANCE,

    /* Missing indicators для устойчивости к неполным данным. */
    FEAT_MISS_AGE,
    FEAT_MISS_UPDRS_III,
    FEAT_MISS_SCOPA_AUT,
    FEAT_MISS_HOEHN_YAHR,
    FEAT_MISS_MOCA_DEFICIT,
    FEAT_MISS_NDUFA4L2,
    FEAT_MISS_NDUFS2,
    FEAT_MISS_PINK1,
    FEAT_MISS_PPARGC1A,
    FEAT_MISS_NLRP3,
    FEAT_MISS_IL1B,
    FEAT_MISS_S100A8,
    FEAT_MISS_CXCL8,
    FEAT_MISS_MITO_SCORE,
    FEAT_MISS_INFLAM_SCORE,
    FEAT_MISS_IMBALANCE,

    MODEL_FEATURE_COUNT
};

/* Placeholder-параметры.
   Эти значения можно безопасно заменить на реально обученные. */
static const double MODEL_INTERCEPT = -0.35;

/* Линейные коэффициенты для 32 фичей.
   Сейчас инициализированы осмысленным bootstrap-вариантом, близким к текущей логике. */
static const double MODEL_COEF[MODEL_FEATURE_COUNT] = {
    1.10, 1.15, 0.90, 0.70, 0.95,   /* clinical + cognitive */
   -0.20,-0.20,-0.20,-0.20,         /* mito genes */
    0.22, 0.22, 0.22, 0.22,         /* inflam genes */
   -0.45, 0.55, 0.65,               /* mito/inflam/imbalance aggregates */
   -0.08,-0.08,-0.06,-0.06,-0.06,   /* missing indicators (clinical/cognitive) */
   -0.04,-0.04,-0.04,-0.04,         /* missing mito genes */
   -0.04,-0.04,-0.04,-0.04,         /* missing inflam genes */
   -0.07,-0.07,-0.07                /* missing aggregate indicators */
};

/* Imputer и scaler экспортируются из тренировки.
   По умолчанию: identity scaling и нулевой imputation для z-score части. */
static const double MODEL_IMPUTER_MEDIAN[MODEL_FEATURE_COUNT] = {
    65.0, 31.0, 24.0, 2.0, 7.0,
    0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0
};

static const double MODEL_SCALER_MEAN[MODEL_FEATURE_COUNT] = {0};
static const double MODEL_SCALER_SCALE[MODEL_FEATURE_COUNT] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};

/* Пороги категорий риска. */
static const double MODEL_THR_LOW = 0.33;
static const double MODEL_THR_HIGH = 0.66;

#endif
