#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"

static GameScreen finishScreen = UNKNOWN;

static const Rectangle backButton = { 60, 470, 140, 44 };
static const Rectangle quizButton = { 740, 470, 160, 44 };

void InitLessonScreen(void)
{
    finishScreen = UNKNOWN;
}

void UpdateLessonScreen(void)
{
    if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TOPICS;
    if (UiButtonClicked(quizButton) || IsKeyPressed(KEY_ENTER)) finishScreen = QUIZ;
}

void DrawLessonScreen(void)
{
    const Topic *topic = GetTopic(selectedTopic);
    if (topic == NULL) return;

    UiDrawHeader(topic->title);
    UiDrawWrappedText(topic->lesson, (Rectangle){ 60, 100, 840, 350 }, 22, UI_TEXT);

    UiButtonDrawEx(backButton, "Back", UI_MUTED, true);
    UiButtonDraw(quizButton, "Start quiz");
}

void UnloadLessonScreen(void)
{
}

GameScreen FinishLessonScreen(void)
{
    return finishScreen;
}
