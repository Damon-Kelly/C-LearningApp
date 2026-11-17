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

// The game is drawn on a fixed 960x540 canvas that is scaled to fit the window (or full screen).
// All layout code uses these instead of the real window size.
#define GAME_WIDTH   960
#define GAME_HEIGHT  540

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
typedef enum GameScreen {
    UNKNOWN = -1,
    TITLE = 0,
    SECTIONS,
    TOPICS,
    LESSON,
    EXAM,
    PROFILE,
    SETTINGS,
    PROGRESS,
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
extern Font font;               // UI font (proportional), for body text
extern Font fontLarge;          // Same typeface built bigger, for headings (UiText picks it)
extern Font monoFont;           // Code font (monospace, drawn at CODE_FONT_SIZE)
extern bool quitRequested;      // Set to true to close the app
extern int selectedSection;     // Chosen on the Sections screen
extern int selectedTopic;       // Index inside the chosen section
extern float uiScale;           // Window pixels per canvas pixel (see raylib_game.c)
extern Vector2 uiOffset;        // Where the canvas starts inside the window

// Full path of a file in the resources folder next to the executable
const char *ResourcePath(const char *relativePath);

// A short green message shown on the title screen (for example "Your account was deleted.").
// It stays until the user logs in, signs up or switches form.
void SetTitleNotice(const char *text);

//----------------------------------------------------------------------------------
// Screen Functions Declaration
//----------------------------------------------------------------------------------
void InitTitleScreen(void);
void UpdateTitleScreen(void);
void DrawTitleScreen(void);
void UnloadTitleScreen(void);
GameScreen FinishTitleScreen(void);

void InitSectionsScreen(void);
void UpdateSectionsScreen(void);
void DrawSectionsScreen(void);
void UnloadSectionsScreen(void);
GameScreen FinishSectionsScreen(void);

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

void InitExamScreen(void);
void UpdateExamScreen(void);
void DrawExamScreen(void);
void UnloadExamScreen(void);
GameScreen FinishExamScreen(void);

void InitProfileScreen(void);
void UpdateProfileScreen(void);
void DrawProfileScreen(void);
void UnloadProfileScreen(void);
GameScreen FinishProfileScreen(void);

void InitSettingsScreen(void);
void UpdateSettingsScreen(void);
void DrawSettingsScreen(void);
void UnloadSettingsScreen(void);
GameScreen FinishSettingsScreen(void);

void InitProgressScreen(void);
void UpdateProgressScreen(void);
void DrawProgressScreen(void);
void UnloadProgressScreen(void);
GameScreen FinishProgressScreen(void);

// One line of overall progress for the logged-in user, e.g. "3 of 12 topic quizzes completed, 1 of 3 exams passed"
const char *ProgressSummaryLine(void);

#endif // SCREENS_H
