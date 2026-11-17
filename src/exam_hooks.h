/**********************************************************************************************
*
*   C Learning App - progress hooks
*
*   The lesson quiz and the exam screen call these functions when an attempt is finished, with
*   the result of every question. They record the attempt in the logged-in user's progress file
*   (see progress.h). ExamUnlocked is still a stub that always says yes.
*
**********************************************************************************************/

#ifndef EXAM_HOOKS_H
#define EXAM_HOOKS_H

#include <stdbool.h>
#include "progress.h"       // QuestionResult

// Called when the learner finishes a topic's quiz page (every question answered).
// section and topic are indexes into the manifest (GetSection / section->topics).
// results has one entry per question, in the order they were asked.
void OnTopicQuizFinished(int section, int topic, int score, int total, const QuestionResult *results, int count);

// Soft gating: should the section exam be offered as ready? Only changes how the exam row is
// shown on the Topics screen. The stub always returns true. Return false to lock it.
bool ExamUnlocked(int section);

// Called once when the learner finishes an exam attempt.
void OnExamFinished(int section, int correct, int total, bool passed, const QuestionResult *results, int count);

#endif // EXAM_HOOKS_H
