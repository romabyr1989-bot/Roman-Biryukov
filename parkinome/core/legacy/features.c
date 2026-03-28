#include "predict.h"

/* Clinical normalization */
double compute_clinical_index(const parkinome_input_t *in) {

    double updrs = in->updrs_iii / 132.0;
    double moca_def = (30.0 - in->moca) / 30.0;
    double scopa = in->scopa_aut / 69.0;
    double hy = in->hoehn_yahr / 5.0;

    return 0.4 * updrs +
           0.2 * moca_def +
           0.2 * scopa +
           0.2 * hy;
}

/* Molecular */
double compute_mito_score(const parkinome_input_t *in) {
    return (in->ndufa4l2 +
            in->ndufs2 +
            in->pink1 +
            in->ppargc1a) / 4.0;
}

double compute_inflam_score(const parkinome_input_t *in) {
    return (in->nlrp3 +
            in->il1b +
            in->s100a8 +
            in->cxcl8) / 4.0;
}

double compute_imbalance(double inflam, double mito) {
    return inflam - mito;
}