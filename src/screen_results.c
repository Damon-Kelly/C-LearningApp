#include "raylib.h"
#include "screens.h"
#include "ui.h"

static GameScreen finishScreen = UNKNOWN;

static const Rectangle retryButton  = { 280, 340, 180, 52 };
static const Rectangle topicsButton = { 500, 340, 180, 52 };

void InitResultsScreen(void)
{
    finishScreen = UNKNOWN;
}

void UpdateResultsScreen(void)
{
    if (UiButtonClicked(retryButton)) finishScreen = QUIZ;
    if (UiButtonClicked(topicsButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TOPICS;
}

void DrawResultsScreen(void)
{
    float cx = GetScreenWidth()/2.0f;
    int percent = (lastTotal > 0) ? (lastScore*100)/lastTotal : 0;

    const char *message = "Keep going - re-read the lesson and try again.";
    if (percent == 100) message = "Perfect score!";
    else if (percent >= 60) message = "Nice work - retry to get the rest.";

    UiDrawHeader("Results");
    UiDrawTextCentered(TextFormat("%d / %d", lastScore, lastTotal), cx, 150, 80, UI_ACCENT);
    UiDrawTextCentered(message, cx, 260, 22, UI_TEXT);

    UiButtonDraw(retryButton, "Retry quiz");
    UiButtonDrawEx(topicsButton, "Topics", UI_MUTED, true);
}

void UnloadResultsScreen(void)
{
}

GameScreen FinishResultsScreen(void)
{
    return finishScreen;
}
