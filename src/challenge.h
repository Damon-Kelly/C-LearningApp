/**********************************************************************************************
*
*   C Learning App - coding challenges: write a function, the app runs hidden tests on it
*
*   A section's challenges live in one text file (named by @challenges in sections.txt):
*
*       @challenge Sum of an array
*       @level 1                        1 = warm-up, 2 = practice, 3 = harder
*       @text
*       What to write, in plain words.
*       @end
*       @starter                        what the editor starts with
*       int sum(int *a, int n) {
*           return 0;
*       }
*       @end
*       @solution                       shown after solving, or after 3 tries
*       ...
*       @end
*       @helpers                        optional: code the tests use (placed after the student's code)
*       ...
*       @end
*       @forbid [ | Use pointer arithmetic, not [ ].      optional: text the code may not contain
*       @noleaks                        optional: a test fails if memory is not freed
*       @hint One sentence that helps.
*       @test sum of 1 to 5             a test: statements that run inside main, then the output
*       int a[] = {1, 2, 3, 4, 5};
*       printf("%d\n", sum(a, 5));
*       @expect
*       15
*       @end
*       @hidden empty array             like @test, but the student does not see the code or output
*       ...
*
*   Each test is a separate program: the student's code, the helpers, then main with the test.
*   It runs in the Visualizer's interpreter (cinterp.c), so a crash becomes an explanation and
*   any failed test can be opened in the Visualizer. tools/verify_content.py also compiles every
*   solution with gcc and checks it passes all tests and that the starter does not.
*
*   No raylib here, so this can be tested on its own.
*
**********************************************************************************************/

#ifndef CHALLENGE_H
#define CHALLENGE_H

#include <stdbool.h>

#define MAX_CHALLENGES         6
#define MAX_CHALLENGE_TESTS    8
#define MAX_FORBID             4

typedef struct ChallengeTest {
    char name[96];
    char *code;                     // statements run inside main
    char *expect;                   // exact output expected
    bool hidden;
    int line;                       // line of @test in the file
} ChallengeTest;

typedef struct Challenge {
    char title[96];
    int level;                      // 1..3
    char *text;
    char *starter;
    char *solution;
    char *helpers;                  // may be NULL
    char hint[256];
    char forbid[MAX_FORBID][16];
    char forbidWhy[MAX_FORBID][160];
    int forbidCount;
    bool noLeaks;
    ChallengeTest tests[MAX_CHALLENGE_TESTS];
    int testCount;
    int line;
} Challenge;

typedef struct ChallengeSet {
    Challenge items[MAX_CHALLENGES];
    int count;
} ChallengeSet;

// Reads a challenge file. On failure returns false with a "file:line: message" in err.
bool LoadChallenges(const char *path, ChallengeSet *set, char *err, int errSize);
void UnloadChallenges(ChallengeSet *set);

// The full program for one test: student code + helpers + main(test). Returns the line in the
// program where the student's code ends (lines after it belong to the helpers and the test).
int ChallengeProgram(const Challenge *c, const char *code, int test, char *out, int cap);

typedef struct TestResult {
    bool passed;
    bool wrongOutput;               // it ran to the end but printed something else
    int diffLine;                   // with wrongOutput: first output line that differs (1-based)
    char message[400];              // why it failed (empty when passed)
    char got[1024];                 // what it printed (cut off after 1023 characters)
} TestResult;

typedef struct GradeResult {
    bool ruleBroken;                // a @forbid text appears in the code
    bool compileError;              // the code (with the tests) does not compile
    char message[480];              // explanation for a broken rule or a compile error
    int errorLine;                  // line in the student's code (0 = not in their code)
    int passedCount;
    TestResult tests[MAX_CHALLENGE_TESTS];
} GradeResult;

// Runs every test on the student's code
void GradeChallenge(const Challenge *c, const char *code, GradeResult *out);

// Progress key for a challenge, stored like a topic: "challenge_basics.txt#2"
const char *ChallengeKey(const char *file, int index);

#endif // CHALLENGE_H
