#include "exam_hooks.h"

// Stubs: progress tracking is not wired up yet. Replace the bodies when it is.

void OnTopicQuizFinished(int section, int topic, int score, int total)
{
    (void)section; (void)topic; (void)score; (void)total;
}

bool ExamUnlocked(int section)
{
    (void)section;
    return true;
}

void OnExamFinished(int section, int correct, int total, bool passed)
{
    (void)section; (void)correct; (void)total; (void)passed;
}
