/**********************************************************************************************
*
*   C Learning App - what to offer on the "Continue where you left off" card
*
*   NextUpFind() combines three things the app already knows:
*     - where the user last was (resume.h: a lesson page or a coding challenge),
*     - what they have already passed (progress.h),
*     - the section list (content.h).
*
*   Rules, in order:
*     1. The last lesson or challenge is not finished yet      -> NEXTUP_CONTINUE (same page)
*     2. It is finished (quiz passed / challenge solved)       -> NEXTUP_UPNEXT: the next topic
*        whose quiz is not passed, going on from where they were and wrapping round to the start
*     3. Nothing recorded at all                               -> NEXTUP_START: the first topic
*     4. Every topic quiz is passed                            -> NEXTUP_DONE
*     5. No sections loaded                                    -> NEXTUP_NONE (the card is hidden)
*
**********************************************************************************************/

#ifndef NEXTUP_H
#define NEXTUP_H

#include <stdbool.h>

typedef enum NextUpMode { NEXTUP_NONE, NEXTUP_START, NEXTUP_CONTINUE, NEXTUP_UPNEXT, NEXTUP_DONE } NextUpMode;
typedef enum NextUpKind { NEXTUP_LESSON, NEXTUP_CHALLENGE } NextUpKind;

typedef struct NextUp {
    NextUpMode mode;
    NextUpKind kind;
    int section;                // index in the section list
    int topic;                  // lesson: topic index; challenge: challenge index
    int page, pageCount;        // lesson: 0-based page to open, and how many pages (0 = unknown)
    long long time;             // CONTINUE only: when it was last opened (Unix time)
    char title[112];            // topic title, or "Challenge: <title>"
    char sectionName[48];
    int topicNumber, topicTotal;    // lesson: "topic 3 of 5" within its section
    bool tried;                 // lesson: its quiz has been attempted (but not passed)
    int bestScore, bestTotal;   // with tried
} NextUp;

void NextUpFind(NextUp *out);

#endif // NEXTUP_H
