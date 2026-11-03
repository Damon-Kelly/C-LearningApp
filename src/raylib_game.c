/*******************************************************************************************
*
*   C Learning App
*
*   Based on the raylib game template (Ramon Santamaria, zlib/libpng license).
*   Owns the window, the main loop, and screen switching with a fade transition.
*
********************************************************************************************/

#include "raylib.h"
#include "screens.h"
#include "ui.h"

//----------------------------------------------------------------------------------
// Shared Variables Definition (declared in screens.h)
//----------------------------------------------------------------------------------
GameScreen currentScreen = TITLE;
Font font = { 0 };
bool quitRequested = false;
int selectedTopic = 0;
int lastScore = 0;
int lastTotal = 0;

//----------------------------------------------------------------------------------
// Module Variables
//----------------------------------------------------------------------------------
static const int screenWidth = 960;
static const int screenHeight = 540;

// Table of screens, indexed by GameScreen. To add a screen: add it to the enum
// in screens.h, write its five functions, and add one line here.
static const ScreenOps screens[SCREEN_COUNT] = {
    [TITLE]   = { InitTitleScreen,   UpdateTitleScreen,   DrawTitleScreen,   UnloadTitleScreen,   FinishTitleScreen },
    [TOPICS]  = { InitTopicsScreen,  UpdateTopicsScreen,  DrawTopicsScreen,  UnloadTopicsScreen,  FinishTopicsScreen },
    [LESSON]  = { InitLessonScreen,  UpdateLessonScreen,  DrawLessonScreen,  UnloadLessonScreen,  FinishLessonScreen },
    [QUIZ]    = { InitQuizScreen,    UpdateQuizScreen,    DrawQuizScreen,    UnloadQuizScreen,    FinishQuizScreen },
    [RESULTS] = { InitResultsScreen, UpdateResultsScreen, DrawResultsScreen, UnloadResultsScreen, FinishResultsScreen },
};

// Fade transition state
static float transAlpha = 0.0f;
static bool onTransition = false;
static bool transFadeOut = false;
static GameScreen transFromScreen = UNKNOWN;
static GameScreen transToScreen = UNKNOWN;

static void TransitionToScreen(GameScreen screen);
static void UpdateTransition(void);
static void UpdateDrawFrame(void);

//----------------------------------------------------------------------------------
// Program main entry point
//----------------------------------------------------------------------------------
int main(void)
{
    InitWindow(screenWidth, screenHeight, "C Learning App");
    SetExitKey(KEY_NULL);           // ESC is used for "back"; the window close button still quits
    SetTargetFPS(60);

    font = GetFontDefault();        // TODO: load a nicer font from resources/

    currentScreen = TITLE;
    screens[currentScreen].init();

    while (!WindowShouldClose() && !quitRequested)
    {
        UpdateDrawFrame();
    }

    screens[currentScreen].unload();

    CloseWindow();

    return 0;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
// Request a fade transition to another screen
static void TransitionToScreen(GameScreen screen)
{
    onTransition = true;
    transFadeOut = false;
    transFromScreen = currentScreen;
    transToScreen = screen;
    transAlpha = 0.0f;
}

// Fade to black, swap screens at full black, fade back in
static void UpdateTransition(void)
{
    const float step = 0.08f;

    if (!transFadeOut)
    {
        transAlpha += step;

        if (transAlpha >= 1.0f)
        {
            transAlpha = 1.0f;

            screens[transFromScreen].unload();
            screens[transToScreen].init();
            currentScreen = transToScreen;

            transFadeOut = true;
        }
    }
    else
    {
        transAlpha -= step;

        if (transAlpha <= 0.0f)
        {
            transAlpha = 0.0f;
            transFadeOut = false;
            onTransition = false;
            transFromScreen = UNKNOWN;
            transToScreen = UNKNOWN;
        }
    }
}

// Update and draw one frame
static void UpdateDrawFrame(void)
{
    // Update (input is ignored while a transition is running)
    if (!onTransition)
    {
        screens[currentScreen].update();

        GameScreen next = screens[currentScreen].finish();
        if (next != UNKNOWN) TransitionToScreen(next);
    }
    else UpdateTransition();

    // Draw
    BeginDrawing();

        ClearBackground(UI_BG);

        screens[currentScreen].draw();

        if (onTransition) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, transAlpha));

    EndDrawing();
}
