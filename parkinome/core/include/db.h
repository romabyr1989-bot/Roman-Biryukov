#ifndef DB_H
#define DB_H

/* Коды результата операций БД. */
#define DB_OK 0
#define DB_ERR 1
#define DB_DUPLICATE 2

/* Инициализирует схему, миграции и индексы (идемпотентно). */
int db_init(void);
/* Сохраняет пару input/output; возвращает DB_DUPLICATE при конфликте уникальности. */
int db_save_prediction(const char *input_json, const char *output_json);
/* Возвращает JSON-массив истории (caller обязан освободить память через free). */
char* db_get_predictions_json(int limit);

#endif
