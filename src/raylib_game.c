/*******************************************************************************************
*
*   C Learning App
*
*   Based on the raylib game template (Ramon Santamaria, zlib/libpng license).
*   Owns the window, the main loop, and screen switching with a fade transition.
*
********************************************************************************************/

#include "raylib.h"
#include <math.h>        // roundf, ceilf
#include "screens.h"
#include "codepanel.h"     // CODE_FONT_SIZE
#include "ui.h"
#include "content.h"

//----------------------------------------------------------------------------------
// Shared Variables Definition (declared in screens.h)
//----------------------------------------------------------------------------------
GameScreen currentScreen = TITLE;
Font font = { 0 };
Font fontLarge = { 0 };
Font monoFont = { 0 };
bool quitRequested = false;
int selectedSection = 0;
int selectedTopic = 0;

//----------------------------------------------------------------------------------
// Module Variables
//----------------------------------------------------------------------------------
static const int screenWidth = GAME_WIDTH;
static const int screenHeight = GAME_HEIGHT;

// Screens are laid out on a fixed 960x540 canvas. It is drawn straight to the window through a
// 2D camera (zoom + offset), so text and shapes are rendered at the real window resolution and
// stay sharp. Black bars fill any space the canvas does not cover.
float uiScale = 1.0f;                 // window pixels per canvas pixel
Vector2 uiOffset = { 0, 0 };          // where the canvas starts in the window
static bool startFullscreen = true;     // F11 or Alt+Enter toggles at any time

// Table of screens, indexed by GameScreen. To add a screen: add it to the enum
// in screens.h, write its five functions, and add one line here.
static const ScreenOps screens[SCREEN_COUNT] = {
    [TITLE]    = { InitTitleScreen,    UpdateTitleScreen,    DrawTitleScreen,    UnloadTitleScreen,    FinishTitleScreen },
    [SECTIONS] = { InitSectionsScreen, UpdateSectionsScreen, DrawSectionsScreen, UnloadSectionsScreen, FinishSectionsScreen },
    [TOPICS]   = { InitTopicsScreen,   UpdateTopicsScreen,   DrawTopicsScreen,   UnloadTopicsScreen,   FinishTopicsScreen },
    [LESSON]   = { InitLessonScreen,   UpdateLessonScreen,   DrawLessonScreen,   UnloadLessonScreen,   FinishLessonScreen },
    [EXAM]     = { InitExamScreen,     UpdateExamScreen,     DrawExamScreen,     UnloadExamScreen,     FinishExamScreen },
    [PROFILE]  = { InitProfileScreen,  UpdateProfileScreen,  DrawProfileScreen,  UnloadProfileScreen,  FinishProfileScreen },
    [SETTINGS] = { InitSettingsScreen, UpdateSettingsScreen, DrawSettingsScreen, UnloadSettingsScreen, FinishSettingsScreen },
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
static void LoadFontsForScale(float scale);
static void UnloadFonts(void);
static void ToggleFullscreenMode(void);
static Rectangle CanvasDestination(void);

// Resources are found next to the executable, so it works from any working directory.
// (CMake copies src/resources there; the Makefile build runs from src/ directly.)
const char *ResourcePath(const char *relativePath)
{
    return TextFormat("%sresources/%s", GetApplicationDirectory(), relativePath);
}

//----------------------------------------------------------------------------------
// Program main entry point
//----------------------------------------------------------------------------------
int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "C Learning App");
    SetWindowMinSize(480, 270);
    SetExitKey(KEY_NULL);           // ESC is used for "back"; the window close button still quits
    SetTargetFPS(60);

    if (startFullscreen) ToggleFullscreenMode();

    // Fonts are built at the size they appear on screen (see LoadFontsForScale), so they are
    // loaded here once for the starting window and again whenever the window scale changes.
    LoadFontsForScale(1.0f);

    LoadManifest(ResourcePath("content/sections.txt"));     // errors are shown on the Sections screen

    currentScreen = TITLE;
    screens[currentScreen].init();

    while (!WindowShouldClose() && !quitRequested)
    {
        UpdateDrawFrame();
    }

    screens[currentScreen].unload();

    UnloadFonts();
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

// Borderless full screen where raylib supports it (5.5+), real full screen otherwise
static void ToggleFullscreenMode(void)
{
#if (RAYLIB_VERSION_MAJOR > 5) || (RAYLIB_VERSION_MAJOR == 5 && RAYLIB_VERSION_MINOR >= 5)
    ToggleBorderlessWindowed();
#else
    ToggleFullscreen();
#endif
}

