/**********************************************************************************************
*
*   Progress: how the logged-in user is doing.
*
*   Left: three totals and one bar per section (topics completed, exam status).
*   Right: the most recent attempts and a short "Practice next" list.
*   Everything is worked out from progress.c and the section list, nothing is stored here.
*   Click a section to open its topics, or a "Practice next" row to open that lesson.
*
**********************************************************************************************/

#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"
#include "progress.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static GameScreen finishScreen = UNKNOWN;

static const Rectangle backButton = { 60, 480, 140, 40 };

// Layout
#define LIST_LEFT     60.0f
#define LIST_WIDTH    510.0f
#define LIST_TOP      176.0f
#define LIST_BOTTOM   468.0f
#define ROW_PITCH     70.0f
#define ROW_HEIGHT    62.0f
#define RIGHT_LEFT    600.0f
#define RIGHT_WIDTH   300.0f
#define RECENT_SHOWN  5
#define PRACTICE_SHOWN 3

#define CARD_BORDER   UI_BORDER
#define BAR_BACK      UI_PANEL2

static float scrollY = 0.0f;

//----------------------------------------------------------------------------------
// Numbers
//----------------------------------------------------------------------------------
typedef struct Totals {
    int topics, topicsDone;
    int exams, examsPassed;
    int attempts;
} Totals;

static void CountSection(const SectionDef *s, int *done, int *examState, AttemptStats *exam)
{
    *done = 0;
    for (int t = 0; t < s->topicCount; t++)
    {
        AttemptStats st;
        if (ProgressGetStats(ATTEMPT_QUIZ, s->name, s->topics[t].file, &st) && st.passed) (*done)++;
    }

    // examState: 0 = no exam, 1 = not taken, 2 = tried, 3 = passed
    *examState = 0;
    memset(exam, 0, sizeof(*exam));
    if (s->examFile[0] != '\0')
    {
        *examState = 1;
        if (ProgressGetStats(ATTEMPT_EXAM, s->name, "", exam)) *examState = exam->passed ? 3 : 2;
    }
}

static Totals ComputeTotals(void)
{
    Totals t = { 0 };
    for (int i = 0; i < GetSectionCount(); i++)
    {
        const SectionDef *s = GetSection(i);
        if (s->topicCount == 0) continue;

        int done, examState;
        AttemptStats exam;
        CountSection(s, &done, &examState, &exam);

        t.topics += s->topicCount;
        t.topicsDone += done;
        if (examState > 0) t.exams++;
        if (examState == 3) t.examsPassed++;
    }
    t.attempts = ProgressAttemptCount();
    return t;
}

const char *ProgressSummaryLine(void)
{
    Totals t = ComputeTotals();
    if (t.attempts == 0) return "No quizzes or exams taken yet. Open a topic to get started.";
    if (t.exams == 0) return TextFormat("%d of %d topic quizzes completed.", t.topicsDone, t.topics);
    return TextFormat("%d of %d topic quizzes completed, %d of %d exams passed.", t.topicsDone, t.topics, t.examsPassed, t.exams);
}

//----------------------------------------------------------------------------------
// Helpers
//----------------------------------------------------------------------------------
static void DrawCard(Rectangle r, Color border)
{
    DrawRectangleRounded(r, 0.1f, 8, border);
    DrawRectangleRounded((Rectangle){ r.x + 2, r.y + 2, r.width - 4, r.height - 4 }, 0.1f, 8, UI_PANEL);
}

static int ShownSections(int *index, int max)
{
    int n = 0;
    for (int i = 0; i < GetSectionCount() && n < max; i++) if (GetSection(i)->topicCount > 0) index[n++] = i;
    return n;
}

static float MaxScroll(int rows)
{
    float max = rows*ROW_PITCH - (LIST_BOTTOM - LIST_TOP);
    return (max > 0) ? max : 0;
}

static Rectangle RowRect(int i)
{
    return (Rectangle){ LIST_LEFT, LIST_TOP + i*ROW_PITCH - scrollY, LIST_WIDTH, ROW_HEIGHT };
}

static bool InList(void)
{
    return CheckCollisionPointRec(GetMousePosition(), (Rectangle){ 0, LIST_TOP, 580, LIST_BOTTOM - LIST_TOP });
}

// Shortens text with "..." until it fits the width
static const char *Fit(const char *text, float width, float size)
{
    static char buf[96];
    snprintf(buf, sizeof(buf), "%s", text);
    int n = (int)strlen(buf);
    if (UiMeasureText(buf, size).x <= width) return buf;
    while (n > 1 && UiMeasureText(TextFormat("%s...", buf), size).x > width) buf[--n] = '\0';
    return TextFormat("%s...", buf);
}

static const char *TopicTitle(const char *sectionName, const char *file)
{
    for (int i = 0; i < GetSectionCount(); i++)
    {
        const SectionDef *s = GetSection(i);
        if (strcmp(s->name, sectionName) != 0) continue;
        for (int t = 0; t < s->topicCount; t++) if (strcmp(s->topics[t].file, file) == 0) return s->topics[t].title;
    }
    return file;
}

