/**********************************************************************************************
*
*   C Learning App - coding challenge screen
*
*   The learner writes a function, presses Run tests, and every test runs on their code in the
*   Visualizer's interpreter. Each test shows passed / failed with the reason (a crash is
*   explained in plain words), and any visible test can be stepped through in the Visualizer.
*
*   Layout on the 960x540 canvas:
*     top    challenge title, level, Prev / Next / Back
*     left   code editor, Run tests / Hint / Reset / Solution
*     right  the task (with the hint and any rules), the tests and the selected test's details
*
*   Drafts are kept in memory per challenge while the app runs, so leaving to read a lesson or
*   open the Visualizer does not lose the code. Each graded run is saved as an attempt in the
*   progress file (topic key "challenge_<section>.txt#n", see ChallengeKey).
*
**********************************************************************************************/

#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "editor.h"
#include "challenge.h"
#include "content.h"
#include "progress.h"
#include "users.h"      // GetCurrentUser: drafts belong to one user
#include "resume.h"     // remembers the challenge, for "Continue where you left off"

#include <stdio.h>
#include <string.h>
#include <math.h>

//----------------------------------------------------------------------------------
// Layout
//----------------------------------------------------------------------------------
static const Rectangle codeRect      = { 16, 54, 452, 400 };
static const Rectangle runButton     = { 16, 462, 120, 34 };
static const Rectangle hintButton    = { 142, 462, 72, 34 };
static const Rectangle resetButton   = { 220, 462, 72, 34 };
static const Rectangle solveButton   = { 298, 462, 170, 34 };
static const Rectangle taskRect      = { 484, 54, 460, 176 };
static const Rectangle testsRect     = { 484, 238, 460, 290 };
static const Rectangle prevButton    = { 620, 10, 74, 32 };
static const Rectangle nextButton    = { 700, 10, 74, 32 };
static const Rectangle backButton    = { 784, 10, 160, 32 };

#define TEST_ROW_H   24.0f
#define SOLUTION_AFTER_TRIES 3

//----------------------------------------------------------------------------------
// The challenges of every section (loaded on first use, shared with the Topics and Progress screens)
//----------------------------------------------------------------------------------
static ChallengeSet sets[MAX_SECTIONS];
static bool setTried[MAX_SECTIONS];
static bool setLoaded[MAX_SECTIONS];

const ChallengeSet *SectionChallenges(int section)
{
    const SectionDef *s = GetSection(section);
    if (s == NULL || section >= MAX_SECTIONS || s->challengeFile[0] == '\0') return NULL;
    if (!setTried[section])
    {
        setTried[section] = true;
        char err[256];
        setLoaded[section] = LoadChallenges(ResourcePath(TextFormat("content/%s", s->challengeFile)), &sets[section], err, sizeof(err));
        if (!setLoaded[section]) TraceLog(LOG_WARNING, "CHALLENGE: %s", err);
    }
    return setLoaded[section] ? &sets[section] : NULL;
}

void UnloadAllChallenges(void)
{
    for (int i = 0; i < MAX_SECTIONS; i++)
    {
        if (setLoaded[i]) UnloadChallenges(&sets[i]);
        setLoaded[i] = setTried[i] = false;
    }
}

//----------------------------------------------------------------------------------
// State
//----------------------------------------------------------------------------------
int selectedChallenge = 0;

static GameScreen finishScreen = UNKNOWN;
static CodeEditor editor;
static int openSection = -1, openIndex = -1;     // the challenge in the editor

// Per challenge, for as long as the app runs
static char drafts[MAX_SECTIONS][MAX_CHALLENGES][EDITOR_MAX_TEXT];
static bool hasDraft[MAX_SECTIONS][MAX_CHALLENGES];
static int tries[MAX_SECTIONS][MAX_CHALLENGES];
static bool hintShown[MAX_SECTIONS][MAX_CHALLENGES];

static GradeResult grade;
static bool graded = false;
static bool showingSolution = false;
static int selectedTest = 0;
static UiScroll taskScroll, detailScroll;
static float taskContentH = 0, detailContentH = 0;
static double solvedTime = -100;                 // when all tests last passed (for the celebration)

// Confetti for a solved challenge
#define CONFETTI 90
typedef struct Bit { Vector2 p, v; float spin, angle; Color c; } Bit;
static Bit confetti[CONFETTI];

