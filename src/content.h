/**********************************************************************************************
*
*   C Learning App - lesson and quiz content
*
*   Content is plain C data (see content.c). Later sprints can load the same
*   structures from a file instead.
*
**********************************************************************************************/

#ifndef CONTENT_H
#define CONTENT_H

#include <stddef.h>     // NULL, returned by GetTopic() for a bad index

#define OPTIONS_PER_QUESTION 4

typedef struct Question {
    const char *prompt;
    const char *options[OPTIONS_PER_QUESTION];
    int correct;                // Index into options
    const char *explanation;    // Shown after answering
} Question;

typedef struct Topic {
    const char *title;
    const char *summary;        // One line shown on the topics screen
    const char *lesson;         // Lesson body, '\n' for line breaks
    const Question *questions;
    int questionCount;
} Topic;

int GetTopicCount(void);
const Topic *GetTopic(int index);   // NULL if index is out of range

#endif // CONTENT_H