static const char *DateText(long long when)
{
    time_t t = (time_t)when;
    struct tm *local = localtime(&t);
    static char text[32];
    if (local == NULL || strftime(text, sizeof(text), "%d %b, %H:%M", local) == 0) return "";
    return text;
}

// "Practice next": topics tried but not passed (lowest best score first), then topics not started
typedef struct PracticeItem { int section, topic; bool started; int percent; } PracticeItem;

static int BuildPractice(PracticeItem *out, int max)
{
    int n = 0;

    // Tried but not passed, weakest first
    for (int round = 0; round < max; round++)
    {
        PracticeItem best = { -1, -1, true, 101 };
        for (int i = 0; i < GetSectionCount(); i++)
        {
            const SectionDef *s = GetSection(i);
            for (int t = 0; t < s->topicCount; t++)
            {
                AttemptStats st;
                if (!ProgressGetStats(ATTEMPT_QUIZ, s->name, s->topics[t].file, &st) || st.passed) continue;

                bool taken = false;
                for (int k = 0; k < n; k++) if (out[k].section == i && out[k].topic == t) taken = true;
                if (taken) continue;

                int percent = (st.bestTotal > 0) ? (st.bestScore*100)/st.bestTotal : 0;
                if (percent < best.percent) best = (PracticeItem){ i, t, true, percent };
            }
        }
        if (best.section < 0) break;
        out[n++] = best;
    }

    // Then topics never attempted, in lesson order
    for (int i = 0; i < GetSectionCount() && n < max; i++)
    {
        const SectionDef *s = GetSection(i);
        for (int t = 0; t < s->topicCount && n < max; t++)
        {
            AttemptStats st;
            if (!ProgressGetStats(ATTEMPT_QUIZ, s->name, s->topics[t].file, &st)) out[n++] = (PracticeItem){ i, t, false, 0 };
        }
    }
    return n;
}

static Rectangle PracticeRect(int i)
{
    return (Rectangle){ RIGHT_LEFT, 362.0f + i*34.0f, RIGHT_WIDTH, 30 };
}

//----------------------------------------------------------------------------------
// Screen functions
//----------------------------------------------------------------------------------
void InitProgressScreen(void)
{
    finishScreen = UNKNOWN;
    scrollY = 0.0f;
}

void UpdateProgressScreen(void)
{
    int shown[MAX_SECTIONS];
    int rows = ShownSections(shown, MAX_SECTIONS);

    scrollY -= GetMouseWheelMove()*45.0f;
    if (IsKeyDown(KEY_DOWN)) scrollY += 6.0f;
    if (IsKeyDown(KEY_UP)) scrollY -= 6.0f;
    float max = MaxScroll(rows);
    if (scrollY < 0) scrollY = 0;
    if (scrollY > max) scrollY = max;

    for (int i = 0; i < rows; i++)
    {
        if (InList() && UiButtonClicked(RowRect(i)))
        {
            selectedSection = shown[i];
            finishScreen = TOPICS;
        }
    }

    PracticeItem practice[PRACTICE_SHOWN];
    int count = BuildPractice(practice, PRACTICE_SHOWN);
    for (int i = 0; i < count; i++)
    {
        if (UiButtonClicked(PracticeRect(i)))
        {
            selectedSection = practice[i].section;
            selectedTopic = practice[i].topic;
            finishScreen = LESSON;
        }
    }

    if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = SECTIONS;
}

static void DrawTile(float x, const char *label, const char *value)
{
    Rectangle r = { x, 112, 164, 52 };
    DrawCard(r, CARD_BORDER);
    UiText(label, (Vector2){ r.x + 12, r.y + 6 }, 14, UI_MUTED);
    UiText(value, (Vector2){ r.x + 12, r.y + 22 }, 22, UI_TEXT);
}

