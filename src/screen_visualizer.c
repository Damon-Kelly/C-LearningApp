/**********************************************************************************************
*
*   C Learning App - Visualizer screen
*
*   Write (or load) a C program, press Run, then step through it and watch memory: the stack
*   with one box per function call, the heap with every malloc'd block, and arrows for
*   pointers. The program runs in our own interpreter (cinterp.c), which records a snapshot
*   before every statement, so stepping backwards is as easy as stepping forwards.
*
*   Layout on the 960x540 canvas:
*     left   code editor, the program's input, the step controls and the timeline
*     right  memory (stack and heap), a status line, and the program's output
*
**********************************************************************************************/

#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "editor.h"
#include "cinterp.h"
#include "codepanel.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>     // abs

//----------------------------------------------------------------------------------
// Layout
//----------------------------------------------------------------------------------
static const Rectangle codeRect    = { 16, 54, 452, 318 };
static const Rectangle inputRect   = { 16, 398, 452, 46 };
static const Rectangle runButton   = { 16, 454, 92, 34 };
static const Rectangle prevButton  = { 114, 454, 80, 34 };
static const Rectangle nextButton  = { 200, 454, 80, 34 };
static const Rectangle playButton  = { 286, 454, 80, 34 };
static const Rectangle speedButton = { 372, 454, 46, 34 };
static const Rectangle sliderRect  = { 16, 504, 452, 22 };
static const Rectangle memRect     = { 484, 54, 460, 318 };
static const Rectangle statusRect  = { 484, 377, 460, 62 };
static const Rectangle outRect     = { 484, 444, 460, 84 };
static const Rectangle examplesButton = { 620, 10, 150, 32 };
static const Rectangle backButton  = { 784, 10, 160, 32 };

#define COL_W      206.0f
#define COL_GAP    22.0f
#define ROW_H      24.0f
#define VALUE_W    72.0f
#define ADDR_W     44.0f

#define PTR_COLOR      ((Color){ 88, 166, 255, 255 })
#define DANGLING       ((Color){ 248, 81, 73, 255 })
#define CHANGED_COLOR  ((Color){ 255, 166, 60, 255 })
#define EXEC_COLOR     ((Color){ 240, 200, 80, 255 })

//----------------------------------------------------------------------------------
// Examples
//----------------------------------------------------------------------------------
typedef struct Example { const char *title; const char *code; const char *input; } Example;

static const Example examples[] = {
    { "Pointers: swap two ints",
      "#include <stdio.h>\n\nvoid swap(int *a, int *b) {\n    int t = *a;\n    *a = *b;\n    *b = t;\n}\n\nint main(void) {\n    int x = 1, y = 2;\n    swap(&x, &y);\n    printf(\"x = %d, y = %d\\n\", x, y);\n    return 0;\n}\n", "" },
    { "Arrays: walk with a pointer",
      "#include <stdio.h>\n\nint main(void) {\n    int a[5] = {3, 1, 4, 1, 5};\n    int *p = a;\n    int sum = 0;\n    for (int i = 0; i < 5; i++) {\n        sum += *p;\n        p++;\n    }\n    printf(\"sum = %d\\n\", sum);\n    return 0;\n}\n", "" },
    { "Recursion: factorial",
      "#include <stdio.h>\n\nint fact(int n) {\n    if (n <= 1) {\n        return 1;\n    }\n    return n * fact(n - 1);\n}\n\nint main(void) {\n    int r = fact(4);\n    printf(\"4! = %d\\n\", r);\n    return 0;\n}\n", "" },
    { "Strings: reverse in place",
      "#include <stdio.h>\n#include <string.h>\n\nint main(void) {\n    char s[] = \"hello\";\n    int i = 0;\n    int j = strlen(s) - 1;\n    while (i < j) {\n        char t = s[i];\n        s[i] = s[j];\n        s[j] = t;\n        i++;\n        j--;\n    }\n    printf(\"%s\\n\", s);\n    return 0;\n}\n", "" },
    { "Linked list: build, print, free",
      "#include <stdio.h>\n#include <stdlib.h>\n\nstruct Node {\n    int data;\n    struct Node *next;\n};\n\nint main(void) {\n    struct Node *head = NULL;\n    for (int i = 1; i <= 3; i++) {\n        struct Node *n = malloc(sizeof(struct Node));\n        n->data = i * 10;\n        n->next = head;\n        head = n;\n    }\n    for (struct Node *c = head; c != NULL; c = c->next) {\n        printf(\"%d \", c->data);\n    }\n    printf(\"\\n\");\n    while (head != NULL) {\n        struct Node *t = head;\n        head = head->next;\n        free(t);\n    }\n    return 0;\n}\n", "" },
    { "Structs on the heap",
      "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\ntypedef struct Player {\n    char name[8];\n    int score;\n} Player;\n\nint main(void) {\n    Player *team = malloc(2 * sizeof(Player));\n    strcpy(team[0].name, \"Ana\");\n    team[0].score = 7;\n    strcpy(team[1].name, \"Ben\");\n    team[1].score = 9;\n    Player *best = &team[0];\n    if (team[1].score > best->score) {\n        best = &team[1];\n    }\n    printf(\"Best: %s\\n\", best->name);\n    free(team);\n    return 0;\n}\n", "" },
    { "Reading input with scanf",
      "#include <stdio.h>\n\nint main(void) {\n    int n;\n    scanf(\"%d\", &n);\n    int total = 0;\n    for (int i = 0; i < n; i++) {\n        int x;\n        scanf(\"%d\", &x);\n        total += x;\n    }\n    printf(\"total = %d\\n\", total);\n    return 0;\n}\n", "3\n4 8 15\n" },
    { "Bug: use after free",
      "#include <stdio.h>\n#include <stdlib.h>\n\nint main(void) {\n    int *scores = malloc(3 * sizeof(int));\n    scores[0] = 90;\n    scores[1] = 75;\n    scores[2] = 82;\n    free(scores);\n    printf(\"%d\\n\", scores[0]);\n    return 0;\n}\n", "" },
    { "Bug: off by one",
      "#include <stdio.h>\n\nint main(void) {\n    int a[4] = {2, 4, 6, 8};\n    int total = 0;\n    for (int i = 0; i <= 4; i++) {\n        total += a[i];\n    }\n    printf(\"%d\\n\", total);\n    return 0;\n}\n", "" },
    { "Bug: dangling pointer",
      "#include <stdio.h>\n\nint *makeNumber(void) {\n    int n = 42;\n    return &n;\n}\n\nint main(void) {\n    int *p = makeNumber();\n    printf(\"%d\\n\", *p);\n    return 0;\n}\n", "" },
    { "Bug: memory leak",
      "#include <stdlib.h>\n\nint main(void) {\n    int *a = malloc(4 * sizeof(int));\n    a[0] = 1;\n    a = malloc(8 * sizeof(int));\n    a[0] = 2;\n    free(a);\n    return 0;\n}\n", "" },
};
#define EXAMPLE_COUNT ((int)(sizeof(examples)/sizeof(examples[0])))

