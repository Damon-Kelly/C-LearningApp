#include "progress.h"
#include "users.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ATTEMPTS     400        // the most recent ones are kept in memory
#define MAX_RESULTS      32         // questions remembered per attempt
#define PROGRESS_LINE    1024
#define NAME_LEN         64

typedef struct Attempt {
    AttemptKind kind;
    long long time;
    char section[NAME_LEN];
    char topic[NAME_LEN];
    int score, total;
    bool passed;
    int count;
    unsigned keys[MAX_RESULTS];
    bool correct[MAX_RESULTS];
} Attempt;

static char folder[512] = "";
static Attempt attempts[MAX_ATTEMPTS];
static int attemptCount = 0;
static int loadedUser = 0;          // the user whose file is in memory, 0 = none

//----------------------------------------------------------------------------------
// Helpers
//----------------------------------------------------------------------------------
static void PathFor(int userId, char *out, int size)
{
    snprintf(out, (size_t)size, "%sprogress_%d.txt", folder, userId);
}

// Names go into a '|' separated file, so those characters are replaced
static void Clean(char *dst, int dstSize, const char *src)
{
    int n = 0;
    for (; *src && n < dstSize - 1; src++) dst[n++] = (*src == '|' || *src == '\n' || *src == '\r') ? '/' : *src;
    dst[n] = '\0';
}

static bool IsNumber(const char *s)
{
    if (*s == '\0') return false;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return false;
    return true;
}

unsigned ProgressQuestionKey(const char *prompt)
{
    uint32_t hash = 2166136261u;
    bool started = false, pendingSpace = false;

    for (const unsigned char *p = (const unsigned char *)prompt; *p; p++)
    {
        if (isspace(*p)) { pendingSpace = started; continue; }

        if (pendingSpace) { hash = (hash ^ (uint32_t)' ')*16777619u; pendingSpace = false; }
        hash = (hash ^ (uint32_t)tolower(*p))*16777619u;
        started = true;
    }
    return (unsigned)hash;
}

//----------------------------------------------------------------------------------
// File reading
//----------------------------------------------------------------------------------
// "kind|time|section|topic|score|total|passed|key:1,key:0,..." Splits the line in place.
static bool ParseLine(char *line, Attempt *a)
{
    line[strcspn(line, "\r\n")] = '\0';

    char *part[8];
    int count = 0;
    part[count++] = line;
    for (char *c = line; *c != '\0'; c++)
    {
        if (*c != '|') continue;
        *c = '\0';
        if (count >= 8) return false;
        part[count++] = c + 1;
    }
    if (count != 8) return false;

    if (strcmp(part[0], "quiz") == 0) a->kind = ATTEMPT_QUIZ;
    else if (strcmp(part[0], "exam") == 0) a->kind = ATTEMPT_EXAM;
    else return false;

    if (!IsNumber(part[1]) || !IsNumber(part[4]) || !IsNumber(part[5])) return false;

    a->time = strtoll(part[1], NULL, 10);
    snprintf(a->section, sizeof(a->section), "%s", part[2]);
    snprintf(a->topic, sizeof(a->topic), "%s", part[3]);
    a->score = atoi(part[4]);
    a->total = atoi(part[5]);
    a->passed = (part[6][0] == '1');

    // key:1,key:0,...
    a->count = 0;
    char *p = part[7];
    while (*p != '\0' && a->count < MAX_RESULTS)
    {
        char *end;
        unsigned long key = strtoul(p, &end, 16);
        if (end == p || *end != ':' || (end[1] != '0' && end[1] != '1')) break;

        a->keys[a->count] = (unsigned)key;
        a->correct[a->count] = (end[1] == '1');
        a->count++;

        p = end + 2;
        if (*p == ',') p++;
    }
    return true;
}

static void AddToMemory(const Attempt *a)
{
    if (attemptCount >= MAX_ATTEMPTS)
    {
        memmove(&attempts[0], &attempts[1], sizeof(Attempt)*(size_t)(MAX_ATTEMPTS - 1));
        attemptCount = MAX_ATTEMPTS - 1;
    }
    attempts[attemptCount++] = *a;
}

