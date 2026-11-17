/**********************************************************************************************
*
*   C Learning App - code and output panels
*
*   Draws code in the monospace font with line numbers, simple syntax colours and an
*   optional highlighted line (used by the trace page).
*
**********************************************************************************************/

#ifndef CODEPANEL_H
#define CODEPANEL_H

#include "raylib.h"

#define CODE_FONT_SIZE    18
#define CODE_LINE_HEIGHT  24

int CodeLineCount(const char *text);

// Syntax colouring, shared with the Visualizer's editor: one class per character of a line
typedef enum CodeClass { CLS_NORMAL, CLS_KEYWORD, CLS_STRING, CLS_COMMENT, CLS_NUMBER, CLS_PREPROC, CLS_COUNT } CodeClass;
void CodeClassify(const char *s, int n, unsigned char *cls);
Color CodeClassColor(int cls);
float CodePanelHeight(const char *code);
float OutputPanelHeight(const char *output);

// lang is "c" or "cpp" (shown as a badge). highlightLine is 1-based, 0 for none.
void DrawCodePanel(Rectangle bounds, const char *code, const char *lang, int highlightLine);

// Dark terminal-style box with an "Output" label
void DrawOutputPanel(Rectangle bounds, const char *output);

#endif // CODEPANEL_H