//----------------------------------------------------------------------------------
// State
//----------------------------------------------------------------------------------
static GameScreen finishScreen = UNKNOWN;
static GameScreen returnTo = SECTIONS;

static CodeEditor codeEd;               // static: the code is kept between visits
static CodeEditor inputEd;
static bool started = false;

static char preloadCode[EDITOR_MAX_TEXT];
static char preloadInput[1024];
static bool preloadPending = false;

static CiRun prog;
static bool haveRun = false;
static int step = 0, prevStep = -1;
static double stepTime = 0;
static bool playing = false;
static double playTimer = 0;
static int speedIndex = 0;
static const float speeds[] = { 0.7f, 0.35f, 0.12f };
static const char *speedNames[] = { "1x", "2x", "5x" };
static bool sliderDrag = false;
static bool examplesOpen = false;
static UiScroll memScroll;
static float memContentHeight = 0;

//----------------------------------------------------------------------------------
// Opening from elsewhere
//----------------------------------------------------------------------------------
// A lesson snippet (no main) gets the usual includes and a main around it
static void MakeRunnable(const char *code, char *out, int cap)
{
    if (strstr(code, "main(") != NULL) { snprintf(out, (size_t)cap, "%s", code); return; }
    int n = snprintf(out, (size_t)cap, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\nint main(void) {\n");
    const char *p = code;
    while (*p && n < cap - 40)
    {
        n += snprintf(out + n, (size_t)(cap - n), "    ");
        while (*p && *p != '\n' && n < cap - 40) out[n++] = *p++;
        out[n++] = '\n';
        if (*p == '\n') p++;
    }
    snprintf(out + n, (size_t)(cap - n), "    return 0;\n}\n");
}

void OpenVisualizer(const char *code, const char *input, GameScreen backTo)
{
    MakeRunnable(code, preloadCode, sizeof(preloadCode));
    snprintf(preloadInput, sizeof(preloadInput), "%s", input ? input : "");
    preloadPending = true;
    returnTo = backTo;
}

void OpenVisualizerHome(void)
{
    preloadPending = false;
    returnTo = SECTIONS;
}

//----------------------------------------------------------------------------------
// Running and stepping
//----------------------------------------------------------------------------------
static const CiSnapshot *Snap(int i) { return (haveRun && i >= 0 && i < prog.stepCount) ? &prog.steps[i] : NULL; }

static void GoToStep(int s)
{
    if (!haveRun || prog.stepCount == 0) return;
    if (s < 0) s = 0;
    if (s > prog.stepCount - 1) s = prog.stepCount - 1;
    if (s == step) return;
    prevStep = step;
    step = s;
    stepTime = GetTime();
    const CiSnapshot *sn = Snap(step);
    if (sn && sn->line > 0) EditorShowLine(&codeEd, codeRect, sn->line);
}

static void RunProgram(void)
{
    if (haveRun) CiFree(&prog);
    CiExecute(codeEd.text, inputEd.text, &prog);
    haveRun = true;
    step = 0;
    prevStep = -1;
    stepTime = GetTime();
    playing = false;
    memScroll.offset = 0;
    if (prog.compileError) EditorShowLine(&codeEd, codeRect, prog.errorLine);
    else if (prog.stepCount > 0) EditorShowLine(&codeEd, codeRect, prog.steps[0].line);
}

static void ClearRun(void)
{
    if (haveRun) CiFree(&prog);
    haveRun = false;
    playing = false;
}

static void LoadExample(int i)
{
    EditorSetText(&codeEd, examples[i].code);
    EditorSetText(&inputEd, examples[i].input);
    RunProgram();
}

//----------------------------------------------------------------------------------
// Init / Unload
//----------------------------------------------------------------------------------
void InitVisualizerScreen(void)
{
    finishScreen = UNKNOWN;
    examplesOpen = false;
    inputEd.singleLineFeel = true;
    if (preloadPending)
    {
        preloadPending = false;
        EditorSetText(&codeEd, preloadCode);
        EditorSetText(&inputEd, preloadInput);
        RunProgram();
    }
    else if (!started) LoadExample(0);
    else if (!haveRun) RunProgram();
    started = true;
    codeEd.focused = false;
}

void UnloadVisualizerScreen(void)
{
    playing = false;
}

GameScreen FinishVisualizerScreen(void) { return finishScreen; }

//----------------------------------------------------------------------------------
// Update
//----------------------------------------------------------------------------------
static void UpdateSlider(void)
{
    if (!haveRun || prog.stepCount < 2) return;
    Vector2 m = GetMousePosition();
    Rectangle hit = { sliderRect.x - 6, sliderRect.y - 4, sliderRect.width + 12, sliderRect.height + 8 };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, hit)) { sliderDrag = true; playing = false; }
    if (sliderDrag)
    {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) sliderDrag = false;
        float t = (m.x - sliderRect.x)/sliderRect.width;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        GoToStep((int)(t*(prog.stepCount - 1) + 0.5f));
    }
}

