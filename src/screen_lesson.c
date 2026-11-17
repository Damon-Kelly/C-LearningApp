/**********************************************************************************************
*
*   Lesson player: shows one topic as a series of pages.
*
*   Page types (see docs/CONTENT_FORMAT.md):
*     explain  - text and code examples with their output
*     trace    - step through code while a memory picture updates
*     predict  - "what does this print?" with four choices
*     quiz     - several multiple-choice questions and a score
*
*   Authors: press F5 on this screen to reload the lesson file without restarting.
*
**********************************************************************************************/

#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"
#include "codepanel.h"
#include "memview.h"
#include "exam_hooks.h"
#include "review.h"         // "Review answers" after a quiz (shared with the exam)

#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------------
// Layout
//----------------------------------------------------------------------------------
#define CONTENT_TOP     122.0f
#define CONTENT_BOTTOM  468.0f
#define PAGE_LEFT       60.0f
#define CONTENT_WIDTH   840.0f
#define ITEM_GAP        14.0f

static const Rectangle backButton = { 60, 480, 140, 40 };
static const Rectangle nextButton = { 720, 480, 180, 40 };

static const Rectangle tracePrevButton  = { 60, 430, 110, 38 };
static const Rectangle traceNextButton  = { 180, 430, 140, 38 };
static const Rectangle traceResetButton = { 330, 430, 90, 38 };

static const Rectangle quizRetryButton  = { 330, 300, 150, 48 };
static const Rectangle quizTopicsButton = { 500, 300, 150, 48 };

// With mistakes to review there are three buttons on the result: Retry, Review answers, Topics
static const Rectangle quizRetryButton3  = { 215, 300, 150, 48 };
static const Rectangle quizReviewButton3 = { 385, 300, 190, 48 };
static const Rectangle quizTopicsButton3 = { 595, 300, 150, 48 };
static bool QuizHasMistakes(void) { return ReviewMissedCount() > 0; }
#define RETRY_RECT   (QuizHasMistakes() ? quizRetryButton3 : quizRetryButton)
#define TOPICS_RECT  (QuizHasMistakes() ? quizTopicsButton3 : quizTopicsButton)

//----------------------------------------------------------------------------------
// State
//----------------------------------------------------------------------------------
static GameScreen finishScreen = UNKNOWN;

static Lesson lesson;               // zero-initialised; UnloadLesson keeps it that way
static bool loaded = false;
static char loadError[256] = "";
static int pageIndex = 0;

static UiScroll explainScroll = { 0 };   // explain pages
static int traceStep = -1;          // trace: -1 = before the first line runs
static int predictSelected = -1;    // predict
static bool predictAnswered = false;
static int quizIndex = 0;           // quiz
static int quizScore = 0;
static bool quizCorrectAt[MAX_QUESTIONS];  // per question, reported to progress tracking at the end
static int quizPicked[MAX_QUESTIONS];      // option picked for each question, for the review
static bool quizReviewing = false;         // stepping through the missed questions
static int quizSelected = -1;
static bool quizAnswered = false;
static bool quizDone = false;

//----------------------------------------------------------------------------------
// Helpers
//----------------------------------------------------------------------------------
static const TopicRef *CurrentTopic(void)
{
    const SectionDef *section = GetSection(selectedSection);
    if (section == NULL || selectedTopic < 0 || selectedTopic >= section->topicCount) return NULL;
    return &section->topics[selectedTopic];
}

static void EnterPage(void)
{
    explainScroll.offset = 0.0f;
    traceStep = -1;
    predictSelected = -1;
    predictAnswered = false;
    quizIndex = 0;
    quizScore = 0;
    quizSelected = -1;
    quizAnswered = false;
    quizDone = false;
    quizReviewing = false;
}

static void LoadCurrentLesson(void)
{
    UnloadLesson(&lesson);
    loaded = false;
    loadError[0] = '\0';

    const TopicRef *topic = CurrentTopic();
    if (topic == NULL)
    {
        snprintf(loadError, sizeof(loadError), "No topic selected.");
        return;
    }

    loaded = LoadLesson(ResourcePath(TextFormat("content/%s", topic->file)), &lesson, loadError, sizeof(loadError));
}

static const Item *FirstItemOfKind(const Page *page, ItemKind kind)
{
    for (int i = 0; i < page->itemCount; i++) if (page->items[i].kind == kind) return &page->items[i];
    return NULL;
}

