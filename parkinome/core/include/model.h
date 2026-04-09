#ifndef NEUROSTRATA_MODEL_H
#define NEUROSTRATA_MODEL_H

/*
 * NeuroStrata — Molecular Progression Stratification Engine
 *
 * Feature layout (NEUROSTRATA_FEATURE_COUNT = 15):
 *
 *  [0]  age            — years at enrolment               (30–80)
 *  [1]  updrs_iii      — MDS-UPDRS Part III score          (0–132)
 *  [2]  moca           — Montreal Cognitive Assessment     (0–30)
 *  [3]  scopa_aut      — SCOPA-AUT autonomic scale         (0–69)
 *  [4]  hoehn_yahr     — Hoehn & Yahr stage               (1–5)
 *
 *  Mitochondrial genes (log2-CPM, z-scored vs cohort mean):
 *  [5]  ndufa5         — NADH:Ubiquinone Oxidoreductase subunit A5 (Complex I)
 *  [6]  ndufs2         — NADH:Ubiquinone Oxidoreductase subunit S2 (Complex I)
 *  [7]  pink1          — PTEN-induced kinase 1 (mitophagy)
 *  [8]  ppargc1a       — PGC-1α (mitochondrial biogenesis)
 *  [9]  cox7a2         — Cytochrome c oxidase subunit 7A2 (Complex IV)
 *
 *  Inflammatory genes (log2-CPM, z-scored vs cohort mean):
 *  [10] tlr4           — Toll-like receptor 4
 *  [11] nlrp3          — NLRP3 inflammasome
 *  [12] il1b           — Interleukin-1 beta
 *  [13] s100a8         — S100 calcium binding protein A8 (calprotectin)
 *  [14] cxcl8          — IL-8 / CXCL8 (neutrophil chemoattractant)
 *
 * Changes from original model.c:
 *   - NDUFA4L2 → NDUFA5  (aligns with dissertation sec. 5.3 / 5.6)
 *   - Added COX7A2        (key predictor, dissertation fig. 5.4)
 *   - Added TLR4          (top inflammatory predictor, dissertation fig. 5.4)
 *   - Feature count 13 → 15
 *   - Linear regression head added for continuous ISP (pts/yr)
 *   - Input validation ranges added
 *   - Gene expression expected as z-scored log2-CPM (see model_zscore_gene())
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Constants ─────────────────────────────────────────────────────────── */

#define NEUROSTRATA_FEATURE_COUNT   15
#define NEUROSTRATA_MITO_START       5   /* inclusive */
#define NEUROSTRATA_MITO_END        10   /* exclusive: genes [5..9] */
#define NEUROSTRATA_MITO_N           5
#define NEUROSTRATA_INFLAM_START    10   /* inclusive */
#define NEUROSTRATA_INFLAM_END      15   /* exclusive: genes [10..14] */
#define NEUROSTRATA_INFLAM_N         5

/* ISP range observed in PPMI cohort (MDS-UPDRS pts / year) */
#define NEUROSTRATA_ISP_MIN        -2.0
#define NEUROSTRATA_ISP_MAX        15.0
#define NEUROSTRATA_ISP_FAST_THR    5.2   /* Q4 lower bound (dissertation sec. 3.3) */
#define NEUROSTRATA_ISP_SLOW_THR    1.4   /* Q1 upper bound */

/* Return codes */
#define NEUROSTRATA_OK                  0
#define NEUROSTRATA_NULL_POINTER       -1
#define NEUROSTRATA_ERR_INVALID_INPUT  -2
#define NEUROSTRATA_ERR_IO             -3
#define NEUROSTRATA_ERR_RANGE          -4   /* clinical value out of valid range */

/* Progression category (maps to ISP quartile groups) */
#define NEUROSTRATA_CAT_SLOW    0   /* ISP <= 1.4 pts/yr  (Q1) */
#define NEUROSTRATA_CAT_MODERATE 1  /* ISP 1.4–5.2 pts/yr (Q2-Q3) */
#define NEUROSTRATA_CAT_FAST    2   /* ISP >= 5.2 pts/yr  (Q4) */

/* ─── Cohort normalisation parameters ───────────────────────────────────── */
/*
 * Clinical features are z-scored using PPMI cohort statistics
 * (dissertation table 2.1).  Gene expression must be z-scored externally
 * (log2-CPM → subtract cohort mean → divide by cohort SD) before passing
 * to model_fill_features().  Use model_zscore_gene() for convenience.
 */
typedef struct {
    double mean;
    double sd;
} norm_params_t;

/* PPMI baseline cohort statistics (dissertation Table 2.1) */
static const norm_params_t CLINICAL_NORM[5] = {
    { 61.4,  9.8  },   /* [0] age */
    { 20.8,  8.9  },   /* [1] updrs_iii */
    { 27.1,  2.3  },   /* [2] moca */
    {  7.5,  5.2  },   /* [3] scopa_aut  — representative PPMI value */
    {  1.6,  0.5  },   /* [4] hoehn_yahr */
};

/* ─── Model structures ───────────────────────────────────────────────────── */

/*
 * Logistic head — predicts P(fast progressor).
 * Inputs: z-scored features [0..14].
 */
typedef struct {
    double intercept;
    double weights[NEUROSTRATA_FEATURE_COUNT];
} logistic_model_t;