void UpdateVisualizerScreen(void)
{
    // The examples menu takes all clicks while it is open
    if (examplesOpen)
    {
        for (int i = 0; i < EXAMPLE_COUNT; i++)
        {
            Rectangle r = { examplesButton.x - 130, examplesButton.y + 38 + i*30.0f, 280, 28 };
            if (UiButtonClicked(r)) { LoadExample(i); examplesOpen = false; return; }
        }
        if (IsKeyPressed(KEY_ESCAPE) || (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(GetMousePosition(), examplesButton))) examplesOpen = false;
        while (GetCharPressed() > 0) { }
        return;
    }

    EditorUpdate(&codeEd, codeRect);
    EditorUpdate(&inputEd, inputRect);
    if (codeEd.changed || inputEd.changed) ClearRun();       // the old steps no longer match the code

    bool typing = codeEd.focused || inputEd.focused;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (UiButtonClicked(runButton) || IsKeyPressed(KEY_F5) || (ctrl && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R))))
    {
        RunProgram();
        codeEd.focused = inputEd.focused = false;
    }

    bool canStep = haveRun && prog.stepCount > 0;
    if (canStep)
    {
        if (UiButtonClicked(nextButton) || (!typing && (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))) || IsKeyPressed(KEY_F10)) { GoToStep(step + 1); playing = false; }
        if (UiButtonClicked(prevButton) || (!typing && (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))) || IsKeyPressed(KEY_F9)) { GoToStep(step - 1); playing = false; }
        if (!typing && IsKeyPressed(KEY_HOME)) GoToStep(0);
        if (!typing && IsKeyPressed(KEY_END)) GoToStep(prog.stepCount - 1);
        if (UiButtonClicked(playButton) || (!typing && IsKeyPressed(KEY_SPACE)))
        {
            playing = !playing;
            if (playing && step >= prog.stepCount - 1) GoToStep(0);
            playTimer = 0;
        }
        UpdateSlider();
    }
    if (UiButtonClicked(speedButton)) speedIndex = (speedIndex + 1)%3;

    if (playing)
    {
        playTimer += GetFrameTime();
        if (playTimer >= speeds[speedIndex])
        {
            playTimer = 0;
            if (step >= prog.stepCount - 1) playing = false;
            else GoToStep(step + 1);
        }
    }

    // Memory panel scrolling (wheel only while the mouse is over it)
    bool overMem = CheckCollisionPointRec(GetMousePosition(), memRect);
    float maxScroll = memContentHeight - (memRect.height - 30);
    if (maxScroll < 0) maxScroll = 0;
    UiScrollUpdateEx(&memScroll, (Rectangle){ memRect.x + memRect.width - 8, memRect.y + 28, 5, memRect.height - 34 }, memRect.height - 30, maxScroll, overMem ? 40.0f : 0.0f, false);

    if (UiButtonClicked(examplesButton)) examplesOpen = true;
    if (UiButtonClicked(backButton) || (!typing && IsKeyPressed(KEY_ESCAPE))) finishScreen = returnTo;
    if (typing && IsKeyPressed(KEY_ESCAPE)) codeEd.focused = inputEd.focused = false;
}

//----------------------------------------------------------------------------------
// Memory drawing
//----------------------------------------------------------------------------------
typedef struct Drawn { int addr, size; Rectangle r; int column; } Drawn;
typedef struct Arrow { Rectangle from; long target; const CiType *base; int column; } Arrow;

#define MAX_DRAWN 900
#define MAX_ARROWS 300
static Drawn drawn[MAX_DRAWN];
static int drawnCount = 0;
static Arrow arrows[MAX_ARROWS];
static int arrowCount = 0;

static const CiSnapshot *cur = NULL, *prev = NULL;
static bool drawing = true;          // false: only measuring heights

static void Register(int addr, int size, Rectangle r, int column)
{
    if (!drawing || drawnCount >= MAX_DRAWN) return;
    drawn[drawnCount++] = (Drawn){ addr, size, r, column };
}

static bool IsScalar(const CiType *t) { return t->kind != CI_ARRAY && t->kind != CI_STRUCT; }

static float Glow(void)
{
    if (abs(step - prevStep) != 1) return 0.0f;     // glow only when stepping one line, not when jumping
    float age = (float)(GetTime() - stepTime);
    return (age < 0.7f) ? 1.0f - age/0.7f : 0.0f;
}

static bool ValueChanged(int addr, const CiType *t)
{
    if (prev == NULL) return false;
    char a[48], b[48];
    CiFormatValue(&prog, cur, addr, t, a, sizeof(a));
    CiFormatValue(&prog, prev, addr, t, b, sizeof(b));
    return strcmp(a, b) != 0;
}

static void MonoText(const char *s, float x, float y, float size, Color c)
{
    DrawTextEx(monoFont, s, (Vector2){ roundf(x), roundf(y) }, size, 0, c);
}

static void MonoFit(const char *s, float x, float y, float size, float maxW, Color c)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", s);
    while (strlen(buf) > 1 && MeasureTextEx(monoFont, buf, size, 0).x > maxW) buf[strlen(buf) - 1] = '\0';
    MonoText(buf, x, y, size, c);
}