static Color AnswerColor(int i, int correct, int selected, bool answered)
{
    if (!answered) return UI_ACCENT;
    if (i == correct) return UI_GOOD;
    if (i == selected) return UI_BAD;
    return UI_MUTED;
}

// The right-hand button is "Next page", except while a quiz is still in progress
static bool NextPageVisible(const Page *page)
{
    bool onLast = (pageIndex == lesson.pageCount - 1);
    if (onLast) return false;
    if (page->type == PAGE_QUIZ && !quizDone) return false;
    return true;
}

//----------------------------------------------------------------------------------
// Explain page
//----------------------------------------------------------------------------------
static float ItemHeight(const Item *item, float width)
{
    if (item->kind == ITEM_TEXT) return UiMeasureWrappedText(item->text, width, 20);

    float height = CodePanelHeight(item->text);
    if (item->output != NULL) height += 6 + OutputPanelHeight(item->output);
    return height;
}

static float ExplainContentHeight(const Page *page)
{
    float height = 0;
    for (int i = 0; i < page->itemCount; i++)
    {
        height += ItemHeight(&page->items[i], CONTENT_WIDTH - 16);
        if (i < page->itemCount - 1) height += ITEM_GAP;
    }
    return height;
}

// The explain page's scroll bar, along the right edge of the content area
static Rectangle ExplainTrack(void)
{
    return (Rectangle){ PAGE_LEFT + CONTENT_WIDTH - 6, CONTENT_TOP, 6, CONTENT_BOTTOM - CONTENT_TOP };
}

static void UpdateExplain(const Page *page)
{
    float viewHeight = CONTENT_BOTTOM - CONTENT_TOP;
    float maxScroll = ExplainContentHeight(page) - viewHeight;
    if (maxScroll < 0) maxScroll = 0;

    UiScrollUpdate(&explainScroll, ExplainTrack(), viewHeight, maxScroll, 40.0f);
}

static void DrawExplain(const Page *page)
{
    Rectangle view = { PAGE_LEFT, CONTENT_TOP, CONTENT_WIDTH, CONTENT_BOTTOM - CONTENT_TOP };
    float innerWidth = CONTENT_WIDTH - 16;
    float contentHeight = ExplainContentHeight(page);

    UiBeginScissor(view);

    float y = view.y - explainScroll.offset;
    for (int i = 0; i < page->itemCount; i++)
    {
        const Item *item = &page->items[i];
        float height = ItemHeight(item, innerWidth);

        if (item->kind == ITEM_TEXT)
        {
            UiDrawWrappedText(item->text, (Rectangle){ view.x, y, innerWidth, 100000 }, 20, UI_TEXT);
        }
        else
        {
            float codeHeight = CodePanelHeight(item->text);
            DrawCodePanel((Rectangle){ view.x, y, innerWidth, codeHeight }, item->text, item->lang, 0);
            if (item->output != NULL)
            {
                DrawOutputPanel((Rectangle){ view.x, y + codeHeight + 6, innerWidth, OutputPanelHeight(item->output) }, item->output);
            }
        }

        y += height + ITEM_GAP;
    }

    EndScissorMode();

    // Scroll bar, only when the page is longer than the view
    if (contentHeight > view.height)
    {
        float maxScroll = contentHeight - view.height;
        UiScrollDraw(&explainScroll, ExplainTrack(), view.height, maxScroll);
        if (explainScroll.offset < maxScroll - 1.0f) UiText("scroll for more", (Vector2){ view.x + view.width/2.0f - 50, CONTENT_BOTTOM + 4 }, 14, UI_MUTED);
    }
}

//----------------------------------------------------------------------------------
// Trace page
//----------------------------------------------------------------------------------
static void UpdateTrace(const Page *page)
{
    if (IsKeyPressed(KEY_RIGHT) || UiButtonClicked(traceNextButton))
    {
        if (traceStep < page->stepCount - 1) traceStep++;
    }
    else if (IsKeyPressed(KEY_LEFT) || UiButtonClicked(tracePrevButton))
    {
        if (traceStep > -1) traceStep--;
    }
    else if (UiButtonClicked(traceResetButton)) traceStep = -1;
}