static const Challenge *Current(void)
{
    const ChallengeSet *set = SectionChallenges(selectedSection);
    if (set == NULL || selectedChallenge < 0 || selectedChallenge >= set->count) return NULL;
    return &set->items[selectedChallenge];
}

static int ChallengeCount(void)
{
    const ChallengeSet *set = SectionChallenges(selectedSection);
    return set ? set->count : 0;
}

static bool SolvedBefore(void)
{
    const SectionDef *s = GetSection(selectedSection);
    AttemptStats st;
    return s && ProgressGetStats(ATTEMPT_QUIZ, s->name, ChallengeKey(s->challengeFile, selectedChallenge), &st) && st.passed;
}

static bool SolutionUnlocked(void)
{
    return SolvedBefore() || tries[selectedSection][selectedChallenge] >= SOLUTION_AFTER_TRIES ||
           (graded && !grade.compileError && !grade.ruleBroken && grade.passedCount == Current()->testCount);
}

// Saves this challenge as the place to continue from
static void NoteResume(void)
{
    const SectionDef *s = GetSection(selectedSection);
    if (s != NULL && Current() != NULL) ResumeSetChallenge(s->name, selectedChallenge);
}

static const char *LevelName(int level)
{
    return level <= 1 ? "Warm-up" : level == 2 ? "Practice" : "Harder";
}

static Color LevelColor(int level)
{
    return level <= 1 ? UI_GOOD : level == 2 ? UI_ACCENT : UI_ACCENT2;
}

//----------------------------------------------------------------------------------
// Switching challenges, saving drafts
//----------------------------------------------------------------------------------
static void SaveDraft(void)
{
    if (openSection < 0 || openSection >= MAX_SECTIONS || openIndex < 0 || showingSolution) return;
    memcpy(drafts[openSection][openIndex], editor.text, EDITOR_MAX_TEXT);
    hasDraft[openSection][openIndex] = true;
}

static void OpenChallenge(int index)
{
    SaveDraft();
    showingSolution = false;
    selectedChallenge = index;
    const Challenge *c = Current();
    if (c == NULL) return;

    openSection = selectedSection;
    openIndex = index;
    editor.readOnly = false;
    EditorSetText(&editor, hasDraft[openSection][openIndex] ? drafts[openSection][openIndex] : c->starter);
    editor.focused = false;
    graded = false;
    selectedTest = 0;
    taskScroll.offset = detailScroll.offset = 0;
    solvedTime = -100;
}

static void ToggleSolution(void)
{
    const Challenge *c = Current();
    if (c == NULL) return;
    if (!showingSolution)
    {
        SaveDraft();
        showingSolution = true;
        EditorSetText(&editor, c->solution);
        editor.readOnly = true;
    }
    else
    {
        showingSolution = false;
        editor.readOnly = false;
        EditorSetText(&editor, drafts[openSection][openIndex]);
    }
    graded = false;
}

static void Celebrate(void)
{
    solvedTime = GetTime();
    for (int i = 0; i < CONFETTI; i++)
    {
        float a = (float)GetRandomValue(200, 340)*DEG2RAD;
        float speed = (float)GetRandomValue(260, 620);
        Color palette[] = { UI_ACCENT, UI_ACCENT2, UI_GOOD, UI_WARN, (Color){ 240, 240, 240, 255 } };
        confetti[i] = (Bit){ { (float)GetRandomValue(200, 760), 545 }, { cosf(a)*speed*0.6f, sinf(a)*speed },
                             (float)GetRandomValue(-400, 400), 0, palette[i%5] };
    }
}

static void RunTests(void)
{
    const Challenge *c = Current();
    if (c == NULL || showingSolution) return;

    GradeChallenge(c, editor.text, &grade);
    graded = true;
    tries[selectedSection][selectedChallenge]++;
    detailScroll.offset = 0;

    // Show the first failing test (the first test when everything passed)
    selectedTest = 0;
    if (!grade.compileError && !grade.ruleBroken)
        for (int t = 0; t < c->testCount; t++) if (!grade.tests[t].passed) { selectedTest = t; break; }
    if (grade.compileError && grade.errorLine > 0) EditorShowLine(&editor, codeRect, grade.errorLine);

    bool all = !grade.compileError && !grade.ruleBroken && grade.passedCount == c->testCount;
    if (all) Celebrate();

    // Code that compiled counts as an attempt (score = tests passed)
    if (!grade.compileError && !grade.ruleBroken)
    {
        QuestionResult results[MAX_CHALLENGE_TESTS];
        for (int t = 0; t < c->testCount; t++) results[t] = (QuestionResult){ c->tests[t].name, grade.tests[t].passed };
        const SectionDef *s = GetSection(selectedSection);
        ProgressRecordAttempt(ATTEMPT_QUIZ, s->name, ChallengeKey(s->challengeFile, selectedChallenge),
                              grade.passedCount, c->testCount, all, results, c->testCount);
    }
    SaveDraft();
}