// One value box (int, char, pointer...)
static void DrawCell(Rectangle box, int addr, const CiType *t, int column, bool compact)
{
    Register(addr, t->size, box, column);
    if (!drawing) return;

    char text[48];
    CiFormatValue(&prog, cur, addr, t, text, sizeof(text));
    bool isPtr = (t->kind == CI_PTR);
    bool unset = (strcmp(text, "?") == 0);
    bool changed = ValueChanged(addr, t);

    float glow = changed ? Glow() : 0.0f;
    if (glow > 0) DrawRectangleRounded((Rectangle){ box.x - 3, box.y - 3, box.width + 6, box.height + 6 }, 0.3f, 6, Fade(CHANGED_COLOR, 0.45f*glow));
    DrawRectangleRounded(box, 0.2f, 6, isPtr ? (Color){ 18, 32, 52, 255 } : (Color){ 13, 17, 23, 255 });
    DrawRectangleRoundedLinesEx(box, 0.2f, 6, changed ? 2.0f : 1.0f, changed ? CHANGED_COLOR : (Color){ 72, 80, 92, 255 });

    float size = compact ? 12.0f : 14.0f;
    Color tc = unset ? UI_MUTED : (isPtr ? PTR_COLOR : UI_TEXT);
    if (isPtr && strcmp(text, "NULL") != 0 && !unset)
    {
        // A pointer: a dot where the arrow starts, and the address in small text
        long target = 0;
        unsigned char raw[8];
        bool init;
        if (CiSnapRead(cur, addr, 8, raw, &init)) memcpy(&target, raw, 8);
        if (arrowCount < MAX_ARROWS) arrows[arrowCount++] = (Arrow){ box, target, t->base, column };
        DrawCircle((int)(box.x + box.width - 8), (int)(box.y + box.height/2), 3.5f, PTR_COLOR);
        Vector2 ts = MeasureTextEx(monoFont, text, size - 1, 0);
        MonoText(text, box.x + (box.width - 12 - ts.x)/2.0f, box.y + (box.height - ts.y)/2.0f, size - 1, Fade(PTR_COLOR, 0.8f));
        return;
    }
    Vector2 ts = MeasureTextEx(monoFont, text, size, 0);
    if (ts.x > box.width - 4) { MonoFit(text, box.x + 2, box.y + (box.height - ts.y)/2.0f, size, box.width - 4, tc); return; }
    MonoText(text, box.x + (box.width - ts.x)/2.0f, box.y + (box.height - ts.y)/2.0f, size, tc);
}

static float DrawVar(float x, float y, float w, const char *label, const CiType *t, int addr, int column);

// Arrays of scalars as a row of cells with small indexes under them
static float DrawCells(float x, float y, float w, const CiType *t, int addr, int column)
{
    const CiType *e = t->base;
    float cellW = (e->kind == CI_CHAR) ? 26.0f : (e->kind == CI_PTR) ? 50.0f : (e->kind == CI_DOUBLE || e->kind == CI_FLOAT || e->kind == CI_LONG) ? 46.0f : 38.0f;
    int perRow = (int)((w - 10)/(cellW + 2));
    if (perRow < 1) perRow = 1;
    int n = t->len > 48 ? 48 : t->len;
    float cy = y;
    for (int i = 0; i < n; i++)
    {
        int col = i%perRow;
        if (i > 0 && col == 0) cy += 32;
        Rectangle box = { x + 8 + col*(cellW + 2), cy, cellW, 20 };
        DrawCell(box, addr + i*e->size, e, column, true);
        if (drawing)
        {
            const char *idx = TextFormat("%d", i);
            Vector2 is = MeasureTextEx(monoFont, idx, 10, 0);
            MonoText(idx, box.x + (box.width - is.x)/2, box.y + 21, 10, UI_MUTED);
        }
    }
    if (t->len > n && drawing) UiText(TextFormat("... %d more", t->len - n), (Vector2){ x + 8, cy + 34 }, 11, UI_MUTED);
    return (cy - y) + 34 + (t->len > n ? 16 : 0);
}

// One variable (any type). Returns the height used.
static float DrawVar(float x, float y, float w, const char *label, const CiType *t, int addr, int column)
{
    float labelW = w - VALUE_W - ADDR_W - 10;
    if (IsScalar(t))
    {
        Rectangle box = { x + w - VALUE_W - 4, y + 2, VALUE_W, 20 };
        if (drawing)
        {
            MonoFit(label, x + 6, y + 5, 13, labelW, UI_TEXT);
            UiText(TextFormat("@%d", addr), (Vector2){ x + w - VALUE_W - ADDR_W, y + 7 }, 10, UI_MUTED);
        }
        DrawCell(box, addr, t, column, false);
        return ROW_H;
    }

    // Arrays and structs: a header row, then the contents indented
    Rectangle header = { x, y, w, ROW_H };
    Register(addr, t->size, (Rectangle){ x + 2, y + 2, w - 4, 20 }, column);
    if (drawing)
    {
        MonoFit(label, x + 6, y + 5, 13, labelW + VALUE_W - 40, UI_TEXT);
        float lw = MeasureTextEx(monoFont, label, 13, 0).x;
        if (lw > labelW + VALUE_W - 40) lw = labelW + VALUE_W - 40;
        UiTextFit(t->name, (Vector2){ x + 12 + lw, y + 6 }, 11, w - lw - ADDR_W - 20, UI_MUTED);
        UiText(TextFormat("@%d", addr), (Vector2){ x + w - ADDR_W, y + 7 }, 10, UI_MUTED);
    }
    float h = ROW_H;
    (void)header;

    if (t->kind == CI_ARRAY)
    {
        if (IsScalar(t->base)) h += DrawCells(x, y + h, w, t, addr, column);
        else
        {
            int n = t->len > 12 ? 12 : t->len;
            for (int i = 0; i < n; i++) h += DrawVar(x + 10, y + h, w - 10, TextFormat("[%d]", i), t->base, addr + i*t->base->size, column);
            if (t->len > n) { if (drawing) UiText(TextFormat("... %d more", t->len - n), (Vector2){ x + 16, y + h + 2 }, 11, UI_MUTED); h += 16; }
        }
    }
    else
    {
        const CiStruct *s = &prog.structs[t->structIndex];
        for (int i = 0; i < s->fieldCount; i++)
        {
            // A thin guide line on the left shows which fields belong to the struct
            float fh = DrawVar(x + 10, y + h, w - 10, s->fields[i].name, s->fields[i].type, addr + s->fields[i].offset, column);
            if (drawing) DrawRectangle((int)x + 4, (int)(y + h), 2, (int)fh, Fade(UI_ACCENT2, 0.5f));
            h += fh;
        }
    }
    return h + 2;
}

