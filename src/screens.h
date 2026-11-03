/**********************************************************************************************
*
*   C Learning App - screen manager declarations
*
*   Based on the raylib game template (Ramon Santamaria, zlib/libpng license).
*   Each screen has Init / Update / Draw / Unload / Finish functions.
*   Finish returns the screen to go to next, or UNKNOWN while the screen is still active.
*
**********************************************************************************************/

#ifndef SCREENS_H
#define SCREENS_H

#include "raylib.h"

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
typedef enum GameScreen {
    UNKNOWN = -1,
    TITLE = 0,
    TOPICS,
    LESSON,
    QUIZ,
    RESULTS,
    SCREEN_COUNT
} GameScreen;

// One entry per screen; raylib_game.c keeps a table of these indexed by GameScreen
typedef struct ScreenOps {
    void (*init)(void);
    void (*update)(void);
    void (*draw)(void);
    void (*unload)(void);
    GameScreen (*finish)(void);
} ScreenOps;

//----------------------------------------------------------------------------------
// Global Variables Declaration (shared by several modules, defined in raylib_game.c)
//----------------------------------------------------------------------------------
extern GameScreen currentScreen;
extern Font font;
extern bool quitRequested;      // Set to true to close the app
extern int selectedTopic;       // Index of the topic chosen on the Topics screen
extern int lastScore;           // Result of the most recent quiz
extern int lastTotal;

//----------------------------------------------------------------------------------
// Screen Functions Declaration
//----------------------------------------------------------------------------------
void InitTitleScreen(void);
void UpdateTitleScreen(void);
void DrawTitleScreen(void);
void UnloadTitleScreen(void);
GameScreen FinishTitleScreen(void);

void InitTopicsScreen(void);
void UpdateTopicsScreen(void);
void DrawTopicsScreen(void);
void UnloadTopicsScreen(void);
GameScreen FinishTopicsScreen(void);

void InitLessonScreen(void);
void UpdateLessonScreen(void);
void DrawLessonScreen(void);
void UnloadLessonScreen(void);
GameScreen FinishLessonScreen(void);

void InitQuizScreen(void);
void UpdateQuizScreen(void);
void DrawQuizScreen(void);
void UnloadQuizScreen(void);
GameScreen FinishQuizScreen(void);

void InitResultsScreen(void);
void UpdateResultsScreen(void);
void DrawResultsScreen(void);
void UnloadResultsScreen(void);
GameScreen FinishResultsScreen(void);

#endif // SCREENS_H