static void StepThroughTest(int t)
{
    const Challenge *c = Current();
    if (c == NULL || c->tests[t].hidden) return;
    static char program[20000];
    ChallengeProgram(c, editor.text, t, program, sizeof(program));
    OpenVisualizer(program, "", CHALLENGE);
    finishScreen = VISUALIZER;
}

//----------------------------------------------------------------------------------
// Init / Unload
//----------------------------------------------------------------------------------
void InitChallengeScreen(void)
{
    finishScreen = UNKNOWN;

    // Someone else logged in: forget the previous user's drafts and tries
    static char draftsOwner[64] = "";
    if (strcmp(draftsOwner, GetCurrentUser()) != 0)
    {
        snprintf(draftsOwner, sizeof(draftsOwner), "%s", GetCurrentUser());
        memset(hasDraft, 0, sizeof(hasDraft));
        memset(tries, 0, sizeof(tries));
        memset(hintShown, 0, sizeof(hintShown));
        openSection = openIndex = -1;
    }
    // Coming back from the Visualizer keeps everything; a different challenge loads its draft
    if (openSection != selectedSection || openIndex != selectedChallenge) OpenChallenge(selectedChallenge);
    editor.focused = false;
    NoteResume();
}

void UnloadChallengeScreen(void)
{
    SaveDraft();
}

GameScreen FinishChallengeScreen(void) { return finishScreen; }

//----------------------------------------------------------------------------------
// Update
//----------------------------------------------------------------------------------
static Rectangle TestRowRect(int t)
{
    return (Rectangle){ testsRect.x + 8, testsRect.y + 30 + t*TEST_ROW_H, testsRect.width - 16, TEST_ROW_H - 2 };
}

static Rectangle DetailRect(void)
{
    const Challenge *c = Current();
    float top = testsRect.y + 30 + (c ? c->testCount : 0)*TEST_ROW_H + 6;
    return (Rectangle){ testsRect.x + 8, top, testsRect.width - 16, testsRect.y + testsRect.height - 8 - top };
}

static Rectangle StepButtonRect(void)
{
    Rectangle d = DetailRect();
    return (Rectangle){ d.x + d.width - 124, d.y + 2, 120, 24 };
}

static bool ShowsErrorBox(void) { return graded && (grade.compileError || grade.ruleBroken); }

void UpdateChallengeScreen(void)
{
    const Challenge *c = Current();
    if (c == NULL)
    {
        if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TOPICS;
        return;
    }

    EditorUpdate(&editor, codeRect);
    if (editor.changed && graded && !showingSolution) { graded = false; }    // old results no longer match the code

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (UiButtonClicked(runButton) || IsKeyPressed(KEY_F5) || (ctrl && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R))))
    {
        if (showingSolution) ToggleSolution();
        RunTests();
        editor.focused = false;
    }

    if (UiButtonClicked(hintButton)) hintShown[selectedSection][selectedChallenge] = !hintShown[selectedSection][selectedChallenge];
    if (UiButtonClicked(resetButton) && !showingSolution)
    {
        EditorSetText(&editor, c->starter);
        graded = false;
        SaveDraft();
    }
    if (UiButtonClicked(solveButton) && (showingSolution || SolutionUnlocked())) ToggleSolution();

    // Tests: click a row to see it; "Step through" opens it in the Visualizer
    if (!ShowsErrorBox())
    {
        for (int t = 0; t < c->testCount; t++)
            if (UiButtonClicked(TestRowRect(t))) { selectedTest = t; detailScroll.offset = 0; }
        if (!c->tests[selectedTest].hidden && UiButtonClicked(StepButtonRect())) StepThroughTest(selectedTest);
    }

    Rectangle d = DetailRect();
    bool overDetail = CheckCollisionPointRec(GetMousePosition(), d);
    float detailMax = fmaxf(0, detailContentH - (d.height - 30));
    UiScrollUpdateEx(&detailScroll, (Rectangle){ d.x + d.width - 5, d.y + 30, 4, d.height - 32 }, d.height - 30, detailMax, overDetail ? 36.0f : 0.0f, false);

    bool overTask = CheckCollisionPointRec(GetMousePosition(), taskRect);
    float taskMax = fmaxf(0, taskContentH - (taskRect.height - 34));
    UiScrollUpdateEx(&taskScroll, (Rectangle){ taskRect.x + taskRect.width - 8, taskRect.y + 30, 4, taskRect.height - 36 }, taskRect.height - 34, taskMax, overTask ? 36.0f : 0.0f, false);

    if (selectedChallenge > 0 && UiButtonClicked(prevButton)) { OpenChallenge(selectedChallenge - 1); NoteResume(); }
    if (selectedChallenge < ChallengeCount() - 1 && UiButtonClicked(nextButton)) { OpenChallenge(selectedChallenge + 1); NoteResume(); }

    if (editor.focused && IsKeyPressed(KEY_ESCAPE)) editor.focused = false;
    else if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TOPICS;

    // Confetti falls for a few seconds
    float dt = GetFrameTime();
    if (GetTime() - solvedTime < 4.0)
        for (int i = 0; i < CONFETTI; i++)
        {
            confetti[i].v.y += 700*dt;
            confetti[i].v.x *= 0.99f;
            confetti[i].p.x += confetti[i].v.x*dt;
            confetti[i].p.y += confetti[i].v.y*dt;
            confetti[i].angle += confetti[i].spin*dt;
        }
}

