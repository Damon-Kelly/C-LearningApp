/**********************************************************************************************
*
*   C Learning App - user accounts (plain text storage)
*
*   One line per user in users.txt:   name|email|password|id|created
*   (older files with only name|email|password still work: ids are added the first time the
*   file is read.) The id never changes, so progress can later be stored against it even if
*   the user renames themselves.
*
*   NOTE: passwords are stored as readable text on purpose (prototype). Only the C standard
*   library is used, so this module can be tested without raylib.
*
**********************************************************************************************/

#ifndef USERS_H
#define USERS_H

#include <stdbool.h>

#define USER_FIELD_LEN  64

// What the profile page needs. The password is deliberately not included.
typedef struct UserInfo {
    int id;
    char name[USER_FIELD_LEN];
    char email[USER_FIELD_LEN];
    long long created;          // Unix time of sign-up, 0 for accounts made before this was recorded
} UserInfo;

// Where users.txt lives (call once before anything else)
void UsersSetFile(const char *path);

//----------------------------------------------------------------------------------
// Sign up / log in / log out
//----------------------------------------------------------------------------------
// Creates an account and logs it in. On failure returns false and writes a short message to err.
bool UserRegister(const char *name, const char *email, const char *password, char *err, int errSize);

// nameOrEmail is matched against both fields (case-insensitive). Password is case-sensitive.
bool UserLogin(const char *nameOrEmail, const char *password, char *err, int errSize);

void LogoutUser(void);

// Name of the logged-in user, "" when nobody is logged in
const char *GetCurrentUser(void);

// Stable id of the logged-in user, 0 when nobody is logged in
int GetCurrentUserId(void);

// Fills out with the logged-in user's details (re-reads the file). False if nobody is logged in.
bool UserGetCurrent(UserInfo *out);

//----------------------------------------------------------------------------------
// Account changes for the logged-in user. Each one asks for the current password.
// All return false and write a short message to err on failure.
//----------------------------------------------------------------------------------
bool UserChangeName(const char *newName, const char *password, char *err, int errSize);
bool UserChangeEmail(const char *newEmail, const char *password, char *err, int errSize);
bool UserChangePassword(const char *oldPassword, const char *newPassword, char *err, int errSize);

// Removes the account for good and logs out
bool UserDeleteAccount(const char *password, char *err, int errSize);

#endif // USERS_H
