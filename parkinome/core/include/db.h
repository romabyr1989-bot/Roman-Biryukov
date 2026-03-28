#ifndef DB_H
#define DB_H

int db_init(void);
int db_save_prediction(const char *input_json, const char *output_json);
int db_save_batch(const char *input_json, const char *output_json);
char* db_get_predictions_json(int limit);

#endif
