#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <cjson/cJSON.h>

#include "predictor.h"
#include "json_io.h"   // Чтение файлов для отдачи HTML
#include "db.h"

#define PORT 8080
#define BUFFER_SIZE 8192
#define MAX_REQUEST_SIZE (2 * 1024 * 1024)

enum {
    REQ_READ_OK = 0,
    REQ_READ_ERR = 1,
    REQ_READ_TOO_LARGE = 2
};

static char* read_ui_file(void) {
    const char *candidates[] = {
        "web/index.html",
        "./web/index.html",
        "parkinome/core/web/index.html",
        "./parkinome/core/web/index.html"
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        char *html = read_file(candidates[i]);
        if (html) return html;
    }

    return NULL;
}

/* Системный вызов write() может отправить только часть буфера, поэтому пишем до конца. */
static int write_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, data + sent, len - sent);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int parse_content_length(const char *request) {
    const char *p = NULL;
    if (!request) return 0;
    p = strstr(request, "Content-Length:");
    if (!p) p = strstr(request, "content-length:");
    if (!p) return 0;
    p += 15;
    while (*p == ' ' || *p == '\t') p++;
    int n = atoi(p);
    return (n > 0) ? n : 0;
}

/* Читает полный HTTP-запрос с учетом Content-Length. Caller освобождает *out_buf. */
static int read_http_request(int client, char **out_buf, int *out_len) {
    int total = 0;
    int cap = BUFFER_SIZE;
    int content_len = -1;
    int expected_total = -1;
    char *buf = NULL;
    char *hdr_end = NULL;

    if (!out_buf || !out_len) return -1;
    *out_buf = NULL;
    *out_len = 0;

    buf = malloc((size_t)cap + 1);
    if (!buf) return -1;

    while (1) {
        int space = cap - total;
        int n;
        if (space <= 0) {
            if (cap >= MAX_REQUEST_SIZE) {
                free(buf);
                return REQ_READ_TOO_LARGE;
            }
            cap *= 2;
            if (cap > MAX_REQUEST_SIZE) cap = MAX_REQUEST_SIZE;
            char *grown = realloc(buf, (size_t)cap + 1);
            if (!grown) {
                free(buf);
                return -1;
            }
            buf = grown;
            space = cap - total;
        }

        n = (int)read(client, buf + total, (size_t)space);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';

        if (expected_total < 0) {
            hdr_end = strstr(buf, "\r\n\r\n");
            if (hdr_end) {
                int header_len = (int)(hdr_end - buf) + 4;
                content_len = parse_content_length(buf);
                expected_total = header_len + content_len;
                if (expected_total < header_len) {
                    free(buf);
                    return REQ_READ_ERR;
                }
                if (expected_total > MAX_REQUEST_SIZE) {
                    free(buf);
                    return REQ_READ_TOO_LARGE;
                }
                if (expected_total == header_len) break;
            }
        }

        if (expected_total >= 0 && total >= expected_total) break;
    }

    if (total <= 0) {
        free(buf);
        return REQ_READ_ERR;
    }

    buf[total] = '\0';
    *out_buf = buf;
    *out_len = total;
    return REQ_READ_OK;
}

/* ===== ОТВЕТ ===== */
/* Формируем HTTP-ответ динамически: HTML может быть больше фиксированного буфера. */
void send_response(int client, const char *status, const char *type, const char *body) {

    size_t body_len = strlen(body);
    int header_len = snprintf(NULL, 0,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n\r\n",
        status, type, body_len
    );

    if (header_len < 0) return;

    size_t total_len = (size_t)header_len + body_len;
    char *response = malloc(total_len + 1);
    if (!response) return;

    snprintf(response, (size_t)header_len + 1,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n\r\n",
        status, type, body_len
    );

    memcpy(response + header_len, body, body_len);
    response[total_len] = '\0';

    write_all(client, response, total_len);
    free(response);
}

static void handle_predict_route(int client, char *body) {
    char result[8192];

    if (!body || run_prediction(body, result, sizeof(result)) != 0) {
        send_response(client, "400 Bad Request", "text/plain", "Error");
        return;
    }

    /* /predict только считает прогноз; сохранение выполняется отдельным маршрутом /save. */
    send_response(client, "200 OK", "application/json", result);
}

/* ===== ОБРАБОТЧИКИ ===== */
void handle_predict(int client, char *body) {
    handle_predict_route(client, body);
}

