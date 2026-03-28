#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#include "db.h"

#define DB_FILE "data/parkinome.db"

static char* extract_patient_id(const char *input_json) {
    if (!input_json) return NULL;

    cJSON *root = cJSON_Parse(input_json);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return NULL;
    }

    cJSON *pid = cJSON_GetObjectItem(root, "patient_id");
    if (!pid) {
        cJSON_Delete(root);
        return NULL;
    }

    char tmp[64] = {0};

    if (cJSON_IsString(pid) && pid->valuestring) {
        snprintf(tmp, sizeof(tmp), "%s", pid->valuestring);
    } else if (cJSON_IsNumber(pid)) {
        snprintf(tmp, sizeof(tmp), "%.0f", pid->valuedouble);
    } else {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_Delete(root);

    if (tmp[0] == '\0') return NULL;
    char *out = malloc(strlen(tmp) + 1);
    if (!out) return NULL;
    strcpy(out, tmp);
    return out;
}

int db_init(void) {
    sqlite3 *db = NULL;

    if (sqlite3_open(DB_FILE, &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return 1;
    }

    const char *sql_predictions =
        "CREATE TABLE IF NOT EXISTS predictions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "patient_id TEXT,"
        "input_json TEXT NOT NULL,"
        "output_json TEXT NOT NULL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    const char *sql_batches =
        "CREATE TABLE IF NOT EXISTS batches ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "input_json TEXT NOT NULL,"
        "output_json TEXT NOT NULL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    if (sqlite3_exec(db, sql_predictions, NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 1;
    }

    if (sqlite3_exec(db, sql_batches, NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);
    return 0;
}

int db_save_prediction(const char *input_json, const char *output_json) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    char *patient_id = NULL;

    if (!input_json || !output_json) return 1;

    if (sqlite3_open(DB_FILE, &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return 1;
    }

    const char *sql =
        "INSERT INTO predictions (patient_id, input_json, output_json) VALUES (?, ?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 1;
    }

    patient_id = extract_patient_id(input_json);

    if (patient_id) sqlite3_bind_text(stmt, 1, patient_id, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 1);

    sqlite3_bind_text(stmt, 2, input_json, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, output_json, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    free(patient_id);

    return (rc == SQLITE_DONE) ? 0 : 1;
}

int db_save_batch(const char *input_json, const char *output_json) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;

    if (!input_json || !output_json) return 1;

    if (sqlite3_open(DB_FILE, &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return 1;
    }

    const char *sql =
        "INSERT INTO batches (input_json, output_json) VALUES (?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 1;
    }

    sqlite3_bind_text(stmt, 1, input_json, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, output_json, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return (rc == SQLITE_DONE) ? 0 : 1;
}

char* db_get_predictions_json(int limit) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    cJSON *arr = NULL;
    char *out = NULL;

    if (limit <= 0) limit = 50;

    if (sqlite3_open(DB_FILE, &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return NULL;
    }

    const char *sql =
        "SELECT id, patient_id, input_json, output_json, created_at "
        "FROM predictions ORDER BY id DESC LIMIT ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, limit);

    arr = cJSON_CreateArray();
    if (!arr) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cJSON *row = cJSON_CreateObject();
        if (!row) continue;

        cJSON_AddNumberToObject(row, "id", sqlite3_column_int(stmt, 0));

        const unsigned char *pid = sqlite3_column_text(stmt, 1);
        if (pid) cJSON_AddStringToObject(row, "patient_id", (const char*)pid);
        else cJSON_AddNullToObject(row, "patient_id");

        const unsigned char *in = sqlite3_column_text(stmt, 2);
        const unsigned char *out_json = sqlite3_column_text(stmt, 3);
        const unsigned char *created = sqlite3_column_text(stmt, 4);

        cJSON_AddStringToObject(row, "input_json", in ? (const char*)in : "");
        cJSON_AddStringToObject(row, "output_json", out_json ? (const char*)out_json : "");
        cJSON_AddStringToObject(row, "created_at", created ? (const char*)created : "");

        cJSON_AddItemToArray(arr, row);
    }

    out = cJSON_PrintUnformatted(arr);

    cJSON_Delete(arr);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return out;
}
