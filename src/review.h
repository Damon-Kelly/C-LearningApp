/**********************************************************************************************
*
*   C Learning App - review of missed questions
*
*   Shared by the lesson quiz and the section exam. When an attempt finishes, the screen
*   hands over every question with what the user answered (ReviewAdd). The result screen then
*   offers a "Review answers" button that steps through the questions that were missed, showing
*   the user's answer, the right answer and the explanation.
*
*   The text pointers in a ReviewItem are not copied, so they must stay valid while the user
*   is on the screen (they point into the loaded lesson or exam). Typed answers are copied.
*
**********************************************************************************************/

#ifndef REVIEW_H
#define REVIEW_H

#include <stdbool.h>
#include "content.h"        // MAX_OPTIONS, MAX_ANSWER_LEN

#define MAX_REVIEW_ITEMS  24

typedef struct ReviewItem {
    int number;                         // 1-based position of the question in the attempt
    const char *prompt;
    const char *code;                   // optional code shown with the question, or NULL
    const char *lang;                   // "c" or "cpp" (only used with code)
    const char *why;                    // explanation, or NULL
    bool wasCorrect;

    bool typed;                         // false: multiple choice, true: typed answer
    const char *options[MAX_OPTIONS];   // choice: the options in their original order
    int optionCount;
    int correct;                        // choice: index of the right option
    int picked;                         // choice: index the user picked
    char typedAnswer[MAX_ANSWER_LEN];   // typed: what the user typed
    const char *modelAnswer;            // typed: one accepted answer
} ReviewItem;

// Forget everything (start of an attempt)
void ReviewClear(void);

// Remember one answered question (the item is copied)
void ReviewAdd(const ReviewItem *item);

// How many of the questions were missed
int ReviewMissedCount(void);

// Start stepping through the missed questions. False if there are none.
bool ReviewOpen(void);

// Call every frame while reviewing. Returns true on the frame the user leaves the review.
bool ReviewUpdate(void);

// Draws the review content and its buttons (Previous / Back to results / Next mistake)
void ReviewDraw(void);

#endif // REVIEW_H
