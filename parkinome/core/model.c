#include <math.h>
#include "model_weights.h"

/* clamp для стабильности */
static double clamp(double x) {
    if (x > 20.0) return 20.0;
    if (x < -20.0) return -20.0;
    return x;
}

double compute_risk_score(double clinical,
                          double mito,
                          double inflam,
                          double imbalance,
                          double age) {

    double score =
        B0 +
        B1 * clinical +
        B2 * mito +
        B3 * inflam +
        B4 * imbalance +
        B5 * (age / 90.0);

    return clamp(score);
}

double logistic(double x) {
    return 1.0 / (1.0 + exp(-x));
}

double compute_isp(double clinical,
                   double mito,
                   double inflam,
                   double age) {

    return G0 +
           G1 * clinical +
           G2 * mito +
           G3 * inflam +
           G4 * age;
}