//----------------------------------------------------------------------------------
// Fonts
//----------------------------------------------------------------------------------
// Text is drawn on the 960x540 canvas and the canvas is zoomed to fill the window. A font image
// that is smaller than the text on screen gets stretched, which is what makes text blurry. So each
// font is rebuilt at (largest size it is drawn at on the canvas) x (window scale):
//   font       body text, canvas sizes up to about 28 (UiText picks it for size 26 and below)
//   fontLarge  headings, canvas sizes up to about 56 (the title is 48 x UI_FONT_SCALE)
//   monoFont   code, always CODE_FONT_SIZE
// The scale is rounded to steps of 0.25 so dragging the window edge does not reload every frame.
static float loadedFontScale = 0.0f;

static Font LoadSharpFont(const char *file, int size)
{
    Font f = LoadFontEx(ResourcePath(file), size, NULL, 0);
    if (f.texture.id == 0 || f.glyphCount == 0) return GetFontDefault();
    GenTextureMipmaps(&f.texture);                          // smooth when drawn smaller than built
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

static void UnloadFonts(void)
{
    Font def = GetFontDefault();
    if (font.texture.id != 0 && font.texture.id != def.texture.id) UnloadFont(font);
    if (fontLarge.texture.id != 0 && fontLarge.texture.id != def.texture.id) UnloadFont(fontLarge);
    if (monoFont.texture.id != 0 && monoFont.texture.id != def.texture.id) UnloadFont(monoFont);
    font = fontLarge = monoFont = (Font){ 0 };
}

static void LoadFontsForScale(float scale)
{
    float q = ceilf(scale*4.0f)/4.0f;                       // round up to a step of 0.25
    if (q < 1.0f) q = 1.0f;
    if (q > 4.0f) q = 4.0f;
    if (q == loadedFontScale) return;

    UnloadFonts();
    font      = LoadSharpFont("Carlito-Regular.ttf", (int)ceilf(28.0f*q));
    fontLarge = LoadSharpFont("Carlito-Regular.ttf", (int)ceilf(56.0f*q));
    monoFont  = LoadSharpFont("DejaVuSansMono.ttf", (int)ceilf(CODE_FONT_SIZE*q));
    loadedFontScale = q;
}

// Where the canvas lands in the window: as large as fits, same shape, centred
static Rectangle CanvasDestination(void)
{
    float w = (float)GetScreenWidth();
    float h = (float)GetScreenHeight();
    float scale = (w/screenWidth < h/screenHeight) ? w/screenWidth : h/screenHeight;
    float dw = screenWidth*scale, dh = screenHeight*scale;
    return (Rectangle){ (w - dw)/2.0f, (h - dh)/2.0f, dw, dh };
}

// Update and draw one frame
static void UpdateDrawFrame(void)
{
    if (IsKeyPressed(KEY_F11) || (IsKeyPressed(KEY_ENTER) && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)))) ToggleFullscreenMode();

    // Screens work in canvas coordinates, so map the mouse from window to canvas
    Rectangle dest = CanvasDestination();
    SetMouseOffset((int)-dest.x, (int)-dest.y);
    SetMouseScale(screenWidth/dest.width, screenHeight/dest.height);
    uiScale = dest.width/screenWidth;
    uiOffset = (Vector2){ roundf(dest.x), roundf(dest.y) };   // whole pixels keep text crisp

    // Rebuild the fonts when the window scale changes enough to matter
    LoadFontsForScale(uiScale);

    // Update (input is ignored while a transition is running)
    if (!onTransition)
    {
        screens[currentScreen].update();

        GameScreen next = screens[currentScreen].finish();
        if (next != UNKNOWN) TransitionToScreen(next);
    }
    else UpdateTransition();

    BeginDrawing();

        ClearBackground(BLACK);

        BeginMode2D((Camera2D){ .offset = uiOffset, .target = { 0, 0 }, .rotation = 0.0f, .zoom = uiScale });

            DrawRectangle(0, 0, screenWidth, screenHeight, UI_BG);

            screens[currentScreen].draw();

            if (onTransition) DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, transAlpha));

        EndMode2D();

        // Black bars on top, so nothing a screen draws outside the canvas shows
        DrawRectangle(0, 0, (int)dest.x, GetScreenHeight(), BLACK);
        DrawRectangle((int)(dest.x + dest.width + 0.5f), 0, GetScreenWidth(), GetScreenHeight(), BLACK);
        DrawRectangle(0, 0, GetScreenWidth(), (int)dest.y, BLACK);
        DrawRectangle(0, (int)(dest.y + dest.height + 0.5f), GetScreenWidth(), GetScreenHeight(), BLACK);

    EndDrawing();
}
