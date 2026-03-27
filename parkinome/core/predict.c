#include "predict.h"

/*
 * Этот файл теперь просто прокси к model.c
 * Вся логика — в parkinome_predict()
 */

/* ===== MAIN WRAPPER ===== */
int parkinome_run(parkinome_input_t *in, parkinome_output_t *out) {

    if (!in || !out) {
        return PARKINOME_NULL_POINTER;
    }

    /* вызываем модель */
    return parkinome_predict(in, out);
}