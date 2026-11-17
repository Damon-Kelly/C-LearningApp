#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "progress.h"     // completed / best score badges
#include "content.h"
#include "exam_hooks.h"
#include "challenge.h"

static GameScreen finishScreen = UNKNOWN;

static const Rectangle backButton = { 60, 480, 140, 40 };

// The list area. Rows are 90px apart and the list scrolls when it is longer than the area.
#define LIST_TOP     100.0f
#define LIST_BOTTOM  468.0f
#define ROW_PITCH    90.0f
#define ROW_HEIGHT   78.0f

#define EXAM_COLOR   ((Color){ 214, 124, 30, 255 })
#define CHALLENGE_COLOR UI_ACCENT2

static UiScroll listScroll = { 0 };
#define LIST_TRACK ((Rectangle){ 912, LIST_TOP, 6, LIST_BOTTOM - LIST_TOP })
static int scrollSection = -1;      // the section the scroll position belongs to, so returning from a lesson keeps the place

// Topics first, then the coding challenges, then (if the section has one) the exam as the last row
static int ChallengeRows(void)
{
    const ChallengeSet *set = SectionChallenges(selectedSection);
    return set ? set->count : 0;
}

static int RowCount(const SectionDef *section)
{
    return section->topicCount + ChallengeRows() + (section->examFile[0] != '\0' ? 1 : 0);
}

static const char *LevelWord(int level)
{
    return level <= 1 ? "Warm-up" : level == 2 ? "Practice" : "Harder";
}

static float MaxScroll(const SectionDef *section)
{
    // Last row's top is at (count-1)*pitch + 10; it needs 56 for the button and about 30 for its summary line
    float listHeight = (RowCount(section) - 1)*ROW_PITCH + 10.0f + ROW_HEIGHT + 10.0f;
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
            else if (i < section->topicCount + ChallengeRows())
            {
                selectedChallenge = i - section->topicCount;
                finishScreen = CHALLENGE;
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
        if (r.y + r.height < LIST_TOP || r.y > LIST_BOTTOM) continue;

        int ci = i - section->topicCount;
        bool isChallenge = (ci >= 0 && ci < ChallengeRows());
        bool isExam = (i >= section->topicCount + ChallengeRows());
        const Challenge *ch = isChallenge ? &SectionChallenges(selectedSection)->items[ci] : NULL;
        bool enabled = !isExam || ExamUnlocked(selectedSection);
        bool hover = enabled && InList() && CheckCollisionPointRec(GetMousePosition(), r);
        Color accent = isExam ? EXAM_COLOR : isChallenge ? CHALLENGE_COLOR : UI_ACCENT;

        // A card: glow and border on hover, a round badge on the left, title and summary
        if (hover) DrawRectangleRounded((Rectangle){ r.x - 3, r.y - 3, r.width + 6, r.height + 6 }, 0.14f, 8, Fade(accent, 0.22f));
        DrawRectangleRounded(r, 0.12f, 8, hover ? accent : UI_BORDER);
        DrawRectangleRounded((Rectangle){ r.x + 1, r.y + 1, r.width - 2, r.height - 2 }, 0.12f, 8, hover ? UI_PANEL2 : UI_PANEL);

        // Progress (from the partner's progress tracking): passed turns the badge green with a tick
        AttemptStats stats;
        bool tried = isExam ? ProgressGetStats(ATTEMPT_EXAM, section->name, "", &stats)
                   : isChallenge ? ProgressGetStats(ATTEMPT_QUIZ, section->name, ChallengeKey(section->challengeFile, ci), &stats)
                   : ProgressGetStats(ATTEMPT_QUIZ, section->name, section->topics[i].file, &stats);
        bool passed = tried && stats.passed;

        Vector2 c = { r.x + 38, r.y + r.height/2 };
        DrawCircleV(c, 20, !enabled ? UI_PANEL2 : passed ? UI_GOOD : accent);
        if (passed)
        {
            DrawLineEx((Vector2){ c.x - 8, c.y + 1 }, (Vector2){ c.x - 2, c.y + 7 }, 3.0f, WHITE);
            DrawLineEx((Vector2){ c.x - 2, c.y + 7 }, (Vector2){ c.x + 9, c.y - 6 }, 3.0f, WHITE);
        }
        else
        {
            const char *badge = isExam ? "Exam" : isChallenge ? "{ }" : TextFormat("%d", i + 1);
            float bsize = isExam ? 14 : isChallenge ? 18 : 20;
            Vector2 bs = UiMeasureText(badge, bsize);
            UiText(badge, (Vector2){ c.x - bs.x/2, c.y - bs.y/2 }, bsize, enabled ? WHITE : UI_MUTED);
        }

        // "Completed 4/5" / "Passed 8/10" in green, or "Best 3/5" while not passed yet
        float badgeW = 0;
        if (tried)
        {
            const char *text = (isChallenge && passed) ? "Solved"
                             : TextFormat("%s  %d/%d", passed ? (isExam ? "Passed" : "Completed") : "Best", stats.bestScore, stats.bestTotal);
            Vector2 ts = UiMeasureText(text, 16);
            badgeW = ts.x + 24;
            Rectangle pill = { r.x + r.width - 44 - badgeW, r.y + r.height/2 - 14, badgeW, 28 };
            Color pc = passed ? UI_GOOD : UI_WARN;
            DrawRectangleRounded(pill, 1.0f, 8, Fade(pc, 0.15f));
            DrawRectangleRoundedLinesEx(pill, 1.0f, 8, 1.0f, Fade(pc, 0.6f));
            UiText(text, (Vector2){ pill.x + 12, pill.y + (pill.height - ts.y)/2 }, 16, pc);
        }

        const char *title = isExam ? "Section exam" : isChallenge ? TextFormat("Challenge: %s", ch->title) : section->topics[i].title;
        const char *summary = isExam ? (enabled ? "Test yourself on the whole section. Retry as often as you like."
                                                : "Finish the quizzes in this section's topics to unlock the exam.")
                            : isChallenge ? TextFormat("%s - write the code, then run the tests on it", LevelWord(ch->level))
                            : section->topics[i].summary;
        UiTextFit(title, (Vector2){ r.x + 76, r.y + 14 }, 22, r.width - 140 - badgeW, enabled ? UI_TEXT : UI_MUTED);
        UiTextFit(summary, (Vector2){ r.x + 76, r.y + 46 }, 16, r.width - 140 - badgeW, UI_MUTED);
        UiText(">", (Vector2){ r.x + r.width - 30, r.y + r.height/2 - 13 }, 24, hover ? accent : UI_BORDER);
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
