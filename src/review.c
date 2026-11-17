#include "review.h"
#include "raylib.h"
#include "screens.h"        // monoFont, GAME_WIDTH
#include "ui.h"
#include "codepanel.h"

#include <stdio.h>
#include <string.h>

#define PAGE_LEFT       60.0f
#define CONTENT_WIDTH   840.0f
#define PROMPT_Y        144.0f
#define CONTENT_BOTTOM  476.0f

#define ANSWER_RED      UI_BAD
#define ANSWER_GREEN    UI_GOOD

static const Rectangle prevButton = { 60, 480, 140, 40 };
static const Rectangle doneButton = { 390, 480, 180, 40 };
static const Rectangle nextButton = { 720, 480, 180, 40 };

static ReviewItem items[MAX_REVIEW_ITEMS];
static int itemCount = 0;
static int missed[MAX_REVIEW_ITEMS];        // indexes into items[] of the missed questions
static int missedCount = 0;
static int cursor = 0;                      // position in missed[]

//----------------------------------------------------------------------------------
// Collecting the questions
//----------------------------------------------------------------------------------
void ReviewClear(void)
{
    itemCount = 0;
    missedCount = 0;
    cursor = 0;
}

void ReviewAdd(const ReviewItem *item)
{
    if (itemCount >= MAX_REVIEW_ITEMS) return;
    items[itemCount++] = *item;
}

int ReviewMissedCount(void)
{
    int n = 0;
    for (int i = 0; i < itemCount; i++) if (!items[i].wasCorrect) n++;
    return n;
}

bool ReviewOpen(void)
{
    missedCount = 0;
    for (int i = 0; i < itemCount; i++) if (!items[i].wasCorrect) missed[missedCount++] = i;

    cursor = 0;
    return missedCount > 0;
}

//----------------------------------------------------------------------------------
// Update
//----------------------------------------------------------------------------------
bool ReviewUpdate(void)
{
    if (UiButtonClicked(doneButton) || IsKeyPressed(KEY_ESCAPE)) return true;

    if (cursor > 0 && (UiButtonClicked(prevButton) || IsKeyPressed(KEY_LEFT))) cursor--;
    else if (cursor < missedCount - 1 && (UiButtonClicked(nextButton) || IsKeyPressed(KEY_RIGHT))) cursor++;

    return false;
}

//----------------------------------------------------------------------------------
// Draw
//----------------------------------------------------------------------------------
static void DrawWhy(const ReviewItem *it, float y)
{
    if (it->why == NULL || it->why[0] == '\0') return;
    if (y + 40.0f > CONTENT_BOTTOM) return;

    UiText("Explanation", (Vector2){ PAGE_LEFT, y }, 16, UI_MUTED);
    UiDrawWrappedText(it->why, (Rectangle){ PAGE_LEFT, y + 22.0f, CONTENT_WIDTH, CONTENT_BOTTOM - (y + 22.0f) }, 18, UI_TEXT);
}

static void DrawChoiceItem(const ReviewItem *it, float y)
{
    float codeBottom = y;

    // With code the options sit to the right of it, like in the exam; without it they fill the width
    float codeHeight = 0;
    if (it->code != NULL)
    {
        codeHeight = CodePanelHeight(it->code);
        DrawCodePanel((Rectangle){ PAGE_LEFT, y, 500, codeHeight }, it->code, it->lang, 0);
        codeBottom = y + codeHeight;
    }

    float optionsBottom = y;
    for (int i = 0; i < it->optionCount; i++)
    {
        Color fill = UI_MUTED;
        if (i == it->correct) fill = ANSWER_GREEN;
        else if (i == it->picked) fill = ANSWER_RED;

        Rectangle r = (it->code != NULL) ? (Rectangle){ 580, y + i*52.0f, 320, 44 }
                                         : (Rectangle){ PAGE_LEFT, y + i*46.0f, CONTENT_WIDTH, 40 };
        UiButtonDrawEx(r, TextFormat("%c)  %s", 'A' + i, it->options[i]), fill, false);

        if (r.y + r.height > optionsBottom) optionsBottom = r.y + r.height;
    }

    float below = (it->code != NULL) ? codeBottom : optionsBottom;
    DrawWhy(it, below + 12.0f);
}

static void DrawTypedItem(const ReviewItem *it, float y)
{
    float blockX = PAGE_LEFT;
    float codeBottom = y;

    if (it->code != NULL)
    {
        float codeHeight = CodePanelHeight(it->code);
        DrawCodePanel((Rectangle){ PAGE_LEFT, y, 470, codeHeight }, it->code, it->lang, 0);
        codeBottom = y + codeHeight;
        blockX = 560.0f;
    }

    const char *typed = (it->typedAnswer[0] != '\0') ? it->typedAnswer : "(nothing)";
    const char *model = (it->modelAnswer != NULL) ? it->modelAnswer : "";

    UiText("You typed", (Vector2){ blockX, y }, 16, UI_MUTED);
    DrawTextEx(monoFont, typed, (Vector2){ blockX, y + 20 }, CODE_FONT_SIZE, 0, ANSWER_RED);

    UiText("Accepted answer", (Vector2){ blockX, y + 54 }, 16, UI_MUTED);
    DrawTextEx(monoFont, model, (Vector2){ blockX, y + 74 }, CODE_FONT_SIZE, 0, ANSWER_GREEN);

    float blockBottom = y + 74.0f + CODE_FONT_SIZE + 6.0f;
    float below = (codeBottom > blockBottom) ? codeBottom : blockBottom;
    DrawWhy(it, below + 12.0f);
}

void ReviewDraw(void)
{
    if (missedCount == 0)
    {
        UiText("Nothing to review.", (Vector2){ PAGE_LEFT, PROMPT_Y }, 22, UI_MUTED);
        UiButtonDraw(doneButton, "Back to results");
        return;
    }

    const ReviewItem *it = &items[missed[cursor]];

    UiText(TextFormat("Missed question %d of %d   (question %d)", cursor + 1, missedCount, it->number), (Vector2){ PAGE_LEFT, 120 }, 16, UI_MUTED);

    if (!it->typed)
    {
        const char *legend = "green = right answer, red = your answer";
        Vector2 legendSize = UiMeasureText(legend, 16);
        UiText(legend, (Vector2){ PAGE_LEFT + CONTENT_WIDTH - legendSize.x, 120 }, 16, UI_MUTED);
    }

    float promptHeight = UiMeasureWrappedText(it->prompt, CONTENT_WIDTH, 22);
    UiDrawWrappedText(it->prompt, (Rectangle){ PAGE_LEFT, PROMPT_Y, CONTENT_WIDTH, promptHeight + 4.0f }, 22, UI_TEXT);

    float y = PROMPT_Y + promptHeight + 10.0f;
    if (it->typed) DrawTypedItem(it, y);
    else DrawChoiceItem(it, y);

    if (cursor > 0) UiButtonDrawEx(prevButton, "Previous", UI_MUTED, true);
    UiButtonDraw(doneButton, "Back to results");
    if (cursor < missedCount - 1) UiButtonDraw(nextButton, "Next mistake");
}
