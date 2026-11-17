/**********************************************************************************************
*
*   C Learning App - multi-line code editor (see editor.h)
*
*   The text is one NUL-terminated buffer without tabs, so a column is simply an offset from
*   the start of a line and every character is the same width (monospace font).
*
**********************************************************************************************/

#include "editor.h"
#include "screens.h"        // monoFont
#include "codepanel.h"      // syntax colours

#include <ctype.h>
#include <string.h>

#define FONT_SIZE    16.0f
#define LINE_H       20.0f
#define PAD          8.0f
#define GUTTER       38.0f

#define EDITOR_BG        ((Color){ 13, 17, 23, 255 })
#define EDITOR_BORDER    ((Color){ 48, 54, 61, 255 })
#define EDITOR_FOCUS     ((Color){ 88, 166, 255, 255 })
#define GUTTER_TEXT      ((Color){ 72, 80, 92, 255 })
#define SELECTION        ((Color){ 38, 79, 120, 255 })
#define EXEC_BAND        ((Color){ 235, 190, 60, 50 })
#define EXEC_MARK        ((Color){ 240, 200, 80, 255 })
#define PREV_BAND        ((Color){ 63, 185, 80, 28 })
#define ERROR_BAND       ((Color){ 248, 81, 73, 60 })

enum { EDIT_TYPE = 1, EDIT_DELETE = 2, EDIT_OTHER = 3 };

static float CharW(void) { return MeasureTextEx(monoFont, "M", FONT_SIZE, 0).x; }
static float Gutter(const CodeEditor *e) { return e->singleLineFeel ? 0.0f : GUTTER; }

//----------------------------------------------------------------------------------
// Text helpers
//----------------------------------------------------------------------------------
static int Len(const CodeEditor *e) { return (int)strlen(e->text); }

static int LineStart(const CodeEditor *e, int pos) { while (pos > 0 && e->text[pos - 1] != '\n') pos--; return pos; }
static int LineEnd(const CodeEditor *e, int pos) { while (e->text[pos] && e->text[pos] != '\n') pos++; return pos; }

static int LineOf(const CodeEditor *e, int pos)
{
    int line = 0;
    for (int i = 0; i < pos && e->text[i]; i++) if (e->text[i] == '\n') line++;
    return line;
}

static int LineCount(const CodeEditor *e)
{
    int n = 1;
    for (const char *p = e->text; *p; p++) if (*p == '\n') n++;
    return n;
}

static int PosOfLine(const CodeEditor *e, int line)
{
    int pos = 0;
    while (line > 0 && e->text[pos]) { if (e->text[pos] == '\n') line--; pos++; }
    return pos;
}

static int PosAt(const CodeEditor *e, int line, int col)
{
    int start = PosOfLine(e, line);
    int end = LineEnd(e, start);
    return (start + col < end) ? start + col : end;
}

static bool HasSelection(const CodeEditor *e) { return e->anchor != e->caret; }
static int SelMin(const CodeEditor *e) { return e->anchor < e->caret ? e->anchor : e->caret; }
static int SelMax(const CodeEditor *e) { return e->anchor > e->caret ? e->anchor : e->caret; }

static void PushUndo(CodeEditor *e, int kind)
{
    // Consecutive typing (or deleting) within a second counts as one undo step
    double now = GetTime();
    if (kind != EDIT_OTHER && kind == e->lastEditKind && now - e->lastEditTime < 1.0) { e->lastEditTime = now; return; }
    if (e->undoCount == EDITOR_UNDO)
    {
        memmove(e->undo[0], e->undo[1], sizeof(e->undo[0])*(EDITOR_UNDO - 1));
        memmove(&e->undoCaret[0], &e->undoCaret[1], sizeof(int)*(EDITOR_UNDO - 1));
        e->undoCount--;
    }
    memcpy(e->undo[e->undoCount], e->text, sizeof(e->text));
    e->undoCaret[e->undoCount] = e->caret;
    e->undoCount++;
    e->lastEditKind = kind;
    e->lastEditTime = now;
}

static void DeleteRange(CodeEditor *e, int a, int b)
{
    if (b <= a) return;
    memmove(e->text + a, e->text + b, strlen(e->text + b) + 1);
    e->caret = e->anchor = a;
    e->changed = true;
}