static void DrawTrace(const Page *page)
{
    const Item *code = FirstItemOfKind(page, ITEM_CODE);
    if (code == NULL) return;

    const Step *step = (traceStep >= 0) ? &page->steps[traceStep] : NULL;
    const Step *prev = (traceStep >= 1) ? &page->steps[traceStep - 1] : NULL;

    // Banner: what the line that just ran did
    const char *note = (step != NULL) ? step->note : "Press Next step to run the first line of code.";
    UiDrawWrappedText(note, (Rectangle){ PAGE_LEFT, CONTENT_TOP, CONTENT_WIDTH, 62 }, 20, (step != NULL) ? UI_TEXT : UI_MUTED);

    const float areaY = 192.0f;
    const float areaHeight = 230.0f;

    DrawCodePanel((Rectangle){ PAGE_LEFT, areaY, 430, CodePanelHeight(code->text) }, code->text, code->lang, (step != NULL) ? step->line : 0);
    DrawMemory((Rectangle){ 510, areaY, 390, areaHeight }, step, prev);

    UiButtonDrawEx(tracePrevButton, "< Prev", UI_MUTED, traceStep > -1);
    UiButtonDrawEx(traceNextButton, "Next step >", UI_ACCENT, traceStep < page->stepCount - 1);
    UiButtonDrawEx(traceResetButton, "Reset", UI_MUTED, true);
    UiText(TextFormat("Step %d of %d   (arrow keys work too)", traceStep + 1, page->stepCount), (Vector2){ 440, 441 }, 16, UI_MUTED);
}

//----------------------------------------------------------------------------------
// Predict page
//----------------------------------------------------------------------------------
static float PredictCodeY(const Page *page)
{
    const Item *prompt = FirstItemOfKind(page, ITEM_TEXT);
    float promptHeight = (prompt != NULL) ? UiMeasureWrappedText(prompt->text, CONTENT_WIDTH, 20) : 0.0f;
    return CONTENT_TOP + promptHeight + 10.0f;
}

static Rectangle PredictOptionRect(float codeY, int i)
{
    return (Rectangle){ 580, codeY + i*52.0f, 320, 44 };
}

static void UpdatePredict(const Page *page)
{
    if (predictAnswered) return;

    float codeY = PredictCodeY(page);
    for (int i = 0; i < page->optionCount; i++)
    {
        if (UiButtonClicked(PredictOptionRect(codeY, i)) || IsKeyPressed(KEY_ONE + i))
        {
            predictSelected = i;
            predictAnswered = true;
            break;
        }
    }
}

static void DrawPredict(const Page *page)
{
    const Item *prompt = FirstItemOfKind(page, ITEM_TEXT);
    const Item *code = FirstItemOfKind(page, ITEM_CODE);
    if (code == NULL) return;

    float codeY = PredictCodeY(page);
    float codeHeight = CodePanelHeight(code->text);

    if (prompt != NULL) UiDrawWrappedText(prompt->text, (Rectangle){ PAGE_LEFT, CONTENT_TOP, CONTENT_WIDTH, 100 }, 20, UI_TEXT);
    DrawCodePanel((Rectangle){ PAGE_LEFT, codeY, 500, codeHeight }, code->text, code->lang, 0);

    for (int i = 0; i < page->optionCount; i++)
    {
        Color fill = AnswerColor(i, page->correct, predictSelected, predictAnswered);
        UiButtonDrawEx(PredictOptionRect(codeY, i), TextFormat("%c)  %s", 'A' + i, page->options[i]), fill, !predictAnswered);
    }

    float belowCode = codeY + codeHeight + 10.0f;

    if (predictAnswered)
    {
        // Real output on the right under the options, explanation under the code
        float outputY = codeY + page->optionCount*52.0f + 6.0f;
        DrawOutputPanel((Rectangle){ 580, outputY, 320, OutputPanelHeight(code->output) }, code->output);

        const char *verdict = (predictSelected == page->correct) ? "Correct!" : "Not quite.";
        const char *why = (page->why != NULL) ? page->why : "";
        UiDrawWrappedText(TextFormat("%s %s", verdict, why), (Rectangle){ PAGE_LEFT, belowCode, 500, CONTENT_BOTTOM - belowCode }, 18, UI_TEXT);
    }
    else
    {
        UiText("Click an answer or press 1-4", (Vector2){ PAGE_LEFT, belowCode }, 18, UI_MUTED);
    }
}

