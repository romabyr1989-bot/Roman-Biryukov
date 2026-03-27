#include <math.h>
#include "predict.h"

/* ===== GROUP WEIGHTS ===== */
#define W_CLINICAL   0.3
#define W_COGNITIVE  0.2
#define W_BIOMARKER  0.5

/* ===== NORMALIZATION ===== */
double normalize(double x, double min, double max) {
    if (x < min) x = min;
    if (x > max) x = max;
    return (x - min) / (max - min);
}

/* ===== CLINICAL ===== */
double calc_clinical(parkinome_input_t *in, int *used) {

    double sum = 0;
    int count = 0;

    if (in->has_age) {
        sum += normalize(in->age, 40, 90);
        count++;
    }

    if (in->has_updrs_iii) {
        sum += normalize(in->updrs_iii, 0, 100);
        count++;
    }

    if (in->has_hoehn_yahr) {
        sum += normalize(in->hoehn_yahr, 0, 5);
        count++;
    }

    if (in->has_scopa_aut) {
        sum += normalize(in->scopa_aut, 0, 100);
        count++;
    }

    *used = (count > 0);
    return (count > 0) ? sum / count : 0;
}

/* ===== COGNITIVE ===== */
double calc_cognitive(parkinome_input_t *in, int *used) {

    if (in->has_moca) {
        *used = 1;
        /* ниже MOCA → выше риск */
        return 1.0 - normalize(in->moca, 0, 30);
    }

    *used = 0;
    return 0;
}

/* ===== BIOMARKERS ===== */
double calc_biomarkers(parkinome_input_t *in, int *used) {

    double sum = 0;
    int count = 0;

    if (in->has_ndufa4l2) { sum += normalize(in->ndufa4l2, 0, 10); count++; }
    if (in->has_ndufs2)   { sum += normalize(in->ndufs2, 0, 10); count++; }
    if (in->has_pink1)    { sum += normalize(in->pink1, 0, 10); count++; }
    if (in->has_ppargc1a) { sum += normalize(in->ppargc1a, 0, 10); count++; }
    if (in->has_nlrp3)    { sum += normalize(in->nlrp3, 0, 10); count++; }
    if (in->has_il1b)     { sum += normalize(in->il1b, 0, 10); count++; }
    if (in->has_s100a8)   { sum += normalize(in->s100a8, 0, 10); count++; }
    if (in->has_cxcl8)    { sum += normalize(in->cxcl8, 0, 10); count++; }

    *used = (count > 0);
    return (count > 0) ? sum / count : 0;
}

/* ===== MAIN MODEL ===== */
int parkinome_predict(parkinome_input_t *in, parkinome_output_t *out) {

    if (!in || !out) return PARKINOME_NULL_POINTER;

    int use_clinical = 0;
    int use_cognitive = 0;
    int use_biomarker = 0;

    double clinical = calc_clinical(in, &use_clinical);
    double cognitive = calc_cognitive(in, &use_cognitive);
    double biomarkers = calc_biomarkers(in, &use_biomarker);

    double total_score = 0;
    double total_weight = 0;

    /* combine groups */
    if (use_clinical) {
        total_score += clinical * W_CLINICAL;
        total_weight += W_CLINICAL;
    }

    if (use_cognitive) {
        total_score += cognitive * W_COGNITIVE;
        total_weight += W_COGNITIVE;
    }

    if (use_biomarker) {
        total_score += biomarkers * W_BIOMARKER;
        total_weight += W_BIOMARKER;
    }

    /* если нет данных */
    if (total_weight == 0) {
        return PARKINOME_ERR_INVALID_INPUT;
    }

    double risk = total_score / total_weight;

    /* ===== OUTPUT ===== */
    out->risk_probability = risk;
    out->isp = risk;

    /* category */
    if (risk < 0.33)
        out->category = 0;
    else if (risk < 0.66)
        out->category = 1;
    else
        out->category = 2;

    /* confidence = доля использованных групп */
    out->confidence = total_weight;

    return PARKINOME_OK;
}