static void Insert(CodeEditor *e, const char *s, int n)
{
    if (HasSelection(e)) DeleteRange(e, SelMin(e), SelMax(e));
    int len = Len(e);
    if (len + n >= EDITOR_MAX_TEXT - 1) n = EDITOR_MAX_TEXT - 1 - len;
    if (n <= 0) return;
    memmove(e->text + e->caret + n, e->text + e->caret, (size_t)(len - e->caret + 1));
    memcpy(e->text + e->caret, s, (size_t)n);
    e->caret += n;
    e->anchor = e->caret;
    e->changed = true;
}

// Copies text into the buffer with tabs as 4 spaces and Windows line endings removed
static void Normalize(const char *src, char *out, int cap)
{
    int n = 0;
    for (const char *p = src; *p && n < cap - 5; p++)
    {
        if (*p == '\r') continue;
        if (*p == '\t') { for (int k = 0; k < 4; k++) out[n++] = ' '; continue; }
        if ((unsigned char)*p < 32 && *p != '\n') continue;
        if ((unsigned char)*p > 126) continue;      // the fonts only have plain ASCII
        out[n++] = *p;
    }
    out[n] = '\0';
}

void EditorSetText(CodeEditor *e, const char *text)
{
    Normalize(text, e->text, EDITOR_MAX_TEXT);
    e->caret = e->anchor = 0;
    e->scroll.offset = 0;
    e->scrollX = 0;
    e->undoCount = 0;
    e->changed = true;
}

//----------------------------------------------------------------------------------
// Scrolling
//----------------------------------------------------------------------------------
static Rectangle TextArea(const CodeEditor *e, Rectangle b)
{
    return (Rectangle){ b.x + Gutter(e), b.y, b.width - Gutter(e) - 10, b.height };
}

static float ContentHeight(const CodeEditor *e) { return LineCount(e)*LINE_H + 2*PAD; }
static float MaxScroll(const CodeEditor *e, Rectangle b) { float m = ContentHeight(e) - b.height; return m > 0 ? m : 0; }

static void KeepCaretVisible(CodeEditor *e, Rectangle b)
{
    int line = LineOf(e, e->caret);
    float top = PAD + line*LINE_H;
    if (top < e->scroll.offset) e->scroll.offset = top - PAD;
    if (top + LINE_H > e->scroll.offset + b.height - PAD) e->scroll.offset = top + LINE_H - b.height + PAD;
    if (e->scroll.offset < 0) e->scroll.offset = 0;

    Rectangle area = TextArea(e, b);
    float x = (e->caret - LineStart(e, e->caret))*CharW();
    if (x < e->scrollX) e->scrollX = x - 40 > 0 ? x - 40 : 0;
    if (x > e->scrollX + area.width - 2*PAD - 8) e->scrollX = x - area.width + 2*PAD + 40;
}

void EditorShowLine(CodeEditor *e, Rectangle b, int line)
{
    if (line <= 0) return;
    float top = PAD + (line - 1)*LINE_H;
    if (top < e->scroll.offset + LINE_H) e->scroll.offset = top - 2*LINE_H;
    if (top + LINE_H > e->scroll.offset + b.height - LINE_H) e->scroll.offset = top - b.height/2;
    float max = MaxScroll(e, b);
    if (e->scroll.offset > max) e->scroll.offset = max;
    if (e->scroll.offset < 0) e->scroll.offset = 0;
}

static int PosFromPoint(const CodeEditor *e, Rectangle b, Vector2 m)
{
    Rectangle area = TextArea(e, b);
    int line = (int)((m.y - b.y - PAD + e->scroll.offset)/LINE_H);
    int lines = LineCount(e);
    if (line < 0) line = 0;
    if (line >= lines) line = lines - 1;
    float x = m.x - area.x - PAD + e->scrollX;
    int col = (int)(x/CharW() + 0.5f);
    if (col < 0) col = 0;
    return PosAt(e, line, col);
}

//----------------------------------------------------------------------------------
// Update
//----------------------------------------------------------------------------------
static bool Repeat(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }
static bool IsWord(char c) { return isalnum((unsigned char)c) || c == '_'; }

static void MoveCaret(CodeEditor *e, int pos, bool shift, bool keepCol)
{
    e->caret = pos;
    if (!shift) e->anchor = pos;
    if (!keepCol) e->wantCol = pos - LineStart(e, pos);
    e->blinkStamp = GetTime();
}

