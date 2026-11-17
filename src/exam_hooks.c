#include "exam_hooks.h"
#include "content.h"        // GetSection

void OnTopicQuizFinished(int section, int topic, int score, int total, const QuestionResult *results, int count)
{
    const SectionDef *def = GetSection(section);
    if (def == NULL || topic < 0 || topic >= def->topicCount) return;

    bool passed = (total > 0) && (score*100 >= PROGRESS_PASS_PERCENT*total);
    ProgressRecordAttempt(ATTEMPT_QUIZ, def->name, def->topics[topic].file, score, total, passed, results, count);
}

// Soft gating is not wired up yet: the exam is always available
bool ExamUnlocked(int section)
{
    (void)section;
    return true;
}

void OnExamFinished(int section, int correct, int total, bool passed, const QuestionResult *results, int count)
{
    const SectionDef *def = GetSection(section);
    if (def == NULL) return;

    ProgressRecordAttempt(ATTEMPT_EXAM, def->name, "", correct, total, passed, results, count);
}
