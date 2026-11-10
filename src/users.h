/**********************************************************************************************
*
*   C Learning App - user accounts (plain text storage)
*
*   One line per user in users.txt:   name|email|password
*   NOTE: passwords are stored as readable text on purpose (prototype). Only the C standard
*   library is used, so this module can be tested without raylib.
*
**********************************************************************************************/

#ifndef USERS_H
#define USERS_H

#include <stdbool.h>

#define USER_FIELD_LEN  64

// Where users.txt lives (call once before Register/Login)
void UsersSetFile(const char *path);

// Creates an account and logs it in. On failure returns false and writes a short message to err.
bool UserRegister(const char *name, const char *email, const char *password, char *err, int errSize);

// nameOrEmail is matched against both fields (case-insensitive). Password is case-sensitive.
bool UserLogin(const char *nameOrEmail, const char *password, char *err, int errSize);

// Name of the logged-in user, "" when nobody is logged in
const char *GetCurrentUser(void);
void LogoutUser(void);

#endif // USERS_H
