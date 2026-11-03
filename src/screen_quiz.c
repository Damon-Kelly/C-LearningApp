#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"

static GameScreen finishScreen = UNKNOWN;

static int questionIndex = 0;
static int score = 0;
static int selected = -1;       // Option the player picked, -1 until answered
static bool answered = false;

static const Rectangle nextButton = { 740, 480, 160, 44 };

static Rectangle OptionRect(int i)
{
    return (Rectangle){ 60, 190.0f + i*56.0f, 840, 48 };
}

void InitQuizScreen(void)
{
    finishScreen = UNKNOWN;
    questionIndex = 0;
    score = 0;
    selected = -1;
    answered = false;
}

void UpdateQuizScreen(void)
{
    const Topic *topic = GetTopic(selectedTopic);
    if (topic == NULL) return;

    const Question *q = &topic->questions[questionIndex];

    if (!answered)
    {
        for (int i = 0; i < OPTIONS_PER_QUESTION; i++)
        {
            if (UiButtonClicked(OptionRect(i)) || IsKeyPressed(KEY_ONE + i))
            {
                selected = i;
                answered = true;
                if (i == q->correct) score++;
                break;
            }
        }
    }
    else if (UiButtonClicked(nextButton) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))
    {
        if (questionIndex + 1 >= topic->questionCount)
        {
            // Keep questionIndex in range: Draw still runs during the fade-out
            lastScore = score;
            lastTotal = topic->questionCount;
            finishScreen = RESULTS;
        }
        else
        {
            questionIndex++;
            selected = -1;
            answered = false;
        }
    }
}

void DrawQuizScreen(void)
{
    const Topic *topic = GetTopic(selectedTopic);
    if (topic == NULL) return;

    const Question *q = &topic->questions[questionIndex];

    UiDrawHeader(TextFormat("%s quiz  (%d/%d)", topic->title, questionIndex + 1, topic->questionCount));
    UiDrawWrappedText(q->prompt, (Rectangle){ 60, 95, 840, 85 }, 24, UI_TEXT);

    for (int i = 0; i < OPTIONS_PER_QUESTION; i++)
    {
        Color fill = UI_ACCENT;

        if (answered)
        {
            if (i == q->correct) fill = UI_GOOD;
            else if (i == selected) fill = UI_BAD;
            else fill = UI_MUTED;
        }

        UiButtonDrawEx(OptionRect(i), TextFormat("%c)  %s", 'A' + i, q->options[i]), fill, !answered);
    }

    if (answered)
    {
        const char *verdict = (selected == q->correct) ? "Correct!" : "Not quite.";
        UiDrawWrappedText(TextFormat("%s %s", verdict, q->explanation), (Rectangle){ 60, 414, 660, 70 }, 18, UI_TEXT);

        bool isLast = (questionIndex + 1 >= topic->questionCount);
        UiButtonDraw(nextButton, isLast ? "See results" : "Next");
    }
    else
    {
        DrawTextEx(font, "Click an answer or press 1-4", (Vector2){ 60, 490 }, 18, 1.8f, UI_MUTED);
    }
}

void UnloadQuizScreen(void)
{
}

GameScreen FinishQuizScreen(void)
{
    return finishScreen;
}
