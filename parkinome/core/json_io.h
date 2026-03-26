#ifndef JSON_IO_H
#define JSON_IO_H

#include <cjson/cJSON.h>
#include "predict.h"

int parse_patient(cJSON *json, parkinome_input_t *in);
char* read_file(const char* filename);

#endif