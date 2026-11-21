#include "nextup.h"
#include "screens.h"        // SectionChallenges
#include "content.h"
#include "challenge.h"
#include "progress.h"
#include "resume.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FLAT  (MAX_SECTIONS*MAX_TOPICS_PER_SECTION)

// Every topic of every section as one list, in lesson order
static int flatSection[MAX_FLAT];
static int flatTopic[MAX_FLAT];
static int flatCount = 0;

static void BuildFlat(void)
{
    flatCount = 0;
    for (int i = 0; i < GetSectionCount(); i++)
    {
        const SectionDef *s = GetSection(i);
        for (int t = 0; t < s->topicCount && flatCount < MAX_FLAT; t++)
        {
            flatSection[flatCount] = i;
            flatTopic[flatCount] = t;
            flatCount++;
        }
    }
}

static int FlatIndex(int section, int topic)
{
    for (int i = 0; i < flatCount; i++) if (flatSection[i] == section && flatTopic[i] == topic) return i;
    return -1;
}

static int FindSection(const char *name)
{
    for (int i = 0; i < GetSectionCount(); i++) if (strcmp(GetSection(i)->name, name) == 0) return i;
    return -1;
}

static int FindTopic(const SectionDef *s, const char *file)
{
    for (int t = 0; t < s->topicCount; t++) if (strcmp(s->topics[t].file, file) == 0) return t;
    return -1;
}

static void FillLesson(NextUp *out, NextUpMode mode, int section, int topic)
{
    const SectionDef *s = GetSection(section);
    out->mode = mode;
    out->kind = NEXTUP_LESSON;
    out->section = section;
    out->topic = topic;
    snprintf(out->title, sizeof(out->title), "%s", s->topics[topic].title);
    snprintf(out->sectionName, sizeof(out->sectionName), "%s", s->name);
    out->topicNumber = topic + 1;
    out->topicTotal = s->topicCount;

    AttemptStats st;
    if (ProgressGetStats(ATTEMPT_QUIZ, s->name, s->topics[topic].file, &st))
    {
        out->tried = true;
        out->bestScore = st.bestScore;
        out->bestTotal = st.bestTotal;
    }
}

void NextUpFind(NextUp *out)
{
    memset(out, 0, sizeof(*out));
    out->mode = NEXTUP_NONE;

    BuildFlat();
    if (flatCount == 0) return;

    ResumePoint r;
    bool have = ResumeGet(&r);
    int last = -1;          // flat index of the topic the search goes on after

    if (have)
    {
        int si = FindSection(r.section);
        if (si >= 0)
        {
            const SectionDef *s = GetSection(si);

            if (r.kind == RESUME_LESSON)
            {
                int ti = FindTopic(s, r.key);
                if (ti >= 0)
                {
                    last = FlatIndex(si, ti);
                    AttemptStats st;
                    bool passed = ProgressGetStats(ATTEMPT_QUIZ, s->name, s->topics[ti].file, &st) && st.passed;
                    if (!passed)
                    {
                        FillLesson(out, NEXTUP_CONTINUE, si, ti);
                        out->time = r.time;
                        out->pageCount = r.pageCount;
                        out->page = r.page;
                        if (out->pageCount > 0 && out->page >= out->pageCount) out->page = out->pageCount - 1;
                        return;
                    }
                }
            }
            else
            {
                int ci = atoi(r.key);
                const ChallengeSet *set = SectionChallenges(si);
                if (set != NULL && ci >= 0 && ci < set->count)
                {
                    AttemptStats cs;
                    bool solved = ProgressGetStats(ATTEMPT_QUIZ, s->name, ChallengeKey(s->challengeFile, ci), &cs) && cs.passed;
                    if (!solved)
                    {
                        out->mode = NEXTUP_CONTINUE;
                        out->kind = NEXTUP_CHALLENGE;
                        out->section = si;
                        out->topic = ci;
                        out->time = r.time;
                        snprintf(out->title, sizeof(out->title), "Challenge: %s", set->items[ci].title);
                        snprintf(out->sectionName, sizeof(out->sectionName), "%s", s->name);
                        return;
                    }
                    // Solved: carry on with this section's topics, from its first one
                    int first = FlatIndex(si, 0);
                    if (first >= 0) last = first - 1;
                }
            }
        }
    }

    // The next topic whose quiz is not passed, after `last`, wrapping round once
    for (int k = 1; k <= flatCount; k++)
    {
        int idx = (last + k + flatCount) % flatCount;
        const SectionDef *s = GetSection(flatSection[idx]);
        AttemptStats st;
        bool passed = ProgressGetStats(ATTEMPT_QUIZ, s->name, s->topics[flatTopic[idx]].file, &st) && st.passed;
        if (passed) continue;

        bool started = have || ProgressAttemptCount() > 0;
        FillLesson(out, started ? NEXTUP_UPNEXT : NEXTUP_START, flatSection[idx], flatTopic[idx]);
        return;
    }

    out->mode = NEXTUP_DONE;
}