// The contents of a heap block, without a header row (the block's title already names the type)
static float DrawContents(float x, float y, float w, const CiType *t, int addr, int column)
{
    if (t->kind == CI_STRUCT)
    {
        const CiStruct *st = &prog.structs[t->structIndex];
        float h = 2;
        for (int i = 0; i < st->fieldCount; i++)
            h += DrawVar(x, y + h, w, st->fields[i].name, st->fields[i].type, addr + st->fields[i].offset, column);
        return h + 2;
    }
    if (t->kind == CI_ARRAY && IsScalar(t->base)) return DrawCells(x, y + 4, w, t, addr, column) + 4;
    if (t->kind == CI_ARRAY)
    {
        float h = 2;
        int n = t->len > 12 ? 12 : t->len;
        for (int i = 0; i < n; i++) h += DrawVar(x, y + h, w, TextFormat("[%d]", i), t->base, addr + i*t->base->size, column);
        return h + 2;
    }
    return DrawVar(x, y + 2, w, "", t, addr, column) + 4;
}

// A titled box (a function call, the globals, or a heap block) around some variables
static float DrawBox(float x, float y, float w, const char *title, const char *right, Color border, Color titleColor, float contentH, bool faded)
{
    float h = contentH + 30;
    if (drawing)
    {
        DrawRectangleRounded((Rectangle){ x, y, w, h }, 0.04f, 6, faded ? Fade(UI_PANEL, 0.6f) : UI_PANEL);
        DrawRectangleRoundedLinesEx((Rectangle){ x, y, w, h }, 0.04f, 6, 1.5f, border);
        UiTextFit(title, (Vector2){ x + 8, y + 5 }, 14, w - 70, titleColor);
        if (right) { Vector2 rs = UiMeasureText(right, 11); UiText(right, (Vector2){ x + w - rs.x - 8, y + 8 }, 11, UI_MUTED); }
        DrawLine((int)x + 6, (int)y + 25, (int)(x + w - 6), (int)y + 25, Fade(border, 0.6f));
    }
    return h;
}

static float FrameContent(float x, float y, const CiFrame *f)
{
    float h = 4;
    for (int i = 0; i < f->varCount; i++)
    {
        const CiVar *v = &cur->vars[f->firstVar + i];
        h += DrawVar(x, y + h, COL_W - 8, v->name, v->type, v->addr, 0);
    }
    if (f->varCount == 0 && drawing) UiText("(no variables yet)", (Vector2){ x + 8, y + h + 2 }, 12, UI_MUTED);
    if (f->varCount == 0) h += 20;
    return h + 4;
}

// What a heap block holds: the type it was first stored as, or ints/chars as a guess
static const CiType *BlockType(const CiBlock *b, CiType *scratch)
{
    const CiType *e = b->hint;
    static CiType guessInt = { CI_INT, 4, 4, NULL, 0, -1, "int" };
    static CiType guessChar = { CI_CHAR, 1, 1, NULL, 0, -1, "char" };
    if (e == NULL) e = (b->size%4 == 0) ? &guessInt : &guessChar;
    int n = b->size/e->size;
    if (n <= 1 && b->size == e->size) return e;
    *scratch = (CiType){ CI_ARRAY, n*e->size, e->align, (CiType *)e, n, -1, "" };
    static char name[48];
    snprintf(name, sizeof(name), "%s[%d]", e->name, n);
    scratch->name = name;
    return scratch;
}

static float DrawMemoryColumns(float top)
{
    float sx = memRect.x + 8, hx = memRect.x + 8 + COL_W + COL_GAP;
    float sy = top, hy = top;
    bool finished = (cur->line == 0);

    // Stack column
    if (cur->globalCount > 0)
    {
        bool d = drawing; drawing = false;
        float ch = 4; for (int i = 0; i < cur->globalCount; i++) ch += DrawVar(sx + 4, 0, COL_W - 8, cur->vars[i].name, cur->vars[i].type, cur->vars[i].addr, 0);
        drawing = d;
        DrawBox(sx, sy, COL_W, "Globals", NULL, UI_BORDER, UI_MUTED, ch + 4, false);
        float h = 4;
        if (drawing) for (int i = 0; i < cur->globalCount; i++) h += DrawVar(sx + 4, sy + 30 + h, COL_W - 8, cur->vars[i].name, cur->vars[i].type, cur->vars[i].addr, 0);
        sy += ch + 34 + 8;
    }
    for (int f = 0; f < cur->frameCount; f++)
    {
        const CiFrame *fr = &cur->frames[f];
        bool current = (f == cur->frameCount - 1);
        bool d = drawing; drawing = false;
        float ch = FrameContent(sx + 4, 0, fr);
        drawing = d;
        DrawBox(sx, sy, COL_W, TextFormat("%s()", fr->func), TextFormat("line %d", fr->line),
                current ? UI_ACCENT : UI_BORDER, current ? UI_TEXT : UI_MUTED, ch, !current);
        if (current && drawing) DrawRectangleRounded((Rectangle){ sx - 1, sy + 4, 3, 18 }, 1.0f, 4, EXEC_COLOR);
        FrameContent(sx + 4, sy + 26, fr);
        sy += ch + 30 + 8;
    }
    if (cur->frameCount == 0 && drawing)
        UiText(finished ? "main has returned: the stack is empty" : "", (Vector2){ sx + 4, sy + 4 }, 12, UI_MUTED);

    // Heap column
    if (cur->blockCount == 0 && drawing) UiText("Nothing allocated with malloc", (Vector2){ hx + 4, hy + 4 }, 12, UI_MUTED);
    for (int i = 0; i < cur->blockCount; i++)
    {
        const CiBlock *b = &cur->blocks[i];
        bool freed = b->freeLine != 0;
        bool leaked = finished && !freed;
        CiType scratch;
        const CiType *t = BlockType(b, &scratch);

        char title[64], right[40];
        if (freed) snprintf(title, sizeof(title), "freed on line %d", b->freeLine);
        else if (leaked) snprintf(title, sizeof(title), "%s  (leaked!)", t->name);
        else snprintf(title, sizeof(title), "%s", t->name);
        snprintf(right, sizeof(right), "%d bytes @%d", b->size, b->addr);

        float ch;
        if (freed) ch = 20;
        else { bool d = drawing; drawing = false; ch = DrawContents(hx + 4, 0, COL_W - 8, t, b->addr, 1); drawing = d; }
        Color border = freed ? (Color){ 72, 80, 92, 255 } : leaked ? DANGLING : UI_ACCENT2;
        DrawBox(hx, hy, COL_W, title, right, border, freed ? UI_MUTED : (leaked ? DANGLING : UI_TEXT), ch, freed);
        // The whole block is an arrow target (a pointer to a struct points at the block)
        Register(b->addr, b->size, (Rectangle){ hx, hy, COL_W, ch + 30 }, 1);
        if (freed) { if (drawing) UiText("this memory is no longer yours", (Vector2){ hx + 10, hy + 31 }, 11, UI_MUTED); }
        else DrawContents(hx + 4, hy + 28, COL_W - 8, t, b->addr, 1);
        hy += ch + 30 + 8;
    }

    return (sy > hy ? sy : hy) - top;
}

