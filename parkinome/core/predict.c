#include "predict.h"
#include "model_weights.h"

/* prototypes */
int validate_input(const parkinome_input_t *in);

double compute_clinical_index(const parkinome_input_t *in);
double compute_mito_score(const parkinome_input_t *in);
double compute_inflam_score(const parkinome_input_t *in);
double compute_imbalance(double inflam, double mito);

double compute_risk_score(double clinical,
                          double mito,
                          double inflam,
                          double imbalance,
                          double age);

double logistic(double x);
double compute_isp(double clinical,
                   double mito,
                   double inflam,
                   double age);

int parkinome_predict(
    const parkinome_input_t *in,
    parkinome_output_t *out
) {

    if (!in || !out)
        return PARKINOME_NULL_POINTER;

    int status = validate_input(in);
    if (status != PARKINOME_OK)
        return status;

    /* Features */
    double clinical = compute_clinical_index(in);
    double mito = compute_mito_score(in);
    double inflam = compute_inflam_score(in);
    double imbalance = compute_imbalance(inflam, mito);

    /* Risk */
    double raw = compute_risk_score(
        clinical, mito, inflam, imbalance, in->age
    );

    double prob = logistic(raw);

    /* Category */
    int category;
    if (prob < 0.33) category = 0;
    else if (prob < 0.66) category = 1;
    else category = 2;

    /* ISP */
    double isp = compute_isp(clinical, mito, inflam, in->age);

    /* CI */
    double ci_lower = isp - 1.96 * MODEL_SIGMA;
    double ci_upper = isp + 1.96 * MODEL_SIGMA;

    /* Output */
    out->isp = isp;
    out->risk_probability = prob;
    out->risk_score_raw = raw;
    out->category = category;

    out->ci_lower = ci_lower;
    out->ci_upper = ci_upper;

    out->clinical_index = clinical;
    out->mito_score = mito;
    out->inflam_score = inflam;
    out->imbalance_index = imbalance;

    return PARKINOME_OK;
}