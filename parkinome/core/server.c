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

/* ===== read_file берём из json_io.c ===== */
char* read_file(const char* filename);

/* ===== HTTP response ===== */
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

/* ===== FORM ===== */
void handle_form(int client) {

    char *json_data = read_file("patient.json");

    double age=0, updrs=0, moca=0, scopa=0, hy=0;
    double ndufa4l2=0, ndufs2=0, pink1=0, ppargc1a=0;
    double nlrp3=0, il1b=0, s100a8=0, cxcl8=0;

    if (json_data) {
        cJSON *json = cJSON_Parse(json_data);
        free(json_data);

        if (json) {
            #define GET(name) cJSON_GetObjectItem(json, #name)->valuedouble

            age = GET(age);
            updrs = GET(updrs_iii);
            moca = GET(moca);
            scopa = GET(scopa_aut);
            hy = GET(hoehn_yahr);

            ndufa4l2 = GET(ndufa4l2);
            ndufs2 = GET(ndufs2);
            pink1 = GET(pink1);
            ppargc1a = GET(ppargc1a);
            nlrp3 = GET(nlrp3);
            il1b = GET(il1b);
            s100a8 = GET(s100a8);
            cxcl8 = GET(cxcl8);

            cJSON_Delete(json);
        }
    }

    char html[BUFFER_SIZE * 2];

    snprintf(html, sizeof(html),
    "<!DOCTYPE html>"
    "<html><head><style>"
    "body{font-family:Arial;margin:0}"
    ".container{display:flex;height:100vh}"
    ".left,.right{width:50%%;padding:20px}"
    ".left{background:#f5f5f5}"
    ".right{background:#fff;border-left:1px solid #ddd}"
    "input{width:100%%;margin-bottom:10px;padding:5px}"
    "button{padding:10px;width:100%%;font-size:16px}"
    "</style></head><body>"

    "<div class='container'>"

    "<div class='left'>"
    "<h2>Patient Input</h2>"

    "Age<input id='age' value='%.1f'>"
    "UPDRS III<input id='updrs' value='%.1f'>"
    "MOCA<input id='moca' value='%.1f'>"
    "SCOPA AUT<input id='scopa' value='%.1f'>"
    "Hoehn-Yahr<input id='hy' value='%.1f'>"

    "<h3>Biomarkers</h3>"
    "NDUFA4L2<input id='ndufa4l2' value='%.2f'>"
    "NDUFS2<input id='ndufs2' value='%.2f'>"
    "PINK1<input id='pink1' value='%.2f'>"
    "PPARGC1A<input id='ppargc1a' value='%.2f'>"
    "NLRP3<input id='nlrp3' value='%.2f'>"
    "IL1B<input id='il1b' value='%.2f'>"
    "S100A8<input id='s100a8' value='%.2f'>"
    "CXCL8<input id='cxcl8' value='%.2f'>"

    "<button onclick='send()'>Predict</button>"
    "</div>"

    "<div class='right'>"
    "<h2>Results</h2>"
    "<pre id='result'>Loaded from patient.json</pre>"
    "</div>"

    "</div>"

    "<script>"
    "function send(){"
    "let data={"
    "age:+age.value,"
    "updrs_iii:+updrs.value,"
    "moca:+moca.value,"
    "scopa_aut:+scopa.value,"
    "hoehn_yahr:+hy.value,"
    "ndufa4l2:+ndufa4l2.value,"
    "ndufs2:+ndufs2.value,"
    "pink1:+pink1.value,"
    "ppargc1a:+ppargc1a.value,"
    "nlrp3:+nlrp3.value,"
    "il1b:+il1b.value,"
    "s100a8:+s100a8.value,"
    "cxcl8:+cxcl8.value};"

    "fetch('/predict',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})"
    ".then(r=>r.text())"
    ".then(d=>result.innerText=d);"
    "}"
    "</script>"

    "</body></html>",

    age, updrs, moca, scopa, hy,
    ndufa4l2, ndufs2, pink1, ppargc1a,
    nlrp3, il1b, s100a8, cxcl8
    );

    send_response(client, "200 OK", "text/html", html);
}

/* ===== HEALTH ===== */
void handle_health(int client) {
    send_response(client, "200 OK", "text/plain", "Parkinome API is running");
}

/* ===== PREDICT ===== */
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