#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"

static GameScreen finishScreen = UNKNOWN;

static const Rectangle backButton = { 60, 480, 140, 40 };

// Two columns of cards, room for 4 rows (8 sections) above the Back button.
// Add a row of space here if you ever need more than 8 sections.
#define CARD_W      410.0f
#define CARD_H      84.0f
#define CARD_PITCH  92.0f

static Rectangle CardRect(int i)
{
    return (Rectangle){ 60.0f + (i % 2)*430.0f, 100.0f + (i / 2)*CARD_PITCH, CARD_W, CARD_H };
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

    if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TITLE;
}

void DrawSectionsScreen(void)
{
    UiDrawHeader("Sections");

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
        UiText(TextFormat("%d. %s", i + 1, section->name), (Vector2){ r.x + 16, r.y + 10 }, 24, titleColor);

        // Status sits on the title line, right-aligned, so the description gets the rest of the card
        const char *status = available ? TextFormat("%d topic%s", section->topicCount, section->topicCount == 1 ? "" : "s") : "Coming soon";
        Vector2 statusSize = UiMeasureText(status, 16);
        UiText(status, (Vector2){ r.x + r.width - 16 - statusSize.x, r.y + 16 }, 16, available ? UI_ACCENT : UI_MUTED);

        UiDrawWrappedText(section->description, (Rectangle){ r.x + 16, r.y + 44, r.width - 32, 34 }, 16, UI_MUTED);
    }

    UiButtonDrawEx(backButton, "Back", UI_MUTED, true);
}

void UnloadSectionsScreen(void)
{
}

GameScreen FinishSectionsScreen(void)
{
    return finishScreen;
}