//----------------------------------------------------------------------------------
// Quiz page
//----------------------------------------------------------------------------------
static Rectangle QuizOptionRect(int i)
{
    return (Rectangle){ PAGE_LEFT, 214.0f + i*46.0f, CONTENT_WIDTH, 40 };
}

static void UpdateQuiz(const Page *page)
{
    if (quizDone)
    {
        if (QuizHasMistakes() && UiButtonClicked(quizReviewButton3) && ReviewOpen())
        {
            quizReviewing = true;
            return;
        }
        if (UiButtonClicked(RETRY_RECT))
        {
            quizIndex = 0;
            quizScore = 0;
            quizSelected = -1;
            quizAnswered = false;
            quizDone = false;
        }
        if (UiButtonClicked(TOPICS_RECT)) finishScreen = TOPICS;
        return;
    }

    const Question *q = &page->questions[quizIndex];

    if (!quizAnswered)
    {
        for (int i = 0; i < q->optionCount; i++)
        {
            if (UiButtonClicked(QuizOptionRect(i)) || IsKeyPressed(KEY_ONE + i))
            {
                quizSelected = i;
                quizAnswered = true;
                quizCorrectAt[quizIndex] = (i == q->correct);
                quizPicked[quizIndex] = i;
                if (i == q->correct) quizScore++;
                break;
            }
        }
    }
    else if (UiButtonClicked(nextButton) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))
    {
        if (quizIndex + 1 >= page->questionCount)
        {
            quizDone = true;
            QuestionResult results[MAX_QUESTIONS];
            for (int i = 0; i < page->questionCount; i++) results[i] = (QuestionResult){ page->questions[i].prompt, quizCorrectAt[i] };
            OnTopicQuizFinished(selectedSection, selectedTopic, quizScore, page->questionCount, results, page->questionCount);

            // Hand every question to the review, so missed ones can be looked at again
            ReviewClear();
            for (int i = 0; i < page->questionCount; i++)
            {
                const Question *qq = &page->questions[i];
                ReviewItem item = { 0 };
                item.number = i + 1;
                item.prompt = qq->prompt;
                item.why = qq->why;
                item.wasCorrect = quizCorrectAt[i];
                item.optionCount = qq->optionCount;
                for (int j = 0; j < qq->optionCount; j++) item.options[j] = qq->options[j];
                item.correct = qq->correct;
                item.picked = quizPicked[i];
                ReviewAdd(&item);
            }
        }
        else
        {
            quizIndex++;
            quizSelected = -1;
            quizAnswered = false;
        }
    }
}

static void DrawQuiz(const Page *page)
{
    if (quizDone)
    {
        float cx = GAME_WIDTH/2.0f;
        int percent = (page->questionCount > 0) ? (quizScore*100)/page->questionCount : 0;

        const char *message = "Keep going - re-read the lesson and try again.";
        if (percent == 100) message = "Perfect score!";
        else if (percent >= 70) message = "Nice work!";

        UiDrawTextCentered(TextFormat("%d / %d", quizScore, page->questionCount), cx, 160, 80, UI_ACCENT);
        UiDrawTextCentered(message, cx, 255, 22, UI_TEXT);
        UiButtonDraw(RETRY_RECT, "Retry quiz");
        if (QuizHasMistakes()) UiButtonDraw(quizReviewButton3, "Review answers");
        UiButtonDrawEx(TOPICS_RECT, "Topics", UI_MUTED, true);
        return;
    }

    const Question *q = &page->questions[quizIndex];

    UiText(TextFormat("Question %d of %d", quizIndex + 1, page->questionCount), (Vector2){ PAGE_LEFT, CONTENT_TOP - 2 }, 16, UI_MUTED);
    UiDrawWrappedText(q->prompt, (Rectangle){ PAGE_LEFT, CONTENT_TOP + 22, CONTENT_WIDTH, 66 }, 22, UI_TEXT);

    for (int i = 0; i < q->optionCount; i++)
    {
        Color fill = AnswerColor(i, q->correct, quizSelected, quizAnswered);
        UiButtonDrawEx(QuizOptionRect(i), TextFormat("%c)  %s", 'A' + i, q->options[i]), fill, !quizAnswered);
    }

    if (quizAnswered)
    {
        const char *verdict = (quizSelected == q->correct) ? "Correct!" : "Not quite.";
        const char *why = (q->why != NULL) ? q->why : "";
        float y = QuizOptionRect(q->optionCount).y + 2;
        UiDrawWrappedText(TextFormat("%s %s", verdict, why), (Rectangle){ PAGE_LEFT, y, 640, 476 - y }, 18, UI_TEXT);

        bool isLast = (quizIndex + 1 >= page->questionCount);
        UiButtonDraw(nextButton, isLast ? "See score" : "Next question");
    }
    else
    {
        UiText("Click an answer or press 1-4", (Vector2){ PAGE_LEFT, QuizOptionRect(q->optionCount).y + 4 }, 18, UI_MUTED);
    }
}

