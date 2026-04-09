/*
 * NeuroStrata — Molecular Progression Stratification Engine
 * model.c — prediction core
 *
 * Changes from parkinome/model.c v1:
 *   [FIX-1] NDUFA4L2 → NDUFA5 (dissertation sec. 5.3)
 *   [FIX-2] Added COX7A2 and TLR4 (dissertation fig. 5.4)
 *   [FIX-3] Feature count 13 → 15
 *   [FIX-4] Added linear regression head for continuous ISP (pts/yr)
 *           out->isp now carries MDS-UPDRS pts/yr, NOT probability
 *   [FIX-5] Clinical features z-scored internally using PPMI cohort stats
 *   [FIX-6] Gene expression z-scoring utility (model_zscore_gene)
 *   [FIX-7] Input validation with clinical range checks
 *   [FIX-8] mean4() replaced with mean_n() for arbitrary gene group size
 *   [FIX-9] model_save/load extended to write both heads atomically
 *   [FIX-10] ISP confidence interval placeholder (±1.64 * residual SE)
 *   [FIX-11] Branding: parkinome → neurostrata (shims kept for compat)
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "model.h"

#define LOGIT_CLAMP  30.0
#define MODEL_FILE_VERSION 2

/* ─── Placeholder weights (REPLACE with elastic-net coefficients from PPMI) ─
 *
 * These are structurally correct placeholders that reproduce the sign
 * pattern from the dissertation (negative weights for mitochondrial genes,
 * positive for inflammatory genes).
 *
 * LOGISTIC HEAD weights (feature order matches model.h):
 *   [0] age, [1] updrs_iii, [2] moca, [3] scopa_aut, [4] hoehn_yahr
 *   [5] ndufa5, [6] ndufs2, [7] pink1, [8] ppargc1a, [9] cox7a2
 *   [10] tlr4, [11] nlrp3, [12] il1b, [13] s100a8, [14] cxcl8
 *
 * All gene features arrive z-scored → weights are in logit-per-SD units.
 * Clinical features are z-scored internally.
 */
static neurostrata_model_t g_active_model = {
    .version = MODEL_FILE_VERSION,

    .logistic = {
        .intercept = -0.35,
        .weights = {
        /*  age      updrs   moca   scopa  h&y  */
            0.18,    0.22,  -0.19,  0.14, -0.08,
        /*  ndufa5  ndufs2  pink1  pgc1a  cox7a2  */
           -0.38,  -0.31,  -0.35, -0.28,  -0.25,
        /*  tlr4    nlrp3   il1b   s100a8 cxcl8  */
            0.42,   0.38,   0.36,   0.31,  0.28
        }
    },

    /*
     * LINEAR HEAD — predicts ISP in MDS-UPDRS pts/year.
     * Intercept ≈ cohort mean ISP (3.5 pts/yr, dissertation sec. 3.2).
     * Weights are in (pts/yr)-per-SD units for z-scored inputs.
     */
    .linear = {
        .intercept = 3.50,
        .weights = {
        /*  age     updrs   moca    scopa  h&y  */
            0.39,   0.48,  -0.41,   0.31,  0.22,
        /*  ndufa5  ndufs2  pink1  pgc1a  cox7a2  */
           -0.82,  -0.67,  -0.76, -0.61,  -0.54,
        /*  tlr4    nlrp3   il1b   s100a8 cxcl8  */
            0.91,   0.82,   0.78,   0.67,  0.61
        }
    }
};

/* Residual standard error of the linear head from 10-fold CV (placeholder).
 * Replace with actual SE from PPMI training. */
#define LINEAR_RESIDUAL_SE 2.14   /* pts/yr  (dissertation table 5.1 RMSE) */
#define Z95               1.645   /* one-sided 95% */

/* ─── Internal utilities ──────────────────────────────────────────────────── */

