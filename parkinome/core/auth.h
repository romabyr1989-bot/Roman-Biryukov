#ifndef AUTH_H
#define AUTH_H

const char* auth_login(const char *json_body);
const char* auth_get_role(const char *token);

#endif