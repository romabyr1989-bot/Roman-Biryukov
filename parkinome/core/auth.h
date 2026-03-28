#ifndef AUTH_H
#define AUTH_H

typedef enum {
    ROLE_NONE = 0,
    ROLE_DOCTOR,
    ROLE_RESEARCHER,
    ROLE_ADMIN
} user_role_t;

user_role_t get_role_from_token(const char *auth_header);

#endif
