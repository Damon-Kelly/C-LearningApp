#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"
#include "exam_hooks.h"

static GameScreen finishScreen = UNKNOWN;

static const Rectangle backButton = { 60, 480, 140, 40 };

// The list area. Rows are 90px apart and the list scrolls when it is longer than the area.
#define LIST_TOP     100.0f
#define LIST_BOTTOM  468.0f
#define ROW_PITCH    90.0f
#define ROW_HEIGHT   56.0f

#define EXAM_COLOR   ((Color){ 214, 124, 30, 255 })

static UiScroll listScroll = { 0 };
#define LIST_TRACK ((Rectangle){ 912, LIST_TOP, 6, LIST_BOTTOM - LIST_TOP })
static int scrollSection = -1;      // the section the scroll position belongs to, so returning from a lesson keeps the place

// Topics first, then (if the section has one) the exam as the last row
static int RowCount(const SectionDef *section)
{
    return section->topicCount + (section->examFile[0] != '\0' ? 1 : 0);
}

static float MaxScroll(const SectionDef *section)
{
    // Last row's top is at (count-1)*pitch + 10; it needs 56 for the button and about 30 for its summary line
    float listHeight = (RowCount(section) - 1)*ROW_PITCH + 10.0f + ROW_HEIGHT + 30.0f;
    float max = listHeight - (LIST_BOTTOM - LIST_TOP);
    return (max > 0) ? max : 0;
}

static Rectangle RowRect(int i)
{
    return (Rectangle){ 60, LIST_TOP + 10.0f + i*ROW_PITCH - listScroll.offset, 840, ROW_HEIGHT };
}

static bool InList(void)
{
    return CheckCollisionPointRec(GetMousePosition(), (Rectangle){ 0, LIST_TOP, 960, LIST_BOTTOM - LIST_TOP });
}

void InitTopicsScreen(void)
{
    finishScreen = UNKNOWN;
    if (scrollSection != selectedSection)
    {
        listScroll.offset = 0.0f;
        scrollSection = selectedSection;
    }
}

void UpdateTopicsScreen(void)
{
    const SectionDef *section = GetSection(selectedSection);

    if (section != NULL)
    {
        UiScrollUpdate(&listScroll, LIST_TRACK, LIST_BOTTOM - LIST_TOP, MaxScroll(section), 45.0f);

        for (int i = 0; i < RowCount(section); i++)
        {
            if (!InList() || !UiButtonClicked(RowRect(i))) continue;

            if (i < section->topicCount)
            {
                selectedTopic = i;
                finishScreen = LESSON;
            }
            else if (ExamUnlocked(selectedSection)) finishScreen = EXAM;
        }
    }

    if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = SECTIONS;
}

void DrawTopicsScreen(void)
{
    const SectionDef *section = GetSection(selectedSection);
    if (section == NULL) return;

    UiDrawHeader(section->name);
    UiDrawLoggedInAs();

    UiBeginScissor((Rectangle){ 0, LIST_TOP, GAME_WIDTH, LIST_BOTTOM - LIST_TOP });

    for (int i = 0; i < RowCount(section); i++)
    {
        Rectangle r = RowRect(i);
        if (r.y + r.height + 30 < LIST_TOP || r.y > LIST_BOTTOM) continue;

        if (i < section->topicCount)
        {
            const TopicRef *topic = &section->topics[i];
            UiButtonDraw(r, topic->title);
            UiText(topic->summary, (Vector2){ r.x + 8, r.y + r.height + 6 }, 18, UI_MUTED);
        }
        else
        {
            bool unlocked = ExamUnlocked(selectedSection);
            UiButtonDrawEx(r, "Section exam", unlocked ? EXAM_COLOR : UI_MUTED, unlocked);
            const char *summary = unlocked ? "Test yourself on the whole section. Retry as often as you like."
                                           : "Finish the quizzes in this section's topics to unlock the exam.";
            UiText(summary, (Vector2){ r.x + 8, r.y + r.height + 6 }, 18, UI_MUTED);
        }
    }

    EndScissorMode();

    float max = MaxScroll(section);
    if (max > 0)
    {
        UiScrollDraw(&listScroll, LIST_TRACK, LIST_BOTTOM - LIST_TOP, max);
        if (listScroll.offset < max - 1.0f) UiText("scroll for more", (Vector2){ 760, 490 }, 14, UI_MUTED);
    }

    UiButtonDrawEx(backButton, "Back", UI_MUTED, true);
}

void UnloadTopicsScreen(void)
{
}

GameScreen FinishTopicsScreen(void)
{
    return finishScreen;
}