// Reads the logged-in user's file the first time it is needed. False if nobody is logged in.
static bool EnsureLoaded(void)
{
    int id = GetCurrentUserId();
    if (id <= 0)
    {
        attemptCount = 0;
        loadedUser = 0;
        return false;
    }
    if (id == loadedUser) return true;

    attemptCount = 0;
    loadedUser = id;

    char path[560];
    PathFor(id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (f == NULL) return true;             // no file yet: nothing recorded

    // Only the newest MAX_ATTEMPTS lines are kept
    char line[PROGRESS_LINE];
    int lines = 0;
    while (fgets(line, sizeof(line), f) != NULL) lines++;
    rewind(f);

    int skip = (lines > MAX_ATTEMPTS) ? lines - MAX_ATTEMPTS : 0;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (skip > 0) { skip--; continue; }

        Attempt a;
        if (ParseLine(line, &a)) AddToMemory(&a);
    }
    fclose(f);
    return true;
}

//----------------------------------------------------------------------------------
// Public functions
//----------------------------------------------------------------------------------
void ProgressSetFolder(const char *path)
{
    snprintf(folder, sizeof(folder), "%s", path);
}

bool ProgressRecordAttempt(AttemptKind kind, const char *section, const char *topic,
                           int score, int total, bool passed, const QuestionResult *results, int count)
{
    if (!EnsureLoaded()) return false;

    Attempt a;
    memset(&a, 0, sizeof(a));
    a.kind = kind;
    a.time = (long long)time(NULL);
    Clean(a.section, sizeof(a.section), section);
    Clean(a.topic, sizeof(a.topic), topic);
    a.score = score;
    a.total = total;
    a.passed = passed;
    for (int i = 0; i < count && a.count < MAX_RESULTS; i++)
    {
        a.keys[a.count] = ProgressQuestionKey(results[i].prompt);
        a.correct[a.count] = results[i].correct;
        a.count++;
    }

    AddToMemory(&a);

    char path[560];
    PathFor(loadedUser, path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (f == NULL) return false;

    fprintf(f, "%s|%lld|%s|%s|%d|%d|%d|", (kind == ATTEMPT_QUIZ) ? "quiz" : "exam", a.time, a.section, a.topic, a.score, a.total, a.passed ? 1 : 0);
    for (int i = 0; i < a.count; i++) fprintf(f, "%s%08x:%d", (i > 0) ? "," : "", a.keys[i], a.correct[i] ? 1 : 0);
    fprintf(f, "\n");

    return fclose(f) == 0;
}

bool ProgressGetStats(AttemptKind kind, const char *section, const char *topic, AttemptStats *out)
{
    memset(out, 0, sizeof(*out));
    if (!EnsureLoaded()) return false;

    char s[NAME_LEN], t[NAME_LEN];
    Clean(s, sizeof(s), section);
    Clean(t, sizeof(t), topic);

    for (int i = 0; i < attemptCount; i++)
    {
        const Attempt *a = &attempts[i];
        if (a->kind != kind || strcmp(a->section, s) != 0 || strcmp(a->topic, t) != 0) continue;

        // Best = highest percentage (compared by cross-multiplying, so no division by zero)
        bool better = (out->attempts == 0) || ((long long)a->score*out->bestTotal > (long long)out->bestScore*a->total);
        if (better) { out->bestScore = a->score; out->bestTotal = a->total; }

        out->lastScore = a->score;
        out->lastTotal = a->total;
        if (a->passed) out->passed = true;
        out->attempts++;
    }
    return out->attempts > 0;
}

void ProgressUnload(void)
{
    attemptCount = 0;
    loadedUser = 0;
}

void ProgressDeleteUser(int userId)
{
    if (userId <= 0) return;

    char path[560];
    PathFor(userId, path, sizeof(path));
    remove(path);

    if (userId == loadedUser) ProgressUnload();
}
