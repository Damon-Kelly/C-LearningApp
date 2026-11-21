/**********************************************************************************************
*
*   Sections: the home screen after logging in.
*
*   A "Continue where you left off" card on top (see nextup.h for how it chooses), then one card
*   per section. Cards and the hero fade up one after another when the screen opens, and glow
*   smoothly under the mouse.
*
**********************************************************************************************/

#include "raylib.h"
#include "screens.h"
#include "ui.h"
#include "content.h"
#include "nextup.h"

#include <math.h>
#include <time.h>

static GameScreen finishScreen = UNKNOWN;

static const Rectangle backButton     = { 60, 480, 140, 40 };       // acts as "Log out"
static const Rectangle progressButton = { 500, 480, 120, 40 };
static const Rectangle profileButton  = { 640, 480, 120, 40 };
static const Rectangle settingsButton = { 780, 480, 120, 40 };
static const Rectangle visualizerButton = { 220, 480, 240, 40 };

static const Rectangle heroRect = { 60, 108, 840, 82 };

static NextUp nextUp;                       // what the hero card offers, worked out when the screen opens

// Two columns of section cards under the hero card, up to 4 rows (8 sections). The cards shrink a
// little when there are many rows, so everything fits above the buttons.
#define CARD_W        410.0f
#define CARDS_BOTTOM  470.0f
#define CARD_GAP      8.0f

static float cardsTop = 112.0f;
static float cardH = 84.0f;
static float cardPitch = 92.0f;

static void ComputeLayout(void)
{
    cardsTop = (nextUp.mode != NEXTUP_NONE) ? 202.0f : 112.0f;

    int rows = (GetSectionCount() + 1)/2;
    if (rows < 1) rows = 1;

    float h = (CARDS_BOTTOM - cardsTop - (rows - 1)*CARD_GAP)/(float)rows;
    if (h > 84.0f) h = 84.0f;
    if (h < 62.0f) h = 62.0f;

    cardH = h;
    cardPitch = h + CARD_GAP;
}

static Rectangle CardRect(int i)
{
    return (Rectangle){ 60.0f + (i % 2)*430.0f, cardsTop + (i / 2)*cardPitch, CARD_W, cardH };
}

//----------------------------------------------------------------------------------
// The hero card
//----------------------------------------------------------------------------------
static const char *AgoText(long long when)
{
    long long diff = (long long)time(NULL) - when;
    if (diff < 0) diff = 0;

    if (diff < 60) return "just now";
    if (diff < 3600) return TextFormat("%d min ago", (int)(diff/60));
    if (diff < 86400) return TextFormat("%d h ago", (int)(diff/3600));

    int days = (int)(diff/86400);
    if (days == 1) return "yesterday";
    return TextFormat("%d days ago", days);
}

static const char *HeroLabel(void)
{
    switch (nextUp.mode)
    {
        case NEXTUP_START: return "START LEARNING";
        case NEXTUP_CONTINUE: return "CONTINUE WHERE YOU LEFT OFF";
        case NEXTUP_UPNEXT: return "UP NEXT";
        default: return "ALL TOPIC QUIZZES PASSED";
    }
}

static const char *HeroTitle(void)
{
    return (nextUp.mode == NEXTUP_DONE) ? "Nice work, every topic is done" : nextUp.title;
}

static const char *HeroSubtitle(void)
{
    const NextUp *n = &nextUp;

    if (n->mode == NEXTUP_DONE) return "Try a section exam or a coding challenge, or look back over your progress.";

    if (n->kind == NEXTUP_CHALLENGE) return TextFormat("%s  -  coding challenge  -  %s", n->sectionName, AgoText(n->time));

    if (n->mode == NEXTUP_CONTINUE) return TextFormat("%s  -  topic %d of %d  -  %s", n->sectionName, n->topicNumber, n->topicTotal, AgoText(n->time));
    if (n->tried) return TextFormat("%s  -  topic %d of %d  -  quiz not passed yet", n->sectionName, n->topicNumber, n->topicTotal);
    return TextFormat("%s  -  topic %d of %d", n->sectionName, n->topicNumber, n->topicTotal);
}

static const char *HeroButtonText(void)
{
    switch (nextUp.mode)
    {
        case NEXTUP_CONTINUE: return "Continue  >";
        case NEXTUP_DONE: return "View progress";
        default: return "Start  >";
    }
}

