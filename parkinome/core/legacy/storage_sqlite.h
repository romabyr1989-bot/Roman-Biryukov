#ifndef STORAGE_SQLITE_H
#define STORAGE_SQLITE_H

int db_init();
int db_save_patient(const char *json);
char* db_get_patients();

#endif