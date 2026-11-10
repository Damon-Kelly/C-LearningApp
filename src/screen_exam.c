/**********************************************************************************************
*
*   Section exam: a short test drawn from a pool of questions.
*
*   Intro -> questions (multiple choice, or a short typed answer) -> result.
*   The question pool lives in a text file named in sections.txt (@exam), see
*   docs/CONTENT_FORMAT.md. Authors: press F5 to reload the exam file.
*
**********************************************************************************************/

#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"
#include "codepanel.h"
#include "exam_hooks.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

//----------------------------------------------------------------------------------
// Layout
//----------------------------------------------------------------------------------
#define PAGE_LEFT       60.0f
#define CONTENT_WIDTH   840.0f
#define CONTENT_TOP     122.0f
#define CONTENT_BOTTOM  476.0f
#define PROMPT_Y        144.0f

static const Rectangle backButton = { 60, 480, 140, 40 };
static const Rectangle nextButton = { 720, 480, 180, 40 };
static const Rectangle startButton = { 390, 380, 180, 48 };
static const Rectangle retryButton = { 405, 410, 150, 44 };

//----------------------------------------------------------------------------------
// State
//----------------------------------------------------------------------------------
typedef enum Phase { PHASE_INTRO, PHASE_QUESTION, PHASE_RESULT } Phase;

static GameScreen finishScreen = UNKNOWN;

static Exam exam;                           // zero-initialised; UnloadExam keeps it that way
static bool loaded = false;
static char loadError[256] = "";

static Phase phase = PHASE_INTRO;
static int order[MAX_EXAM_QUESTIONS];       // pool index of each question in this attempt
static int total = 0;
static bool correctAt[MAX_EXAM_QUESTIONS];
static int optionMap[MAX_OPTIONS];          // shown position -> index in the question's options
static int qIndex = 0;
static int score = 0;
static bool answered = false;
static bool lastCorrect = false;
static int selected = -1;                   // shown position of the clicked option
static char typed[MAX_ANSWER_LEN] = "";
static unsigned attemptCounter = 0;
static unsigned rngState = 1;

//----------------------------------------------------------------------------------
// Helpers
//----------------------------------------------------------------------------------
static unsigned NextRandom(void)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

static const SectionDef *CurrentSection(void)
{
    return GetSection(selectedSection);
}

static const ExamQuestion *CurrentQuestion(void)
{
    return &exam.pool[order[qIndex]];
}

static void LoadCurrentExam(void)
{
    UnloadExam(&exam);
    loaded = false;
    loadError[0] = '\0';

    const SectionDef *section = CurrentSection();
    if (section == NULL || section->examFile[0] == '\0')
    {
        snprintf(loadError, sizeof(loadError), "This section has no exam yet.");
        return;
    }

    loaded = LoadExam(ResourcePath(TextFormat("content/%s", section->examFile)), &exam, loadError, sizeof(loadError));
}

static void BeginQuestion(void)
{
    const ExamQuestion *q = CurrentQuestion();

    answered = false;
    lastCorrect = false;
    selected = -1;
    typed[0] = '\0';

    // Shuffle the options so the right answer is not always in the same place
    for (int i = 0; i < q->optionCount; i++) optionMap[i] = i;
    for (int i = q->optionCount - 1; i > 0; i--)
    {
        int j = (int)(NextRandom() % (unsigned)(i + 1));
        int tmp = optionMap[i]; optionMap[i] = optionMap[j]; optionMap[j] = tmp;
    }
}

static void StartAttempt(void)
{
    attemptCounter++;
    rngState = (unsigned)time(NULL)*2654435761u + attemptCounter*40503u;
    if (rngState == 0) rngState = 1;

    total = ExamDraw(&exam, order, MAX_EXAM_QUESTIONS, rngState);
    memset(correctAt, 0, sizeof(correctAt));
    qIndex = 0;
    score = 0;
    phase = PHASE_QUESTION;
    BeginQuestion();
}

static void RecordAnswer(bool correct)
{
    answered = true;
    lastCorrect = correct;
    correctAt[qIndex] = correct;
    if (correct) score++;
}

static void GoToNext(void)
{
    if (qIndex + 1 >= total)
    {
        phase = PHASE_RESULT;
        bool passed = (score*100 >= exam.passPercent*total);
        OnExamFinished(selectedSection, score, total, passed);
    }
    else
    {
        qIndex++;
        BeginQuestion();
    }
}

