#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"

static GameScreen finishScreen = UNKNOWN;

static const Rectangle backButton     = { 60, 480, 140, 40 };       // acts as "Log out"
static const Rectangle profileButton  = { 640, 480, 120, 40 };
static const Rectangle settingsButton = { 780, 480, 120, 40 };

// Two columns of cards: with 6 sections that is 3 rows
static Rectangle CardRect(int i)
{
    return (Rectangle){ 60.0f + (i % 2)*430.0f, 110.0f + (i / 2)*124.0f, 410, 112 };
}

void InitSectionsScreen(void)
{
    finishScreen = UNKNOWN;
}

void UpdateSectionsScreen(void)
{
    for (int i = 0; i < GetSectionCount(); i++)
    {
        const SectionDef *section = GetSection(i);
        if (section->topicCount > 0 && UiButtonClicked(CardRect(i)))
        {
            selectedSection = i;
            finishScreen = TOPICS;
        }
    }

    if (UiButtonClicked(profileButton)) finishScreen = PROFILE;
    if (UiButtonClicked(settingsButton)) finishScreen = SETTINGS;

    // Going back to the title screen logs the user out
    if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TITLE;
}

void DrawSectionsScreen(void)
{
    UiDrawHeader("Sections");
    UiDrawLoggedInAs();

    if (GetContentError()[0] != '\0')
    {
        UiDrawWrappedText(TextFormat("Problem loading sections.txt: %s", GetContentError()), (Rectangle){ 60, 110, 840, 200 }, 20, UI_BAD);
    }

    for (int i = 0; i < GetSectionCount(); i++)
    {
        const SectionDef *section = GetSection(i);
        Rectangle r = CardRect(i);
        bool available = section->topicCount > 0;
        bool hover = available && CheckCollisionPointRec(GetMousePosition(), r);

        // Card: border first, then fill, so the border colour can react to hover
        DrawRectangleRounded(r, 0.1f, 8, hover ? UI_ACCENT : (Color){ 200, 204, 216, 255 });
        DrawRectangleRounded((Rectangle){ r.x + 2, r.y + 2, r.width - 4, r.height - 4 }, 0.1f, 8, available ? WHITE : (Color){ 236, 238, 244, 255 });

        Color titleColor = available ? UI_TEXT : UI_MUTED;
        UiText(TextFormat("%d. %s", i + 1, section->name), (Vector2){ r.x + 16, r.y + 12 }, 24, titleColor);
        UiDrawWrappedText(section->description, (Rectangle){ r.x + 16, r.y + 42, r.width - 32, 46 }, 16, UI_MUTED);

        const char *status = available ? TextFormat("%d topic%s", section->topicCount, section->topicCount == 1 ? "" : "s") : "Coming soon";
        UiText(status, (Vector2){ r.x + 16, r.y + r.height - 24 }, 16, available ? UI_ACCENT : UI_MUTED);
    }

    UiButtonDrawEx(backButton, "Log out", UI_MUTED, true);
    UiButtonDraw(profileButton, "Profile");
    UiButtonDraw(settingsButton, "Settings");
}

void UnloadSectionsScreen(void)
{
}

GameScreen FinishSectionsScreen(void)
{
    return finishScreen;
}
