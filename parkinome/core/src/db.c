#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#include "db.h"

#define DB_FILE "data/parkinome.db"
#define DB_FILE_ALT "parkinome/core/data/parkinome.db"

/* Открывает единый файл БД независимо от cwd.
   Приоритет: рядом с бинарником -> legacy пути. */
static int open_db(sqlite3 **db) {
    char exe_path[PATH_MAX];
    char exe_dir[PATH_MAX];
    char db_from_exe[PATH_MAX];
    uint32_t exe_size = sizeof(exe_path);

    if (!db) return SQLITE_ERROR;
    *db = NULL;

    /* Самый надежный вариант: БД рядом с бинарником (<exe_dir>/data/parkinome.db). */
    if (_NSGetExecutablePath(exe_path, &exe_size) == 0) {
        snprintf(exe_dir, sizeof(exe_dir), "%s", exe_path);
        snprintf(db_from_exe, sizeof(db_from_exe), "%s/data/parkinome.db", dirname(exe_dir));
        if (access(db_from_exe, F_OK) == 0) {
            return sqlite3_open(db_from_exe, db);
        }
    }

    /* Обратная совместимость со старыми сценариями запуска. */
    if (access("data", F_OK) == 0) {
        return sqlite3_open(DB_FILE, db);
    }
    if (access("parkinome/core/data", F_OK) == 0) {
        return sqlite3_open(DB_FILE_ALT, db);
    }
    return sqlite3_open(DB_FILE, db);
}

static char* normalize_patient_id(const char *raw) {
    size_t len = 0;
    size_t start = 0;
    size_t end = 0;
    char *out = NULL;

    if (!raw) return NULL;
    len = strlen(raw);
    if (len == 0) return NULL;

    /* Удаляем только внешние пробелы, внутренние символы сохраняем как есть. */
    while (start < len && (raw[start] == ' ' || raw[start] == '\t' || raw[start] == '\n' || raw[start] == '\r')) start++;
    if (start == len) return NULL;
    end = len - 1;
    while (end > start && (raw[end] == ' ' || raw[end] == '\t' || raw[end] == '\n' || raw[end] == '\r')) end--;

    out = malloc((end - start + 2));
    if (!out) return NULL;
    memcpy(out, raw + start, end - start + 1);
    out[end - start + 1] = '\0';
    return out;
}

static int payload_count(cJSON *root) {
    cJSON *patients = NULL;
    if (!root) return 0;
    if (cJSON_IsArray(root)) return cJSON_GetArraySize(root);
    if (!cJSON_IsObject(root)) return 0;
    patients = cJSON_GetObjectItem(root, "patients");
    if (patients && cJSON_IsArray(patients)) return cJSON_GetArraySize(patients);
    return 1;
}

static cJSON* payload_item(cJSON *root, int idx) {
    cJSON *patients = NULL;
    if (!root || idx < 0) return NULL;
    if (cJSON_IsArray(root)) return cJSON_GetArrayItem(root, idx);
    if (!cJSON_IsObject(root)) return NULL;
    patients = cJSON_GetObjectItem(root, "patients");
    if (patients && cJSON_IsArray(patients)) return cJSON_GetArrayItem(patients, idx);
    return (idx == 0) ? root : NULL;
}

static void add_number_or_null(cJSON *obj, const char *name, cJSON *src) {
    cJSON *item = NULL;
    if (!obj || !name || !src || !cJSON_IsObject(src)) {
        if (obj && name) cJSON_AddNullToObject(obj, name);
        return;
    }
    item = cJSON_GetObjectItem(src, name);
    if (item && cJSON_IsNumber(item)) cJSON_AddNumberToObject(obj, name, item->valuedouble);
    else cJSON_AddNullToObject(obj, name);
}

static void add_string_or_null(cJSON *obj, const char *name, cJSON *src) {
    cJSON *item = NULL;
    if (!obj || !name || !src || !cJSON_IsObject(src)) {
        if (obj && name) cJSON_AddNullToObject(obj, name);
        return;
    }
    item = cJSON_GetObjectItem(src, name);
    if (item && cJSON_IsString(item) && item->valuestring) cJSON_AddStringToObject(obj, name, item->valuestring);
    else cJSON_AddNullToObject(obj, name);
}

