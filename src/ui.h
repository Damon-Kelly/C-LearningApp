/**********************************************************************************************
*
*   C Learning App - small immediate-style UI helpers
*
*   Buttons are split in two so they fit the Update/Draw screen structure:
*   call UiButtonClicked() in Update and UiButtonDraw() in Draw with the same Rectangle.
*
**********************************************************************************************/

#ifndef UI_H
#define UI_H

#include "raylib.h"

// Palette: a dark "IDE" theme
#define UI_BG       ((Color){  13,  17,  23, 255 })    // page background
#define UI_PANEL    ((Color){  22,  27,  34, 255 })    // cards and panels
#define UI_PANEL2   ((Color){  33,  38,  45, 255 })    // raised: secondary buttons, hover
#define UI_BORDER   ((Color){  48,  54,  61, 255 })
#define UI_TEXT     ((Color){ 230, 237, 243, 255 })
#define UI_MUTED    ((Color){ 139, 148, 158, 255 })    // as a button fill: draws a secondary (outlined) button
#define UI_ACCENT   ((Color){  56, 139, 253, 255 })
#define UI_ACCENT2  ((Color){ 163, 113, 247, 255 })    // purple, for gradients and highlights
#define UI_GOOD     ((Color){  63, 185,  80, 255 })
#define UI_BAD      ((Color){ 248,  81,  73, 255 })
#define UI_WARN     ((Color){ 210, 153,  34, 255 })
#define UI_INPUT    ((Color){   1,   4,   9, 255 })    // inside text boxes

// The UI font (Carlito, loaded in raylib_game.c) looks smaller than raylib's default font at the
// same size, so every size passed to UiText / UiMeasureText is scaled up by this factor.
#define UI_FONT_SCALE 1.1f

// Extra space between letters, as a fraction of the text size (Carlito looks cramped with none)
#define UI_FONT_SPACING 0.03f

// Draw text with the UI font at the given size, and measure it. Use these instead of
// DrawTextEx/MeasureTextEx with `font`, so spacing and scaling stay the same everywhere.
void UiText(const char *text, Vector2 pos, float size, Color color);
// Like UiText, but text wider than maxWidth is shortened with "..." (for names, emails and other user input)
void UiTextFit(const char *text, Vector2 pos, float size, float maxWidth, Color color);
Vector2 UiMeasureText(const char *text, float size);

// Clip drawing to a rectangle given in canvas coordinates (like BeginScissorMode, but it follows
// the window scale). End it with EndScissorMode().
void UiBeginScissor(Rectangle area);

// True on the frame the left mouse button is released over bounds
bool UiButtonClicked(Rectangle bounds);

// Vertical scroll bar. Keep one UiScroll per scrolling area (static in the screen file).
// Update handles the mouse wheel, Up/Down keys, dragging the thumb and clicking the track.
// While the thumb is being dragged (and on the frame it is let go) UiButtonClicked returns
// false, so letting go of the thumb over a button does not press it.
typedef struct UiScroll {
    float offset;        // how far the content is scrolled, 0..maxScroll
    bool dragging;       // the thumb is held with the mouse
    float grab;          // where on the thumb it was grabbed
} UiScroll;
void UiScrollUpdate(UiScroll *sc, Rectangle track, float viewHeight, float maxScroll, float wheelStep);
// Same, but the Up/Down keys can be left alone (useful when a text editor needs them); wheelStep 0 ignores the wheel
void UiScrollUpdateEx(UiScroll *sc, Rectangle track, float viewHeight, float maxScroll, float wheelStep, bool arrowKeys);
void UiScrollDraw(const UiScroll *sc, Rectangle track, float viewHeight, float maxScroll);
extern char uiScrollDebug[160];     // last scroll bar state, for the F3 debug overlay

// Draw a button; enabled=false turns off the hover/press effect
void UiButtonDraw(Rectangle bounds, const char *text);
void UiButtonDrawEx(Rectangle bounds, const char *text, Color fill, bool enabled);

// Screen title at the top-left with an accent underline
void UiDrawHeader(const char *title);

// The animated page background (a slowly drifting dot grid), drawn under every screen
void UiDrawBackground(float width, float height);

// Faint, slowly scrolling C code (syntax coloured) filling an area: decoration for the title screen
void UiDrawCodeRain(Rectangle area, float speed);

// True when two colours are the same
bool UiSameColor(Color a, Color b);

//----------------------------------------------------------------------------------
// Motion helpers
//----------------------------------------------------------------------------------
// Colour between a (t = 0) and b (t = 1)
Color UiMix(Color a, Color b, float t);

// Easing curves for t from 0 to 1: fast then slow (out), slow then fast (in)
float UiEaseOut(float t);
float UiEaseIn(float t);

// 0..1 that glides toward 1 while `hovered` and back to 0 when not, so hover effects ease in and out
// instead of jumping. bounds identifies the thing (use the same rectangle every frame). Call from Draw.
float UiHover(Rectangle bounds, bool hovered);

// raylib_game.c calls this when a screen appears. UiEnter(delay, duration) then gives 0..1 (eased)
// for "how far into its entrance animation is something that starts `delay` seconds after that".
void UiMarkScreenStart(void);
float UiEnter(float delay, float duration);

// Fades an area in from the page background: draw it after the thing it covers. t = UiEnter(...)
void UiFadeIn(Rectangle area, float t);

// "Logged in as <name>" in small muted text just under the header line. Draws nothing when
// nobody is logged in.
void UiDrawLoggedInAs(void);

// Text centered horizontally around centerX
void UiDrawTextCentered(const char *text, float centerX, float y, float fontSize, Color color);

// Word-wrapped text inside bounds. Honors '\n' and leading spaces (for code samples).
// Stops drawing when it runs out of height. Returns the y position just below the last line.
float UiDrawWrappedText(const char *text, Rectangle bounds, float fontSize, Color color);

// Height the text would take when wrapped to the given width (draws nothing)
float UiMeasureWrappedText(const char *text, float width, float fontSize);

// Single-line text box, split like buttons: call UiTextBoxUpdate in Update for the box that
// has focus, and UiTextBoxDraw in Draw for every box (same Rectangle for both).
// Click to place the cursor; Left/Right (Ctrl = by word), Home, End, Backspace, Delete, Ctrl+V.
void UiTextBoxUpdate(Rectangle bounds, char *buf, int bufSize, bool masked);
void UiTextBoxDraw(Rectangle bounds, const char *label, const char *buf, bool active, bool masked);
// The same in the code font, without a label (used for typed exam answers)
void UiCodeBoxUpdate(Rectangle bounds, char *buf, int bufSize);
void UiCodeBoxDraw(Rectangle bounds, const char *buf, bool active);

#endif // UI_H