//----------------------------------------------------------------------------------
// Draw
//----------------------------------------------------------------------------------
static void Panel(Rectangle r, const char *title)
{
    DrawRectangleRounded(r, 0.04f, 6, UI_PANEL);
    DrawRectangleRoundedLinesEx(r, 0.04f, 6, 1.0f, UI_BORDER);
    UiText(title, (Vector2){ r.x + 12, r.y + 7 }, 15, UI_TEXT);
}

static void Mono(const char *s, float x, float y, float size, Color c)
{
    DrawTextEx(monoFont, s, (Vector2){ roundf(x), roundf(y) }, size, 0, c);
}

// One line of s (0-based) into out; returns false past the end
static bool GetLine(const char *s, int index, char *out, int cap)
{
    int line = 0;
    const char *p = s;
    while (line < index && *p) { if (*p == '\n') line++; p++; }
    if (line < index || (*p == '\0' && index > 0 && p > s && p[-1] == '\n')) return false;
    int n = 0;
    while (*p && *p != '\n' && n < cap - 1) out[n++] = *p++;
    out[n] = '\0';
    return true;
}

static int LineCount(const char *s)
{
    if (s == NULL || *s == '\0') return 0;
    int n = 1;
    for (const char *p = s; *p; p++) if (*p == '\n' && p[1] != '\0') n++;
    return n;
}

static void DrawHeader(const Challenge *c)
{
    UiTextFit(c->title, (Vector2){ 16, 9 }, 26, 340, UI_TEXT);
    float x = 16 + fminf(UiMeasureText(c->title, 26).x, 340) + 14;

    const char *lvl = LevelName(c->level);
    Vector2 ls = UiMeasureText(lvl, 13);
    Rectangle pill = { x, 15, ls.x + 18, 22 };
    Color lc = LevelColor(c->level);
    DrawRectangleRounded(pill, 1.0f, 8, Fade(lc, 0.15f));
    DrawRectangleRoundedLinesEx(pill, 1.0f, 8, 1.0f, Fade(lc, 0.6f));
    UiText(lvl, (Vector2){ pill.x + 9, pill.y + (pill.height - ls.y)/2 }, 13, lc);
    UiText(TextFormat("Challenge %d of %d", selectedChallenge + 1, ChallengeCount()), (Vector2){ pill.x + pill.width + 12, 18 }, 14, UI_MUTED);

    DrawRectangleGradientH(16, 46, 928, 2, UI_ACCENT2, UI_ACCENT);
    UiButtonDrawEx(prevButton, "< Prev", UI_MUTED, selectedChallenge > 0);
    bool solvedNow = graded && !ShowsErrorBox() && grade.passedCount == c->testCount;
    UiButtonDrawEx(nextButton, "Next >", solvedNow ? UI_GOOD : UI_MUTED, selectedChallenge < ChallengeCount() - 1);
    UiButtonDrawEx(backButton, "Back to topics", UI_MUTED, true);
}

