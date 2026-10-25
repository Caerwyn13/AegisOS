#ifndef AUTH_H
#define AUTH_H

#include "types.h"

#define AUTH_USERNAME_MAX 16
#define AUTH_PASSWORD_MAX 64
#define AUTH_PASSWORD_MIN 8

int auth_load(void);
int auth_create_accounts(const char *root_password,
                        const char *user_password);
int auth_verify(const char *username, const char *password);
void auth_session_start(const char *username, int is_root);
void auth_session_logout(void);
int auth_is_logged_in(void);
int auth_is_root(void);
int auth_is_sensitive_file(const char *name);
const char *auth_username(void);

#endif
