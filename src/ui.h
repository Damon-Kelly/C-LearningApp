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

// Palette
#define UI_BG      ((Color){ 245, 246, 250, 255 })
#define UI_TEXT    ((Color){  30,  34,  48, 255 })
#define UI_MUTED   ((Color){ 110, 116, 135, 255 })
#define UI_ACCENT  ((Color){  52, 101, 222, 255 })
#define UI_GOOD    ((Color){  46, 160, 100, 255 })
#define UI_BAD     ((Color){ 206,  64,  64, 255 })

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
void UiScrollDraw(const UiScroll *sc, Rectangle track, float viewHeight, float maxScroll);

// Draw a button; enabled=false turns off the hover/press effect
void UiButtonDraw(Rectangle bounds, const char *text);
void UiButtonDrawEx(Rectangle bounds, const char *text, Color fill, bool enabled);

// Screen title at the top-left with an accent underline
void UiDrawHeader(const char *title);

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
// has focus (typing, backspace, Ctrl+V), and UiTextBoxDraw in Draw for every box.
// masked=true shows * instead of the characters (passwords).
void UiTextBoxUpdate(char *buf, int bufSize);
void UiTextBoxDraw(Rectangle bounds, const char *label, const char *buf, bool active, bool masked);

#endif // UI_H