static void DrawTask(const Challenge *c)
{
    Panel(taskRect, "Task");
    bool hint = hintShown[selectedSection][selectedChallenge];

    // Rules (forbidden text, no leaks) in one line under the title
    char rules[256] = "";
    for (int i = 0; i < c->forbidCount; i++)
        snprintf(rules + strlen(rules), sizeof(rules) - strlen(rules), "%s%s", rules[0] ? "  " : "", c->forbidWhy[i]);
    if (c->noLeaks) snprintf(rules + strlen(rules), sizeof(rules) - strlen(rules), "%sFree everything you malloc.", rules[0] ? "  " : "");

    const float w = taskRect.width - 34;
    float textH = UiMeasureWrappedText(c->text, w, 15);
    float rulesH = rules[0] ? UiMeasureWrappedText(TextFormat("Rule: %s", rules), w, 14) + 8 : 0;
    float hintH = hint ? UiMeasureWrappedText(TextFormat("Hint: %s", c->hint), w, 14) + 8 : 0;
    taskContentH = textH + rulesH + hintH + 4;

    Rectangle view = { taskRect.x + 12, taskRect.y + 30, taskRect.width - 24, taskRect.height - 34 };
    UiBeginScissor(view);
    float y = view.y - taskScroll.offset;
    UiDrawWrappedText(c->text, (Rectangle){ view.x, y, w, 2000 }, 15, UI_TEXT);
    y += textH + 8;
    if (rules[0]) { UiDrawWrappedText(TextFormat("Rule: %s", rules), (Rectangle){ view.x, y, w, 2000 }, 14, UI_WARN); y += rulesH; }
    if (hint) UiDrawWrappedText(TextFormat("Hint: %s", c->hint), (Rectangle){ view.x, y, w, 2000 }, 14, (Color){ 120, 190, 255, 255 });
    EndScissorMode();

    float max = fmaxf(0, taskContentH - view.height);
    if (max > 0) UiScrollDraw(&taskScroll, (Rectangle){ taskRect.x + taskRect.width - 8, taskRect.y + 30, 4, taskRect.height - 36 }, view.height, max);
}

static void DrawTick(Vector2 c, Color col)
{
    DrawLineEx((Vector2){ c.x - 5, c.y }, (Vector2){ c.x - 1, c.y + 4 }, 2.0f, col);
    DrawLineEx((Vector2){ c.x - 1, c.y + 4 }, (Vector2){ c.x + 6, c.y - 4 }, 2.0f, col);
}

static void DrawCross(Vector2 c, Color col)
{
    DrawLineEx((Vector2){ c.x - 4, c.y - 4 }, (Vector2){ c.x + 4, c.y + 4 }, 2.0f, col);
    DrawLineEx((Vector2){ c.x + 4, c.y - 4 }, (Vector2){ c.x - 4, c.y + 4 }, 2.0f, col);
}

// Expected and actual output side by side; the first different line is marked
static float DrawOutputs(const Challenge *c, const TestResult *r, float x, float y, float w, bool draw)
{
    const ChallengeTest *t = &c->tests[selectedTest];
    float colW = (w - 10)/2;
    if (draw)
    {
        UiText("Expected", (Vector2){ x, y }, 13, UI_MUTED);
        UiText(r ? "Your output" : "", (Vector2){ x + colW + 10, y }, 13, UI_MUTED);
    }
    y += 18;
    int lines = LineCount(t->expect);
    if (r && LineCount(r->got) > lines) lines = LineCount(r->got);
    if (r == NULL && lines == 0) lines = 1;
    char a[128], b[128];
    for (int i = 0; i < lines; i++)
    {
        bool hasA = i < LineCount(t->expect) && GetLine(t->expect, i, a, sizeof(a));
        bool hasB = r && i < LineCount(r->got) && GetLine(r->got, i, b, sizeof(b));
        if (draw)
        {
            bool diff = r && r->wrongOutput && (i + 1 == r->diffLine);
            if (diff) DrawRectangleRec((Rectangle){ x - 2, y - 1, w + 4, 16 }, Fade(UI_BAD, 0.16f));
            if (hasA) Mono(a, x, y, 13, (Color){ 200, 230, 200, 255 });
            if (r) Mono(hasB ? b : (i == 0 ? "(nothing)" : ""), x + colW + 10, y, 13, hasB ? (diff ? (Color){ 255, 170, 165, 255 } : (Color){ 200, 230, 200, 255 }) : UI_MUTED);
        }
        y += 16;
    }
    return y;
}

