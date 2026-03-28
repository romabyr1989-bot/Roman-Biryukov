#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define DB_FILE "patients.db"

/* ===== INIT ===== */
int db_init() {

    sqlite3 *db;

    if (sqlite3_open(DB_FILE, &db)) {
        return 1;
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS patients ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "data TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    sqlite3_exec(db, sql, 0, 0, 0);

    sqlite3_close(db);
    return 0;
}

/* ===== SAVE ===== */
int db_save_patient(const char *json) {

    sqlite3 *db;
    sqlite3_stmt *stmt;

    if (sqlite3_open(DB_FILE, &db)) return 1;

    const char *sql = "INSERT INTO patients (data) VALUES (?);";

    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);

    sqlite3_step(stmt);

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0;
}

/* ===== GET ===== */
char* db_get_patients() {

    sqlite3 *db;
    sqlite3_stmt *stmt;

    if (sqlite3_open(DB_FILE, &db)) return NULL;

    const char *sql = "SELECT data FROM patients;";

    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    char *result = malloc(8192);
    strcpy(result, "[");

    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {

        const char *data = (const char*)sqlite3_column_text(stmt, 0);

        if (!first) strcat(result, ",");
        strcat(result, data);

        first = 0;
    }

    strcat(result, "]");

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return result;
}