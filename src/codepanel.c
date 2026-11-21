#include "codepanel.h"
#include "screens.h"    // monoFont, font
#include "ui.h"         // UiText

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define PANEL_PADDING   10
#define GUTTER_WIDTH    36
#define OUTPUT_LINE_H   22
#define OUTPUT_HEADER_H 22

#define CODE_BG         ((Color){  22,  27,  34, 255 })
#define CODE_LINE_BG    ((Color){  70,  60,  25, 255 })
#define OUTPUT_BG       ((Color){   1,   4,   9, 255 })
#define GUTTER_COLOR    ((Color){ 110, 116, 135, 255 })


static const Color classColors[CLS_COUNT] = {
    { 220, 220, 220, 255 },     // normal
    {  86, 156, 214, 255 },     // keyword
    { 206, 145, 120, 255 },     // string
    { 106, 153,  85, 255 },     // comment
    { 181, 206, 168, 255 },     // number
    { 197, 134, 192, 255 },     // preprocessor
};

static const char *keywords[] = {
    "int", "char", "float", "double", "void", "long", "short", "unsigned", "signed", "const",
    "static", "struct", "class", "public", "private", "protected", "return", "if", "else",
    "for", "while", "do", "switch", "case", "break", "continue", "default", "sizeof", "new",
    "delete", "nullptr", "NULL", "true", "false", "bool", "typedef", "enum", "using",
    "namespace", "auto", NULL
};

static bool IsKeyword(const char *s, int len)
{
    for (int i = 0; keywords[i] != NULL; i++)
    {
        if ((int)strlen(keywords[i]) == len && strncmp(keywords[i], s, (size_t)len) == 0) return true;
    }
    return false;
}

// Fills cls[0..n-1] with a colour class per character
void CodeClassify(const char *s, int n, unsigned char *cls)
{
    int first = 0;
    while (first < n && s[first] == ' ') first++;
    if (first < n && s[first] == '#')
    {
        for (int i = 0; i < n; i++) cls[i] = CLS_PREPROC;
        return;
    }

    int i = 0;
    while (i < n)
    {
        char c = s[i];

        if (c == '/' && i + 1 < n && s[i + 1] == '/')
        {
            while (i < n) cls[i++] = CLS_COMMENT;
        }
        else if (c == '/' && i + 1 < n && s[i + 1] == '*')
        {
            int j = i + 2;
            while (j + 1 < n && !(s[j] == '*' && s[j + 1] == '/')) j++;
            int end = (j + 1 < n) ? j + 2 : n;
            while (i < end) cls[i++] = CLS_COMMENT;
        }
        else if (c == '"' || c == '\'')
        {
            int j = i + 1;
            while (j < n && s[j] != c)
            {
                if (s[j] == '\\' && j + 1 < n) j++;
                j++;
            }
            int end = (j < n) ? j + 1 : n;
            while (i < end) cls[i++] = CLS_STRING;
        }
        else if (isalpha((unsigned char)c) || c == '_')
        {
            int j = i;
            while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
            unsigned char kind = IsKeyword(s + i, j - i) ? CLS_KEYWORD : CLS_NORMAL;
            while (i < j) cls[i++] = kind;
        }
        else if (isdigit((unsigned char)c))
        {
            int j = i;
            while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '.')) j++;
            while (i < j) cls[i++] = CLS_NUMBER;
        }
        else cls[i++] = CLS_NORMAL;
    }
}

Color CodeClassColor(int cls) { return classColors[(cls >= 0 && cls < CLS_COUNT) ? cls : 0]; }

// Copies the next line of text into buf. Returns the start of the next line, or NULL after the last.
static const char *NextLine(const char *p, char *buf, int bufSize, int *len)
{
    int n = 0;
    while (*p != '\0' && *p != '\n')
    {
        if (n < bufSize - 1) buf[n++] = (*p == '\t') ? ' ' : *p;
        p++;
    }
    buf[n] = '\0';
    *len = n;
    return (*p == '\n') ? p + 1 : NULL;
}

