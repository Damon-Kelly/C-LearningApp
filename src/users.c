#include "users.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_USERS       200
#define USERS_LINE_LEN  512

typedef struct UserRecord {
    int id;
    char name[USER_FIELD_LEN];
    char email[USER_FIELD_LEN];
    char password[USER_FIELD_LEN];
    long long created;
} UserRecord;

static char usersFile[512] = "users.txt";
static char currentUser[USER_FIELD_LEN] = "";
static int currentId = 0;

// Every operation reads the whole file into this table, changes it, and writes it back.
static UserRecord users[MAX_USERS];
static int userCount = 0;

//----------------------------------------------------------------------------------
// Small helpers
//----------------------------------------------------------------------------------
static void SetError(char *err, int errSize, const char *message)
{
    if (err != NULL && errSize > 0) snprintf(err, (size_t)errSize, "%s", message);
}

static bool SameText(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

// Copies src into dst without leading/trailing spaces
static void TrimCopy(char *dst, int dstSize, const char *src)
{
    while (*src == ' ') src++;
    int n = (int)strlen(src);
    while (n > 0 && src[n - 1] == ' ') n--;
    if (n > dstSize - 1) n = dstSize - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

static bool IsNumber(const char *s)
{
    if (*s == '\0') return false;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return false;
    return true;
}

static bool IsValidEmail(const char *e)
{
    const char *at = strchr(e, '@');
    if (at == NULL || at == e || strchr(at + 1, '@') != NULL) return false;

    const char *dot = strrchr(at, '.');
    if (dot == NULL || dot == at + 1 || dot[1] == '\0') return false;

    return strchr(e, ' ') == NULL && strchr(e, '|') == NULL;
}

//----------------------------------------------------------------------------------
// Input rules (shared by sign up and the change functions)
//----------------------------------------------------------------------------------
static bool CheckName(const char *n, char *err, int errSize)
{
    if (n[0] == '\0') { SetError(err, errSize, "Enter a name."); return false; }
    if (strchr(n, '|') != NULL || strchr(n, '@') != NULL) { SetError(err, errSize, "The name cannot contain | or @."); return false; }
    return true;
}

static bool CheckEmail(const char *e, char *err, int errSize)
{
    if (!IsValidEmail(e)) { SetError(err, errSize, "Enter a valid email address."); return false; }
    return true;
}

static bool CheckPassword(const char *p, char *err, int errSize)
{
    if (strlen(p) < 4) { SetError(err, errSize, "The password needs at least 4 characters."); return false; }
    if (strchr(p, '|') != NULL) { SetError(err, errSize, "The password cannot contain |."); return false; }
    return true;
}

//----------------------------------------------------------------------------------
// File reading and writing
//----------------------------------------------------------------------------------
// "name|email|password" or "name|email|password|id|created". Splits the line in place.
static bool ParseLine(char *line, UserRecord *rec)
{
    line[strcspn(line, "\r\n")] = '\0';

    char *part[6];
    int count = 0;
    part[count++] = line;

    for (char *c = line; *c != '\0'; c++)
    {
        if (*c != '|') continue;
        *c = '\0';
        if (count >= 6) return false;
        part[count++] = c + 1;
    }
    if (count != 3 && count != 5) return false;

    snprintf(rec->name, sizeof(rec->name), "%s", part[0]);
    snprintf(rec->email, sizeof(rec->email), "%s", part[1]);
    snprintf(rec->password, sizeof(rec->password), "%s", part[2]);
    rec->id = (count == 5 && IsNumber(part[3])) ? atoi(part[3]) : 0;
    rec->created = (count == 5 && IsNumber(part[4])) ? strtoll(part[4], NULL, 10) : 0;
    return rec->name[0] != '\0';
}

// Writes to a temporary file first, so a failed write never destroys the existing accounts
static bool SaveAll(void)
{
    char tmp[520];
    snprintf(tmp, sizeof(tmp), "%s.tmp", usersFile);

    FILE *f = fopen(tmp, "w");
    if (f == NULL) return false;

    for (int i = 0; i < userCount; i++)
    {
        fprintf(f, "%s|%s|%s|%d|%lld\n", users[i].name, users[i].email, users[i].password, users[i].id, users[i].created);
    }

    if (fclose(f) != 0) { remove(tmp); return false; }

    remove(usersFile);                              // rename() will not overwrite on Windows
    return rename(tmp, usersFile) == 0;             // if this fails the .tmp file still has everything
}

static int NextId(void)
{
    int next = 1;
    for (int i = 0; i < userCount; i++) if (users[i].id >= next) next = users[i].id + 1;
    return next;
}

// Reads users.txt into users[]. A missing file just means "no accounts yet". Accounts from
// the older file format get an id here and the file is rewritten once to store it.
static bool LoadAll(char *err, int errSize)
{
    userCount = 0;

    FILE *f = fopen(usersFile, "r");
    if (f == NULL) return true;

    char line[USERS_LINE_LEN];
    while (fgets(line, sizeof(line), f) != NULL)
    {
        UserRecord rec;
        if (!ParseLine(line, &rec)) continue;

        if (userCount >= MAX_USERS)
        {
            fclose(f);
            userCount = 0;
            SetError(err, errSize, "Too many accounts in users.txt.");
            return false;
        }
        users[userCount++] = rec;
    }
    fclose(f);

    bool migrated = false;
    int next = NextId();
    for (int i = 0; i < userCount; i++)
    {
        if (users[i].id <= 0) { users[i].id = next++; migrated = true; }
    }
    if (migrated) SaveAll();

    return true;
}

static int FindById(int id)
{
    for (int i = 0; i < userCount; i++) if (users[i].id == id) return i;
    return -1;
}

// True if another account (not index `except`) already uses this name or email
static bool NameTaken(const char *name, int except)
{
    for (int i = 0; i < userCount; i++) if (i != except && SameText(users[i].name, name)) return true;
    return false;
}

static bool EmailTaken(const char *email, int except)
{
    for (int i = 0; i < userCount; i++) if (i != except && SameText(users[i].email, email)) return true;
    return false;
}

// Loads the file and finds the logged-in user. Returns the index in users[], or -1 with err set.
static int LoadCurrent(char *err, int errSize)
{
    if (currentId <= 0) { SetError(err, errSize, "You are not logged in."); return -1; }
    if (!LoadAll(err, errSize)) return -1;

    int index = FindById(currentId);
    if (index < 0) { SetError(err, errSize, "This account no longer exists."); return -1; }
    return index;
}

static bool PasswordOk(int index, const char *password, char *err, int errSize)
{
    if (strcmp(users[index].password, password) != 0)
    {
        SetError(err, errSize, "The current password is wrong.");
        return false;
    }
    return true;
}

//----------------------------------------------------------------------------------
// Public functions
//----------------------------------------------------------------------------------
void UsersSetFile(const char *path)
{
    snprintf(usersFile, sizeof(usersFile), "%s", path);
}

bool UserRegister(const char *name, const char *email, const char *password, char *err, int errSize)
{
    char n[USER_FIELD_LEN];
    char e[USER_FIELD_LEN];
    TrimCopy(n, sizeof(n), name);
    TrimCopy(e, sizeof(e), email);

    if (!CheckName(n, err, errSize)) return false;
    if (!CheckEmail(e, err, errSize)) return false;
    if (!CheckPassword(password, err, errSize)) return false;

    if (!LoadAll(err, errSize)) return false;
    if (NameTaken(n, -1)) { SetError(err, errSize, "That name is already taken."); return false; }
    if (EmailTaken(e, -1)) { SetError(err, errSize, "That email is already registered."); return false; }
    if (userCount >= MAX_USERS) { SetError(err, errSize, "No more accounts can be created."); return false; }

    UserRecord *rec = &users[userCount];
    rec->id = NextId();
    snprintf(rec->name, sizeof(rec->name), "%s", n);
    snprintf(rec->email, sizeof(rec->email), "%s", e);
    snprintf(rec->password, sizeof(rec->password), "%s", password);
    rec->created = (long long)time(NULL);
    userCount++;

    if (!SaveAll()) { userCount--; SetError(err, errSize, "Could not write the users file."); return false; }

    snprintf(currentUser, sizeof(currentUser), "%s", rec->name);
    currentId = rec->id;
    return true;
}

bool UserLogin(const char *nameOrEmail, const char *password, char *err, int errSize)
{
    char id[USER_FIELD_LEN];
    TrimCopy(id, sizeof(id), nameOrEmail);

    if (id[0] == '\0') { SetError(err, errSize, "Enter your name or email."); return false; }
    if (!LoadAll(err, errSize)) return false;
    if (userCount == 0) { SetError(err, errSize, "No accounts yet - create one first."); return false; }

    for (int i = 0; i < userCount; i++)
    {
        if ((SameText(users[i].name, id) || SameText(users[i].email, id)) && strcmp(users[i].password, password) == 0)
        {
            snprintf(currentUser, sizeof(currentUser), "%s", users[i].name);
            currentId = users[i].id;
            return true;
        }
    }

    SetError(err, errSize, "Wrong name/email or password.");
    return false;
}

void LogoutUser(void)
{
    currentUser[0] = '\0';
    currentId = 0;
}

const char *GetCurrentUser(void)
{
    return currentUser;
}

int GetCurrentUserId(void)
{
    return currentId;
}

bool UserGetCurrent(UserInfo *out)
{
    int index = LoadCurrent(NULL, 0);
    if (index < 0) return false;

    out->id = users[index].id;
    snprintf(out->name, sizeof(out->name), "%s", users[index].name);
    snprintf(out->email, sizeof(out->email), "%s", users[index].email);
    out->created = users[index].created;
    return true;
}

bool UserChangeName(const char *newName, const char *password, char *err, int errSize)
{
    char n[USER_FIELD_LEN];
    TrimCopy(n, sizeof(n), newName);

    int me = LoadCurrent(err, errSize);
    if (me < 0) return false;
    if (!PasswordOk(me, password, err, errSize)) return false;
    if (!CheckName(n, err, errSize)) return false;
    if (strcmp(users[me].name, n) == 0) { SetError(err, errSize, "That is already your name."); return false; }
    if (NameTaken(n, me)) { SetError(err, errSize, "That name is already taken."); return false; }

    snprintf(users[me].name, sizeof(users[me].name), "%s", n);
    if (!SaveAll()) { SetError(err, errSize, "Could not write the users file."); return false; }

    snprintf(currentUser, sizeof(currentUser), "%s", n);
    return true;
}

bool UserChangeEmail(const char *newEmail, const char *password, char *err, int errSize)
{
    char e[USER_FIELD_LEN];
    TrimCopy(e, sizeof(e), newEmail);

    int me = LoadCurrent(err, errSize);
    if (me < 0) return false;
    if (!PasswordOk(me, password, err, errSize)) return false;
    if (!CheckEmail(e, err, errSize)) return false;
    if (strcmp(users[me].email, e) == 0) { SetError(err, errSize, "That is already your email."); return false; }
    if (EmailTaken(e, me)) { SetError(err, errSize, "That email is already registered."); return false; }

    snprintf(users[me].email, sizeof(users[me].email), "%s", e);
    if (!SaveAll()) { SetError(err, errSize, "Could not write the users file."); return false; }
    return true;
}

bool UserChangePassword(const char *oldPassword, const char *newPassword, char *err, int errSize)
{
    int me = LoadCurrent(err, errSize);
    if (me < 0) return false;
    if (!PasswordOk(me, oldPassword, err, errSize)) return false;
    if (!CheckPassword(newPassword, err, errSize)) return false;
    if (strcmp(oldPassword, newPassword) == 0) { SetError(err, errSize, "The new password must be different."); return false; }

    snprintf(users[me].password, sizeof(users[me].password), "%s", newPassword);
    if (!SaveAll()) { SetError(err, errSize, "Could not write the users file."); return false; }
    return true;
}

bool UserDeleteAccount(const char *password, char *err, int errSize)
{
    int me = LoadCurrent(err, errSize);
    if (me < 0) return false;
    if (!PasswordOk(me, password, err, errSize)) return false;

    for (int i = me; i < userCount - 1; i++) users[i] = users[i + 1];
    userCount--;

    if (!SaveAll()) { SetError(err, errSize, "Could not write the users file."); return false; }

    LogoutUser();
    return true;
}
