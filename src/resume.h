/**********************************************************************************************
*
*   C Learning App - where the user left off
*
*   Remembers the last lesson page or coding challenge the logged-in user opened, so the
*   Sections screen can offer a "Continue where you left off" card. One line in
*   resume_<userId>.txt, next to users.txt and the progress files:
*
*     kind|time|section|key|page|pageCount
*
*   kind is "lesson" (key = lesson file name) or "challenge" (key = challenge number, 0-based).
*   Only the C standard library and users.h are used, so this can be tested without raylib.
*
**********************************************************************************************/

#ifndef RESUME_H
#define RESUME_H

#include <stdbool.h>

typedef enum ResumeKind { RESUME_LESSON, RESUME_CHALLENGE } ResumeKind;

typedef struct ResumePoint {
    ResumeKind kind;
    long long time;             // Unix time it was last opened or moved
    char section[64];           // section name
    char key[64];               // lesson file name, or the challenge number as text
    int page;                   // lesson: 0-based page the user was on
    int pageCount;              // lesson: how many pages it has (0 = unknown)
} ResumePoint;

// Folder that holds the resume files, with a trailing slash ("" = current folder)
void ResumeSetFolder(const char *folder);

// The logged-in user's last position. False if nobody is logged in or nothing was saved yet.
bool ResumeGet(ResumePoint *out);

// Saves a new position for the logged-in user (does nothing when nobody is logged in)
void ResumeSetLesson(const char *section, const char *lessonFile, int page, int pageCount);
void ResumeSetChallenge(const char *section, int challengeIndex);

// Forget what was read (call when the user logs out)
void ResumeUnload(void);

// Deletes a user's resume file (call when the account is deleted)
void ResumeDeleteUser(int userId);

#endif // RESUME_H
