#ifndef PREDICTOR_H
#define PREDICTOR_H

#include <stddef.h>

/* Унифицированный предиктор:
   input_json поддерживает объект пациента, массив пациентов или { "patients": [...] }.
   output_json получает результат в формате { "count": N, "patients": [...] }. */
int run_prediction(const char *input_json, char *output_json, size_t out_size);
int predictor_init_model(const char *model_path);

#endif