static float PromptHeight(const ExamQuestion *q)
{
    return UiMeasureWrappedText(q->prompt, CONTENT_WIDTH, 22);
}

//----------------------------------------------------------------------------------
// Multiple choice
//----------------------------------------------------------------------------------
// Without code the options fill the width; with code they sit to the right of it.
static Rectangle ChoiceRect(const ExamQuestion *q, int i)
{
    if (q->code == NULL) return (Rectangle){ PAGE_LEFT, 214.0f + i*46.0f, CONTENT_WIDTH, 40 };

    float codeY = PROMPT_Y + PromptHeight(q) + 10.0f;
    return (Rectangle){ 580, codeY + i*52.0f, 320, 44 };
}

static void UpdateChoice(const ExamQuestion *q)
{
    if (answered) return;

    for (int i = 0; i < q->optionCount; i++)
    {
        if (UiButtonClicked(ChoiceRect(q, i)) || IsKeyPressed(KEY_ONE + i))
        {
            selected = i;
            RecordAnswer(optionMap[i] == q->correct);
            break;
        }
    }
}

static void DrawChoice(const ExamQuestion *q)
{
    for (int i = 0; i < q->optionCount; i++)
    {
        Color fill = UI_ACCENT;
        if (answered)
        {
            if (optionMap[i] == q->correct) fill = UI_GOOD;
            else if (i == selected) fill = UI_BAD;
            else fill = UI_MUTED;
        }
        UiButtonDrawEx(ChoiceRect(q, i), TextFormat("%c)  %s", 'A' + i, q->options[optionMap[i]]), fill, !answered);
    }

    const char *verdict = lastCorrect ? "Correct!" : "Not quite.";
    const char *why = (q->why != NULL) ? q->why : "";

    if (q->code != NULL)
    {
        float codeY = PROMPT_Y + PromptHeight(q) + 10.0f;
        float codeHeight = CodePanelHeight(q->code);
        DrawCodePanel((Rectangle){ PAGE_LEFT, codeY, 500, codeHeight }, q->code, q->lang, 0);

        float belowCode = codeY + codeHeight + 10.0f;
        if (answered) UiDrawWrappedText(TextFormat("%s %s", verdict, why), (Rectangle){ PAGE_LEFT, belowCode, 500, CONTENT_BOTTOM - belowCode }, 18, lastCorrect ? UI_GOOD : UI_TEXT);
        else UiText("Click an answer or press 1-4", (Vector2){ PAGE_LEFT, belowCode }, 18, UI_MUTED);
    }
    else
    {
        float y = ChoiceRect(q, q->optionCount).y + 2;
        if (answered) UiDrawWrappedText(TextFormat("%s %s", verdict, why), (Rectangle){ PAGE_LEFT, y, 640, CONTENT_BOTTOM - y }, 18, lastCorrect ? UI_GOOD : UI_TEXT);
        else UiText("Click an answer or press 1-4", (Vector2){ PAGE_LEFT, y + 2 }, 18, UI_MUTED);
    }
}

//----------------------------------------------------------------------------------
// Typed-answer text box (kept in this file so it cannot clash with other UI code)
//----------------------------------------------------------------------------------
// Edits buf with printable ASCII and backspace; returns true on the frame Enter is pressed.
static bool ExamInputUpdate(char *buf, int size)
{
    int len = (int)strlen(buf);

    int key = GetCharPressed();
    while (key > 0)
    {
        if (key >= 32 && key <= 126 && len < size - 1)
        {
            buf[len++] = (char)key;
            buf[len] = '\0';
        }
        key = GetCharPressed();
    }

    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len > 0) buf[len - 1] = '\0';

    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

static void ExamInputDraw(Rectangle bounds, const char *buf, bool active)
{
    DrawRectangleRounded(bounds, 0.2f, 8, WHITE);
    DrawRectangleLinesEx(bounds, 2.0f, active ? UI_ACCENT : UI_MUTED);

    Vector2 size = MeasureTextEx(monoFont, buf, CODE_FONT_SIZE, 0);
    Vector2 pos = { bounds.x + 12, bounds.y + (bounds.height - (float)CODE_FONT_SIZE)/2.0f };
    DrawTextEx(monoFont, buf, pos, CODE_FONT_SIZE, 0, UI_TEXT);

    if (active && ((int)(GetTime()*2.0) % 2 == 0)) DrawRectangle((int)(pos.x + size.x + 2), (int)pos.y, 2, CODE_FONT_SIZE, UI_TEXT);
}