static char* extract_patient_id(const char *input_json) {
    if (!input_json) return NULL;

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        if (root) cJSON_Delete(root);
        return NULL;
    }

    cJSON *patient_obj = NULL;
    cJSON *pid = NULL;

    if (cJSON_IsObject(root)) {
        cJSON *patients = cJSON_GetObjectItem(root, "patients");
        if (patients && cJSON_IsArray(patients)) {
            /* Для группового сохранения patient_id не извлекаем:
               уникальность patient_id применяется только к одиночным пациентам. */
            if (cJSON_GetArraySize(patients) != 1) {
                cJSON_Delete(root);
                return NULL;
            }
            patient_obj = cJSON_GetArrayItem(patients, 0);
        } else {
            patient_obj = root;
        }
    } else if (cJSON_IsArray(root) && cJSON_GetArraySize(root) > 0) {
        if (cJSON_GetArraySize(root) != 1) {
            cJSON_Delete(root);
            return NULL;
        }
        patient_obj = cJSON_GetArrayItem(root, 0);
    }

    if (!patient_obj || !cJSON_IsObject(patient_obj)) {
        cJSON_Delete(root);
        return NULL;
    }

    pid = cJSON_GetObjectItem(patient_obj, "patient_id");
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
    return normalize_patient_id(tmp);
}

/* Извлекает агрегированные выходные поля для одиночного пациента.
   Для batch payload возвращает 0 (колонки остаются NULL). */
static int extract_output_metrics(
    const char *output_json,
    double *risk_probability,
    double *confidence,
    double *isp,
    double *mito_score,
    double *inflam_score,
    double *imbalance,
    char category[32]
) {
    cJSON *root = NULL;
    cJSON *patient_obj = NULL;
    cJSON *patients = NULL;
    cJSON *item = NULL;

    if (!output_json) return 0;
    root = cJSON_Parse(output_json);
    if (!root) return 0;

    if (cJSON_IsObject(root)) {
        patients = cJSON_GetObjectItem(root, "patients");
        if (patients && cJSON_IsArray(patients)) {
            if (cJSON_GetArraySize(patients) != 1) {
                cJSON_Delete(root);
                return 0;
            }
            patient_obj = cJSON_GetArrayItem(patients, 0);
        } else {
            patient_obj = root;
        }
    } else if (cJSON_IsArray(root)) {
        if (cJSON_GetArraySize(root) != 1) {
            cJSON_Delete(root);
            return 0;
        }
        patient_obj = cJSON_GetArrayItem(root, 0);
    }

    if (!patient_obj || !cJSON_IsObject(patient_obj)) {
        cJSON_Delete(root);
        return 0;
    }

    item = cJSON_GetObjectItem(patient_obj, "risk_probability");
    if (!(item && cJSON_IsNumber(item))) { cJSON_Delete(root); return 0; }
    *risk_probability = item->valuedouble;

    item = cJSON_GetObjectItem(patient_obj, "confidence");
    if (!(item && cJSON_IsNumber(item))) { cJSON_Delete(root); return 0; }
    *confidence = item->valuedouble;

    item = cJSON_GetObjectItem(patient_obj, "isp");
    if (!(item && cJSON_IsNumber(item))) { cJSON_Delete(root); return 0; }
    *isp = item->valuedouble;

    item = cJSON_GetObjectItem(patient_obj, "mito_score");
    if (item && cJSON_IsNumber(item)) *mito_score = item->valuedouble;

    item = cJSON_GetObjectItem(patient_obj, "inflam_score");
    if (item && cJSON_IsNumber(item)) *inflam_score = item->valuedouble;

    item = cJSON_GetObjectItem(patient_obj, "imbalance");
    if (item && cJSON_IsNumber(item)) *imbalance = item->valuedouble;

    item = cJSON_GetObjectItem(patient_obj, "category");
    if (item && cJSON_IsString(item) && item->valuestring) {
        snprintf(category, 32, "%s", item->valuestring);
    } else {
        category[0] = '\0';
    }

    cJSON_Delete(root);
    return 1;
}