static double clamp(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static double normalize_01(double x, double min_val, double max_val) {
    if (max_val <= min_val) return 0.0;
    return (clamp(x, min_val, max_val) - min_val) / (max_val - min_val);
}

static int level_from_index(double x01) {
    if (x01 < 0.33) return 0;
    if (x01 < 0.66) return 1;
    return 2;
}

/*
 * mean_n — mean of values[0..n-1] where present[i] flags valid entries.
 * Returns 0.0 and sets *used_out = 0 when no values are present.
 */
static double mean_n(const double *values, const int *present, int n, int *used_out) {
    double sum = 0.0;
    int k = 0, i;
    for (i = 0; i < n; i++) {
        if (present[i]) { sum += values[i]; k++; }
    }
    if (used_out) *used_out = k;
    return (k > 0) ? (sum / (double)k) : 0.0;
}

/* ─── Public utility functions ────────────────────────────────────────────── */

/*
 * model_zscore_gene — convert raw log2-CPM to z-score using cohort stats.
 *
 * Caller is responsible for supplying the correct cohort_mean and cohort_sd
 * derived from the reference PPMI baseline RNA-seq run.
 */
double model_zscore_gene(double log2cpm, double cohort_mean, double cohort_sd) {
    if (cohort_sd <= 0.0) return 0.0;
    return (log2cpm - cohort_mean) / cohort_sd;
}

/*
 * model_validate_input — range-check clinical values.
 * Gene expression z-scores are not range-checked here (cohort-dependent).
 *
 * Returns NEUROSTRATA_OK or NEUROSTRATA_ERR_RANGE.
 */
int model_validate_input(const neurostrata_input_t *in) {
    if (!in) return NEUROSTRATA_NULL_POINTER;

    if (in->has_age        && (in->age < 18.0   || in->age > 120.0))   return NEUROSTRATA_ERR_RANGE;
    if (in->has_updrs_iii  && (in->updrs_iii < 0.0 || in->updrs_iii > 132.0)) return NEUROSTRATA_ERR_RANGE;
    if (in->has_moca       && (in->moca < 0.0   || in->moca > 30.0))   return NEUROSTRATA_ERR_RANGE;
    if (in->has_scopa_aut  && (in->scopa_aut < 0.0 || in->scopa_aut > 69.0))  return NEUROSTRATA_ERR_RANGE;
    if (in->has_hoehn_yahr && (in->hoehn_yahr < 1.0 || in->hoehn_yahr > 5.0)) return NEUROSTRATA_ERR_RANGE;

    return NEUROSTRATA_OK;
}

/* ─── Feature vector ──────────────────────────────────────────────────────── */

/*
 * model_fill_features — populate x[] and present[] from neurostrata_input_t.
 *
 * Clinical features are z-scored internally using PPMI cohort statistics
 * (CLINICAL_NORM, defined in model.h).
 *
 * Gene expression features are expected ALREADY z-scored by the caller
 * via model_zscore_gene().  This keeps the normalisation step explicit
 * and auditable for regulatory purposes.
 */
void model_fill_features(const neurostrata_input_t *in,
                         double x[NEUROSTRATA_FEATURE_COUNT],
                         int present[NEUROSTRATA_FEATURE_COUNT]) {
    int i;
    if (!in || !x || !present) return;

    /* Zero-initialise — missing features contribute 0 to the dot product */
    for (i = 0; i < NEUROSTRATA_FEATURE_COUNT; i++) { x[i] = 0.0; present[i] = 0; }

    /* ── Clinical features — z-scored internally ── */
#define FILL_CLINICAL(IDX, FIELD, HAS_FIELD) \
    present[IDX] = in->HAS_FIELD; \
    if (in->HAS_FIELD) { \
        x[IDX] = (in->FIELD - CLINICAL_NORM[IDX].mean) / \
                 (CLINICAL_NORM[IDX].sd > 0.0 ? CLINICAL_NORM[IDX].sd : 1.0); \
    }

    FILL_CLINICAL(0, age,        has_age)
    FILL_CLINICAL(1, updrs_iii,  has_updrs_iii)
    FILL_CLINICAL(2, moca,       has_moca)
    FILL_CLINICAL(3, scopa_aut,  has_scopa_aut)
    FILL_CLINICAL(4, hoehn_yahr, has_hoehn_yahr)
#undef FILL_CLINICAL

    /* ── Mitochondrial genes — caller-supplied z-scores ── */
    /* [FIX-1] ndufa5 replaces ndufa4l2 */
    present[5] = in->has_ndufa5;   x[5]  = in->has_ndufa5   ? in->ndufa5   : 0.0;
    present[6] = in->has_ndufs2;   x[6]  = in->has_ndufs2   ? in->ndufs2   : 0.0;
    present[7] = in->has_pink1;    x[7]  = in->has_pink1    ? in->pink1    : 0.0;
    present[8] = in->has_ppargc1a; x[8]  = in->has_ppargc1a ? in->ppargc1a : 0.0;
    /* [FIX-2] cox7a2 added */
    present[9] = in->has_cox7a2;   x[9]  = in->has_cox7a2   ? in->cox7a2   : 0.0;

    /* ── Inflammatory genes — caller-supplied z-scores ── */
    /* [FIX-2] tlr4 added */
    present[10] = in->has_tlr4;    x[10] = in->has_tlr4    ? in->tlr4    : 0.0;
    present[11] = in->has_nlrp3;   x[11] = in->has_nlrp3   ? in->nlrp3   : 0.0;
    present[12] = in->has_il1b;    x[12] = in->has_il1b    ? in->il1b    : 0.0;
    present[13] = in->has_s100a8;  x[13] = in->has_s100a8  ? in->s100a8  : 0.0;
    present[14] = in->has_cxcl8;   x[14] = in->has_cxcl8   ? in->cxcl8   : 0.0;
}

/* ─── Prediction heads ────────────────────────────────────────────────────── */

double model_sigmoid(double z) {
    double cz = clamp(z, -LOGIT_CLAMP, LOGIT_CLAMP);
    return 1.0 / (1.0 + exp(-cz));
}

double model_predict_probability(const logistic_model_t *m,
                                 const double x[NEUROSTRATA_FEATURE_COUNT]) {
    int i;
    double z;
    if (!m || !x) return 0.5;
    z = m->intercept;
    for (i = 0; i < NEUROSTRATA_FEATURE_COUNT; i++) z += m->weights[i] * x[i];
    return model_sigmoid(z);
}

/*
 * model_predict_isp — linear regression head.
 * Returns ISP in MDS-UPDRS pts/year.
 * [FIX-4] This is the correct output for out->isp (was mistakenly set to
 *         the logistic probability in the original code).
 */
double model_predict_isp(const linear_model_t *m,
                         const double x[NEUROSTRATA_FEATURE_COUNT]) {
    int i;
    double isp;
    if (!m || !x) return 3.5; /* cohort mean as fallback */
    isp = m->intercept;
    for (i = 0; i < NEUROSTRATA_FEATURE_COUNT; i++) isp += m->weights[i] * x[i];
    /* Clamp to physiologically plausible range */
    return clamp(isp, NEUROSTRATA_ISP_MIN, NEUROSTRATA_ISP_MAX);
}

/* ─── Active model management ─────────────────────────────────────────────── */

int model_set_active(const neurostrata_model_t *m) {
    if (!m) return NEUROSTRATA_NULL_POINTER;
    g_active_model = *m;
    return NEUROSTRATA_OK;
}

const neurostrata_model_t *model_get_active(void) {
    return &g_active_model;
}

/* ─── Serialisation ───────────────────────────────────────────────────────── */

/*
 * File format (version 2):
 *   Line 1: version (int)
 *   Line 2: logistic intercept
 *   Line 3: logistic weights (space-separated, NEUROSTRATA_FEATURE_COUNT values)
 *   Line 4: linear intercept
 *   Line 5: linear weights (space-separated)
 */
int model_save(const char *path, const neurostrata_model_t *m) {
    int i;
    FILE *fp;
    if (!path || !m) return NEUROSTRATA_NULL_POINTER;

    fp = fopen(path, "w");
    if (!fp) return NEUROSTRATA_ERR_IO;

    if (fprintf(fp, "%d\n", MODEL_FILE_VERSION) < 0) goto io_err;
    if (fprintf(fp, "%.17g\n", m->logistic.intercept) < 0) goto io_err;
    for (i = 0; i < NEUROSTRATA_FEATURE_COUNT; i++) {
        char sep = (i + 1 == NEUROSTRATA_FEATURE_COUNT) ? '\n' : ' ';
        if (fprintf(fp, "%.17g%c", m->logistic.weights[i], sep) < 0) goto io_err;
    }
    if (fprintf(fp, "%.17g\n", m->linear.intercept) < 0) goto io_err;
    for (i = 0; i < NEUROSTRATA_FEATURE_COUNT; i++) {
        char sep = (i + 1 == NEUROSTRATA_FEATURE_COUNT) ? '\n' : ' ';
        if (fprintf(fp, "%.17g%c", m->linear.weights[i], sep) < 0) goto io_err;
    }

    fclose(fp);
    return NEUROSTRATA_OK;

io_err:
    fclose(fp);
    return NEUROSTRATA_ERR_IO;
}

int model_load(const char *path, neurostrata_model_t *m) {
    int i, ver;
    FILE *fp;
    if (!path || !m) return NEUROSTRATA_NULL_POINTER;

    fp = fopen(path, "r");
    if (!fp) return NEUROSTRATA_ERR_IO;

    if (fscanf(fp, "%d", &ver) != 1)                        goto io_err;
    if (ver != MODEL_FILE_VERSION)                           goto io_err;
    if (fscanf(fp, "%lf", &m->logistic.intercept) != 1)     goto io_err;
    for (i = 0; i < NEUROSTRATA_FEATURE_COUNT; i++)
        if (fscanf(fp, "%lf", &m->logistic.weights[i]) != 1) goto io_err;
    if (fscanf(fp, "%lf", &m->linear.intercept) != 1)       goto io_err;
    for (i = 0; i < NEUROSTRATA_FEATURE_COUNT; i++)
        if (fscanf(fp, "%lf", &m->linear.weights[i]) != 1)  goto io_err;

    m->version = ver;
    fclose(fp);
    return NEUROSTRATA_OK;

io_err:
    fclose(fp);
    return NEUROSTRATA_ERR_IO;
}

int model_load_active(const char *path) {
    neurostrata_model_t m;
    int rc = model_load(path, &m);
    if (rc != NEUROSTRATA_OK) return rc;
    g_active_model = m;
    return NEUROSTRATA_OK;
}

/* ─── Main prediction function ────────────────────────────────────────────── */

int neurostrata_predict(neurostrata_input_t *in, neurostrata_output_t *out) {
    const neurostrata_model_t *model;
    double x[NEUROSTRATA_FEATURE_COUNT];
    int    present[NEUROSTRATA_FEATURE_COUNT];
    int    i, rc;
    int    mito_used = 0, inflam_used = 0;
    double margin, p, isp_raw;

    if (!in || !out) return NEUROSTRATA_NULL_POINTER;
    memset(out, 0, sizeof(*out));

    /* [FIX-7] Validate clinical ranges before doing anything */
    rc = model_validate_input(in);
    if (rc != NEUROSTRATA_OK) return rc;

    model_fill_features(in, x, present);

    out->features_total = NEUROSTRATA_FEATURE_COUNT;
    out->features_used  = 0;
    for (i = 0; i < NEUROSTRATA_FEATURE_COUNT; i++)
        if (present[i]) out->features_used++;

    if (out->features_used == 0) return NEUROSTRATA_ERR_INVALID_INPUT;

    model = model_get_active();

    /* ── Logistic head → risk probability ── */
    p = model_predict_probability(&model->logistic, x);
    out->risk_probability = p;

    /* ── Linear head → ISP in pts/yr ──────── [FIX-4] ── */
    isp_raw    = model_predict_isp(&model->linear, x);
    out->isp   = isp_raw;
    out->isp_low  = clamp(isp_raw - Z95 * LINEAR_RESIDUAL_SE,
                          NEUROSTRATA_ISP_MIN, NEUROSTRATA_ISP_MAX);
    out->isp_high = clamp(isp_raw + Z95 * LINEAR_RESIDUAL_SE,
                          NEUROSTRATA_ISP_MIN, NEUROSTRATA_ISP_MAX);

    /* ── Category from ISP quartile thresholds (dissertation sec. 3.3) ── */
    out->category = (isp_raw <= NEUROSTRATA_ISP_SLOW_THR)  ? NEUROSTRATA_CAT_SLOW :
                    (isp_raw <  NEUROSTRATA_ISP_FAST_THR)  ? NEUROSTRATA_CAT_MODERATE :
                                                              NEUROSTRATA_CAT_FAST;

    /* ── Confidence: data completeness + separation from decision boundary ── */
    margin = fabs(p - 0.5) * 2.0;
    out->confidence = 0.7 * ((double)out->features_used /
                             (double)out->features_total)
                    + 0.3 * margin;

    /* ── Biological axis scores ── [FIX-8] use mean_n ── */
    {
        /* Mito genes: indices 5–9 */
        out->mito_score = mean_n(x + NEUROSTRATA_MITO_START,
                                 present + NEUROSTRATA_MITO_START,
                                 NEUROSTRATA_MITO_N, &mito_used);
        out->mito_genes_used = mito_used;

        /* Inflam genes: indices 10–14 */
        out->inflam_score = mean_n(x + NEUROSTRATA_INFLAM_START,
                                   present + NEUROSTRATA_INFLAM_START,
                                   NEUROSTRATA_INFLAM_N, &inflam_used);
        out->inflam_genes_used = inflam_used;
    }

    out->imbalance = (mito_used && inflam_used)
                   ? (out->inflam_score - out->mito_score)
                   : 0.0;

    /* ── ISP additive breakdown (linear head weights × features) ── */
    out->breakdown_clinical =
          model->linear.weights[0] * x[0]
        + model->linear.weights[1] * x[1]
        + model->linear.weights[3] * x[3]
        + model->linear.weights[4] * x[4];

    out->breakdown_cognitive =
          model->linear.weights[2] * x[2];

    out->breakdown_mitochondrial = 0.0;
    for (i = NEUROSTRATA_MITO_START; i < NEUROSTRATA_MITO_END; i++)
        out->breakdown_mitochondrial += model->linear.weights[i] * x[i];

    out->breakdown_inflammation = 0.0;
    for (i = NEUROSTRATA_INFLAM_START; i < NEUROSTRATA_INFLAM_END; i++)
        out->breakdown_inflammation += model->linear.weights[i] * x[i];

    out->breakdown_imbalance =
        out->breakdown_inflammation - out->breakdown_mitochondrial;

    /* ── Normalised indices (z-score range −3..+3 mapped to 0..1) ── */
    out->mito_index   = mito_used   ? normalize_01(out->mito_score,   -3.0, 3.0) : 0.5;
    out->inflam_index = inflam_used ? normalize_01(out->inflam_score, -3.0, 3.0) : 0.5;
    out->stress_index = (mito_used && inflam_used)
                      ? normalize_01(out->imbalance, -3.0, 3.0) : 0.5;

    out->mito_level   = level_from_index(out->mito_index);
    out->inflam_level = level_from_index(out->inflam_index);

    return NEUROSTRATA_OK;
}