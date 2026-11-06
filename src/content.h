/**********************************************************************************************
*
*   C Learning App - content model and loader
*
*   Lessons and the section list are plain text files (see docs/CONTENT_FORMAT.md).
*   This module parses them into the structs below. It only uses the C standard library,
*   so it can be tested without raylib.
*
**********************************************************************************************/

#ifndef CONTENT_H
#define CONTENT_H

#include <stdbool.h>
#include <stddef.h>     // NULL

//----------------------------------------------------------------------------------
// Limits (the parser reports an error instead of overflowing)
//----------------------------------------------------------------------------------
#define MAX_SECTIONS             8
#define MAX_TOPICS_PER_SECTION   12
#define MAX_PAGES                10
#define MAX_ITEMS                12
#define MAX_STEPS                32
#define MAX_CELLS                8
#define MAX_QUESTIONS            10
#define MAX_OPTIONS              4

#define MAX_TRACE_CODE_LINES     8       // keeps the trace and predict page layouts from overflowing
#define MAX_PREDICT_CODE_LINES   8

//----------------------------------------------------------------------------------
// Lesson model
//----------------------------------------------------------------------------------
typedef enum PageType { PAGE_EXPLAIN, PAGE_TRACE, PAGE_PREDICT, PAGE_QUIZ } PageType;
typedef enum ItemKind { ITEM_TEXT, ITEM_CODE } ItemKind;

// A paragraph of text, or a code example with its (verified) output
typedef struct Item {
    ItemKind kind;
    char lang[8];           // "c" or "cpp" (code items)
    char *text;
    char *output;           // NULL when the code has no output block
} Item;

// One variable in the trace's memory picture.
// value is a literal ("42"), "?" (uninitialised), "NULL", or "->name" (points at cell name)
typedef struct Cell {
    char name[24];
    char type[24];
    char value[24];
    int addr;
} Cell;

typedef struct Step {
    int line;               // 1-based line of the code that just ran
    int cellCount;
    Cell cells[MAX_CELLS];
    char note[200];
} Step;

typedef struct Question {
    char *prompt;
    char *options[MAX_OPTIONS];
    int optionCount;
    int correct;            // 0-based
    char *why;
} Question;

typedef struct Page {
    PageType type;
    char title[64];

    Item items[MAX_ITEMS];  // explain: text and code; trace: the code; predict: prompt and code
    int itemCount;

    Step steps[MAX_STEPS];  // trace
    int stepCount;

    char *options[MAX_OPTIONS];     // predict
    int optionCount;
    int correct;
    char *why;

    Question questions[MAX_QUESTIONS];  // quiz
    int questionCount;
} Page;

typedef struct Lesson {
    char title[64];
    char summary[128];
    Page pages[MAX_PAGES];
    int pageCount;
} Lesson;

//----------------------------------------------------------------------------------
// Section list (sections.txt)
//----------------------------------------------------------------------------------
typedef struct TopicRef {
    char file[64];
    char title[64];
    char summary[128];
} TopicRef;

typedef struct SectionDef {
    char name[48];
    char description[128];
    TopicRef topics[MAX_TOPICS_PER_SECTION];
    int topicCount;
} SectionDef;

//----------------------------------------------------------------------------------
// Functions
//----------------------------------------------------------------------------------
bool LoadManifest(const char *path);            // false on error, see GetContentError()
int GetSectionCount(void);
const SectionDef *GetSection(int index);        // NULL if out of range
const char *GetContentError(void);              // "" when the last load succeeded

// Loads one lesson file. On failure returns false and writes a readable message to err.
// The Lesson must be zeroed before the first call; always call UnloadLesson when done.
bool LoadLesson(const char *path, Lesson *lesson, char *err, int errSize);
void UnloadLesson(Lesson *lesson);

#endif // CONTENT_H
