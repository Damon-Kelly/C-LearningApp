/**********************************************************************************************
*
*   Settings: change name, email or password, or delete the account.
*
*   A menu of four actions; each opens a small form. Every action asks for the current
*   password. After a successful change the screen returns to the menu with a green message;
*   after deleting the account the user is sent to the login screen.
*
**********************************************************************************************/

#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "users.h"
#include "progress.h"

#include <stdio.h>
#include <string.h>

// Same order as the menu rows
typedef enum Mode { MODE_NAME, MODE_EMAIL, MODE_PASSWORD, MODE_DELETE, MODE_MENU } Mode;

#define FIELD_LEN   USER_FIELD_LEN
#define MAX_FIELDS  3
#define MENU_ROWS   4

static GameScreen finishScreen = UNKNOWN;

static Mode mode = MODE_MENU;
static char fields[MAX_FIELDS][FIELD_LEN];
static int active = 0;                      // field with keyboard focus
static char message[128];                   // error (red) in a form, confirmation (green) on the menu
static bool messageGood = false;

static UserInfo info;                       // current details, shown in the menu
static bool haveInfo = false;

static const Rectangle backButton = { 60, 480, 140, 40 };

static const char *menuTitles[MENU_ROWS] = { "Change name", "Change email", "Change password", "Delete account" };

//----------------------------------------------------------------------------------
// Layout
//----------------------------------------------------------------------------------
static Rectangle RowRect(int i)
{
    return (Rectangle){ 60, 112.0f + i*80.0f, 840, 48 };
}

static int FieldCount(void)
{
    switch (mode)
    {
        case MODE_NAME:
        case MODE_EMAIL: return 2;
        case MODE_PASSWORD: return 3;
        case MODE_DELETE: return 1;
        default: return 0;
    }
}

// The delete form has a warning above the fields, so its fields start lower
static Rectangle FieldRect(int i)
{
    float top = (mode == MODE_DELETE) ? 215.0f : 165.0f;
    return (Rectangle){ 300, top + i*66.0f, 360, 38 };
}

static float FieldsEnd(void)
{
    return FieldRect(FieldCount() - 1).y + 38.0f;
}

static Rectangle SubmitRect(void)
{
    return (Rectangle){ 300, FieldsEnd() + 34, 360, 44 };
}

//----------------------------------------------------------------------------------
// Form details
//----------------------------------------------------------------------------------
static const char *FormTitle(void)
{
    return (mode >= 0 && mode < MENU_ROWS) ? menuTitles[mode] : "Settings";
}

static const char *FormHint(void)
{
    switch (mode)
    {
        case MODE_NAME: return "Choose a name nobody else uses (no @ or |).";
        case MODE_EMAIL: return "Enter the new email address.";
        case MODE_PASSWORD: return "Use at least 4 characters (no |).";
        default: return "";
    }
}

static const char *FieldLabel(int i)
{
    switch (mode)
    {
        case MODE_NAME: return (i == 0) ? "New name" : "Current password";
        case MODE_EMAIL: return (i == 0) ? "New email" : "Current password";
        case MODE_PASSWORD: return (i == 0) ? "Current password" : (i == 1 ? "New password" : "Confirm new password");
        default: return "Current password";
    }
}

static bool IsMasked(int i)
{
    switch (mode)
    {
        case MODE_NAME:
        case MODE_EMAIL: return i == 1;
        default: return true;               // password and delete forms: every box is a password
    }
}

//----------------------------------------------------------------------------------
// Actions
//----------------------------------------------------------------------------------
static void ClearFields(void)
{
    memset(fields, 0, sizeof(fields));
    active = 0;
}

static void OpenForm(Mode m)
{
    mode = m;
    ClearFields();
    message[0] = '\0';
}

// Back to the menu, optionally with a green confirmation. Re-reads the account so the menu
// shows the new name or email.
static void GoToMenu(const char *text)
{
    mode = MODE_MENU;
    ClearFields();
    snprintf(message, sizeof(message), "%s", text);
    messageGood = (text[0] != '\0');
    haveInfo = UserGetCurrent(&info);
}

