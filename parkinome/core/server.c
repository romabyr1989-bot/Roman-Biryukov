#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "predict.h"
#include "auth.h"
#include "json_io.h"   // ✅ используем read_file отсюда

int run_prediction(const char *body, char *result, size_t size);
int run_batch(const char *body, char *result, size_t size);

#define PORT 8080
#define BUFFER_SIZE 8192

/* ===== RESPONSE ===== */
void send_response(int client, const char *status, const char *type, const char *body) {

    char response[BUFFER_SIZE];

    snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n\r\n%s",
        status,
        type,
        strlen(body),
        body
    );

    write(client, response, strlen(response));
}

/* ===== AUTH HEADER ===== */
char* get_auth_header(char *request) {

    char *auth = strstr(request, "Authorization:");
    if (!auth) return NULL;

    char *end = strstr(auth, "\r\n");
    if (!end) return NULL;

    static char header[256];
    int len = end - auth;

    strncpy(header, auth, len);
    header[len] = '\0';

    return header;
}

/* ===== HANDLERS ===== */
void handle_predict(int client, char *body) {

    char result[1024];

    if (!body || run_prediction(body, result, sizeof(result)) != 0) {
        send_response(client, "400 Bad Request", "text/plain", "Error");
        return;
    }

    send_response(client, "200 OK", "application/json", result);
}

void handle_batch(int client, char *body) {

    char result[4096];

    if (!body || run_batch(body, result, sizeof(result)) != 0) {
        send_response(client, "400 Bad Request", "text/plain", "Error");
        return;
    }

    send_response(client, "200 OK", "application/json", result);
}

/* ===== MAIN ===== */
int main() {

    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    char buffer[BUFFER_SIZE];

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

        char method[8], path[64];
        sscanf(buffer, "%s %s", method, path);

        char *body = strstr(buffer, "\r\n\r\n");
        if (body) body += 4;

        char *auth_header = get_auth_header(buffer);
        user_role_t role = get_role_from_token(auth_header);

        /* ===== UI ===== */
        if (!strcmp(method, "GET") && !strcmp(path, "/")) {

            char *html = read_file("index.html");

            if (!html) {
                send_response(client_socket, "500 Internal Server Error", "text/plain", "UI not found");
            } else {
                send_response(client_socket, "200 OK", "text/html", html);
                free(html);
            }
        }

        /* ===== ME ===== */
        else if (!strcmp(method, "GET") && !strcmp(path, "/me")) {

            const char *role_str =
                (role == ROLE_DOCTOR) ? "doctor" :
                (role == ROLE_RESEARCHER) ? "researcher" :
                (role == ROLE_ADMIN) ? "admin" :
                "none";

            char resp[128];
            snprintf(resp, sizeof(resp), "{ \"role\": \"%s\" }", role_str);

            send_response(client_socket, "200 OK", "application/json", resp);
        }

        /* ===== PREDICT ===== */
        else if (!strcmp(method, "POST") && !strcmp(path, "/predict")) {

            if (role != ROLE_DOCTOR && role != ROLE_ADMIN) {
                send_response(client_socket, "403 Forbidden", "text/plain", "Access denied");
            } else {
                handle_predict(client_socket, body);
            }
        }

        /* ===== BATCH ===== */
        else if (!strcmp(method, "POST") && !strcmp(path, "/batch")) {

            if (role != ROLE_RESEARCHER && role != ROLE_ADMIN) {
                send_response(client_socket, "403 Forbidden", "text/plain", "Access denied");
            } else {
                handle_batch(client_socket, body);
            }
        }

        else {
            send_response(client_socket, "404 Not Found", "text/plain", "Not Found");
        }

        close(client_socket);
    }

    return 0;
}