int CodeLineCount(const char *text)
{
    if (text == NULL || text[0] == '\0') return 0;
    int lines = 1;
    for (const char *c = text; *c != '\0'; c++) if (*c == '\n') lines++;
    return lines;
}

float CodePanelHeight(const char *code)
{
    return (float)(CodeLineCount(code)*CODE_LINE_HEIGHT + 2*PANEL_PADDING);
}

float OutputPanelHeight(const char *output)
{
    return (float)(OUTPUT_HEADER_H + CodeLineCount(output)*OUTPUT_LINE_H + PANEL_PADDING);
}

void DrawCodePanel(Rectangle bounds, const char *code, const char *lang, int highlightLine)
{
    DrawRectangleRounded(bounds, 0.04f, 6, CODE_BG);
    if (code == NULL || code[0] == '\0') return;

    // Language badge in the top-right corner
    const char *badge = (lang != NULL && strcmp(lang, "cpp") == 0) ? "C++" : "C";
    Vector2 badgeSize = UiMeasureText(badge, 14);
    UiText(badge, (Vector2){ bounds.x + bounds.width - badgeSize.x - 10, bounds.y + 6 }, 14, GUTTER_COLOR);

    const float charW = MeasureTextEx(monoFont, "M", CODE_FONT_SIZE, 0).x;
    const float textX = bounds.x + PANEL_PADDING + GUTTER_WIDTH;

    const char *p = code;
    int lineNo = 0;

    while (p != NULL)
    {
        char buf[256];
        unsigned char cls[256];
        int n;

        p = NextLine(p, buf, sizeof(buf), &n);
        lineNo++;

        float y = bounds.y + PANEL_PADDING + (lineNo - 1)*CODE_LINE_HEIGHT;

        if (lineNo == highlightLine)
        {
            DrawRectangle((int)bounds.x + 3, (int)y - 1, (int)bounds.width - 6, CODE_LINE_HEIGHT, CODE_LINE_BG);
            DrawRectangle((int)bounds.x + 3, (int)y - 1, 4, CODE_LINE_HEIGHT, (Color){ 255, 159, 28, 255 });
        }

        // Line number, right-aligned in the gutter
        const char *num = TextFormat("%d", lineNo);
        Vector2 numSize = MeasureTextEx(monoFont, num, CODE_FONT_SIZE, 0);
        DrawTextEx(monoFont, num, (Vector2){ bounds.x + PANEL_PADDING + GUTTER_WIDTH - 10 - numSize.x, y }, CODE_FONT_SIZE, 0, GUTTER_COLOR);

        // Code, drawn in runs of the same colour (monospace, so columns line up)
        CodeClassify(buf, n, cls);

        int start = 0;
        while (start < n)
        {
            int end = start;
            while (end < n && cls[end] == cls[start]) end++;

            char run[256];
            memcpy(run, buf + start, (size_t)(end - start));
            run[end - start] = '\0';

            DrawTextEx(monoFont, run, (Vector2){ textX + start*charW, y }, CODE_FONT_SIZE, 0, classColors[cls[start]]);
            start = end;
        }
    }
}

void DrawOutputPanel(Rectangle bounds, const char *output)
{
    DrawRectangleRounded(bounds, 0.08f, 6, OUTPUT_BG);
    UiText("Output", (Vector2){ bounds.x + PANEL_PADDING, bounds.y + 5 }, 14, GUTTER_COLOR);

    if (output == NULL || output[0] == '\0') return;

    const char *p = output;
    int lineNo = 0;
    while (p != NULL)
    {
        char buf[256];
        int n;
        p = NextLine(p, buf, sizeof(buf), &n);

        float y = bounds.y + OUTPUT_HEADER_H + lineNo*OUTPUT_LINE_H;
        DrawTextEx(monoFont, buf, (Vector2){ bounds.x + PANEL_PADDING, y }, CODE_FONT_SIZE, 0, (Color){ 200, 230, 200, 255 });
        lineNo++;
    }
}