//----------------------------------------------------------------------------------
// Typed answer
//----------------------------------------------------------------------------------
#define TYPED_CODE_WIDTH 470.0f

static float TypedCodeY(const ExamQuestion *q) { return PROMPT_Y + PromptHeight(q) + 10.0f; }

// With code: the box sits to the right of it. Without: it sits under the prompt.
static Rectangle TypedInputRect(const ExamQuestion *q)
{
    if (q->code != NULL) return (Rectangle){ 560, TypedCodeY(q), 340, 40 };
    return (Rectangle){ PAGE_LEFT, TypedCodeY(q) + 10.0f, 560, 40 };
}

static Rectangle TypedSubmitRect(const ExamQuestion *q)
{
    Rectangle input = TypedInputRect(q);
    if (q->code != NULL) return (Rectangle){ input.x, input.y + 50, 140, 40 };
    return (Rectangle){ input.x + input.width + 20, input.y, 140, 40 };
}

static void UpdateTyped(const ExamQuestion *q)
{
    if (answered)
    {
        while (GetCharPressed() > 0) { }    // drop keys typed after answering
        return;
    }

    bool enter = ExamInputUpdate(typed, MAX_ANSWER_LEN);
    if ((enter || UiButtonClicked(TypedSubmitRect(q))) && typed[0] != '\0')
    {
        RecordAnswer(ExamAnswerMatches(q, typed));
    }
}

static void DrawTyped(const ExamQuestion *q)
{
    float codeY = TypedCodeY(q);
    float bottom = codeY;

    if (q->code != NULL)
    {
        float codeHeight = CodePanelHeight(q->code);
        DrawCodePanel((Rectangle){ PAGE_LEFT, codeY, TYPED_CODE_WIDTH, codeHeight }, q->code, q->lang, 0);
        bottom = codeY + codeHeight;
    }

    Rectangle input = TypedInputRect(q);
    Rectangle submit = TypedSubmitRect(q);
    ExamInputDraw(input, typed, !answered);
    UiButtonDrawEx(submit, "Check", UI_ACCENT, !answered && typed[0] != '\0');

    if (input.y + input.height > bottom) bottom = input.y + input.height;
    if (submit.y + submit.height > bottom) bottom = submit.y + submit.height;
    bottom += 12.0f;

    if (!answered)
    {
        const char *hint = "Type the missing code and press Enter. Spaces and capitals do not matter.";
        UiDrawWrappedText(hint, (Rectangle){ PAGE_LEFT, bottom, CONTENT_WIDTH, CONTENT_BOTTOM - bottom }, 18, UI_MUTED);
        return;
    }

    const char *why = (q->why != NULL) ? q->why : "";
    if (lastCorrect) UiDrawWrappedText(TextFormat("Correct! %s", why), (Rectangle){ PAGE_LEFT, bottom, CONTENT_WIDTH, CONTENT_BOTTOM - bottom }, 18, UI_GOOD);
    else UiDrawWrappedText(TextFormat("Not quite. One accepted answer is:  %s\n%s", q->shown, why), (Rectangle){ PAGE_LEFT, bottom, CONTENT_WIDTH, CONTENT_BOTTOM - bottom }, 18, UI_TEXT);
}

//----------------------------------------------------------------------------------
// Result
//----------------------------------------------------------------------------------
static Vector2 ResultDot(int i)
{
    float pitch = 800.0f/(float)total;
    if (pitch > 52.0f) pitch = 52.0f;
    float startX = GAME_WIDTH/2.0f - (total*pitch)/2.0f + pitch/2.0f;
    return (Vector2){ startX + i*pitch, 340.0f };
}

//----------------------------------------------------------------------------------
// Screen functions
//----------------------------------------------------------------------------------
void InitExamScreen(void)
{
    finishScreen = UNKNOWN;
    phase = PHASE_INTRO;
    LoadCurrentExam();
}