void EditorUpdate(CodeEditor *e, Rectangle b)
{
    e->changed = false;
    Vector2 m = GetMousePosition();
    bool inside = CheckCollisionPointRec(m, b);
    Rectangle track = { b.x + b.width - 8, b.y + 4, 5, b.height - 8 };

    // Scroll bar and wheel (only while the mouse is over the editor)
    UiScrollUpdateEx(&e->scroll, track, b.height, MaxScroll(e, b), inside ? 3*LINE_H : 0.0f, false);
    bool onBar = e->scroll.dragging || (m.x >= track.x - 8 && inside);

    // Mouse: focus, place the caret, drag to select
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (inside && !onBar)
        {
            e->focused = true;
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            int pos = PosFromPoint(e, b, m);
            MoveCaret(e, pos, shift, false);
            e->dragging = true;
        }
        else if (!inside) e->focused = false;
    }
    if (e->dragging)
    {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            e->caret = PosFromPoint(e, b, m);
            e->wantCol = e->caret - LineStart(e, e->caret);
            // Dragging past the top or bottom scrolls
            if (m.y < b.y) e->scroll.offset -= 6;
            if (m.y > b.y + b.height) e->scroll.offset += 6;
        }
        else e->dragging = false;
    }

    if (!e->focused) return;

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool moved = false;

    if (!e->readOnly)
    {
        // Typed characters
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
        {
            if (c < 32 || c > 126) continue;
            PushUndo(e, EDIT_TYPE);
            if (c == '}')
            {
                // '}' on a line of only spaces moves back one indent level
                int ls = LineStart(e, e->caret);
                bool blank = true;
                for (int k = ls; k < e->caret; k++) if (e->text[k] != ' ') blank = false;
                if (blank && e->caret - ls >= 4 && !HasSelection(e)) DeleteRange(e, e->caret - 4, e->caret);
            }
            char ch = (char)c;
            Insert(e, &ch, 1);
            moved = true;
        }

        if (Repeat(KEY_ENTER) || Repeat(KEY_KP_ENTER))
        {
            if (!ctrl)
            {
                PushUndo(e, EDIT_OTHER);
                int ls = LineStart(e, SelMin(e));
                int indent = 0;
                while (e->text[ls + indent] == ' ') indent++;
                int before = SelMin(e) - 1;
                while (before >= ls && e->text[before] == ' ') before--;
                if (before >= ls && e->text[before] == '{') indent += 4;
                char buf[80];
                if (indent > 70) indent = 70;
                buf[0] = '\n';
                memset(buf + 1, ' ', (size_t)indent);
                Insert(e, buf, indent + 1);
                moved = true;
            }
        }
        if (Repeat(KEY_TAB)) { PushUndo(e, EDIT_TYPE); Insert(e, "    ", 4); moved = true; }

        if (Repeat(KEY_BACKSPACE))
        {
            PushUndo(e, EDIT_DELETE);
            if (HasSelection(e)) DeleteRange(e, SelMin(e), SelMax(e));
            else if (e->caret > 0)
            {
                int from = e->caret - 1;
                int ls = LineStart(e, e->caret);
                bool onlySpaces = true;
                for (int k = ls; k < e->caret; k++) if (e->text[k] != ' ') onlySpaces = false;
                if (ctrl) { while (from > 0 && e->text[from - 1] == ' ') from--; while (from > 0 && IsWord(e->text[from - 1])) from--; }
                else if (onlySpaces && e->caret > ls) { int col = e->caret - ls; from = e->caret - ((col - 1)%4 + 1); }
                DeleteRange(e, from, e->caret);
            }
            moved = true;
        }
        if (Repeat(KEY_DELETE))
        {
            PushUndo(e, EDIT_DELETE);
            if (HasSelection(e)) DeleteRange(e, SelMin(e), SelMax(e));
            else if (e->text[e->caret]) DeleteRange(e, e->caret, e->caret + 1);
            moved = true;
        }

        if (ctrl && IsKeyPressed(KEY_V))
        {
            const char *clip = GetClipboardText();
            if (clip)
            {
                PushUndo(e, EDIT_OTHER);
                static char tmp[EDITOR_MAX_TEXT];
                Normalize(clip, tmp, EDITOR_MAX_TEXT);
                Insert(e, tmp, (int)strlen(tmp));
                moved = true;
            }
        }
        if (ctrl && IsKeyPressed(KEY_X) && HasSelection(e))
        {
            PushUndo(e, EDIT_OTHER);
            static char tmp[EDITOR_MAX_TEXT];
            int n = SelMax(e) - SelMin(e);
            memcpy(tmp, e->text + SelMin(e), (size_t)n);
            tmp[n] = '\0';
            SetClipboardText(tmp);
            DeleteRange(e, SelMin(e), SelMax(e));
            moved = true;
        }
        if (ctrl && IsKeyPressed(KEY_Z) && e->undoCount > 0)
        {
            e->undoCount--;
            memcpy(e->text, e->undo[e->undoCount], sizeof(e->text));
            e->caret = e->anchor = e->undoCaret[e->undoCount];
            if (e->caret > Len(e)) e->caret = e->anchor = Len(e);
            e->changed = true;
            e->lastEditKind = 0;
            moved = true;
        }
    }
    else while (GetCharPressed() > 0) { }

    if (ctrl && IsKeyPressed(KEY_C) && HasSelection(e))
    {
        static char tmp[EDITOR_MAX_TEXT];
        int n = SelMax(e) - SelMin(e);
        memcpy(tmp, e->text + SelMin(e), (size_t)n);
        tmp[n] = '\0';
        SetClipboardText(tmp);
    }
    if (ctrl && IsKeyPressed(KEY_A)) { e->anchor = 0; e->caret = Len(e); moved = true; }

    // Caret movement
    if (Repeat(KEY_LEFT))
    {
        int p = e->caret;
        if (HasSelection(e) && !shift) p = SelMin(e);
        else if (p > 0) { p--; if (ctrl) { while (p > 0 && !IsWord(e->text[p])) p--; while (p > 0 && IsWord(e->text[p - 1])) p--; } }
        MoveCaret(e, p, shift, false);
        moved = true;
    }
    if (Repeat(KEY_RIGHT))
    {
        int p = e->caret, len = Len(e);
        if (HasSelection(e) && !shift) p = SelMax(e);
        else if (p < len) { p++; if (ctrl) { while (p < len && IsWord(e->text[p])) p++; while (p < len && !IsWord(e->text[p]) && e->text[p] != '\n') p++; } }
        MoveCaret(e, p, shift, false);
        moved = true;
    }
    int visibleLines = (int)(b.height/LINE_H) - 1;
    int lineMove = 0;
    if (Repeat(KEY_UP)) lineMove = -1;
    if (Repeat(KEY_DOWN)) lineMove = 1;
    if (Repeat(KEY_PAGE_UP)) lineMove = -visibleLines;
    if (Repeat(KEY_PAGE_DOWN)) lineMove = visibleLines;
    if (lineMove != 0)
    {
        int line = LineOf(e, e->caret) + lineMove;
        int lines = LineCount(e);
        if (line < 0) { MoveCaret(e, 0, shift, false); }
        else if (line >= lines) { MoveCaret(e, Len(e), shift, false); }
        else MoveCaret(e, PosAt(e, line, e->wantCol), shift, true);
        moved = true;
    }
    if (IsKeyPressed(KEY_HOME))
    {
        if (ctrl) MoveCaret(e, 0, shift, false);
        else
        {
            // First press: to the first non-space character; again: to column 0
            int ls = LineStart(e, e->caret), first = ls;
            while (e->text[first] == ' ') first++;
            MoveCaret(e, (e->caret == first) ? ls : first, shift, false);
        }
        moved = true;
    }
    if (IsKeyPressed(KEY_END)) { MoveCaret(e, ctrl ? Len(e) : LineEnd(e, e->caret), shift, false); moved = true; }

    if (moved || e->changed)
    {
        e->blinkStamp = GetTime();
        if (e->changed) e->wantCol = e->caret - LineStart(e, e->caret);
        KeepCaretVisible(e, b);
    }
    float max = MaxScroll(e, b);
    if (e->scroll.offset > max) e->scroll.offset = max;
    if (e->scroll.offset < 0) e->scroll.offset = 0;
}

