/**********************************************************************************************
*
*   C Learning App - progress hooks
*
*   The content side of the app calls these functions at the moments progress tracking cares
*   about. They are stubs for now (see exam_hooks.c); the user-accounts / progress work can
*   fill them in without touching the lesson or exam screens.
*
**********************************************************************************************/

#ifndef EXAM_HOOKS_H
#define EXAM_HOOKS_H

#include <stdbool.h>

// Called when the learner finishes a topic's quiz page (every question answered).
// section and topic are indexes into the manifest (GetSection / section->topics).
void OnTopicQuizFinished(int section, int topic, int score, int total);

// Soft gating: should the section exam be offered as ready? Only changes how the exam row is
// shown on the Topics screen. The stub always returns true. Return false to lock it.
bool ExamUnlocked(int section);

// Called once when the learner finishes an exam attempt.
void OnExamFinished(int section, int correct, int total, bool passed);

#endif // EXAM_HOOKS_H
