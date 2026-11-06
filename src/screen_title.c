#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "users.h"

#include <stdio.h>
#include <string.h>

#define FIELD_LEN   USER_FIELD_LEN
#define MAX_FIELDS  4

static GameScreen finishScreen = UNKNOWN;

static bool signUpMode = false;             // false: log in, true: create account
static char fields[MAX_FIELDS][FIELD_LEN];
static int active = 0;                      // field that has keyboard focus
static char message[128];                   // last error, shown in red

static const Rectangle quitButton = { 60, 490, 100, 36 };

// Log in: 0 = name or email, 1 = password
// Sign up: 0 = name, 1 = email, 2 = password, 3 = confirm password
static const char *loginLabels[2]  = { "Name or email", "Password" };
static const char *signUpLabels[4] = { "Name", "Email", "Password", "Confirm password" };

static int FieldCount(void) { return signUpMode ? 4 : 2; }

static bool IsMasked(int i) { return signUpMode ? (i >= 2) : (i == 1); }

static Rectangle FieldRect(int i)
{
    return (Rectangle){ 300, 150.0f + i*66.0f, 360, 38 };
}

// Everything below the fields is placed relative to the last field
static float FieldsEnd(void)
{
    return FieldRect(FieldCount() - 1).y + 38.0f;
}

static Rectangle SubmitRect(void)  { return (Rectangle){ 300, FieldsEnd() + 34, 360, 44 }; }
static Rectangle ToggleRect(void)  { return (Rectangle){ 300, FieldsEnd() + 86, 360, 30 }; }

static void ClearFields(void)
{
    memset(fields, 0, sizeof(fields));
    active = 0;
}

static void Submit(void)
{
    char err[128] = "";
    bool ok;

    if (signUpMode)
    {
        if (strcmp(fields[2], fields[3]) != 0)
        {
            snprintf(err, sizeof(err), "The passwords do not match.");
            ok = false;
        }
        else ok = UserRegister(fields[0], fields[1], fields[2], err, sizeof(err));
    }
    else ok = UserLogin(fields[0], fields[1], err, sizeof(err));

    if (ok)
    {
        message[0] = '\0';
        finishScreen = SECTIONS;
    }
    else snprintf(message, sizeof(message), "%s", err);
}

void InitTitleScreen(void)
{
    finishScreen = UNKNOWN;
    signUpMode = false;
    message[0] = '\0';
    ClearFields();

    // Coming back to this screen (Back from Sections) means logging out
    LogoutUser();
    UsersSetFile(TextFormat("%susers.txt", GetApplicationDirectory()));
}

void UpdateTitleScreen(void)
{
    int n = FieldCount();

    // Click a box to focus it, Tab cycles through them
    for (int i = 0; i < n; i++) if (UiButtonClicked(FieldRect(i))) active = i;
    if (IsKeyPressed(KEY_TAB)) active = (active + 1) % n;

    UiTextBoxUpdate(fields[active], FIELD_LEN);

    if (UiButtonClicked(SubmitRect()) || IsKeyPressed(KEY_ENTER)) Submit();

    if (UiButtonClicked(ToggleRect()))
    {
        signUpMode = !signUpMode;
        message[0] = '\0';
        ClearFields();
    }

    if (UiButtonClicked(quitButton)) quitRequested = true;
}

void DrawTitleScreen(void)
{
    float cx = GAME_WIDTH/2.0f;

    UiDrawTextCentered("C Learning App", cx, 30, 48, UI_TEXT);
    UiDrawTextCentered(signUpMode ? "Create your account" : "Log in to start learning C and C++", cx, 88, 20, UI_MUTED);

    for (int i = 0; i < FieldCount(); i++)
    {
        const char *label = signUpMode ? signUpLabels[i] : loginLabels[i];
        UiTextBoxDraw(FieldRect(i), label, fields[i], i == active, IsMasked(i));
    }

    if (message[0] != '\0') UiDrawTextCentered(message, cx, FieldsEnd() + 8, 16, UI_BAD);

    UiButtonDraw(SubmitRect(), signUpMode ? "Create account" : "Log in");
    UiButtonDrawEx(ToggleRect(), signUpMode ? "I already have an account" : "Create an account", UI_MUTED, true);

    UiButtonDrawEx(quitButton, "Quit", UI_MUTED, true);
}

void UnloadTitleScreen(void)
{
    // Don't keep typed passwords around
    memset(fields, 0, sizeof(fields));
}

GameScreen FinishTitleScreen(void)
{
    return finishScreen;
}
