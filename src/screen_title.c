#include "raylib.h"
#include "screens.h"
#include "ui.h"

static GameScreen finishScreen = UNKNOWN;

static const Rectangle startButton = { 380, 290, 200, 52 };
static const Rectangle quitButton  = { 380, 356, 200, 52 };

void InitTitleScreen(void)
{
    finishScreen = UNKNOWN;
}

void UpdateTitleScreen(void)
{
    if (UiButtonClicked(startButton) || IsKeyPressed(KEY_ENTER)) finishScreen = SECTIONS;
    if (UiButtonClicked(quitButton)) quitRequested = true;
}

void DrawTitleScreen(void)
{
    float cx = GetScreenWidth()/2.0f;

    UiDrawTextCentered("C Learning App", cx, 130, 60, UI_TEXT);
    UiDrawTextCentered("Learn C and C++ with short lessons, traces and quizzes", cx, 210, 22, UI_MUTED);

    UiButtonDraw(startButton, "Start");
    UiButtonDraw(quitButton, "Quit");
}

void UnloadTitleScreen(void)
{
}

GameScreen FinishTitleScreen(void)
{
    return finishScreen;
}