/* Заполняет денормализованные колонки метрик для старых записей, где они отсутствуют.
   Идемпотентно: уже заполненные строки не меняет. */
static int backfill_output_metrics(sqlite3 *db) {
    sqlite3_stmt *sel = NULL;
    sqlite3_stmt *upd = NULL;
    int rc = SQLITE_OK;
    const char *sel_sql =
        "SELECT id, output_json FROM predictions "
        "WHERE risk_probability IS NULL OR confidence IS NULL OR isp IS NULL "
        "OR mito_score IS NULL OR inflam_score IS NULL OR imbalance IS NULL;";
    const char *upd_sql =
        "UPDATE predictions SET "
        "risk_probability = ?, category = ?, confidence = ?, isp = ?, "
        "mito_score = ?, inflam_score = ?, imbalance = ? "
        "WHERE id = ?;";

    if (sqlite3_prepare_v2(db, sel_sql, -1, &sel, NULL) != SQLITE_OK) return 1;
    if (sqlite3_prepare_v2(db, upd_sql, -1, &upd, NULL) != SQLITE_OK) {
        sqlite3_finalize(sel);
        return 1;
    }

    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        int id = sqlite3_column_int(sel, 0);
        const unsigned char *out_json = sqlite3_column_text(sel, 1);
        double risk_probability = 0.0, confidence = 0.0, isp = 0.0;
        double mito_score = 0.0, inflam_score = 0.0, imbalance = 0.0;
        char category[32] = {0};
        int ok = extract_output_metrics(
            out_json ? (const char*)out_json : NULL,
            &risk_probability, &confidence, &isp,
            &mito_score, &inflam_score, &imbalance, category
        );
        if (!ok) continue; /* batch/битый json пропускаем */

        sqlite3_reset(upd);
        sqlite3_clear_bindings(upd);
        sqlite3_bind_double(upd, 1, risk_probability);
        if (category[0] != '\0') sqlite3_bind_text(upd, 2, category, -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(upd, 2);
        sqlite3_bind_double(upd, 3, confidence);
        sqlite3_bind_double(upd, 4, isp);
        sqlite3_bind_double(upd, 5, mito_score);
        sqlite3_bind_double(upd, 6, inflam_score);
        sqlite3_bind_double(upd, 7, imbalance);
        sqlite3_bind_int(upd, 8, id);
        if (sqlite3_step(upd) != SQLITE_DONE) {
            sqlite3_finalize(sel);
            sqlite3_finalize(upd);
            return 1;
        }
    }

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(sel);
        sqlite3_finalize(upd);
        return 1;
    }

    sqlite3_finalize(sel);
    sqlite3_finalize(upd);
    return 0;
}

