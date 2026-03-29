#ifndef DB_H
#define DB_H

#define DB_OK 0
#define DB_ERR 1
#define DB_DUPLICATE 2

int db_init(void);
int db_save_prediction(const char *input_json, const char *output_json);
char* db_get_predictions_json(int limit);

#endif
