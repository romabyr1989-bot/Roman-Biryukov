#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "predictor.h"
#include "json_io.h"
#include "logger.h"
#include "storage_sqlite.h"

#define PORT 8080
#define BUFFER_SIZE 8192

/* ===== HTTP ===== */
void send_response(int client, const char *status, const char *type, const char *body) {

    char response[BUFFER_SIZE];

    snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n\r\n%s",
        status, type, strlen(body), body
    );

    write(client, response, strlen(response));
}

/* ===== ROLE ===== */
const char* get_role(const char *path) {

    char *role = strstr(path, "role=");

    if (!role) return "unknown";

    role += 5;

    if (strstr(role, "doctor")) return "doctor";
    if (strstr(role, "patient")) return "patient";
    if (strstr(role, "pharma")) return "pharma";

    return "unknown";
}

/* ===== STATIC ===== */
void handle_static(int client, const char *path) {

    char fullpath[256];
    snprintf(fullpath, sizeof(fullpath), "web%s", path);

    char *content = read_file(fullpath);

    if (!content) {
        log_error("Static file not found");
        send_response(client, "404 Not Found", "text/plain", "File not found");
        return;
    }

    const char *type =
        strstr(path, ".css") ? "text/css" :
        strstr(path, ".js") ? "application/javascript" :
        strstr(path, ".html") ? "text/html" :
        "text/plain";

    send_response(client, "200 OK", type, content);
    free(content);
}

/* ===== UI ===== */
void handle_form(int client, const char *role) {

    char filepath[256];

    if (strcmp(role, "doctor") == 0)
        strcpy(filepath, "web/doctor.html");
    else if (strcmp(role, "patient") == 0)
        strcpy(filepath, "web/patient.html");
    else if (strcmp(role, "pharma") == 0)
        strcpy(filepath, "web/pharma.html");
    else
        strcpy(filepath, "web/index.html");

    char *html = read_file(filepath);

    if (!html) {
        log_error("UI file not found");
        send_response(client, "500 Internal Server Error", "text/plain", "UI not found");
        return;
    }

    send_response(client, "200 OK", "text/html", html);
    free(html);
}

/* ===== HEALTH ===== */
void handle_health(int client) {
    send_response(client, "200 OK", "text/plain", "Parkinome API is running");
}

/* ===== PREDICT ===== */
void handle_predict(int client, char *body) {

    if (!body) {
        log_error("Predict: no body");
        send_response(client, "400 Bad Request", "text/plain", "No body");
        return;
    }

    char result[512];

    if (run_prediction(body, result, sizeof(result)) != 0) {
        log_error("Prediction failed");
        send_response(client, "400 Bad Request", "text/plain", "Prediction error");
        return;
    }

    send_response(client, "200 OK", "application/json", result);
}

/* ===== BATCH ===== */
void handle_batch(int client, char *body, const char *role) {

    if (strcmp(role, "pharma") != 0) {
        send_response(client, "403 Forbidden", "text/plain", "Access denied");
        return;
    }

    if (!body) {
        log_error("Batch: no body");
        send_response(client, "400 Bad Request", "text/plain", "No body");
        return;
    }

    char result[4096];

    if (run_batch(body, result, sizeof(result)) != 0) {
        log_error("Batch failed");
        send_response(client, "400 Bad Request", "text/plain", "Batch error");
        return;
    }

    send_response(client, "200 OK", "application/json", result);
}

/* ===== STORAGE (SQLite) ===== */

void handle_save(int client, char *body, const char *role) {

    if (strcmp(role, "doctor") != 0) {
        send_response(client, "403 Forbidden", "text/plain", "Access denied");
        return;
    }

    if (!body) {
        log_error("Save: no body");
        send_response(client, "400 Bad Request", "text/plain", "No body");
        return;
    }

    if (db_save_patient(body) != 0) {
        log_error("DB save failed");
        send_response(client, "500 Internal Server Error", "text/plain", "Save failed");
        return;
    }

    send_response(client, "200 OK", "text/plain", "Saved");
}

void handle_get_patients(int client, const char *role) {

    if (strcmp(role, "doctor") != 0) {
        send_response(client, "403 Forbidden", "text/plain", "Access denied");
        return;
    }

    char *data = db_get_patients();

    if (!data) {
        send_response(client, "200 OK", "application/json", "[]");
        return;
    }

    send_response(client, "200 OK", "application/json", data);
    free(data);
}

/* ===== MAIN ===== */
int main() {

    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    char buffer[BUFFER_SIZE];

    /* INIT DB */
    db_init();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 10);

    printf("🚀 Server running on http://localhost:%d\n", PORT);

    while (1) {

        client_socket = accept(server_fd,
                               (struct sockaddr *)&address,
                               (socklen_t*)&addrlen);

        int valread = read(client_socket, buffer, sizeof(buffer) - 1);
        if (valread <= 0) {
            close(client_socket);
            continue;
        }

        buffer[valread] = '\0';

        char method[8], path[256];
        sscanf(buffer, "%s %s", method, path);

        const char *role = get_role(path);

        /* ===== LOG ===== */
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "%s %s [%s]", method, path, role);
        log_info(log_msg);

        char *body = strstr(buffer, "\r\n\r\n");
        if (body) body += 4;

        /* ===== ROUTES ===== */

        if (!strcmp(method, "GET") && !strncmp(path, "/", 1)) {
            handle_form(client_socket, role);
        }
        else if (!strcmp(method, "GET") && !strcmp(path, "/health")) {
            handle_health(client_socket);
        }
        else if (!strcmp(method, "GET") && strstr(path, ".css")) {
            handle_static(client_socket, path);
        }
        else if (!strcmp(method, "GET") && strstr(path, ".js")) {
            handle_static(client_socket, path);
        }
        else if (!strcmp(method, "POST") && !strcmp(path, "/predict")) {
            handle_predict(client_socket, body);
        }
        else if (!strcmp(method, "POST") && !strcmp(path, "/batch")) {
            handle_batch(client_socket, body, role);
        }
        else if (!strcmp(method, "POST") && !strcmp(path, "/patient")) {
            handle_save(client_socket, body, role);
        }
        else if (!strcmp(method, "GET") && !strcmp(path, "/patients")) {
            handle_get_patients(client_socket, role);
        }
        else {
            log_error("Route not found");
            send_response(client_socket, "404 Not Found", "text/plain", "Not Found");
        }

        close(client_socket);
    }

    return 0;
}