static const Drawn *FindTarget(long target, const CiType *base)
{
    const Drawn *best = NULL;
    // 1. Exactly this address with exactly the pointed-to size (a struct, an int...)
    for (int i = 0; i < drawnCount; i++)
        if (drawn[i].addr == target && base && drawn[i].size == base->size) return &drawn[i];
    // 2. The smallest drawn thing containing the address
    for (int i = 0; i < drawnCount; i++)
        if (target >= drawn[i].addr && target < drawn[i].addr + drawn[i].size)
            if (best == NULL || drawn[i].size < best->size) best = &drawn[i];
    return best;
}

static void DrawArrowHeadAt(Vector2 tip, Vector2 from, Color c)
{
    Vector2 d = { tip.x - from.x, tip.y - from.y };
    float len = sqrtf(d.x*d.x + d.y*d.y);
    if (len < 0.001f) return;
    d.x /= len; d.y /= len;
    Vector2 n = { -d.y, d.x };
    Vector2 a = { tip.x - d.x*9 + n.x*5, tip.y - d.y*9 + n.y*5 };
    Vector2 b = { tip.x - d.x*9 - n.x*5, tip.y - d.y*9 - n.y*5 };
    DrawTriangle(tip, b, a, c);
    DrawTriangle(tip, a, b, c);
}

static void DrawArrows(void)
{
    for (int i = 0; i < arrowCount; i++)
    {
        const Arrow *a = &arrows[i];
        Vector2 p0 = { a->from.x + a->from.width - 8, a->from.y + a->from.height/2 };
        const Drawn *t = FindTarget(a->target, a->base);

        // Where does it point? A freed block or a finished function: dangling (red)
        bool dangling = false;
        if (t == NULL) dangling = true;
        else
        {
            for (int b = 0; b < cur->blockCount; b++)
                if (cur->blocks[b].freeLine && a->target >= cur->blocks[b].addr && a->target < cur->blocks[b].addr + cur->blocks[b].size) dangling = true;
        }
        Color c = dangling ? DANGLING : PTR_COLOR;

        if (t == NULL && CiRegion((int)a->target) == 3)
        {
            // A string literal (or other read-only data): show the text it points to
            char str[40] = "";
            int n = 0;
            unsigned char ch;
            bool init;
            while (n < 30 && CiSnapRead(cur, (int)a->target + n, 1, &ch, &init) && ch != 0) str[n++] = (char)ch;
            str[n] = '\0';
            Vector2 p3 = { p0.x + 22, p0.y + 16 };
            DrawLineEx(p0, p3, 2.0f, UI_GOOD);
            DrawArrowHeadAt(p3, p0, UI_GOOD);
            MonoText(TextFormat("\"%s\"", str), p3.x + 4, p3.y - 4, 12, (Color){ 206, 145, 120, 255 });
            continue;
        }
        if (t == NULL)
        {
            // Nothing drawn there: a short red stub
            Vector2 p3 = { p0.x + 26, p0.y - 10 };
            DrawLineEx(p0, p3, 2.0f, c);
            DrawArrowHeadAt(p3, p0, c);
            UiText(CiRegion((int)a->target) == 1 ? "gone" : "?", (Vector2){ p3.x + 3, p3.y - 8 }, 13, c);
            continue;
        }

        Rectangle r = t->r;
        bool sameColumn = (t->column == a->column) && fabsf((r.x + r.width/2) - (a->from.x + a->from.width/2)) < COL_W;
        Vector2 p3, c1, c2;
        if (!sameColumn && r.x > a->from.x)
        {
            p3 = (Vector2){ r.x, r.y + r.height/2 };
            float dx = (p3.x - p0.x)*0.5f;
            c1 = (Vector2){ p0.x + dx, p0.y };
            c2 = (Vector2){ p3.x - dx, p3.y };
        }
        else
        {
            // Same column: loop out to the right and come back into the target's right edge
            p3 = (Vector2){ r.x + r.width, r.y + r.height/2 };
            float bulge = 34 + fabsf(p3.y - p0.y)*0.15f;
            float right = (p0.x > p3.x ? p0.x : p3.x) + bulge;
            if (right > memRect.x + memRect.width - 10) right = memRect.x + memRect.width - 10;   // stay inside the panel
            c1 = (Vector2){ right, p0.y };
            c2 = (Vector2){ right, p3.y };
        }
        DrawSplineSegmentBezierCubic(p0, c1, c2, p3, 2.0f, Fade(c, 0.9f));
        DrawArrowHeadAt(p3, c2, c);
        DrawCircleV(p0, 3.5f, c);
    }
}

