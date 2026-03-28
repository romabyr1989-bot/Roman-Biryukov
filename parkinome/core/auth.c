#include <string.h>
#include "auth.h"

/* ===== TOKEN → ROLE ===== */
user_role_t get_role_from_token(const char *auth_header) {

    if (!auth_header) return ROLE_NONE;

    if (strstr(auth_header, "token_doctor"))
        return ROLE_DOCTOR;

    if (strstr(auth_header, "token_research"))
        return ROLE_RESEARCHER;

    if (strstr(auth_header, "token_admin"))
        return ROLE_ADMIN;

    return ROLE_NONE;
}
