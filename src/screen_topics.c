#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"

static GameScreen finishScreen = UNKNOWN;

static const Rectangle backButton = { 60, 480, 140, 40 };

// NOTE: rows are 90px apart starting at y=110, so 4 topics fit on screen.
// Add scrolling here when a section has more.
static Rectangle TopicRect(int i)
{
    return (Rectangle){ 60, 110.0f + i*90.0f, 840, 56 };
}

void InitTopicsScreen(void)
{
    finishScreen = UNKNOWN;
}

void UpdateTopicsScreen(void)
{
    const SectionDef *section = GetSection(selectedSection);

    if (section != NULL)
    {
        for (int i = 0; i < section->topicCount; i++)
        {
            if (UiButtonClicked(TopicRect(i)))
            {
                selectedTopic = i;
                finishScreen = LESSON;
            }
        }
    }

    if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = SECTIONS;
}

void DrawTopicsScreen(void)
{
    const SectionDef *section = GetSection(selectedSection);
    if (section == NULL) return;

    UiDrawHeader(section->name);

    for (int i = 0; i < section->topicCount; i++)
    {
        Rectangle r = TopicRect(i);
        const TopicRef *topic = &section->topics[i];

        UiButtonDraw(r, topic->title);
        DrawTextEx(font, topic->summary, (Vector2){ r.x + 8, r.y + r.height + 6 }, 18, 1.8f, UI_MUTED);
    }

    UiButtonDrawEx(backButton, "Back", UI_MUTED, true);
}

void UnloadTopicsScreen(void)
{
}

GameScreen FinishTopicsScreen(void)
{
    return finishScreen;
}