static float DrawDetailContent(const Challenge *c, Rectangle d, float y, bool draw)
{
    const ChallengeTest *t = &c->tests[selectedTest];
    const TestResult *r = graded ? &grade.tests[selectedTest] : NULL;
    float x = d.x + 4, w = d.width - 16;

    if (r && !r->passed)
    {
        float h = UiMeasureWrappedText(r->message, w, 14);
        if (draw) UiDrawWrappedText(r->message, (Rectangle){ x, y, w, h + 4 }, 14, (Color){ 255, 150, 140, 255 });
        y += h + 6;
    }
    else if (r && r->passed)
    {
        if (draw) UiText("Passed", (Vector2){ x, y }, 14, UI_GOOD);
        y += 22;
    }

    if (t->hidden)
    {
        const char *msg = "This is a hidden test: it checks a case the task describes, but its code and output are not shown.";
        float h = UiMeasureWrappedText(msg, w, 14);
        if (draw) UiDrawWrappedText(msg, (Rectangle){ x, y, w, h + 4 }, 14, UI_MUTED);
        return y + h + 4;
    }

    if (draw) UiText("The test runs", (Vector2){ x, y }, 13, UI_MUTED);
    y += 18;
    char line[160];
    for (int i = 0; GetLine(t->code, i, line, sizeof(line)); i++)
    {
        if (draw) Mono(line, x, y, 13, (Color){ 156, 220, 254, 255 });
        y += 16;
    }
    y += 6;
    return DrawOutputs(c, (r && (r->wrongOutput || r->passed)) ? r : NULL, x, y, w, draw);
}

static void DrawTests(const Challenge *c)
{
    Panel(testsRect, "Tests");
    if (graded && !ShowsErrorBox())
    {
        bool all = grade.passedCount == c->testCount;
        const char *s = TextFormat("%d of %d passed", grade.passedCount, c->testCount);
        Vector2 ss = UiMeasureText(s, 14);
        UiText(s, (Vector2){ testsRect.x + testsRect.width - 14 - ss.x, testsRect.y + 8 }, 14, all ? UI_GOOD : UI_WARN);
    }

    // The rows
    for (int t = 0; t < c->testCount; t++)
    {
        Rectangle r = TestRowRect(t);
        bool sel = (t == selectedTest) && !ShowsErrorBox();
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        if (sel || hover) DrawRectangleRounded(r, 0.3f, 6, sel ? UI_PANEL2 : Fade(UI_PANEL2, 0.5f));

        Vector2 icon = { r.x + 14, r.y + r.height/2 };
        if (!graded || ShowsErrorBox()) DrawCircleV(icon, 4, UI_BORDER);
        else if (grade.tests[t].passed) DrawTick(icon, UI_GOOD);
        else DrawCross(icon, UI_BAD);

        const char *name = c->tests[t].hidden ? TextFormat("%s  (hidden)", c->tests[t].name) : c->tests[t].name;
        UiTextFit(name, (Vector2){ r.x + 30, r.y + 3 }, 15, r.width - 40, c->tests[t].hidden ? UI_MUTED : UI_TEXT);
    }

    Rectangle d = DetailRect();
    DrawRectangleRounded(d, 0.04f, 6, UI_INPUT);
    DrawRectangleRoundedLinesEx(d, 0.04f, 6, 1.0f, UI_BORDER);

    // A compile error or a broken rule replaces the details
    if (ShowsErrorBox())
    {
        UiText(grade.ruleBroken ? "Rule broken" : "Your code does not compile", (Vector2){ d.x + 10, d.y + 7 }, 15, UI_BAD);
        UiDrawWrappedText(grade.message, (Rectangle){ d.x + 10, d.y + 32, d.width - 20, d.height - 36 }, 14, (Color){ 255, 170, 165, 255 });
        detailContentH = 0;
        return;
    }

    const ChallengeTest *t = &c->tests[selectedTest];
    UiTextFit(TextFormat("Test: %s", t->name), (Vector2){ d.x + 10, d.y + 6 }, 15, d.width - 150, UI_TEXT);
    if (!t->hidden)
    {
        // Small button: run this test in the Visualizer
        Rectangle b = StepButtonRect();
        bool hover = CheckCollisionPointRec(GetMousePosition(), b);
        DrawRectangleRounded(b, 0.4f, 6, hover ? UI_ACCENT2 : Fade(UI_ACCENT2, 0.25f));
        DrawRectangleRoundedLinesEx(b, 0.4f, 6, 1.0f, UI_ACCENT2);
        UiDrawTextCentered("Step through  >", b.x + b.width/2, b.y + 4, 14, WHITE);
    }

    Rectangle view = { d.x + 6, d.y + 30, d.width - 12, d.height - 32 };
    detailContentH = DrawDetailContent(c, view, 0, false) + 4;
    UiBeginScissor(view);
    DrawDetailContent(c, view, view.y - detailScroll.offset, true);
    EndScissorMode();
    float max = fmaxf(0, detailContentH - (d.height - 30));
    if (max > 0) UiScrollDraw(&detailScroll, (Rectangle){ d.x + d.width - 5, d.y + 30, 4, d.height - 32 }, d.height - 30, max);
}

