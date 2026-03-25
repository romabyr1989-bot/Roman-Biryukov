#include <math.h>
#include "predict.h"

static int check_nan(double x) {
    return isnan(x) || isinf(x);
}

int validate_input(const parkinome_input_t *in) {

    if (!in) return PARKINOME_NULL_POINTER;

    /* Clinical ranges */
    if (in->age < 30 || in->age > 90) return PARKINOME_INVALID_RANGE;
    if (in->updrs_iii < 0 || in->updrs_iii > 132) return PARKINOME_INVALID_RANGE;
    if (in->moca < 0 || in->moca > 30) return PARKINOME_INVALID_RANGE;
    if (in->scopa_aut < 0 || in->scopa_aut > 69) return PARKINOME_INVALID_RANGE;
    if (in->hoehn_yahr < 1 || in->hoehn_yahr > 5) return PARKINOME_INVALID_RANGE;

    /* Molecular */
    double genes[] = {
        in->ndufa4l2, in->ndufs2, in->pink1, in->ppargc1a,
        in->nlrp3, in->il1b, in->s100a8, in->cxcl8
    };

    for (int i = 0; i < 8; i++) {
        if (check_nan(genes[i])) return PARKINOME_NAN_DETECTED;
        if (genes[i] < -10 || genes[i] > 10) return PARKINOME_INVALID_RANGE;
    }

    return PARKINOME_OK;
}