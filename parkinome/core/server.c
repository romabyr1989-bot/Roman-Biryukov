#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "predict.h"
#include <cjson/cJSON.h>

#define PORT 8080
#define BUFFER_SIZE 8192

/* ===== JSON ===== */
int parse_patient(cJSON *json, parkinome_input_t *in);

/* =========================
   HTTP response
   ========================= */

void send_response(int client, const char *status, const char *type, const char *body) {

    char response[BUFFER_SIZE];

    snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "\r\n"
        "%s",
        status,
        type,
        strlen(body),
        body
    );

    write(client, response, strlen(response));
}

/* =========================
   UI FORM
   ========================= */

void handle_form(int client) {

    const char *html =
    "<!DOCTYPE html>"
    "<html><body>"
    "<h2>Parkinome Predictor</h2>"

    "<textarea id='json' rows='12' cols='50'>"
    "{"
    "\"age\":65,"
    "\"updrs_iii\":24,"
    "\"moca\":26,"
    "\"scopa_aut\":15,"
    "\"hoehn_yahr\":2,"
    "\"ndufa4l2\":-0.3,"
    "\"ndufs2\":-0.2,"
    "\"pink1\":-0.4,"
    "\"ppargc1a\":-0.1,"
    "\"nlrp3\":0.8,"
    "\"il1b\":0.7,"
    "\"s100a8\":0.6,"
    "\"cxcl8\":0.5"
    "}"
    "</textarea>"

    "<br><br>"
    "<button onclick='send()'>Predict</button>"

    "<pre id='result'></pre>"

    "<script>"
    "function send(){"
    "fetch('/predict',{"
    "method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:document.getElementById('json').value"
    "})"
    ".then(r=>r.text())"
    ".then(d=>document.getElementById('result').innerText=d);"
    "}"
    "</script>"

    "</body></html>";

    send_response(client, "200 OK", "text/html", html);
}

/* =========================
   HEALTH
   ========================= */

void handle_health(int client) {
    send_response(client, "200 OK", "text/plain", "Parkinome API is running");
}

/* =========================
   PREDICT
   ========================= */

void handle_predict(int client, char *body) {

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        send_response(client, "400 Bad Request", "text/plain", "Invalid JSON");
        return;
    }

    parkinome_input_t in = {0};
    parkinome_output_t out = {0};

    if (parse_patient(json, &in) != 0) {
        cJSON_Delete(json);
        send_response(client, "400 Bad Request", "text/plain", "Invalid input");
        return;
    }

    cJSON_Delete(json);

    int status = parkinome_predict(&in, &out);
    if (status != PARKINOME_OK) {
        send_response(client, "500 Internal Server Error", "text/plain", "Model error");
        return;
    }

    char result[256];

    const char *cat =
        (out.category == 0) ? "LOW" :
        (out.category == 1) ? "INTERMEDIATE" :
                              "HIGH";

    snprintf(result, sizeof(result),
        "{ \"isp\": %.3f, \"risk_probability\": %.3f, \"category\": \"%s\" }",
        out.isp,
        out.risk_probability,
        cat
    );

    send_response(client, "200 OK", "application/json", result);
}

/* =========================
   MAIN
   ========================= */

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

        /* ===== ROUTES ===== */

        if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
            handle_form(client_socket);
        }
        else if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
            handle_health(client_socket);
        }
        else if (strcmp(method, "POST") == 0 && strcmp(path, "/predict") == 0) {
            if (body)
                handle_predict(client_socket, body);
            else
                send_response(client_socket, "400 Bad Request", "text/plain", "No body");
        }
        else {
            send_response(client_socket, "404 Not Found", "text/plain", "Not Found");
        }

        close(client_socket);
    }

    return 0;
}