/**********************************************************************************************
*
*   Profile: read-only account details. Changes are made on the Settings screen.
*   The Progress card is a placeholder until progress tracking exists.
*
**********************************************************************************************/

#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "users.h"

#include <ctype.h>
#include <time.h>

static GameScreen finishScreen = UNKNOWN;

static UserInfo info;
static bool haveInfo = false;

static const Rectangle backButton = { 60, 480, 140, 40 };
static const Rectangle settingsButton = { 700, 480, 200, 40 };

static const Rectangle detailsCard = { 60, 112, 840, 208 };
static const Rectangle progressCard = { 60, 336, 840, 112 };

#define CARD_BORDER  ((Color){ 200, 204, 216, 255 })

static void DrawCard(Rectangle r)
{
    DrawRectangleRounded(r, 0.06f, 8, CARD_BORDER);
    DrawRectangleRounded((Rectangle){ r.x + 2, r.y + 2, r.width - 4, r.height - 4 }, 0.06f, 8, WHITE);
}

static const char *MemberSince(long long created)
{
    if (created <= 0) return "Not recorded";

    time_t t = (time_t)created;
    struct tm *local = localtime(&t);
    if (local == NULL) return "Not recorded";

    static char text[40];
    if (strftime(text, sizeof(text), "%d %B %Y", local) == 0) return "Not recorded";
    return text;
}

void InitProfileScreen(void)
{
    finishScreen = UNKNOWN;
    haveInfo = UserGetCurrent(&info);
}

void UpdateProfileScreen(void)
{
    if (!haveInfo)
    {
        // Nobody logged in (should not happen): back to the login screen
        finishScreen = TITLE;
        return;
    }

    if (UiButtonClicked(settingsButton)) finishScreen = SETTINGS;
    if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = SECTIONS;
}

void DrawProfileScreen(void)
{
    UiDrawHeader("Profile");
    UiDrawLoggedInAs();

    if (!haveInfo) return;

    // Account details
    DrawCard(detailsCard);

    // Avatar: a circle with the first letter of the name
    float cx = detailsCard.x + 16 + 44;
    float cy = detailsCard.y + detailsCard.height/2.0f;
    DrawCircleV((Vector2){ cx, cy }, 44, UI_ACCENT);

    char initial[2] = { (char)toupper((unsigned char)info.name[0]), '\0' };
    if (initial[0] == '\0') initial[0] = '?';
    Vector2 initialSize = UiMeasureText(initial, 40);
    UiText(initial, (Vector2){ cx - initialSize.x/2.0f, cy - initialSize.y/2.0f }, 40, WHITE);

    // Label and value rows
    const char *labels[4] = { "Name", "Email", "Member since", "Password" };
    const char *values[4] = { info.name, info.email, MemberSince(info.created), "********" };

    float x = detailsCard.x + 160;
    for (int i = 0; i < 4; i++)
    {
        float y = detailsCard.y + 14 + i*46.0f;
        UiText(labels[i], (Vector2){ x, y }, 16, UI_MUTED);
        UiText(values[i], (Vector2){ x, y + 18 }, 22, UI_TEXT);
    }

    // Progress placeholder
    DrawCard(progressCard);
    UiText("Progress", (Vector2){ progressCard.x + 20, progressCard.y + 14 }, 22, UI_TEXT);
    UiDrawWrappedText("Your completed lessons and quiz scores will appear here soon.",
                      (Rectangle){ progressCard.x + 20, progressCard.y + 50, progressCard.width - 40, 50 }, 18, UI_MUTED);

    UiButtonDrawEx(backButton, "Back", UI_MUTED, true);
    UiButtonDraw(settingsButton, "Edit in settings");
}

void UnloadProfileScreen(void)
{
}

GameScreen FinishProfileScreen(void)
{
    return finishScreen;
}