static void DrawMemoryPanel(void)
{
    DrawRectangleRounded(memRect, 0.03f, 6, UI_PANEL);
    DrawRectangleRoundedLinesEx(memRect, 0.03f, 6, 1.0f, UI_BORDER);
    UiText("Stack", (Vector2){ memRect.x + 12, memRect.y + 6 }, 15, UI_TEXT);
    UiText("one box per function call", (Vector2){ memRect.x + 58, memRect.y + 9 }, 11, UI_MUTED);
    UiText("Heap", (Vector2){ memRect.x + 8 + COL_W + COL_GAP + 4, memRect.y + 6 }, 15, UI_TEXT);
    UiText("malloc'd blocks", (Vector2){ memRect.x + 8 + COL_W + COL_GAP + 44, memRect.y + 9 }, 11, UI_MUTED);

    cur = Snap(step);
    prev = (step > 0) ? Snap(step - 1) : NULL;      // highlight what the line that just ran changed
    if (cur == NULL)
    {
        const char *msg = (haveRun && prog.compileError) ? "The program did not compile (see the message below)" : "Press Run to see memory";
        UiDrawTextCentered(msg, memRect.x + memRect.width/2, memRect.y + memRect.height/2 - 10, 15, UI_MUTED);
        memContentHeight = 0;
        return;
    }

    float top = memRect.y + 30 - memScroll.offset;
    drawnCount = 0;
    arrowCount = 0;
    UiBeginScissor((Rectangle){ memRect.x + 1, memRect.y + 26, memRect.width - 2, memRect.height - 27 });
    drawing = true;
    memContentHeight = DrawMemoryColumns(top) + 8;
    DrawArrows();
    EndScissorMode();

    float maxScroll = memContentHeight - (memRect.height - 30);
    UiScrollDraw(&memScroll, (Rectangle){ memRect.x + memRect.width - 8, memRect.y + 28, 5, memRect.height - 34 }, memRect.height - 30, maxScroll > 0 ? maxScroll : 0);
}

//----------------------------------------------------------------------------------
// Status line and output
//----------------------------------------------------------------------------------
static void DrawStatus(void)
{
    DrawRectangleRounded(statusRect, 0.15f, 6, UI_PANEL);
    Rectangle text = { statusRect.x + 12, statusRect.y + 4, statusRect.width - 24, statusRect.height - 4 };

    if (!haveRun)
    {
        UiDrawWrappedText("The code changed. Press Run (or Ctrl+Enter) to step through it.", text, 13, UI_MUTED);
        return;
    }
    if (prog.compileError)
    {
        DrawRectangleRoundedLinesEx(statusRect, 0.15f, 6, 1.0f, UI_BAD);
        UiDrawWrappedText(TextFormat("Line %d: %s", prog.errorLine, prog.error), text, 13, UI_BAD);
        return;
    }

    const CiSnapshot *s = Snap(step);
    bool last = (step == prog.stepCount - 1);
    if (last && prog.runtimeError)
    {
        DrawRectangleRoundedLinesEx(statusRect, 0.15f, 6, 1.0f, UI_BAD);
        UiDrawWrappedText(TextFormat("Line %d: %s", prog.errorLine, prog.error), text, 13, UI_BAD);
        return;
    }
    if (last && s->line == 0)
    {
        const char *msg = prog.leakedBlocks
            ? TextFormat("Finished, but %d block%s (%d bytes) %s never freed: a memory leak.", prog.leakedBlocks, prog.leakedBlocks == 1 ? "" : "s", prog.leakedBytes, prog.leakedBlocks == 1 ? "was" : "were")
            : (prog.warningCount ? TextFormat("Finished. %d warning%s: %s", prog.warningCount, prog.warningCount == 1 ? "" : "s", prog.warnings[0]) : "Finished: main returned and all memory was freed.");
        Color c = prog.leakedBlocks || prog.warningCount ? UI_WARN : UI_GOOD;
        DrawRectangleRoundedLinesEx(statusRect, 0.15f, 6, 1.0f, c);
        UiDrawWrappedText(msg, text, 13, c);
        return;
    }

    // A warning about the line just run takes priority, then the step's note, then the next line
    int justRan = (step > 0) ? prog.steps[step - 1].line : 0;
    for (int i = 0; i < prog.warningCount; i++)
        if (prog.warningLines[i] == justRan)
        {
            DrawRectangleRoundedLinesEx(statusRect, 0.15f, 6, 1.0f, UI_WARN);
            UiDrawWrappedText(TextFormat("Warning: %s", prog.warnings[i]), text, 13, UI_WARN);
            return;
        }
    const char *msg = s->note[0] ? TextFormat("%s.  Next: line %d.", s->note, s->line) : TextFormat("Next to run: line %d (highlighted in yellow). Step %d of %d.", s->line, step + 1, prog.stepCount);
    if (step == 0) msg = TextFormat("Ready at line %d. Press Step > (or the Right arrow) to run one line at a time.", s->line);
    UiDrawWrappedText(msg, text, 13, s->note[0] ? UI_ACCENT2 : UI_MUTED);
}

