#include "resume.h"
#include "users.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NAME_LEN   64
#define LINE_LEN   512

static char folder[512] = "";
static ResumePoint point;
static bool havePoint = false;
static int loadedUser = 0;          // the user whose file is in memory, 0 = none

//----------------------------------------------------------------------------------
// Helpers
//----------------------------------------------------------------------------------
static void PathFor(int userId, char *out, int size)
{
    snprintf(out, (size_t)size, "%sresume_%d.txt", folder, userId);
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

// "kind|time|section|key|page|pageCount". Splits the line in place.
static bool ParseLine(char *line, ResumePoint *p)
{
    line[strcspn(line, "\r\n")] = '\0';

    char *part[6];
    int count = 0;
    part[count++] = line;
    for (char *c = line; *c != '\0'; c++)
    {
        if (*c != '|') continue;
        *c = '\0';
        if (count >= 6) return false;
        part[count++] = c + 1;
    }
    if (count != 6) return false;

    if (strcmp(part[0], "lesson") == 0) p->kind = RESUME_LESSON;
    else if (strcmp(part[0], "challenge") == 0) p->kind = RESUME_CHALLENGE;
    else return false;

    if (!IsNumber(part[1]) || !IsNumber(part[4]) || !IsNumber(part[5])) return false;
    if (part[2][0] == '\0' || part[3][0] == '\0') return false;

    p->time = strtoll(part[1], NULL, 10);
    snprintf(p->section, sizeof(p->section), "%s", part[2]);
    snprintf(p->key, sizeof(p->key), "%s", part[3]);
    p->page = atoi(part[4]);
    p->pageCount = atoi(part[5]);
    return true;
}

// Reads the logged-in user's file the first time it is needed. False if nobody is logged in.
static bool EnsureLoaded(void)
{
    int id = GetCurrentUserId();
    if (id <= 0)
    {
        havePoint = false;
        loadedUser = 0;
        return false;
    }
    if (id == loadedUser) return true;

    loadedUser = id;
    havePoint = false;

    char path[560];
    PathFor(id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (f == NULL) return true;             // nothing saved yet

    char line[LINE_LEN];
    if (fgets(line, sizeof(line), f) != NULL)
    {
        ResumePoint p;
        if (ParseLine(line, &p)) { point = p; havePoint = true; }
    }
    fclose(f);
    return true;
}

static void Save(void)
{
    char path[560];
    PathFor(loadedUser, path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (f == NULL) return;                  // not being able to save is not worth interrupting the lesson for

    fprintf(f, "%s|%lld|%s|%s|%d|%d\n", (point.kind == RESUME_LESSON) ? "lesson" : "challenge",
            point.time, point.section, point.key, point.page, point.pageCount);
    fclose(f);
}

//----------------------------------------------------------------------------------
// Public functions
//----------------------------------------------------------------------------------
void ResumeSetFolder(const char *path)
{
    snprintf(folder, sizeof(folder), "%s", path);
}

bool ResumeGet(ResumePoint *out)
{
    if (!EnsureLoaded() || !havePoint) return false;
    *out = point;
    return true;
}

void ResumeSetLesson(const char *section, const char *lessonFile, int page, int pageCount)
{
    if (!EnsureLoaded()) return;

    memset(&point, 0, sizeof(point));
    point.kind = RESUME_LESSON;
    point.time = (long long)time(NULL);
    Clean(point.section, sizeof(point.section), section);
    Clean(point.key, sizeof(point.key), lessonFile);
    point.page = (page < 0) ? 0 : page;
    point.pageCount = (pageCount < 0) ? 0 : pageCount;
    havePoint = true;
    Save();
}

void ResumeSetChallenge(const char *section, int challengeIndex)
{
    if (!EnsureLoaded()) return;

    memset(&point, 0, sizeof(point));
    point.kind = RESUME_CHALLENGE;
    point.time = (long long)time(NULL);
    Clean(point.section, sizeof(point.section), section);
    snprintf(point.key, sizeof(point.key), "%d", challengeIndex < 0 ? 0 : challengeIndex);
    havePoint = true;
    Save();
}

void ResumeUnload(void)
{
    havePoint = false;
    loadedUser = 0;
}

void ResumeDeleteUser(int userId)
{
    if (userId <= 0) return;

    char path[560];
    PathFor(userId, path, sizeof(path));
    remove(path);

    if (userId == loadedUser) ResumeUnload();
}