int db_init(void) {
    sqlite3 *db = NULL;

    if (open_db(&db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return 1;
    }

    const char *sql_predictions =
        "CREATE TABLE IF NOT EXISTS predictions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "kind TEXT NOT NULL DEFAULT 'prediction',"
        "patient_id TEXT,"
        "input_json TEXT NOT NULL,"
        "output_json TEXT NOT NULL,"
        "risk_probability REAL,"
        "category TEXT,"
        "confidence REAL,"
        "isp REAL,"
        "mito_score REAL,"
        "inflam_score REAL,"
        "imbalance REAL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    if (sqlite3_exec(db, sql_predictions, NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 1;
    }

    /* Для существующих БД добавляем колонку kind (совместимость со старыми файлами БД). */
    sqlite3_exec(db, "ALTER TABLE predictions ADD COLUMN kind TEXT NOT NULL DEFAULT 'prediction';", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE predictions ADD COLUMN risk_probability REAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE predictions ADD COLUMN category TEXT;", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE predictions ADD COLUMN confidence REAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE predictions ADD COLUMN isp REAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE predictions ADD COLUMN mito_score REAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE predictions ADD COLUMN inflam_score REAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE predictions ADD COLUMN imbalance REAL;", NULL, NULL, NULL);

    /* Одноразовая миграция из старой таблицы batches в predictions. */
    sqlite3_exec(db,
        "INSERT INTO predictions (kind, patient_id, input_json, output_json, created_at) "
        "SELECT 'batch', NULL, input_json, output_json, created_at FROM batches;",
        NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE IF EXISTS batches;", NULL, NULL, NULL);

    /* Нормализуем patient_id перед дедупликацией/индексами. */
    sqlite3_exec(db,
        "UPDATE predictions SET patient_id = TRIM(patient_id) WHERE patient_id IS NOT NULL;",
        NULL, NULL, NULL);
    sqlite3_exec(db,
        "UPDATE predictions SET patient_id = NULL WHERE patient_id = '';",
        NULL, NULL, NULL);

    /* Чистим исторические дубли перед включением уникальных ограничений. */
    sqlite3_exec(db,
        "DELETE FROM predictions "
        "WHERE id NOT IN ("
        "  SELECT MIN(id) FROM predictions WHERE patient_id IS NOT NULL GROUP BY LOWER(patient_id)"
        ") AND patient_id IS NOT NULL;",
        NULL, NULL, NULL);

    sqlite3_exec(db,
        "DELETE FROM predictions "
        "WHERE id NOT IN ("
        "  SELECT MIN(id) FROM predictions GROUP BY input_json, output_json"
        ");",
        NULL, NULL, NULL);

    /* Жесткие ограничения от дублей:
       - patient_id уникален (без учета регистра);
       - одинаковые пары input/output запрещены. */
    if (sqlite3_exec(db,
        "CREATE UNIQUE INDEX IF NOT EXISTS ux_predictions_patient_id "
        "ON predictions(patient_id COLLATE NOCASE) WHERE patient_id IS NOT NULL;",
        NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 1;
    }

    if (sqlite3_exec(db,
        "CREATE UNIQUE INDEX IF NOT EXISTS ux_predictions_payload "
        "ON predictions(input_json, output_json);",
        NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 1;
    }

    /* Обновляем старые строки: перенос метрик из output_json в отдельные колонки. */
    if (backfill_output_metrics(db) != 0) {
        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);
    return 0;
}

int db_save_prediction(const char *input_json, const char *output_json) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    sqlite3_stmt *check_stmt = NULL;
    char *patient_id = NULL;
    double risk_probability = 0.0;
    double confidence = 0.0;
    double isp = 0.0;
    double mito_score = 0.0;
    double inflam_score = 0.0;
    double imbalance = 0.0;
    char category[32] = {0};
    int has_metrics = 0;

    if (!input_json || !output_json) return DB_ERR;

    if (open_db(&db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return DB_ERR;
    }

    /* Для batch payload patient_id == NULL: проверка уникальности идет только по payload. */
    patient_id = extract_patient_id(input_json);
    has_metrics = extract_output_metrics(
        output_json,
        &risk_probability,
        &confidence,
        &isp,
        &mito_score,
        &inflam_score,
        &imbalance,
        category
    );
    if (patient_id) {
        char *normalized = normalize_patient_id(patient_id);
        free(patient_id);
        patient_id = normalized;
    }

    /* Запрещаем запись с уже существующим patient_id (case-insensitive). */
    if (patient_id) {
        const char *check_pid_sql = "SELECT 1 FROM predictions WHERE patient_id = ? COLLATE NOCASE LIMIT 1;";
        if (sqlite3_prepare_v2(db, check_pid_sql, -1, &check_stmt, NULL) != SQLITE_OK) {
            sqlite3_close(db);
            free(patient_id);
            return DB_ERR;
        }
        sqlite3_bind_text(check_stmt, 1, patient_id, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            sqlite3_finalize(check_stmt);
            sqlite3_close(db);
            free(patient_id);
            return DB_DUPLICATE;
        }
        sqlite3_finalize(check_stmt);
        check_stmt = NULL;
    }

    /* Запрещаем дубликат записи (одинаковые input_json + output_json). */
    {
        const char *check_payload_sql =
            "SELECT 1 FROM predictions WHERE input_json = ? AND output_json = ? LIMIT 1;";
        if (sqlite3_prepare_v2(db, check_payload_sql, -1, &check_stmt, NULL) != SQLITE_OK) {
            sqlite3_close(db);
            free(patient_id);
            return DB_ERR;
        }
        sqlite3_bind_text(check_stmt, 1, input_json, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(check_stmt, 2, output_json, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            sqlite3_finalize(check_stmt);
            sqlite3_close(db);
            free(patient_id);
            return DB_DUPLICATE;
        }
        sqlite3_finalize(check_stmt);
        check_stmt = NULL;
    }

    const char *sql =
        "INSERT INTO predictions ("
        "patient_id, input_json, output_json, "
        "risk_probability, category, confidence, isp, mito_score, inflam_score, imbalance"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        free(patient_id);
        return DB_ERR;
    }

    if (patient_id) sqlite3_bind_text(stmt, 1, patient_id, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, 1);

    sqlite3_bind_text(stmt, 2, input_json, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, output_json, -1, SQLITE_TRANSIENT);
    if (has_metrics) sqlite3_bind_double(stmt, 4, risk_probability); else sqlite3_bind_null(stmt, 4);
    if (has_metrics && category[0] != '\0') sqlite3_bind_text(stmt, 5, category, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(stmt, 5);
    if (has_metrics) sqlite3_bind_double(stmt, 6, confidence); else sqlite3_bind_null(stmt, 6);
    if (has_metrics) sqlite3_bind_double(stmt, 7, isp); else sqlite3_bind_null(stmt, 7);
    if (has_metrics) sqlite3_bind_double(stmt, 8, mito_score); else sqlite3_bind_null(stmt, 8);
    if (has_metrics) sqlite3_bind_double(stmt, 9, inflam_score); else sqlite3_bind_null(stmt, 9);
    if (has_metrics) sqlite3_bind_double(stmt, 10, imbalance); else sqlite3_bind_null(stmt, 10);

    int rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    free(patient_id);

    if (rc == SQLITE_CONSTRAINT) return DB_DUPLICATE;
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR;
}

char* db_get_predictions_json(int limit) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    cJSON *arr = NULL;
    char *out = NULL;

    if (limit <= 0) limit = 50;

    if (open_db(&db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return NULL;
    }

    const char *sql =
        "SELECT id, patient_id, input_json, output_json, "
        "risk_probability, category, confidence, isp, mito_score, inflam_score, imbalance, "
        "created_at "
        "FROM predictions ORDER BY created_at DESC, id DESC LIMIT ?;";

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
        const unsigned char *in = sqlite3_column_text(stmt, 2);
        const unsigned char *out_json = sqlite3_column_text(stmt, 3);
        const double risk_col = sqlite3_column_double(stmt, 4);
        const unsigned char *category_col = sqlite3_column_text(stmt, 5);
        const double confidence_col = sqlite3_column_double(stmt, 6);
        const double isp_col = sqlite3_column_double(stmt, 7);
        const double mito_col = sqlite3_column_double(stmt, 8);
        const double inflam_col = sqlite3_column_double(stmt, 9);
        const double imbalance_col = sqlite3_column_double(stmt, 10);
        const unsigned char *created = sqlite3_column_text(stmt, 11);
        const unsigned char *pid = sqlite3_column_text(stmt, 1);
        cJSON *in_root = cJSON_Parse(in ? (const char*)in : "{}");
        cJSON *out_root = cJSON_Parse(out_json ? (const char*)out_json : "{}");
        int in_count = payload_count(in_root);
        int out_count = payload_count(out_root);
        int row_count = (in_count > out_count) ? in_count : out_count;
        if (row_count <= 0) row_count = 1;

        for (int i = 0; i < row_count; i++) {
            cJSON *row = cJSON_CreateObject();
            cJSON *in_item = payload_item(in_root, i);
            cJSON *out_item = payload_item(out_root, i);
            cJSON *pid_in = NULL;
            cJSON *pid_out = NULL;

            if (!row) continue;

            cJSON_AddNumberToObject(row, "id", sqlite3_column_int(stmt, 0));

            pid_in = in_item ? cJSON_GetObjectItem(in_item, "patient_id") : NULL;
            pid_out = out_item ? cJSON_GetObjectItem(out_item, "patient_id") : NULL;
            if (pid_in && cJSON_IsString(pid_in) && pid_in->valuestring) cJSON_AddStringToObject(row, "patient_id", pid_in->valuestring);
            else if (pid_out && cJSON_IsString(pid_out) && pid_out->valuestring) cJSON_AddStringToObject(row, "patient_id", pid_out->valuestring);
            else if (pid) cJSON_AddStringToObject(row, "patient_id", (const char*)pid);
            else cJSON_AddNullToObject(row, "patient_id");

            add_number_or_null(row, "age", in_item);
            add_number_or_null(row, "updrs_iii", in_item);
            add_number_or_null(row, "moca", in_item);
            add_number_or_null(row, "scopa_aut", in_item);
            add_number_or_null(row, "hoehn_yahr", in_item);
            add_number_or_null(row, "ndufa4l2", in_item);
            add_number_or_null(row, "ndufs2", in_item);
            add_number_or_null(row, "pink1", in_item);
            add_number_or_null(row, "ppargc1a", in_item);
            add_number_or_null(row, "nlrp3", in_item);
            add_number_or_null(row, "il1b", in_item);
            add_number_or_null(row, "s100a8", in_item);
            add_number_or_null(row, "cxcl8", in_item);

            /* Сначала пытаемся взять из output_json; если нет — из денормализованных колонок. */
            if (out_item && cJSON_IsObject(out_item)) {
                add_number_or_null(row, "isp", out_item);
                add_number_or_null(row, "risk_probability", out_item);
                add_string_or_null(row, "category", out_item);
                add_number_or_null(row, "confidence", out_item);
                add_number_or_null(row, "mito_score", out_item);
                add_number_or_null(row, "inflam_score", out_item);
                add_number_or_null(row, "imbalance", out_item);
            } else {
                if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) cJSON_AddNumberToObject(row, "isp", isp_col); else cJSON_AddNullToObject(row, "isp");
                if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) cJSON_AddNumberToObject(row, "risk_probability", risk_col); else cJSON_AddNullToObject(row, "risk_probability");
                if (sqlite3_column_type(stmt, 5) != SQLITE_NULL && category_col) cJSON_AddStringToObject(row, "category", (const char*)category_col); else cJSON_AddNullToObject(row, "category");
                if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) cJSON_AddNumberToObject(row, "confidence", confidence_col); else cJSON_AddNullToObject(row, "confidence");
                if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) cJSON_AddNumberToObject(row, "mito_score", mito_col); else cJSON_AddNullToObject(row, "mito_score");
                if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) cJSON_AddNumberToObject(row, "inflam_score", inflam_col); else cJSON_AddNullToObject(row, "inflam_score");
                if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) cJSON_AddNumberToObject(row, "imbalance", imbalance_col); else cJSON_AddNullToObject(row, "imbalance");
            }

            cJSON_AddStringToObject(row, "input_json", in ? (const char*)in : "");
            cJSON_AddStringToObject(row, "output_json", out_json ? (const char*)out_json : "");
            cJSON_AddStringToObject(row, "created_at", created ? (const char*)created : "");

            cJSON_AddItemToArray(arr, row);
        }

        if (in_root) cJSON_Delete(in_root);
        if (out_root) cJSON_Delete(out_root);
    }

    out = cJSON_PrintUnformatted(arr);

    cJSON_Delete(arr);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return out;
}