static void Submit(void)
{
    char err[128] = "";
    bool ok = false;
    const char *done = "";

    switch (mode)
    {
        case MODE_NAME:
        {
            ok = UserChangeName(fields[0], fields[1], err, sizeof(err));
            done = "Your name was changed.";
        } break;
        case MODE_EMAIL:
        {
            ok = UserChangeEmail(fields[0], fields[1], err, sizeof(err));
            done = "Your email was changed.";
        } break;
        case MODE_PASSWORD:
        {
            if (strcmp(fields[1], fields[2]) != 0) snprintf(err, sizeof(err), "The new passwords do not match.");
            else ok = UserChangePassword(fields[0], fields[1], err, sizeof(err));
            done = "Your password was changed.";
        } break;
        case MODE_DELETE:
        {
            int userId = GetCurrentUserId();
            ok = UserDeleteAccount(fields[0], err, sizeof(err));
            if (ok) ProgressDeleteUser(userId);         // the saved progress goes with the account
        } break;
        default: return;
    }

    if (!ok)
    {
        snprintf(message, sizeof(message), "%s", err);
        messageGood = false;
        return;
    }

    if (mode == MODE_DELETE)
    {
        SetTitleNotice("Your account was deleted.");
        finishScreen = TITLE;
        return;
    }

    GoToMenu(done);
}

//----------------------------------------------------------------------------------
// Screen functions
//----------------------------------------------------------------------------------
void InitSettingsScreen(void)
{
    finishScreen = UNKNOWN;
    GoToMenu("");
}

void UpdateSettingsScreen(void)
{
    if (!haveInfo)
    {
        // Nobody logged in (should not happen): back to the login screen
        finishScreen = TITLE;
        return;
    }

    if (mode == MODE_MENU)
    {
        for (int i = 0; i < MENU_ROWS; i++)
        {
            if (UiButtonClicked(RowRect(i))) OpenForm((Mode)i);
        }

        if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = SECTIONS;
        return;
    }

    // A form is open
    int n = FieldCount();
    for (int i = 0; i < n; i++) if (UiButtonClicked(FieldRect(i))) active = i;
    if (IsKeyPressed(KEY_TAB)) active = (active + 1) % n;

    UiTextBoxUpdate(fields[active], FIELD_LEN);

    if (UiButtonClicked(SubmitRect()) || IsKeyPressed(KEY_ENTER)) Submit();

    if (finishScreen == UNKNOWN && (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE))) GoToMenu("");
}

void DrawSettingsScreen(void)
{
    UiDrawHeader(mode == MODE_MENU ? "Settings" : FormTitle());
    UiDrawLoggedInAs();

    if (!haveInfo) return;

    float cx = GAME_WIDTH/2.0f;

    if (mode == MODE_MENU)
    {
        for (int i = 0; i < MENU_ROWS; i++)
        {
            Rectangle r = RowRect(i);
            UiButtonDrawEx(r, menuTitles[i], (i == MODE_DELETE) ? UI_BAD : UI_ACCENT, true);

            const char *summary;
            switch (i)
            {
                case MODE_NAME: summary = TextFormat("Current name: %s", info.name); break;
                case MODE_EMAIL: summary = TextFormat("Current email: %s", info.email); break;
                case MODE_PASSWORD: summary = "You need your current password to set a new one."; break;
                default: summary = "Permanently removes your account. This cannot be undone."; break;
            }
            UiText(summary, (Vector2){ r.x + 8, r.y + r.height + 6 }, 18, UI_MUTED);
        }

        if (message[0] != '\0') UiText(message, (Vector2){ 60, 446 }, 18, messageGood ? UI_GOOD : UI_BAD);

        UiButtonDrawEx(backButton, "Back", UI_MUTED, true);
        return;
    }

    // A form is open
    if (mode == MODE_DELETE)
    {
        UiDrawWrappedText("Deleting your account removes your name, email, password and all saved progress. This cannot be undone.",
                          (Rectangle){ 60, 112, 840, 70 }, 18, UI_BAD);
    }
    else UiDrawTextCentered(FormHint(), cx, 110, 16, UI_MUTED);

    for (int i = 0; i < FieldCount(); i++)
    {
        UiTextBoxDraw(FieldRect(i), FieldLabel(i), fields[i], i == active, IsMasked(i));
    }

    if (message[0] != '\0') UiDrawTextCentered(message, cx, FieldsEnd() + 8, 16, UI_BAD);

    if (mode == MODE_DELETE) UiButtonDrawEx(SubmitRect(), "Delete my account", UI_BAD, true);
    else UiButtonDraw(SubmitRect(), "Save");

    UiButtonDrawEx(backButton, "Cancel", UI_MUTED, true);
}

void UnloadSettingsScreen(void)
{
    // Don't keep typed passwords around
    memset(fields, 0, sizeof(fields));
}

GameScreen FinishSettingsScreen(void)
{
    return finishScreen;
}