static void DrawButtonsAndStatus(const Challenge *c)
{
    UiButtonDrawEx(runButton, "Run tests", UI_GOOD, !showingSolution);
    UiButtonDrawEx(hintButton, "Hint", UI_MUTED, true);
    UiButtonDrawEx(resetButton, "Reset", UI_MUTED, !showingSolution);
    bool unlocked = SolutionUnlocked();
    UiButtonDrawEx(solveButton, showingSolution ? "Back to my code" : "Show solution", UI_MUTED, showingSolution || unlocked);

    int n = tries[selectedSection][selectedChallenge];
    const char *status;
    Color sc = UI_MUTED;
    if (showingSolution) { status = "This is the model solution (read-only). Compare it with yours."; sc = UI_WARN; }
    else if (graded && !ShowsErrorBox() && grade.passedCount == c->testCount) { status = "Solved! Every test passes."; sc = UI_GOOD; }
    else if (!unlocked) status = TextFormat("Ctrl+Enter runs the tests.  The solution unlocks after %d tries (%d so far).", SOLUTION_AFTER_TRIES, n);
    else status = "Ctrl+Enter runs the tests.";
    UiTextFit(status, (Vector2){ 18, 507 }, 14, 450, sc);
}

static void DrawCelebration(void)
{
    double t = GetTime() - solvedTime;
    if (t < 0 || t > 4.0) return;
    for (int i = 0; i < CONFETTI; i++)
    {
        Bit *b = &confetti[i];
        if (b->p.y > 560) continue;
        DrawRectanglePro((Rectangle){ b->p.x, b->p.y, 8, 4 }, (Vector2){ 4, 2 }, b->angle, Fade(b->c, t > 3 ? (float)(4 - t) : 1.0f));
    }
    if (t < 2.2)
    {
        float a = t < 1.6 ? 1.0f : (float)(2.2 - t)/0.6f;
        float s = 1.0f + 0.15f*(float)fmax(0, 0.25 - t)*4;
        Rectangle box = { codeRect.x + codeRect.width/2 - 150*s, codeRect.y + codeRect.height/2 - 40*s, 300*s, 80*s };
        DrawRectangleRounded(box, 0.3f, 8, Fade((Color){ 20, 60, 30, 255 }, 0.94f*a));
        DrawRectangleRoundedLinesEx(box, 0.3f, 8, 2.0f, Fade(UI_GOOD, a));
        UiDrawTextCentered("All tests pass!", box.x + box.width/2, box.y + 12*s, 30*s, Fade(WHITE, a));
        UiDrawTextCentered("Solved", box.x + box.width/2, box.y + 50*s, 15*s, Fade(UI_GOOD, a));
    }
}

void DrawChallengeScreen(void)
{
    const Challenge *c = Current();
    if (c == NULL)
    {
        UiDrawHeader("Coding challenge");
        UiText("This section's challenges could not be loaded.", (Vector2){ 60, 110 }, 18, UI_BAD);
        UiButtonDrawEx(backButton, "Back to topics", UI_MUTED, true);
        return;
    }

    DrawHeader(c);
    EditorMarks marks = { 0, 0, (graded && grade.compileError) ? grade.errorLine : 0 };
    EditorDraw(&editor, codeRect, marks);
    if (showingSolution) DrawRectangleRoundedLinesEx(codeRect, 0.02f, 6, 2.0f, Fade(UI_WARN, 0.7f));
    DrawButtonsAndStatus(c);
    DrawTask(c);
    DrawTests(c);
    DrawCelebration();
}
