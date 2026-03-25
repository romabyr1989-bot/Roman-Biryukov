#ifndef PARKINOME_PREDICT_H
#define PARKINOME_PREDICT_H

/* =========================
   INPUT
   ========================= */

typedef struct {
    double age;
    double updrs_iii;
    double moca;
    double scopa_aut;
    double hoehn_yahr;

    double ndufa4l2;
    double ndufs2;
    double pink1;
    double ppargc1a;
    double nlrp3;
    double il1b;
    double s100a8;
    double cxcl8;
} parkinome_input_t;

/* =========================
   OUTPUT
   ========================= */

typedef struct {
    double isp;
    double risk_probability;
    double risk_score_raw;
    int category;

    double ci_lower;
    double ci_upper;

    double clinical_index;
    double mito_score;
    double inflam_score;
    double imbalance_index;
} parkinome_output_t;

/* =========================
   STATUS CODES
   ========================= */

#define PARKINOME_OK               0
#define PARKINOME_INVALID_RANGE    1
#define PARKINOME_NAN_DETECTED     2
#define PARKINOME_NULL_POINTER     3

/* =========================
   API
   ========================= */

int parkinome_predict(
    const parkinome_input_t *in,
    parkinome_output_t *out
);

#endif