/*
 * Linear head — predicts continuous ISP in MDS-UPDRS pts / year.
 * Same feature vector as logistic head.
 * ISP = intercept_lin + sum(weights_lin[i] * x[i])
 * Trained jointly with logistic head via elastic net (α=0.5).
 */
typedef struct {
    double intercept;
    double weights[NEUROSTRATA_FEATURE_COUNT];
} linear_model_t;

/*
 * Full model bundle: both heads live together so they share the same
 * training run and can be loaded/saved atomically.
 */
typedef struct {
    logistic_model_t logistic;   /* classification head */
    linear_model_t   linear;     /* regression head  */
    int              version;    /* model file format version */
} neurostrata_model_t;

/* ─── Input / output ─────────────────────────────────────────────────────── */

typedef struct {
    /* Clinical (raw values — normalisation is done internally) */
    double age;         int has_age;
    double updrs_iii;   int has_updrs_iii;
    double moca;        int has_moca;
    double scopa_aut;   int has_scopa_aut;
    double hoehn_yahr;  int has_hoehn_yahr;

    /*
     * Gene expression — MUST be provided as z-scored log2-CPM values
     * relative to the reference cohort.
     * Use model_zscore_gene(raw_log2cpm, cohort_mean, cohort_sd).
     * Positive values → above-average expression.
     * Negative values → below-average expression.
     */
    double ndufa5;      int has_ndufa5;    /* was ndufa4l2 — corrected */
    double ndufs2;      int has_ndufs2;
    double pink1;       int has_pink1;
    double ppargc1a;    int has_ppargc1a;
    double cox7a2;      int has_cox7a2;    /* added — key predictor */

    double tlr4;        int has_tlr4;      /* added — top inflammatory predictor */
    double nlrp3;       int has_nlrp3;
    double il1b;        int has_il1b;
    double s100a8;      int has_s100a8;
    double cxcl8;       int has_cxcl8;
} neurostrata_input_t;

typedef struct {
    /* ── Primary outputs ── */
    double isp;              /* Progression Speed Index, MDS-UPDRS pts/yr   */
    double isp_low;          /* 95% CI lower bound (bootstrap, placeholder) */
    double isp_high;         /* 95% CI upper bound */
    double risk_probability; /* P(fast progressor | features), logistic head */
    int    category;         /* 0=slow, 1=moderate, 2=fast */
    double confidence;       /* 0–1: data completeness × margin combined */

    /* ── Biological axes ── */
    double mito_score;       /* mean z-score of mitochondrial genes (present only) */
    double inflam_score;     /* mean z-score of inflammatory genes (present only) */
    double imbalance;        /* inflam_score − mito_score (the key biomarker ratio) */

    double mito_index;       /* mito_score mapped to [0,1] */
    double inflam_index;     /* inflam_score mapped to [0,1] */
    double stress_index;     /* imbalance mapped to [0,1] */

    int    mito_level;       /* 0=low, 1=moderate, 2=high */
    int    inflam_level;     /* 0=low, 1=moderate, 2=high */

    /* ── Additive ISP breakdown (sum ≈ isp) ── */
    double breakdown_clinical;        /* contribution of clinical features */
    double breakdown_cognitive;       /* contribution of MoCA */
    double breakdown_mitochondrial;   /* contribution of mito genes */
    double breakdown_inflammation;    /* contribution of inflam genes */
    double breakdown_imbalance;       /* inflammation − mitochondrial */

    /* ── Data completeness ── */
    int features_used;       /* number of non-missing features */
    int features_total;      /* NEUROSTRATA_FEATURE_COUNT */
    int mito_genes_used;
    int inflam_genes_used;
} neurostrata_output_t;

/* ─── Public API ─────────────────────────────────────────────────────────── */

/* Utility */
double model_zscore_gene(double log2cpm, double cohort_mean, double cohort_sd);
int    model_validate_input(const neurostrata_input_t *in);

/* Feature vector */
void model_fill_features(const neurostrata_input_t *in,
                         double x[NEUROSTRATA_FEATURE_COUNT],
                         int present[NEUROSTRATA_FEATURE_COUNT]);

/* Prediction heads */
double model_sigmoid(double z);
double model_predict_probability(const logistic_model_t *m,
                                 const double x[NEUROSTRATA_FEATURE_COUNT]);
double model_predict_isp(const linear_model_t *m,
                         const double x[NEUROSTRATA_FEATURE_COUNT]);

/* Active model management */
int                         model_set_active(const neurostrata_model_t *m);
int                         model_load_active(const char *path);
const neurostrata_model_t  *model_get_active(void);

/* Serialisation */
int model_save(const char *path, const neurostrata_model_t *m);
int model_load(const char *path, neurostrata_model_t *m);

/* Main predict function */
int neurostrata_predict(neurostrata_input_t *in, neurostrata_output_t *out);

/* Backward-compatibility shim (deprecated — will be removed in v2) */
typedef neurostrata_input_t  parkinome_input_t;
typedef neurostrata_output_t parkinome_output_t;
#define parkinome_predict    neurostrata_predict
#define PARKINOME_OK         NEUROSTRATA_OK
#define PARKINOME_NULL_POINTER      NEUROSTRATA_NULL_POINTER
#define PARKINOME_ERR_INVALID_INPUT NEUROSTRATA_ERR_INVALID_INPUT
#define PARKINOME_FEATURE_COUNT     NEUROSTRATA_FEATURE_COUNT

#ifdef __cplusplus
}
#endif

#endif /* NEUROSTRATA_MODEL_H */