//----------------------------------------------------------------------------------
// Draw
//----------------------------------------------------------------------------------
void EditorDraw(CodeEditor *e, Rectangle b, EditorMarks marks)
{
    DrawRectangleRounded(b, 0.03f, 6, EDITOR_BG);
    DrawRectangleRoundedLinesEx(b, 0.03f, 6, 1.0f, e->focused ? EDITOR_FOCUS : EDITOR_BORDER);

    float cw = CharW();
    Rectangle area = TextArea(e, b);
    int lines = LineCount(e);
    int first = (int)((e->scroll.offset - PAD)/LINE_H);
    if (first < 0) first = 0;
    int last = (int)((e->scroll.offset + b.height)/LINE_H) + 1;
    if (last > lines) last = lines;

    UiBeginScissor((Rectangle){ b.x + 1, b.y + 1, b.width - 2, b.height - 2 });

    int selA = SelMin(e), selB = SelMax(e);
    int pos = PosOfLine(e, first);
    for (int line = first; line < last; line++)
    {
        float y = b.y + PAD - e->scroll.offset + line*LINE_H;
        int ls = pos, le = LineEnd(e, pos);
        int n = le - ls;

        // Line bands: error, then about-to-run and just-ran
        Rectangle band = { b.x + 1, y - 1, b.width - 2, LINE_H };
        if (marks.prevLine == line + 1 && marks.prevLine != marks.execLine) DrawRectangleRec(band, PREV_BAND);
        if (marks.execLine == line + 1) DrawRectangleRec(band, EXEC_BAND);
        if (marks.errorLine == line + 1) DrawRectangleRec(band, ERROR_BAND);

        // Gutter: line number, or an arrow for the line about to run
        if (!e->singleLineFeel)
        {
            const char *num = TextFormat("%d", line + 1);
            Vector2 ns = MeasureTextEx(monoFont, num, 14, 0);
            Color nc = (marks.execLine == line + 1) ? EXEC_MARK : (marks.errorLine == line + 1) ? (Color){ 248, 81, 73, 255 } : GUTTER_TEXT;
            DrawTextEx(monoFont, num, (Vector2){ b.x + GUTTER - 10 - ns.x, y + 2 }, 14, 0, nc);
            if (marks.execLine == line + 1)
            {
                float ax = b.x + 4, ay = y + LINE_H/2 - 1;
                DrawTriangle((Vector2){ ax, ay - 5 }, (Vector2){ ax, ay + 5 }, (Vector2){ ax + 7, ay }, EXEC_MARK);
            }
        }

        // Selection on this line
        if (selA != selB && selA <= le && selB >= ls)
        {
            int a = selA > ls ? selA : ls, z = selB < le ? selB : le;
            float x0 = area.x + PAD - e->scrollX + (a - ls)*cw;
            float x1 = area.x + PAD - e->scrollX + (z - ls)*cw + ((selB > le) ? cw*0.5f : 0);
            DrawRectangleRec((Rectangle){ x0, y, x1 - x0, LINE_H - 2 }, SELECTION);
        }

        // Text, in runs of the same colour
        if (n > 0)
        {
            static unsigned char cls[EDITOR_MAX_TEXT];
            CodeClassify(e->text + ls, n, cls);
            char run[EDITOR_MAX_TEXT];
            int i = 0;
            while (i < n)
            {
                int j = i;
                while (j < n && cls[j] == cls[i]) j++;
                memcpy(run, e->text + ls + i, (size_t)(j - i));
                run[j - i] = '\0';
                float x = area.x + PAD - e->scrollX + i*cw;
                if (x < b.x + b.width && x + (j - i)*cw > area.x)
                    DrawTextEx(monoFont, run, (Vector2){ x, y + 1 }, FONT_SIZE, 0, CodeClassColor(cls[i]));
                i = j;
            }
        }

        // Caret
        if (e->focused && e->caret >= ls && e->caret <= le && ((int)((GetTime() - e->blinkStamp)*2.0) % 2 == 0))
        {
            float cx = area.x + PAD - e->scrollX + (e->caret - ls)*cw;
            DrawRectangle((int)cx, (int)y, 2, (int)LINE_H - 2, (Color){ 230, 237, 243, 255 });
        }

        pos = le + 1;
        if (e->text[le] == '\0') break;
    }
    EndScissorMode();

    // Gutter divider and scroll bar
    if (!e->singleLineFeel) DrawLine((int)(b.x + GUTTER - 3), (int)b.y + 6, (int)(b.x + GUTTER - 3), (int)(b.y + b.height - 6), EDITOR_BORDER);
    Rectangle track = { b.x + b.width - 8, b.y + 4, 5, b.height - 8 };
    UiScrollDraw(&e->scroll, track, b.height, MaxScroll(e, b));
}
