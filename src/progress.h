/**********************************************************************************************
*
*   C Learning App - saved progress (quiz and exam attempts)
*
*   Every finished quiz or exam is one line in progress_<userId>.txt, next to users.txt:
*
*     kind|time|section|topic|score|total|passed|key:1,key:0,...
*
*   kind is "quiz" or "exam"; topic is the lesson file name ("" for an exam); the last field
*   has one entry per question: a key (a hash of the question text) and 1 = right, 0 = wrong.
*   Everything else (best score, completed topics, ...) is worked out when the file is read.
*   Only the C standard library and users.h are used, so this can be tested without raylib.
*
**********************************************************************************************/

#ifndef PROGRESS_H
#define PROGRESS_H

#include <stdbool.h>

// A topic quiz counts as passed at this score (exams use their own pass mark)
#define PROGRESS_PASS_PERCENT  70

// One answered question, as reported by the quiz and exam screens
typedef struct QuestionResult {
    const char *prompt;
    bool correct;
} QuestionResult;

typedef enum AttemptKind { ATTEMPT_QUIZ, ATTEMPT_EXAM } AttemptKind;

// Summary of all attempts at one topic quiz or one section exam
typedef struct AttemptStats {
    int attempts;
    int bestScore, bestTotal;       // the attempt with the highest percentage
    int lastScore, lastTotal;
    bool passed;                    // at least one attempt passed
} AttemptStats;

// Folder that holds the progress files, with a trailing slash ("" = current folder)
void ProgressSetFolder(const char *folder);

// Adds a finished attempt for the logged-in user. section is the section name, topic the lesson
// file name ("" for an exam). False if nobody is logged in or the file could not be written.
bool ProgressRecordAttempt(AttemptKind kind, const char *section, const char *topic,
                           int score, int total, bool passed, const QuestionResult *results, int count);

// Summary for the logged-in user. False if there are no attempts yet.
bool ProgressGetStats(AttemptKind kind, const char *section, const char *topic, AttemptStats *out);

// One finished attempt, as listed on the Progress screen
typedef struct AttemptRecord {
    AttemptKind kind;
    long long time;                 // Unix time
    char section[64];
    char topic[64];                 // lesson file name, "" for an exam
    int score, total;
    bool passed;
} AttemptRecord;

// Fills out with the logged-in user's most recent attempts, newest first. Returns how many (<= max).
int ProgressRecent(AttemptRecord *out, int max);

// How many attempts the logged-in user has made (the most recent 400 are counted)
int ProgressAttemptCount(void);

// Forget what was read (call when the user logs out)
void ProgressUnload(void);

// Deletes a user's progress file (call when the account is deleted)
void ProgressDeleteUser(int userId);

// Same text gives the same key; case and extra spaces are ignored
unsigned ProgressQuestionKey(const char *prompt);

#endif // PROGRESS_H
