/**********************************************************************************************
*
*   C Learning App - a small C interpreter that records every step
*
*   CiExecute() parses a C program and runs it on a simulated memory, saving a snapshot before
*   every statement: the line about to run, the call stack with each function's variables,
*   the heap blocks from malloc, and the output so far. The Visualizer screen draws these.
*
*   It understands a teaching-sized subset of C99:
*     types     char, int, long, float, double, bool, size_t, pointers, arrays (also 2D),
*               structs, typedef
*     code      functions and recursion, if/else, while, do-while, for, switch, break,
*               continue, return, all the usual operators, casts, sizeof, #define constants
*     library   printf, puts, putchar, scanf, fgets, getchar (reading the given input text),
*               malloc, calloc, realloc, free, strlen, strcpy, strncpy, strcat, strcmp,
*               memset, memcpy, abs, sqrt, pow, fabs, rand, srand
*   Not supported: unsigned arithmetic (unsigned is treated as signed), function pointers,
*   unions, enums, goto, variadic functions of your own, files.
*
*   Instead of crashing like a real program, it stops with an explanation at the classic
*   bugs: NULL pointer dereference, use after free, double free, reading or writing past the
*   end of an array or malloc'd block, writing to a string literal, a pointer to a variable
*   that no longer exists, division by zero, stack overflow, and infinite loops (step limit).
*   Reading a variable before it has a value is reported as a warning, and memory that was
*   never freed is reported when the program ends.
*
*   Memory layout (addresses are small decimal numbers, like the lessons' memory pictures):
*       0 ..  999   NULL page: any access here is a NULL pointer dereference
*    1000 .. 4999   stack: variables of each function call, in call order
*    5000 .. 12999  heap: blocks from malloc, never reused, so use-after-free is caught
*   13000 .. 15999  globals and string literals
*   int is 4 bytes, long, double and pointers are 8 bytes, as on a typical 64-bit computer.
*
*   This file has no raylib dependency, so it can be tested on its own.
*
**********************************************************************************************/

#ifndef CINTERP_H
#define CINTERP_H

#include <stdbool.h>

#define CI_NULL_END       1000
#define CI_STACK_BASE     1000
#define CI_STACK_END      5000
#define CI_HEAP_BASE      5000
#define CI_HEAP_END       13000
#define CI_DATA_BASE      13000
#define CI_MEM_SIZE       16000

#define CI_MAX_STEPS      3000      // snapshots; a program that runs longer is stopped ("infinite loop?")
#define CI_NAME_LEN       32
#define CI_MAX_FIELDS     16
#define CI_MAX_WARNINGS   8

typedef enum CiKind {
    CI_VOID, CI_BOOL, CI_CHAR, CI_INT, CI_LONG, CI_FLOAT, CI_DOUBLE,
    CI_PTR, CI_ARRAY, CI_STRUCT
} CiKind;

typedef struct CiType {
    CiKind kind;
    int size, align;
    struct CiType *base;            // pointer: what it points to; array: element type
    int len;                        // array: number of elements
    int structIndex;                // struct: index into CiRun.structs
    const char *name;               // display name, such as "int*" or "struct Node"
} CiType;

typedef struct CiField {
    char name[CI_NAME_LEN];
    CiType *type;
    int offset;
} CiField;

typedef struct CiStruct {
    char name[CI_NAME_LEN];
    CiField fields[CI_MAX_FIELDS];
    int fieldCount;
    int size, align;
    bool complete;
} CiStruct;

typedef struct CiVar {              // a variable in a snapshot
    char name[CI_NAME_LEN];
    CiType *type;
    int addr;
} CiVar;

typedef struct CiFrame {            // one function call in a snapshot
    char func[CI_NAME_LEN];
    int line;                       // line this frame is currently on
    int firstVar, varCount;         // range in CiSnapshot.vars
} CiFrame;

typedef struct CiBlock {            // one malloc'd block
    int addr, size;
    int allocLine, freeLine;        // freeLine 0 = still allocated
    CiType *hint;                   // element type, guessed from the pointer it was stored in (or NULL)
} CiBlock;

typedef struct CiSnapshot {
    int line;                       // line about to run (1-based); 0 = the program has finished
    char note[96];                  // what just happened, e.g. "malloc(8) returned 5000" (may be empty)

    int frameCount;
    CiFrame *frames;                // frames[0] is main, the last one is the current call
    int globalCount;                // vars[0 .. globalCount-1] are the globals
    int varCount;
    CiVar *vars;

    int blockCount;
    CiBlock *blocks;

    int outputLen;                  // CiRun.output[0 .. outputLen-1] had been printed by now

    // Copies of the used part of each memory region, with one "has a value" flag per byte
    int stackUsed, heapUsed, dataUsed;
    unsigned char *stack, *stackInit;
    unsigned char *heap, *heapInit;
    unsigned char *data, *dataInit;
} CiSnapshot;

typedef struct CiRun {
    bool compileError;              // the program could not be parsed (no steps)
    bool runtimeError;              // it started but stopped at a bug (last step shows where)
    bool truncated;                 // stopped at CI_MAX_STEPS
    char error[300];                // explanation of the compile or runtime error
    int errorLine;

    char warnings[CI_MAX_WARNINGS][160];
    int warningLines[CI_MAX_WARNINGS];
    int warningCount;

    int leakedBlocks, leakedBytes;  // memory still allocated when main returned

    char *output;                   // everything printed (NUL-terminated)
    int outputLen;

    CiSnapshot *steps;
    int stepCount;

    CiStruct *structs;
    int structCount;

    void *arena;                    // internal: owns everything above
} CiRun;

// Runs a program. Always fill a zeroed CiRun; call CiFree when done (also after errors).
// Returns false on a compile error (run->error explains).
// input is what the program reads with scanf, fgets and getchar (may be NULL or "").
bool CiExecute(const char *source, const char *input, CiRun *run);
void CiFree(CiRun *run);

// Reads `size` bytes at addr as they were in a snapshot. Returns false if the address is not
// in any copied region. *initialised is false if any byte had no value yet.
bool CiSnapRead(const CiSnapshot *s, int addr, int size, unsigned char *out, bool *initialised);

// Formats the value of type t at addr in a snapshot for display: "42", "3.5", "'A'", "NULL",
// "5008", "?" (no value yet). Arrays and structs are not formatted here (draw their elements).
void CiFormatValue(const CiRun *run, const CiSnapshot *s, int addr, const CiType *t, char *out, int cap);

// Which region an address is in: 0 none/NULL, 1 stack, 2 heap, 3 globals/literals
int CiRegion(int addr);

#endif // CINTERP_H
