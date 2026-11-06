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

// True on the frame the left mouse button is released over bounds
bool UiButtonClicked(Rectangle bounds);

// Draw a button; enabled=false turns off the hover/press effect
void UiButtonDraw(Rectangle bounds, const char *text);
void UiButtonDrawEx(Rectangle bounds, const char *text, Color fill, bool enabled);

// Screen title at the top-left with an accent underline
void UiDrawHeader(const char *title);

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