static void OpenNextUp(void)
{
    const NextUp *n = &nextUp;

    if (n->mode == NEXTUP_DONE) finishScreen = PROGRESS;
    else if (n->kind == NEXTUP_CHALLENGE)
    {
        selectedSection = n->section;
        selectedChallenge = n->topic;
        finishScreen = CHALLENGE;
    }
    else
    {
        LessonOpenAt(n->section, n->topic, (n->mode == NEXTUP_CONTINUE) ? n->page : 0);
        finishScreen = LESSON;
    }
}

static void DrawHero(void)
{
    const NextUp *n = &nextUp;
    if (n->mode == NEXTUP_NONE) return;

    float enter = UiEnter(0.04f, 0.45f);
    Rectangle r = heroRect;
    r.y += (1.0f - enter)*14.0f;

    float h = UiHover(heroRect, CheckCollisionPointRec(GetMousePosition(), heroRect));

    // Card: glow, border, fill, and a thin accent line along the top
    if (h > 0.01f) DrawRectangleRounded((Rectangle){ r.x - 4, r.y - 4, r.width + 8, r.height + 8 }, 0.12f, 8, Fade(UI_ACCENT, 0.22f*h));
    DrawRectangleRounded(r, 0.12f, 8, UiMix(UI_BORDER, UI_ACCENT, 0.35f + 0.65f*h));
    DrawRectangleRounded((Rectangle){ r.x + 1, r.y + 1, r.width - 2, r.height - 2 }, 0.12f, 8, UiMix(UI_PANEL, UI_PANEL2, h));
    DrawRectangleGradientH((int)r.x + 14, (int)r.y + 1, (int)r.width - 28, 2, UI_ACCENT, UI_ACCENT2);

    // Round icon on the left with a soft pulse: a play triangle, or a tick when everything is done
    Vector2 c = { r.x + 46, r.y + r.height/2 };
    bool done = (n->mode == NEXTUP_DONE);
    Color iconColor = done ? UI_GOOD : UI_ACCENT;
    float phase = fmodf((float)GetTime(), 1.8f)/1.8f;
    DrawCircleV(c, 27.0f + 9.0f*phase, Fade(iconColor, 0.28f*(1.0f - phase)));
    DrawCircleV(c, 27.0f, UiMix(iconColor, WHITE, 0.15f*h));
    if (done)
    {
        DrawLineEx((Vector2){ c.x - 10, c.y + 1 }, (Vector2){ c.x - 3, c.y + 8 }, 4.0f, WHITE);
        DrawLineEx((Vector2){ c.x - 3, c.y + 8 }, (Vector2){ c.x + 11, c.y - 8 }, 4.0f, WHITE);
    }
    else DrawTriangle((Vector2){ c.x - 7, c.y - 12 }, (Vector2){ c.x - 7, c.y + 12 }, (Vector2){ c.x + 13, c.y }, WHITE);

    // Button on the right, and (for a lesson in progress) a page bar next to it
    Rectangle button = { r.x + r.width - 16 - 150, r.y + (r.height - 40)/2, 150, 40 };
    UiButtonDrawEx(button, HeroButtonText(), done ? UI_GOOD : UI_ACCENT, true);

    bool showBar = (n->mode == NEXTUP_CONTINUE && n->kind == NEXTUP_LESSON && n->pageCount > 0);
    bool showBest = (n->mode == NEXTUP_UPNEXT && n->tried);
    float textRight = button.x - 20;
    if (showBar || showBest)
    {
        Rectangle bar = { button.x - 24 - 130, r.y + 50, 130, 8 };
        const char *caption = showBar ? TextFormat("Page %d of %d", n->page + 1, n->pageCount) : TextFormat("Best quiz %d/%d", n->bestScore, n->bestTotal);
        Vector2 cs = UiMeasureText(caption, 13);
        UiText(caption, (Vector2){ bar.x + bar.width - cs.x, r.y + 28 }, 13, UI_MUTED);

        DrawRectangleRounded(bar, 1.0f, 6, UI_PANEL2);
        float fraction = showBar ? (float)(n->page + 1)/(float)n->pageCount : (float)n->bestScore/(float)(n->bestTotal > 0 ? n->bestTotal : 1);
        if (fraction > 0.0f) DrawRectangleRounded((Rectangle){ bar.x, bar.y, bar.width*fraction*enter, bar.height }, 1.0f, 6, showBar ? UI_ACCENT : UI_WARN);
        textRight = bar.x - 20;
    }

    // Text
    float tx = r.x + 92;
    UiText(HeroLabel(), (Vector2){ tx, r.y + 10 }, 13, done ? UI_GOOD : UI_ACCENT);
    UiTextFit(HeroTitle(), (Vector2){ tx, r.y + 27 }, 22, textRight - tx, UI_TEXT);
    UiTextFit(HeroSubtitle(), (Vector2){ tx, r.y + 56 }, 15, textRight - tx, UI_MUTED);

    UiFadeIn(r, enter);
}