void handle_save(int client, char *body) {
    if (!body) {
        send_response(client, "400 Bad Request", "text/plain", "Error");
        return;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        send_response(client, "400 Bad Request", "text/plain", "Invalid JSON");
        return;
    }

    cJSON *input = cJSON_GetObjectItem(root, "input");
    cJSON *output = cJSON_GetObjectItem(root, "output");

    if (!input || !output) {
        cJSON_Delete(root);
        send_response(client, "400 Bad Request", "text/plain", "Invalid payload");
        return;
    }

    char *input_json = cJSON_PrintUnformatted(input);
    char *output_json = cJSON_PrintUnformatted(output);

    if (!input_json || !output_json) {
        if (input_json) free(input_json);
        if (output_json) free(output_json);
        cJSON_Delete(root);
        send_response(client, "500 Internal Server Error", "text/plain", "Serialize error");
        return;
    }

    int rc = db_save_prediction(input_json, output_json);

    free(input_json);
    free(output_json);
    cJSON_Delete(root);

    if (rc == DB_DUPLICATE) {
        send_response(client, "409 Conflict", "text/plain", "Duplicate patient_id or record");
        return;
    }

    if (rc != DB_OK) {
        send_response(client, "500 Internal Server Error", "text/plain", "DB error");
        return;
    }

    send_response(client, "200 OK", "application/json", "{ \"status\": \"saved\" }");
}

/* ===== ОСНОВНОЙ ЦИКЛ СЕРВЕРА ===== */
int main() {

    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if (db_init() != 0) {
        fprintf(stderr, "Ошибка инициализации БД\n");
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    {
        int yes = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            perror("setsockopt");
            close(server_fd);
            return 1;
        }
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("🚀 Server running on http://localhost:%d\n", PORT);

    while (1) {

        client_socket = accept(server_fd,
                               (struct sockaddr *)&address,
                               (socklen_t*)&addrlen);

        if (client_socket < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        char *buffer = NULL;
        int req_len = 0;
        int rr = read_http_request(client_socket, &buffer, &req_len);
        if (rr != REQ_READ_OK) {
            if (rr == REQ_READ_TOO_LARGE) {
                send_response(client_socket, "413 Payload Too Large", "text/plain", "Request body too large");
            } else {
                send_response(client_socket, "400 Bad Request", "text/plain", "Invalid HTTP request");
            }
            close(client_socket);
            continue;
        }

        char method[8], path[64];
        sscanf(buffer, "%s %s", method, path);

        /* Для POST тело начинается после пустой строки между заголовками и данными запроса. */
        char *body = strstr(buffer, "\r\n\r\n");
        if (body) body += 4;

        /* ===== МАРШРУТ UI ===== */
        if (!strcmp(method, "GET") && !strcmp(path, "/")) {

            char *html = read_ui_file();

            if (!html) {
                send_response(client_socket, "500 Internal Server Error", "text/plain", "UI not found");
            } else {
                send_response(client_socket, "200 OK", "text/html", html);
                free(html);
            }
        }

        /* ===== МАРШРУТ /me ===== */
        else if (!strcmp(method, "GET") && !strcmp(path, "/me")) {
            char resp[128];
            snprintf(resp, sizeof(resp), "{ \"role\": \"admin\" }");

            send_response(client_socket, "200 OK", "application/json", resp);
        }

        /* ===== МАРШРУТ /favicon.ico ===== */
        else if (!strcmp(method, "GET") && !strcmp(path, "/favicon.ico")) {
            send_response(client_socket, "204 No Content", "image/x-icon", "");
        }

        /* ===== МАРШРУТ /predict ===== */
        else if (!strcmp(method, "POST") && !strcmp(path, "/predict")) {
            handle_predict(client_socket, body);
        }

        /* ===== МАРШРУТ /save ===== */
        else if (!strcmp(method, "POST") && !strcmp(path, "/save")) {
            handle_save(client_socket, body);
        }

        /* ===== МАРШРУТ /history ===== */
        else if (!strcmp(method, "GET") && !strcmp(path, "/history")) {
            char *history = db_get_predictions_json(50);
            if (!history) {
                send_response(client_socket, "500 Internal Server Error", "text/plain", "DB error");
            } else {
                send_response(client_socket, "200 OK", "application/json", history);
                free(history);
            }
        }

        else {
            send_response(client_socket, "404 Not Found", "text/plain", "Not Found");
        }

        free(buffer);
        close(client_socket);
    }

    return 0;
}
