#include "content.h"

#include <stddef.h>

// NOTE: the default raylib font only has basic ASCII, so avoid fancy quotes and dashes.

//----------------------------------------------------------------------------------
// Topic: Pointers
//----------------------------------------------------------------------------------
static const Question pointerQuestions[] = {
    {
        "What does the & operator do in:  int *p = &x;",
        { "Gets the address of x", "Dereferences x", "Declares a reference to x", "Bitwise AND of p and x" },
        0,
        "Put in front of a variable, & gives its address. p then stores that address."
    },
    {
        "After:  int x = 5; int *p = &x; *p = 9;   what is x?",
        { "5", "9", "The address of x", "Undefined" },
        1,
        "*p follows the pointer to x, so assigning through it changes x itself."
    },
    {
        "Which of these is safe to do with a pointer p that is NULL?",
        { "Read *p", "Write *p = 1", "Check if (p == NULL)", "Pass it to strlen()" },
        2,
        "Comparing is fine. Dereferencing NULL is undefined behaviour and usually crashes."
    },
};

//----------------------------------------------------------------------------------
// Topic: Variables and Types
//----------------------------------------------------------------------------------
static const Question typeQuestions[] = {
    {
        "What does sizeof(char) always evaluate to?",
        { "1", "8", "It depends on the platform", "2" },
        0,
        "The C standard defines sizeof(char) as 1. Other sizes can vary by platform."
    },
    {
        "What value does an uninitialised local int hold?",
        { "Always 0", "Indeterminate (garbage)", "Always -1", "NULL" },
        1,
        "Locals are not zeroed. Reading one before assigning it is a bug."
    },
    {
        "What is 7 / 2 when both operands are int?",
        { "3.5", "4", "3", "3.0" },
        2,
        "Integer division discards the fraction, so the result is 3."
    },
};

//----------------------------------------------------------------------------------
// Topic table
//----------------------------------------------------------------------------------
static const Topic topics[] = {
    {
        "Pointers",
        "Addresses, dereferencing and NULL",
        "A pointer is a variable that stores a memory address.\n"
        "\n"
        "  int x = 42;\n"
        "  int *p = &x;    // p holds the address of x\n"
        "  *p = 7;         // follow the pointer: x is now 7\n"
        "\n"
        "&  gives the address of a variable.\n"
        "*  follows a pointer to the value it points at.\n"
        "\n"
        "A pointer that points nowhere should be set to NULL. "
        "Dereferencing NULL is undefined behaviour and usually crashes.",
        pointerQuestions,
        (int)(sizeof(pointerQuestions)/sizeof(pointerQuestions[0]))
    },
    {
        "Variables and Types",
        "Sizes, initialisation and integer maths",
        "Every variable in C has a type that decides its size and how its bits are read.\n"
        "\n"
        "  int count = 3;\n"
        "  double price = 9.99;\n"
        "  char grade = 'A';\n"
        "\n"
        "Typical sizes on 64-bit Linux: char is 1 byte, int is 4, double is 8. "
        "Use sizeof to ask the compiler instead of guessing.\n"
        "\n"
        "Local variables are not zeroed for you. Give them a value before you read them.",
        typeQuestions,
        (int)(sizeof(typeQuestions)/sizeof(typeQuestions[0]))
    },
};

int GetTopicCount(void)
{
    return (int)(sizeof(topics)/sizeof(topics[0]));
}

const Topic *GetTopic(int index)
{
    if (index < 0 || index >= GetTopicCount()) return NULL;
    return &topics[index];
}