//----------------------------------------------------------------------------------
// Screen functions
//----------------------------------------------------------------------------------
void InitSectionsScreen(void)
{
    finishScreen = UNKNOWN;
    NextUpFind(&nextUp);
    ComputeLayout();
}

void UpdateSectionsScreen(void)
{
    ComputeLayout();

    for (int i = 0; i < GetSectionCount(); i++)
    {
        const SectionDef *section = GetSection(i);
        if (section->topicCount > 0 && UiButtonClicked(CardRect(i)))
        {
            selectedSection = i;
            finishScreen = TOPICS;
        }
    }

    if (nextUp.mode != NEXTUP_NONE && UiButtonClicked(heroRect)) OpenNextUp();

    if (UiButtonClicked(progressButton)) finishScreen = PROGRESS;
    if (UiButtonClicked(visualizerButton)) { OpenVisualizerHome(); finishScreen = VISUALIZER; }
    if (UiButtonClicked(profileButton)) finishScreen = PROFILE;
    if (UiButtonClicked(settingsButton)) finishScreen = SETTINGS;

    // Going back to the title screen logs the user out
    if (UiButtonClicked(backButton) || IsKeyPressed(KEY_ESCAPE)) finishScreen = TITLE;
}

void DrawSectionsScreen(void)
{
    ComputeLayout();

    UiDrawHeader("Sections");
    UiDrawLoggedInAs();

    if (GetContentError()[0] != '\0')
    {
        UiDrawWrappedText(TextFormat("Problem loading sections.txt: %s", GetContentError()), (Rectangle){ 60, 112, 840, 200 }, 20, UI_BAD);
    }

    DrawHero();

    const bool compact = (cardH < 72.0f);

    for (int i = 0; i < GetSectionCount(); i++)
    {
        const SectionDef *section = GetSection(i);
        Rectangle base = CardRect(i);
        bool available = section->topicCount > 0;

        // Cards fade up one after another when the screen opens
        float enter = UiEnter(0.12f + i*0.05f, 0.40f);
        Rectangle r = base;
        r.y += (1.0f - enter)*14.0f;

        float h = available ? UiHover(base, CheckCollisionPointRec(GetMousePosition(), base)) : 0.0f;

        // Card: glow, then border, then fill, so the border colour can glide with the mouse
        if (h > 0.01f) DrawRectangleRounded((Rectangle){ r.x - 3, r.y - 3, r.width + 6, r.height + 6 }, 0.12f, 8, Fade(UI_ACCENT, 0.25f*h));
        DrawRectangleRounded(r, 0.1f, 8, UiMix(UI_BORDER, UI_ACCENT, h));
        DrawRectangleRounded((Rectangle){ r.x + 1, r.y + 1, r.width - 2, r.height - 2 }, 0.1f, 8, available ? UiMix(UI_PANEL, UI_PANEL2, h) : UI_BG);

        Color titleColor = available ? UI_TEXT : UI_MUTED;
        UiText(TextFormat("%d. %s", i + 1, section->name), (Vector2){ r.x + 16, r.y + (compact ? 7.0f : 10.0f) }, 24, titleColor);

        // Status sits on the title line, right-aligned, so the description gets the rest of the card
        const char *status = available ? TextFormat("%d topic%s", section->topicCount, section->topicCount == 1 ? "" : "s") : "Coming soon";
        Vector2 statusSize = UiMeasureText(status, 16);
        UiText(status, (Vector2){ r.x + r.width - 16 - statusSize.x, r.y + (compact ? 12.0f : 16.0f) }, 16, available ? UI_ACCENT : UI_MUTED);

        float descY = compact ? 36.0f : 44.0f;
        UiDrawWrappedText(section->description, (Rectangle){ r.x + 16, r.y + descY, r.width - 32, r.height - descY - 4 }, 16, UI_MUTED);

        UiFadeIn(r, enter);
    }

    UiButtonDrawEx(backButton, "Log out", UI_MUTED, true);
    UiButtonDraw(progressButton, "Progress");
    UiButtonDrawEx(visualizerButton, "Open the Visualizer", UI_ACCENT2, true);
    UiButtonDraw(profileButton, "Profile");
    UiButtonDraw(settingsButton, "Settings");
}

void UnloadSectionsScreen(void)
{
}

GameScreen FinishSectionsScreen(void)
{
    return finishScreen;
}
