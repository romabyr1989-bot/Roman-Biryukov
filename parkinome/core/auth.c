#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

typedef struct {
    char token[64];
    char role[32];
} session_t;

static session_t sessions[10];
static int session_count = 0;

/* ===== LOGIN ===== */
const char* auth_login(const char *body) {

    cJSON *json = cJSON_Parse(body);
    if (!json) return NULL;

    const char *login = cJSON_GetObjectItem(json, "login")->valuestring;
    const char *password = cJSON_GetObjectItem(json, "password")->valuestring;

    FILE *f = fopen("users.json", "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *data = malloc(size + 1);
    fread(data, 1, size, f);
    data[size] = '\0';
    fclose(f);

    cJSON *users = cJSON_Parse(data);
    free(data);

    if (!users) return NULL;

    for (int i = 0; i < cJSON_GetArraySize(users); i++) {

        cJSON *u = cJSON_GetArrayItem(users, i);

        if (strcmp(login, cJSON_GetObjectItem(u, "login")->valuestring) == 0 &&
            strcmp(password, cJSON_GetObjectItem(u, "password")->valuestring) == 0) {

            static char token[64];
            snprintf(token, sizeof(token), "token_%d", session_count);

            strcpy(sessions[session_count].token, token);
            strcpy(sessions[session_count].role,
                   cJSON_GetObjectItem(u, "role")->valuestring);

            session_count++;

            cJSON_Delete(users);
            cJSON_Delete(json);

            return token;
        }
    }

    cJSON_Delete(users);
    cJSON_Delete(json);
    return NULL;
}

/* ===== GET ROLE ===== */
const char* auth_get_role(const char *token) {

    for (int i = 0; i < session_count; i++) {
        if (strcmp(token, sessions[i].token) == 0)
            return sessions[i].role;
    }

    return "unknown";
}