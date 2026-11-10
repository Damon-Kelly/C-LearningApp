#include "users.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define USERS_LINE_LEN  256

typedef struct UserRecord {
    char name[USER_FIELD_LEN];
    char email[USER_FIELD_LEN];
    char password[USER_FIELD_LEN];
} UserRecord;

static char usersFile[512] = "users.txt";
static char currentUser[USER_FIELD_LEN] = "";

//----------------------------------------------------------------------------------
// Helpers
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

static bool IsValidEmail(const char *e)
{
    const char *at = strchr(e, '@');
    if (at == NULL || at == e || strchr(at + 1, '@') != NULL) return false;

    const char *dot = strrchr(at, '.');
    if (dot == NULL || dot == at + 1 || dot[1] == '\0') return false;

    return strchr(e, ' ') == NULL && strchr(e, '|') == NULL;
}

// Splits "name|email|password" in place. Returns false for a malformed line.
static bool ParseLine(char *line, UserRecord *rec)
{
    line[strcspn(line, "\r\n")] = '\0';

    char *first = strchr(line, '|');
    if (first == NULL) return false;
    char *second = strchr(first + 1, '|');
    if (second == NULL) return false;

    *first = '\0';
    *second = '\0';
    snprintf(rec->name, sizeof(rec->name), "%s", line);
    snprintf(rec->email, sizeof(rec->email), "%s", first + 1);
    snprintf(rec->password, sizeof(rec->password), "%s", second + 1);
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

    if (n[0] == '\0') { SetError(err, errSize, "Enter a name."); return false; }
    if (strchr(n, '|') != NULL || strchr(n, '@') != NULL) { SetError(err, errSize, "The name cannot contain | or @."); return false; }
    if (!IsValidEmail(e)) { SetError(err, errSize, "Enter a valid email address."); return false; }
    if (strlen(password) < 4) { SetError(err, errSize, "The password needs at least 4 characters."); return false; }

    // Name and email must both be unused
    FILE *f = fopen(usersFile, "r");
    if (f != NULL)
    {
        char line[USERS_LINE_LEN];
        UserRecord rec;

        while (fgets(line, sizeof(line), f) != NULL)
        {
            if (!ParseLine(line, &rec)) continue;

            if (SameText(rec.name, n)) { fclose(f); SetError(err, errSize, "That name is already taken."); return false; }
            if (SameText(rec.email, e)) { fclose(f); SetError(err, errSize, "That email is already registered."); return false; }
        }
        fclose(f);
    }

    f = fopen(usersFile, "a");
    if (f == NULL) { SetError(err, errSize, "Could not write the users file."); return false; }
    fprintf(f, "%s|%s|%s\n", n, e, password);
    fclose(f);

    snprintf(currentUser, sizeof(currentUser), "%s", n);
    return true;
}

bool UserLogin(const char *nameOrEmail, const char *password, char *err, int errSize)
{
    char id[USER_FIELD_LEN];
    TrimCopy(id, sizeof(id), nameOrEmail);

    if (id[0] == '\0') { SetError(err, errSize, "Enter your name or email."); return false; }

    FILE *f = fopen(usersFile, "r");
    if (f == NULL) { SetError(err, errSize, "No accounts yet - create one first."); return false; }

    char line[USERS_LINE_LEN];
    UserRecord rec;

    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (!ParseLine(line, &rec)) continue;

        if ((SameText(rec.name, id) || SameText(rec.email, id)) && strcmp(rec.password, password) == 0)
        {
            fclose(f);
            snprintf(currentUser, sizeof(currentUser), "%s", rec.name);
            return true;
        }
    }

    fclose(f);
    SetError(err, errSize, "Wrong name/email or password.");
    return false;
}

const char *GetCurrentUser(void)
{
    return currentUser;
}

void LogoutUser(void)
{
    currentUser[0] = '\0';
}