//----------------------------------------------------------------------------------
// Screen functions
//----------------------------------------------------------------------------------
void InitLessonScreen(void)
{
    finishScreen = UNKNOWN;
    pageIndex = 0;
    LoadCurrentLesson();
    EnterPage();
}

void UpdateLessonScreen(void)
{
    // Authors: reload the lesson file after editing it
    if (IsKeyPressed(KEY_F5))
    {
        int keep = pageIndex;
        LoadCurrentLesson();
        pageIndex = (loaded && keep < lesson.pageCount) ? keep : 0;
        EnterPage();
        return;
    }

    if (!loaded)
    {
        if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TOPICS;
        return;
    }

    const Page *page = &lesson.pages[pageIndex];

    // While reviewing quiz mistakes, only the review reacts (its buttons, arrows and Esc)
    if (quizReviewing)
    {
        if (ReviewUpdate()) quizReviewing = false;
        return;
    }

    switch (page->type)
    {
        case PAGE_EXPLAIN: UpdateExplain(page); break;
        case PAGE_TRACE: UpdateTrace(page); break;
        case PAGE_PREDICT: UpdatePredict(page); break;
        case PAGE_QUIZ: UpdateQuiz(page); break;
    }

    // Page navigation last, because it changes pageIndex
    if (IsKeyPressed(KEY_ESCAPE)) finishScreen = TOPICS;
    else if (UiButtonClicked(backButton))
    {
        if (pageIndex == 0) finishScreen = TOPICS;
        else { pageIndex--; EnterPage(); }
    }
    else if (NextPageVisible(page) && UiButtonClicked(nextButton))
    {
        pageIndex++;
        EnterPage();
    }
}

void DrawLessonScreen(void)
{
    if (!loaded)
    {
        UiDrawHeader("Could not load this lesson");
        float bottom = UiDrawWrappedText(loadError, (Rectangle){ PAGE_LEFT, 110, CONTENT_WIDTH, 250 }, 20, UI_BAD);
        UiText("Fix the file, then press F5 to try again.", (Vector2){ PAGE_LEFT, bottom + 16 }, 20, UI_MUTED);
        UiButtonDrawEx(backButton, "Topics", UI_MUTED, true);
        return;
    }

    const Page *page = &lesson.pages[pageIndex];

    UiDrawHeader(lesson.title);

    if (quizReviewing)
    {
        ReviewDraw();
        return;
    }

    const char *indicator = TextFormat("Page %d of %d", pageIndex + 1, lesson.pageCount);
    Vector2 indicatorSize = UiMeasureText(indicator, 20);
    UiText(indicator, (Vector2){ PAGE_LEFT + CONTENT_WIDTH - indicatorSize.x, 46 }, 20, UI_MUTED);

    UiText(page->title, (Vector2){ PAGE_LEFT, 90 }, 20, UI_MUTED);

    switch (page->type)
    {
        case PAGE_EXPLAIN: DrawExplain(page); break;
        case PAGE_TRACE: DrawTrace(page); break;
        case PAGE_PREDICT: DrawPredict(page); break;
        case PAGE_QUIZ: DrawQuiz(page); break;
    }

    UiButtonDrawEx(backButton, (pageIndex == 0) ? "Topics" : "Back", UI_MUTED, true);
    if (NextPageVisible(page)) UiButtonDraw(nextButton, "Next page");
}

void UnloadLessonScreen(void)
{
    UnloadLesson(&lesson);
    loaded = false;
}

GameScreen FinishLessonScreen(void)
{
    return finishScreen;
}
