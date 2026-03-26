#ifndef PREDICTOR_H
#define PREDICTOR_H

#include <stddef.h>

int run_prediction(const char *input_json, char *output_json, size_t out_size);
int run_batch(const char *input_json, char *output_json, size_t out_size);

#endif