static void DrawOutput(void)
{
    DrawRectangleRounded(outRect, 0.06f, 6, UI_INPUT);
    DrawRectangleRoundedLinesEx(outRect, 0.06f, 6, 1.0f, UI_BORDER);
    UiText("Output", (Vector2){ outRect.x + 10, outRect.y + 5 }, 13, UI_MUTED);
    if (!haveRun || prog.compileError) return;

    const CiSnapshot *s = Snap(step);
    int len = s ? s->outputLen : 0;
    // Show the last lines that fit
    const float lh = 16;
    int maxLines = (int)((outRect.height - 24)/lh);
    int start = len, lines = 0;
    while (start > 0)
    {
        start--;
        if (prog.output[start] == '\n' && start < len - 1) { lines++; if (lines >= maxLines) { start++; break; } }
    }
    float y = outRect.y + 22;
    char line[256];
    int p = start;
    while (p < len)
    {
        int n = 0;
        while (p < len && prog.output[p] != '\n' && n < (int)sizeof(line) - 1) line[n++] = prog.output[p++];
        line[n] = '\0';
        if (p < len && prog.output[p] == '\n') p++;
        MonoFit(line, outRect.x + 10, y, 14, outRect.width - 20, (Color){ 200, 230, 200, 255 });
        y += lh;
    }
    if (len == 0) UiText("(nothing printed yet)", (Vector2){ outRect.x + 10, outRect.y + 24 }, 12, Fade(UI_MUTED, 0.7f));
}

//----------------------------------------------------------------------------------
// Draw
//----------------------------------------------------------------------------------
static void DrawControls(void)
{
    bool canStep = haveRun && prog.stepCount > 0;
    UiButtonDraw(runButton, "Run");
    UiButtonDrawEx(prevButton, "< Back", UI_MUTED, canStep && step > 0);
    UiButtonDrawEx(nextButton, "Step >", UI_MUTED, canStep && step < prog.stepCount - 1);
    UiButtonDrawEx(playButton, playing ? "Pause" : "Play", UI_MUTED, canStep);
    UiButtonDrawEx(speedButton, speedNames[speedIndex], UI_MUTED, true);
    if (canStep) UiText(TextFormat("%d/%d", step + 1, prog.stepCount), (Vector2){ 424, 464 }, 12, UI_MUTED);

    // Timeline: drag to scrub through the steps
    Rectangle track = { sliderRect.x, sliderRect.y + sliderRect.height/2 - 3, sliderRect.width, 6 };
    DrawRectangleRounded(track, 1.0f, 4, UI_PANEL2);
    if (canStep && prog.stepCount > 1)
    {
        float t = (float)step/(float)(prog.stepCount - 1);
        DrawRectangleRounded((Rectangle){ track.x, track.y, track.width*t, track.height }, 1.0f, 4, UI_ACCENT);
        if (prog.runtimeError) DrawCircle((int)(track.x + track.width), (int)(track.y + 3), 5, UI_BAD);
        float kx = track.x + track.width*t;
        bool hot = sliderDrag || CheckCollisionPointRec(GetMousePosition(), (Rectangle){ sliderRect.x - 6, sliderRect.y - 4, sliderRect.width + 12, sliderRect.height + 8 });
        DrawCircle((int)kx, (int)(track.y + 3), hot ? 9.0f : 7.0f, UI_TEXT);
        DrawCircle((int)kx, (int)(track.y + 3), hot ? 5.0f : 4.0f, UI_ACCENT);
    }
}

static void DrawExamplesMenu(void)
{
    float h = EXAMPLE_COUNT*30.0f + 10;
    Rectangle box = { examplesButton.x - 136, examplesButton.y + 34, 292, h };
    DrawRectangleRounded((Rectangle){ box.x + 4, box.y + 6, box.width, box.height }, 0.04f, 6, Fade(BLACK, 0.5f));
    DrawRectangleRounded(box, 0.04f, 6, UI_PANEL2);
    DrawRectangleRoundedLinesEx(box, 0.04f, 6, 1.0f, UI_BORDER);
    for (int i = 0; i < EXAMPLE_COUNT; i++)
    {
        Rectangle r = { examplesButton.x - 130, examplesButton.y + 38 + i*30.0f, 280, 28 };
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        if (hover) DrawRectangleRounded(r, 0.2f, 6, UI_ACCENT);
        bool bug = strncmp(examples[i].title, "Bug:", 4) == 0;
        UiText(examples[i].title, (Vector2){ r.x + 10, r.y + 5 }, 15, hover ? WHITE : (bug ? (Color){ 255, 140, 130, 255 } : UI_TEXT));
    }
}

void DrawVisualizerScreen(void)
{
    // Header
    UiText("Visualizer", (Vector2){ 16, 8 }, 28, UI_TEXT);
    UiText("Write C, run it, and watch every step in memory", (Vector2){ 182, 18 }, 14, UI_MUTED);
    DrawRectangleGradientH(16, 46, 928, 2, UI_ACCENT, UI_ACCENT2);
    UiButtonDrawEx(examplesButton, "Examples  v", UI_MUTED, true);
    UiButtonDrawEx(backButton, returnTo == LESSON ? "Back to lesson" : "Back", UI_MUTED, true);

    // Editor with the step markers
    EditorMarks marks = { 0, 0, 0 };
    if (haveRun)
    {
        if (prog.compileError) marks.errorLine = prog.errorLine;
        const CiSnapshot *s = Snap(step);
        if (s)
        {
            marks.execLine = s->line;
            if (step > 0) marks.prevLine = prog.steps[step - 1].line;
            if (prog.runtimeError && step == prog.stepCount - 1) { marks.errorLine = prog.errorLine; marks.execLine = 0; }
        }
    }
    EditorDraw(&codeEd, codeRect, marks);
    UiText("Input for scanf (optional)", (Vector2){ inputRect.x + 2, inputRect.y - 20 }, 13, UI_MUTED);
    UiText("Ctrl+Enter = Run   Right/Left = step", (Vector2){ inputRect.x + 236, inputRect.y - 20 }, 12, Fade(UI_MUTED, 0.8f));
    EditorDraw(&inputEd, inputRect, (EditorMarks){ 0, 0, 0 });

    DrawControls();
    DrawMemoryPanel();
    DrawStatus();
    DrawOutput();

    if (examplesOpen) DrawExamplesMenu();
}
