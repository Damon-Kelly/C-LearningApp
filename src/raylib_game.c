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
    [PROGRESS] = { InitProgressScreen, UpdateProgressScreen, DrawProgressScreen, UnloadProgressScreen, FinishProgressScreen },
    [VISUALIZER] = { InitVisualizerScreen, UpdateVisualizerScreen, DrawVisualizerScreen, UnloadVisualizerScreen, FinishVisualizerScreen },
    [CHALLENGE]  = { InitChallengeScreen,  UpdateChallengeScreen,  DrawChallengeScreen,  UnloadChallengeScreen,  FinishChallengeScreen },
};

// Slide transition: the old screen slides out and fades, the new one slides in. Going deeper into
// the app (Sections -> Topics -> Lesson) the new screen comes from the right, going back it comes
// from the left. screenDepth says what "deeper" means; equal depths count as going forward.
static const int screenDepth[SCREEN_COUNT] = {
    [TITLE] = 0, [SECTIONS] = 1, [TOPICS] = 2, [PROFILE] = 2, [SETTINGS] = 2, [PROGRESS] = 2,
    [LESSON] = 3, [EXAM] = 3, [CHALLENGE] = 3, [VISUALIZER] = 4,
};
#define TRANS_OUT_TIME  0.14f       // seconds the old screen takes to leave
#define TRANS_IN_TIME   0.26f       // seconds the new screen takes to arrive
#define TRANS_SLIDE     56.0f       // canvas pixels it travels
#define TRANS_COVER     0.94f       // how far the page colour fades over the screen at the turning point

static bool onTransition = false;
static bool transEntering = false;      // false: the old screen is leaving, true: the new one is arriving
static float transTime = 0.0f;          // seconds into the current phase
static int transDir = 1;                // +1 forward, -1 back
static GameScreen transFromScreen = UNKNOWN;
static GameScreen transToScreen = UNKNOWN;

static void TransitionToScreen(GameScreen screen);
static void UpdateTransition(void);
static void TransitionLook(float *slide, float *cover);
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
    UiMarkScreenStart();

    while (!WindowShouldClose() && !quitRequested)
    {
        UpdateDrawFrame();
    }

    screens[currentScreen].unload();
    UnloadAllChallenges();

    UnloadFonts();
    CloseWindow();

    return 0;
}

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
// Request a slide transition to another screen
static void TransitionToScreen(GameScreen screen)
{
    onTransition = true;
    transEntering = false;
    transTime = 0.0f;
    transFromScreen = currentScreen;
    transToScreen = screen;
    transDir = (screenDepth[screen] >= screenDepth[currentScreen]) ? 1 : -1;
}

// Old screen slides out and fades; at the turning point the screens are swapped; the new one slides in
static void UpdateTransition(void)
{
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;             // a slow frame (a lesson loading) must not skip the animation
    transTime += dt;

    if (!transEntering)
    {
        if (transTime >= TRANS_OUT_TIME)
        {
            screens[transFromScreen].unload();
            screens[transToScreen].init();
            currentScreen = transToScreen;
            UiMarkScreenStart();            // entrance animations inside the new screen start now

            transEntering = true;
            transTime = 0.0f;
        }
    }
    else if (transTime >= TRANS_IN_TIME)
    {
        onTransition = false;
        transEntering = false;
        transFromScreen = UNKNOWN;
        transToScreen = UNKNOWN;
    }
}

// How far the screen is shifted (canvas pixels) and how much the page colour covers it (0..1)
static void TransitionLook(float *slide, float *cover)
{
    *slide = 0.0f;
    *cover = 0.0f;
    if (!onTransition) return;

    if (!transEntering)
    {
        float e = UiEaseIn(transTime/TRANS_OUT_TIME);
        *slide = -transDir*TRANS_SLIDE*e;
        *cover = TRANS_COVER*e;
    }
    else
    {
        float e = UiEaseOut(transTime/TRANS_IN_TIME);
        *slide = transDir*TRANS_SLIDE*(1.0f - e);
        *cover = TRANS_COVER*(1.0f - e);
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
static bool debugOverlay = false;          // F3: mouse and scroll bar state, for tracking down input problems

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
    if (IsKeyPressed(KEY_F3)) debugOverlay = !debugOverlay;
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

        ClearBackground((Color){ 1, 4, 9, 255 });   // the bars around the canvas

        // The page background stays still; only the screen on top slides
        BeginMode2D((Camera2D){ .offset = uiOffset, .target = { 0, 0 }, .rotation = 0.0f, .zoom = uiScale });
            UiDrawBackground(screenWidth, screenHeight);
        EndMode2D();

        float slide, cover;
        TransitionLook(&slide, &cover);

        Vector2 baseOffset = uiOffset;
        uiOffset.x = baseOffset.x + roundf(slide*uiScale);      // UiBeginScissor reads uiOffset, so clipping slides along
        BeginMode2D((Camera2D){ .offset = uiOffset, .target = { 0, 0 }, .rotation = 0.0f, .zoom = uiScale });
            screens[currentScreen].draw();
        EndMode2D();
        uiOffset = baseOffset;

        if (cover > 0.0f)
        {
            BeginMode2D((Camera2D){ .offset = uiOffset, .target = { 0, 0 }, .rotation = 0.0f, .zoom = uiScale });
                DrawRectangle(0, 0, screenWidth, screenHeight, Fade(UI_BG, cover));
            EndMode2D();
        }

        // F3 debug overlay, drawn in window pixels with raylib's built-in font
        if (debugOverlay)
        {
            Vector2 m = GetMousePosition();
            DrawRectangle(0, 0, 900, 70, Fade(BLACK, 0.75f));
            DrawText(TextFormat("canvas mouse %.0f, %.0f   left button: down %d pressed %d released %d   window %dx%d  scale %.2f",
                                m.x, m.y, IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
                                IsMouseButtonReleased(MOUSE_BUTTON_LEFT), GetScreenWidth(), GetScreenHeight(), uiScale), 8, 8, 20, YELLOW);
            DrawText(uiScrollDebug, 8, 38, 20, YELLOW);
        }

        // F3 debug overlay, drawn in window pixels with raylib's built-in font
        if (debugOverlay)
        {
            Vector2 m = GetMousePosition();
            DrawRectangle(0, 0, 900, 70, Fade(BLACK, 0.75f));
            DrawText(TextFormat("canvas mouse %.0f, %.0f   left button: down %d pressed %d released %d   window %dx%d  scale %.2f",
                                m.x, m.y, IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
                                IsMouseButtonReleased(MOUSE_BUTTON_LEFT), GetScreenWidth(), GetScreenHeight(), uiScale), 8, 8, 20, YELLOW);
            DrawText(uiScrollDebug, 8, 38, 20, YELLOW);
        }

        // Black bars on top, so nothing a screen draws outside the canvas shows
        DrawRectangle(0, 0, (int)dest.x, GetScreenHeight(), BLACK);
        DrawRectangle((int)(dest.x + dest.width + 0.5f), 0, GetScreenWidth(), GetScreenHeight(), BLACK);
        DrawRectangle(0, 0, GetScreenWidth(), (int)dest.y, BLACK);
        DrawRectangle(0, (int)(dest.y + dest.height + 0.5f), GetScreenWidth(), GetScreenHeight(), BLACK);

    EndDrawing();
}
