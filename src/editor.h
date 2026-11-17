/**********************************************************************************************
*
*   C Learning App - multi-line code editor (used by the Visualizer)
*
*   Typing, selection (mouse drag or Shift+arrows), Ctrl+A/C/X/V, Ctrl+Z undo, Ctrl+arrows by
*   word, Home/End, PageUp/PageDown, Enter keeps the indentation (and indents after '{'),
*   Tab inserts 4 spaces, '}' on an empty line unindents. Long text scrolls (wheel, scroll bar).
*   Markers show the line about to run, the line that just ran and an error line.
*
**********************************************************************************************/

#ifndef EDITOR_H
#define EDITOR_H

#include "raylib.h"
#include "ui.h"

#define EDITOR_MAX_TEXT   8192
#define EDITOR_UNDO       32

typedef struct CodeEditor {
    char text[EDITOR_MAX_TEXT];
    int caret, anchor;          // selection is between anchor and caret (none when equal)
    int wantCol;                // column Up/Down try to keep
    bool focused;
    bool dragging;
    bool changed;               // the text changed during the last EditorUpdate
    bool readOnly;
    double blinkStamp;
    UiScroll scroll;            // vertical, in pixels
    float scrollX;              // horizontal, in pixels
    bool singleLineFeel;        // smaller box (the program input): no line numbers
    char undo[EDITOR_UNDO][EDITOR_MAX_TEXT];
    int undoCaret[EDITOR_UNDO];
    int undoCount;
    double lastEditTime;
    int lastEditKind;
} CodeEditor;

typedef struct EditorMarks {
    int execLine;               // line about to run (1-based, 0 = none)
    int prevLine;               // line that just ran
    int errorLine;              // line with an error
} EditorMarks;

void EditorSetText(CodeEditor *e, const char *text);
void EditorUpdate(CodeEditor *e, Rectangle bounds);
void EditorDraw(CodeEditor *e, Rectangle bounds, EditorMarks marks);
void EditorShowLine(CodeEditor *e, Rectangle bounds, int line);     // scroll so a line is visible

#endif // EDITOR_H