void DrawProgressScreen(void)
{
    UiDrawHeader("Progress");
    UiDrawLoggedInAs();

    Totals t = ComputeTotals();

    // Totals
    DrawTile(60,  "Topics completed", TextFormat("%d / %d", t.topicsDone, t.topics));
    DrawTile(234, "Exams passed", TextFormat("%d / %d", t.examsPassed, t.exams));
    DrawTile(408, "Attempts", TextFormat("%d", t.attempts));

    // One card per section
    int shown[MAX_SECTIONS];
    int rows = ShownSections(shown, MAX_SECTIONS);

    UiBeginScissor((Rectangle){ 0, LIST_TOP, 590, LIST_BOTTOM - LIST_TOP });
    for (int i = 0; i < rows; i++)
    {
        const SectionDef *s = GetSection(shown[i]);
        Rectangle r = RowRect(i);
        if (r.y + r.height < LIST_TOP || r.y > LIST_BOTTOM) continue;

        int done, examState;
        AttemptStats exam;
        CountSection(s, &done, &examState, &exam);

        bool hover = InList() && CheckCollisionPointRec(GetMousePosition(), r);
        DrawCard(r, hover ? UI_ACCENT : CARD_BORDER);

        UiText(Fit(s->name, 330, 20), (Vector2){ r.x + 14, r.y + 5 }, 20, UI_TEXT);

        const char *count = TextFormat("%d/%d topics", done, s->topicCount);
        Vector2 countSize = UiMeasureText(count, 16);
        UiText(count, (Vector2){ r.x + r.width - 14 - countSize.x, r.y + 9 }, 16, (done == s->topicCount) ? UI_GOOD : UI_MUTED);

        // Bar
        Rectangle bar = { r.x + 14, r.y + 34, r.width - 28, 8 };
        DrawRectangleRounded(bar, 1.0f, 6, BAR_BACK);
        if (done > 0) DrawRectangleRounded((Rectangle){ bar.x, bar.y, bar.width*(float)done/(float)s->topicCount, bar.height }, 1.0f, 6, UI_GOOD);

        const char *examText = "";
        Color examColor = UI_MUTED;
        switch (examState)
        {
            case 1: examText = "Exam: not taken yet"; break;
            case 2: examText = TextFormat("Exam: best %d/%d, not passed yet", exam.bestScore, exam.bestTotal); break;
            case 3: examText = TextFormat("Exam: passed (best %d/%d)", exam.bestScore, exam.bestTotal); examColor = UI_GOOD; break;
            default: examText = "No exam for this section"; break;
        }
        UiText(examText, (Vector2){ r.x + 14, r.y + 44 }, 14, examColor);
    }
    EndScissorMode();

    float max = MaxScroll(rows);
    if (max > 0)
    {
        float viewHeight = LIST_BOTTOM - LIST_TOP;
        float thumbHeight = viewHeight*viewHeight/(viewHeight + max);
        float thumbY = LIST_TOP + (scrollY/max)*(viewHeight - thumbHeight);
        DrawRectangle(578, (int)LIST_TOP, 6, (int)viewHeight, BAR_BACK);
        DrawRectangleRounded((Rectangle){ 578, thumbY, 6, thumbHeight }, 1.0f, 4, UI_MUTED);
    }

    // Recent activity
    UiText("Recent activity", (Vector2){ RIGHT_LEFT, 112 }, 20, UI_TEXT);
    AttemptRecord recent[RECENT_SHOWN];
    int recentCount = ProgressRecent(recent, RECENT_SHOWN);

    if (recentCount == 0) UiText("Nothing yet.", (Vector2){ RIGHT_LEFT, 144 }, 16, UI_MUTED);
    for (int i = 0; i < recentCount; i++)
    {
        const AttemptRecord *a = &recent[i];
        float y = 142.0f + i*38.0f;

        const char *name = (a->kind == ATTEMPT_EXAM) ? TextFormat("Exam: %s", a->section) : TopicTitle(a->section, a->topic);
        UiText(Fit(name, RIGHT_WIDTH - 64, 16), (Vector2){ RIGHT_LEFT, y }, 16, UI_TEXT);
        UiText(DateText(a->time), (Vector2){ RIGHT_LEFT, y + 18 }, 13, UI_MUTED);

        const char *score = TextFormat("%d/%d", a->score, a->total);
        Vector2 scoreSize = UiMeasureText(score, 18);
        UiText(score, (Vector2){ RIGHT_LEFT + RIGHT_WIDTH - scoreSize.x, y }, 18, a->passed ? UI_GOOD : UI_BAD);
    }

    // Practice next
    UiText("Practice next", (Vector2){ RIGHT_LEFT, 330 }, 20, UI_TEXT);
    PracticeItem practice[PRACTICE_SHOWN];
    int practiceCount = BuildPractice(practice, PRACTICE_SHOWN);

    if (practiceCount == 0)
    {
        UiText(t.topics > 0 ? "Every topic completed - nice work!" : "No topics yet.", (Vector2){ RIGHT_LEFT, 362 }, 16, UI_MUTED);
    }
    for (int i = 0; i < practiceCount; i++)
    {
        const SectionDef *s = GetSection(practice[i].section);
        Rectangle r = PracticeRect(i);
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);

        DrawRectangleRounded(r, 0.3f, 6, hover ? UI_BORDER : UI_PANEL2);
        const char *tag = practice[i].started ? TextFormat("best %d%%", practice[i].percent) : "new";
        Vector2 tagSize = UiMeasureText(tag, 14);
        UiText(Fit(s->topics[practice[i].topic].title, RIGHT_WIDTH - tagSize.x - 28, 16), (Vector2){ r.x + 10, r.y + 5 }, 16, UI_TEXT);
        UiText(tag, (Vector2){ r.x + r.width - tagSize.x - 10, r.y + 8 }, 14, practice[i].started ? UI_BAD : UI_ACCENT);
    }

    UiButtonDrawEx(backButton, "Back", UI_MUTED, true);
}

void UnloadProgressScreen(void)
{
}

GameScreen FinishProgressScreen(void)
{
    return finishScreen;
}