void UpdateExamScreen(void)
{
    if (IsKeyPressed(KEY_F5))
    {
        LoadCurrentExam();
        phase = PHASE_INTRO;
        return;
    }

    if (!loaded)
    {
        if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TOPICS;
        return;
    }

    switch (phase)
    {
        case PHASE_INTRO:
        {
            if (UiButtonClicked(startButton) || IsKeyPressed(KEY_ENTER)) StartAttempt();
        } break;
        case PHASE_QUESTION:
        {
            const ExamQuestion *q = CurrentQuestion();

            // Enter submits a typed answer, so "next" must not react on the same frame
            bool wasAnswered = answered;
            if (q->kind == EXAM_CHOICE) UpdateChoice(q);
            else UpdateTyped(q);

            if (wasAnswered && (UiButtonClicked(nextButton) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) GoToNext();
        } break;
        case PHASE_RESULT:
        {
            if (UiButtonClicked(retryButton)) StartAttempt();
        } break;
    }

    if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TOPICS;
}

void DrawExamScreen(void)
{
    const SectionDef *section = CurrentSection();

    if (!loaded)
    {
        UiDrawHeader("Could not load this exam");
        float bottom = UiDrawWrappedText(loadError, (Rectangle){ PAGE_LEFT, 110, CONTENT_WIDTH, 250 }, 20, UI_BAD);
        UiText("Fix the file, then press F5 to try again.", (Vector2){ PAGE_LEFT, bottom + 16 }, 20, UI_MUTED);
        UiButtonDrawEx(backButton, "Topics", UI_MUTED, true);
        return;
    }

    UiDrawHeader(exam.title);
    float cx = GAME_WIDTH/2.0f;

    switch (phase)
    {
        case PHASE_INTRO:
        {
            int choiceCount = exam.drawChoice, typedCount = exam.drawTyped;
            UiDrawTextCentered(section != NULL ? section->name : "", cx, 120, 28, UI_MUTED);
            UiDrawTextCentered(TextFormat("%d questions", choiceCount + typedCount), cx, 186, 40, UI_TEXT);

            const char *mix = (typedCount > 0)
                ? TextFormat("%d multiple choice and %d where you type a short piece of code.", choiceCount, typedCount)
                : TextFormat("%d multiple choice questions.", choiceCount);
            UiDrawTextCentered(mix, cx, 244, 20, UI_TEXT);
            UiDrawTextCentered(TextFormat("Pass mark: %d%%.  Retry as often as you like - each attempt draws new questions.", exam.passPercent), cx, 280, 20, UI_MUTED);
            UiDrawTextCentered("Answers are checked ignoring capitals and spaces.", cx, 312, 20, UI_MUTED);

            UiButtonDraw(startButton, "Start exam");
            UiButtonDrawEx(backButton, "Topics", UI_MUTED, true);
        } break;
        case PHASE_QUESTION:
        {
            const ExamQuestion *q = CurrentQuestion();

            UiText(TextFormat("Question %d of %d", qIndex + 1, total), (Vector2){ PAGE_LEFT, CONTENT_TOP - 2 }, 16, UI_MUTED);
            UiDrawWrappedText(q->prompt, (Rectangle){ PAGE_LEFT, PROMPT_Y, CONTENT_WIDTH, 70 }, 22, UI_TEXT);

            if (q->kind == EXAM_CHOICE) DrawChoice(q);
            else DrawTyped(q);

            if (answered) UiButtonDraw(nextButton, (qIndex + 1 >= total) ? "See result" : "Next question");
            UiButtonDrawEx(backButton, "Quit exam", UI_MUTED, true);
        } break;
        case PHASE_RESULT:
        {
            bool passed = (score*100 >= exam.passPercent*total);
            int percent = (total > 0) ? (score*100)/total : 0;

            UiDrawTextCentered(TextFormat("%d / %d", score, total), cx, 112, 80, passed ? UI_GOOD : UI_BAD);
            UiDrawTextCentered(passed ? "Passed!" : "Not passed this time", cx, 210, 30, UI_TEXT);
            UiDrawTextCentered(TextFormat("%d%%  -  pass mark %d%%", percent, exam.passPercent), cx, 250, 20, UI_MUTED);
            if (!passed) UiDrawTextCentered("Look back over the topics, then try again.", cx, 280, 20, UI_MUTED);

            for (int i = 0; i < total; i++)
            {
                Vector2 c = ResultDot(i);
                DrawCircleV(c, 20, correctAt[i] ? UI_GOOD : UI_BAD);
                const char *label = TextFormat("%d", i + 1);
                Vector2 size = UiMeasureText(label, 20);
                UiText(label, (Vector2){ c.x - size.x/2.0f, c.y - size.y/2.0f }, 20, WHITE);
            }
            UiDrawTextCentered("green = correct, red = missed", cx, 372, 16, UI_MUTED);

            UiButtonDraw(retryButton, "Try again");
            UiButtonDrawEx(backButton, "Topics", UI_MUTED, true);
        } break;
    }
}

void UnloadExamScreen(void)
{
    UnloadExam(&exam);
    loaded = false;
}

GameScreen FinishExamScreen(void)
{
    return finishScreen;
}