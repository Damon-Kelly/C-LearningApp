/**********************************************************************************************
*
*   C Learning App - a small C interpreter that records every step (see cinterp.h)
*
*   Three parts, top to bottom:
*     1. Lexer      source text -> tokens (with a tiny #define)
*     2. Parser     tokens -> a tree of Nodes (one per expression or statement), plus the
*                   struct, typedef and function tables
*     3. Runner     walks the tree on a simulated memory (mem[]), checking every read and
*                   write, and saves a snapshot before each statement
*
*   Errors (compile or runtime) jump straight back to CiExecute with longjmp.
*
**********************************************************************************************/

#include "cinterp.h"

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//----------------------------------------------------------------------------------
// Arena: every allocation for one run, freed together by CiFree
//----------------------------------------------------------------------------------
typedef struct Chunk { struct Chunk *next; size_t used, cap; unsigned char data[]; } Chunk;

static Chunk *arena = NULL;

static void *Alloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (arena == NULL || arena->used + n > arena->cap)
    {
        size_t cap = (n > 1 << 20) ? n : (1 << 20);
        Chunk *c = malloc(sizeof(Chunk) + cap);
        if (c == NULL) return NULL;
        c->next = arena; c->used = 0; c->cap = cap;
        arena = c;
    }
    void *p = arena->data + arena->used;
    arena->used += n;
    memset(p, 0, n);
    return p;
}

static char *Dup(const char *s, int n)
{
    char *p = Alloc((size_t)n + 1);
    memcpy(p, s, (size_t)n);
    p[n] = '\0';
    return p;
}

//----------------------------------------------------------------------------------
// Errors
//----------------------------------------------------------------------------------
static jmp_buf failJump;
static CiRun *run = NULL;
static int curLine = 0;
static bool running = false;        // false while parsing (compile error), true while running

static void Fail(int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(run->error, sizeof(run->error), fmt, ap);
    va_end(ap);
    run->errorLine = line;
    if (running) run->runtimeError = true; else run->compileError = true;
    longjmp(failJump, 1);
}

static void Warn(int line, const char *fmt, ...)
{
    if (run->warningCount >= CI_MAX_WARNINGS) return;
    for (int i = 0; i < run->warningCount; i++) if (run->warningLines[i] == line) return;   // once per line
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(run->warnings[run->warningCount], sizeof(run->warnings[0]), fmt, ap);
    va_end(ap);
    run->warningLines[run->warningCount++] = line;
}

//----------------------------------------------------------------------------------
// Types
//----------------------------------------------------------------------------------
static CiType tVoid   = { CI_VOID,   1, 1, NULL, 0, -1, "void" };
static CiType tBool   = { CI_BOOL,   1, 1, NULL, 0, -1, "bool" };
static CiType tChar   = { CI_CHAR,   1, 1, NULL, 0, -1, "char" };
static CiType tInt    = { CI_INT,    4, 4, NULL, 0, -1, "int" };
static CiType tLong   = { CI_LONG,   8, 8, NULL, 0, -1, "long" };
static CiType tFloat  = { CI_FLOAT,  4, 4, NULL, 0, -1, "float" };
static CiType tDouble = { CI_DOUBLE, 8, 8, NULL, 0, -1, "double" };

#define MAX_STRUCTS 32
static CiStruct structs[MAX_STRUCTS];
static int structCount = 0;

static CiType *PtrTo(CiType *base)
{
    CiType *t = Alloc(sizeof(CiType));
    t->kind = CI_PTR; t->size = 8; t->align = 8; t->base = base; t->structIndex = -1;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s*", base->name);
    t->name = Dup(buf, (int)strlen(buf));
    return t;
}

static CiType *ArrayOf(CiType *base, int len)
{
    CiType *t = Alloc(sizeof(CiType));
    t->kind = CI_ARRAY; t->size = base->size*len; t->align = base->align; t->base = base; t->len = len; t->structIndex = -1;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s[%d]", base->name, len);
    t->name = Dup(buf, (int)strlen(buf));
    return t;
}

// One shared type per struct, so a pointer made before the struct was complete
// (struct Node *next inside struct Node) sees the final size
static CiType *structTypes[MAX_STRUCTS];

static CiType *StructType(int index)
{
    if (structTypes[index])
    {
        structTypes[index]->size = structs[index].size;
        structTypes[index]->align = structs[index].align;
        return structTypes[index];
    }
    CiType *t = Alloc(sizeof(CiType));
    structTypes[index] = t;
    t->kind = CI_STRUCT; t->structIndex = index;
    t->size = structs[index].size; t->align = structs[index].align;
    char buf[64];
    snprintf(buf, sizeof(buf), "struct %s", structs[index].name);
    t->name = Dup(buf, (int)strlen(buf));
    return t;
}

static bool IsInteger(const CiType *t) { return t->kind == CI_BOOL || t->kind == CI_CHAR || t->kind == CI_INT || t->kind == CI_LONG; }
static bool IsFloating(const CiType *t) { return t->kind == CI_FLOAT || t->kind == CI_DOUBLE; }
static bool IsArith(const CiType *t) { return IsInteger(t) || IsFloating(t); }
static bool IsPtr(const CiType *t) { return t->kind == CI_PTR; }
static int AlignUp(int x, int a) { return (x + a - 1)/a*a; }

//----------------------------------------------------------------------------------
// 1. Lexer
//----------------------------------------------------------------------------------
typedef enum TokKind { TK_EOF, TK_IDENT, TK_KEYWORD, TK_INT, TK_FLOAT, TK_CHAR, TK_STRING, TK_PUNCT } TokKind;

typedef struct Tok {
    TokKind kind;
    char text[CI_NAME_LEN];         // identifier, keyword or punctuator
    long ival;
    double fval;
    bool isLong;
    char *str; int strLen;          // string literal contents
    int line;
} Tok;

static Tok *toks = NULL;
static int tokCount = 0, tokCap = 0;

static const char *keywords[] = {
    "int", "char", "long", "short", "float", "double", "void", "bool", "_Bool", "struct", "typedef",
    "if", "else", "while", "do", "for", "return", "break", "continue", "switch", "case", "default",
    "sizeof", "const", "unsigned", "signed", "static", "size_t", "enum", "union", "goto", NULL
};

static const char *puncts[] = {
    "<<=", ">>=", "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", NULL
};

typedef struct Macro { char name[CI_NAME_LEN]; Tok *body; int len; } Macro;
#define MAX_MACROS 64
static Macro macros[MAX_MACROS];
static int macroCount = 0;

static void PushTok(Tok t)
{
    if (tokCount == tokCap)
    {
        int cap = tokCap ? tokCap*2 : 1024;
        Tok *n = Alloc(sizeof(Tok)*(size_t)cap);
        if (toks) memcpy(n, toks, sizeof(Tok)*(size_t)tokCount);
        toks = n; tokCap = cap;
    }
    toks[tokCount++] = t;
}

static int EscapeChar(const char **pp, int line)
{
    const char *p = *pp;
    int c = *p++;
    if (c == '\\')
    {
        c = *p++;
        switch (c)
        {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '0': c = '\0'; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
            case '"': c = '"'; break;
            case 'a': c = '\a'; break;
            default: Fail(line, "unknown escape sequence \\%c", c);
        }
    }
    *pp = p;
    return c;
}

// Lexes src into tokens appended to toks. Used for the program and for #define bodies.
static void Lex(const char *src, int startLine, bool isMacroBody)
{
    const char *p = src;
    int line = startLine;
    bool lineStart = true;

    while (*p)
    {
        if (*p == '\n') { line++; p++; lineStart = true; if (isMacroBody) return; continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*')
        {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') line++; p++; }
            if (*p) p += 2;
            continue;
        }

        if (*p == '#' && lineStart && !isMacroBody)
        {
            // Preprocessor line: #include is ignored, #define NAME value becomes a macro
            const char *q = p + 1;
            while (*q == ' ' || *q == '\t') q++;
            if (strncmp(q, "define", 6) == 0 && (q[6] == ' ' || q[6] == '\t'))
            {
                q += 6;
                while (*q == ' ' || *q == '\t') q++;
                const char *nameStart = q;
                while (isalnum((unsigned char)*q) || *q == '_') q++;
                int nameLen = (int)(q - nameStart);
                if (nameLen == 0 || nameLen >= CI_NAME_LEN) Fail(line, "#define needs a name");
                if (*q == '(') Fail(line, "#define with parameters (a macro function) is not supported; write a function instead");
                if (macroCount >= MAX_MACROS) Fail(line, "too many #defines");
                Macro *m = &macros[macroCount++];
                memcpy(m->name, nameStart, (size_t)nameLen);
                m->name[nameLen] = '\0';

                // Lex the rest of the line into a separate token list
                Tok *saveToks = toks; int saveCount = tokCount, saveCap = tokCap;
                toks = NULL; tokCount = 0; tokCap = 0;
                Lex(q, line, true);
                m->body = toks; m->len = tokCount;
                toks = saveToks; tokCount = saveCount; tokCap = saveCap;
            }
            else if (strncmp(q, "include", 7) != 0 && strncmp(q, "pragma", 6) != 0)
            {
                Fail(line, "only #include and #define are supported");
            }
            while (*p && *p != '\n') p++;
            continue;
        }
        lineStart = false;

        Tok t = { 0 };
        t.line = line;

        if (isalpha((unsigned char)*p) || *p == '_')
        {
            const char *s = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            int n = (int)(p - s);
            if (n >= CI_NAME_LEN) Fail(line, "name too long");
            memcpy(t.text, s, (size_t)n);
            t.kind = TK_IDENT;
            for (int k = 0; keywords[k]; k++) if (strcmp(keywords[k], t.text) == 0) t.kind = TK_KEYWORD;

            // Macro expansion (object-like macros only)
            bool expanded = false;
            if (t.kind == TK_IDENT && !isMacroBody)
            {
                for (int m = 0; m < macroCount; m++) if (strcmp(macros[m].name, t.text) == 0)
                {
                    for (int k = 0; k < macros[m].len; k++) { Tok mt = macros[m].body[k]; mt.line = line; PushTok(mt); }
                    expanded = true;
                    break;
                }
            }
            if (!expanded) PushTok(t);
            continue;
        }

        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1])))
        {
            char *end;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            {
                t.kind = TK_INT; t.ival = strtol(p, &end, 16);
            }
            else
            {
                const char *q = p;
                while (isdigit((unsigned char)*q)) q++;
                if (*q == '.' || *q == 'e' || *q == 'E') { t.kind = TK_FLOAT; t.fval = strtod(p, &end); }
                else { t.kind = TK_INT; t.ival = strtol(p, &end, 10); }
            }
            p = end;
            while (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L' || *p == 'f' || *p == 'F')
            {
                if (*p == 'l' || *p == 'L') t.isLong = true;
                p++;
            }
            PushTok(t);
            continue;
        }

        if (*p == '\'')
        {
            p++;
            t.kind = TK_CHAR;
            t.ival = (char)EscapeChar(&p, line);
            if (*p != '\'') Fail(line, "a character literal holds one character, like 'a'");
            p++;
            PushTok(t);
            continue;
        }

        if (*p == '"')
        {
            char buf[1024];
            int n = 0;
            // Adjacent literals "ab" "cd" are joined, as in C
            while (*p == '"')
            {
                p++;
                while (*p && *p != '"')
                {
                    if (*p == '\n') Fail(line, "missing closing \" on this string");
                    int c = EscapeChar(&p, line);
                    if (n < (int)sizeof(buf) - 1) buf[n++] = (char)c;
                }
                if (*p != '"') Fail(line, "missing closing \" on this string");
                p++;
                const char *q = p;
                while (*q == ' ' || *q == '\t' || *q == '\n') q++;
                if (*q == '"') { while (p < q) { if (*p == '\n') line++; p++; } } else break;
            }
            t.kind = TK_STRING;
            t.str = Dup(buf, n);
            t.strLen = n;
            PushTok(t);
            continue;
        }

        t.kind = TK_PUNCT;
        bool found = false;
        for (int k = 0; puncts[k]; k++)
        {
            size_t n = strlen(puncts[k]);
            if (strncmp(p, puncts[k], n) == 0) { memcpy(t.text, p, n); p += n; found = true; break; }
        }
        if (!found)
        {
            if (strchr("+-*/%=<>!&|^~?:;,.()[]{}", *p) == NULL) Fail(line, "unexpected character '%c'", *p);
            t.text[0] = *p++;
        }
        PushTok(t);
    }
}

//----------------------------------------------------------------------------------
// 2. Parser
//----------------------------------------------------------------------------------
typedef enum NodeKind {
    // expressions
    N_NUM, N_FNUM, N_STR, N_IDENT, N_BINARY, N_ASSIGN, N_UNARY, N_DEREF, N_ADDR,
    N_PREINC, N_PREDEC, N_POSTINC, N_POSTDEC, N_INDEX, N_CALL, N_MEMBER, N_ARROW,
    N_COND, N_CAST, N_SIZEOF_TYPE, N_SIZEOF_EXPR, N_LOGAND, N_LOGOR, N_COMMA, N_INITLIST,
    // statements
    N_BLOCK, N_DECL, N_EXPR, N_IF, N_WHILE, N_DO, N_FOR, N_RETURN, N_BREAK, N_CONTINUE,
    N_SWITCH, N_CASE, N_DEFAULT, N_EMPTY
} NodeKind;

typedef struct Node Node;
struct Node {
    NodeKind kind;
    int line;
    char op[4];                     // operator text for N_BINARY, N_ASSIGN, N_UNARY
    Node *a, *b, *c, *d;            // children (meaning depends on kind)
    Node *next;                     // next item in a list (statements, arguments, init list)
    long ival;
    double fval;
    CiType *type;                   // N_NUM type, N_CAST/N_SIZEOF_TYPE/N_DECL type
    char name[CI_NAME_LEN];         // N_IDENT, N_CALL, N_MEMBER/N_ARROW field, N_DECL variable
    int strAddr;                    // N_STR: where the literal lives in memory
};

typedef struct Func {
    char name[CI_NAME_LEN];
    CiType *ret;
    int paramCount;
    char paramNames[8][CI_NAME_LEN];
    CiType *paramTypes[8];
    Node *body;                     // NULL for a prototype only
    int line;
} Func;

#define MAX_FUNCS 64
static Func funcs[MAX_FUNCS];
static int funcCount = 0;

typedef struct Typedef { char name[CI_NAME_LEN]; CiType *type; } Typedef;
#define MAX_TYPEDEFS 32
static Typedef typedefs[MAX_TYPEDEFS];
static int typedefCount = 0;

static Node *globalDecls = NULL;    // list of N_DECL for global variables
static Node *globalTail = NULL;

static int pos = 0;                 // current token
static bool sawStatic = false;      // the last ParseBaseType saw 'static'

// Memory is needed while parsing, for string literals
static unsigned char mem[CI_MEM_SIZE];
static unsigned char memInit[CI_MEM_SIZE];
static int dataTop = CI_DATA_BASE;
static int literalStart = CI_DATA_BASE, literalEnd = CI_DATA_BASE;
#define TEMP_BASE 15200             // scratch space for struct values returned from functions
static int tempTop = TEMP_BASE;

static Tok *Peek(void) { return &toks[pos]; }
static Tok *PeekAt(int k) { return &toks[(pos + k < tokCount) ? pos + k : tokCount - 1]; }
static bool Is(const char *text) { Tok *t = Peek(); return (t->kind == TK_PUNCT || t->kind == TK_KEYWORD) && strcmp(t->text, text) == 0; }
static bool IsAt(int k, const char *text) { Tok *t = PeekAt(k); return (t->kind == TK_PUNCT || t->kind == TK_KEYWORD) && strcmp(t->text, text) == 0; }
static bool Accept(const char *text) { if (Is(text)) { pos++; return true; } return false; }

static void Expect(const char *text)
{
    if (!Accept(text))
    {
        Tok *t = Peek();
        const char *got = (t->kind == TK_EOF) ? "the end of the program" : (t->kind == TK_STRING) ? "a string" : (t->kind == TK_INT || t->kind == TK_FLOAT) ? "a number" : t->text;
        int line = t->line;
        // A missing ';' is usually noticed on the next line: report the line before
        if (strcmp(text, ";") == 0 && pos > 0 && toks[pos - 1].line < line) line = toks[pos - 1].line;
        Fail(line, "expected '%s' but found '%s'", text, got);
    }
}

static Node *NewNode(NodeKind kind, int line)
{
    Node *n = Alloc(sizeof(Node));
    n->kind = kind;
    n->line = line;
    return n;
}

static CiType *FindTypedef(const char *name)
{
    for (int i = 0; i < typedefCount; i++) if (strcmp(typedefs[i].name, name) == 0) return typedefs[i].type;
    return NULL;
}

static int FindStruct(const char *name)
{
    for (int i = 0; i < structCount; i++) if (strcmp(structs[i].name, name) == 0) return i;
    return -1;
}

static bool IsTypeStart(void)
{
    Tok *t = Peek();
    if (t->kind == TK_KEYWORD)
    {
        static const char *typeWords[] = { "int", "char", "long", "short", "float", "double", "void", "bool", "_Bool", "struct",
                                           "const", "unsigned", "signed", "static", "size_t", "enum", "union", NULL };
        for (int i = 0; typeWords[i]; i++) if (strcmp(t->text, typeWords[i]) == 0) return true;
        return false;
    }
    return t->kind == TK_IDENT && FindTypedef(t->text) != NULL;
}

static Node *ParseExpr(void);
static Node *ParseAssign(void);
static Node *ParseStatement(void);
static CiType *ParseDeclarator(CiType *base, char *name, bool allowNoName, int *line);
static long ConstEval(Node *n);

// Base type: int, char, struct X { ... }, a typedef name...
static CiType *ParseBaseType(void)
{
    int line = Peek()->line;
    bool isLong = false, isShort = false, sawSign = false;
    CiType *t = NULL;

    sawStatic = false;
    for (;;)
    {
        if (Accept("static")) { sawStatic = true; continue; }
        if (Accept("const")) continue;
        if (Accept("unsigned") || Accept("signed")) { sawSign = true; continue; }
        if (Accept("long")) { isLong = true; continue; }
        if (Accept("short")) { isShort = true; continue; }
        break;
    }

    if (Accept("int")) t = &tInt;
    else if (Accept("char")) t = &tChar;
    else if (Accept("float")) t = &tFloat;
    else if (Accept("double")) t = &tDouble;
    else if (Accept("void")) t = &tVoid;
    else if (Accept("bool") || Accept("_Bool")) t = &tBool;
    else if (Accept("size_t")) t = &tLong;
    else if (Is("enum") || Is("union")) Fail(line, "%s is not supported in the Visualizer", Peek()->text);
    else if (Accept("struct"))
    {
        Tok *nameTok = Peek();
        char name[CI_NAME_LEN] = "";
        if (nameTok->kind == TK_IDENT) { snprintf(name, sizeof(name), "%s", nameTok->text); pos++; }
        else snprintf(name, sizeof(name), "(anonymous %d)", structCount);

        int index = FindStruct(name);
        if (index < 0)
        {
            if (structCount >= MAX_STRUCTS) Fail(line, "too many structs");
            index = structCount++;
            snprintf(structs[index].name, CI_NAME_LEN, "%s", name);
        }

        if (Accept("{"))
        {
            CiStruct *s = &structs[index];
            if (s->complete) Fail(line, "struct %s is defined twice", name);
            int offset = 0, align = 1;
            while (!Accept("}"))
            {
                CiType *fieldBase = ParseBaseType();
                do
                {
                    char fieldName[CI_NAME_LEN];
                    int fl;
                    CiType *ft = ParseDeclarator(fieldBase, fieldName, false, &fl);
                    if (ft->kind == CI_STRUCT && !structs[ft->structIndex].complete)
                        Fail(fl, "struct %s cannot contain itself; use a pointer (struct %s *)", name, name);
                    if (s->fieldCount >= CI_MAX_FIELDS) Fail(fl, "too many fields in struct %s", name);
                    offset = AlignUp(offset, ft->align);
                    CiField *f = &s->fields[s->fieldCount++];
                    snprintf(f->name, CI_NAME_LEN, "%s", fieldName);
                    f->type = ft;
                    f->offset = offset;
                    offset += ft->size;
                    if (ft->align > align) align = ft->align;
                } while (Accept(","));
                Expect(";");
            }
            s->size = AlignUp(offset > 0 ? offset : 1, align);
            s->align = align;
            s->complete = true;
            StructType(index);      // update the shared type's size
        }
        t = StructType(index);
        return t;
    }
    else if (Peek()->kind == TK_IDENT && FindTypedef(Peek()->text)) { t = FindTypedef(Peek()->text); pos++; }
    else if (isLong || isShort || sawSign) t = &tInt;
    else Fail(line, "expected a type");

    if (t == &tInt && isLong) t = &tLong;
    if (t == &tDouble && isLong) t = &tDouble;
    (void)isShort;
    return t;
}

// The part after the base type: stars, the name, and [sizes]
static CiType *ParseDeclarator(CiType *base, char *name, bool allowNoName, int *line)
{
    CiType *t = base;
    while (Accept("*")) { t = PtrTo(t); while (Accept("const")) { } }

    Tok *nameTok = Peek();
    *line = nameTok->line;
    name[0] = '\0';
    if (nameTok->kind == TK_IDENT) { snprintf(name, CI_NAME_LEN, "%s", nameTok->text); pos++; }
    else if (Is("(")) Fail(nameTok->line, "function pointers are not supported in the Visualizer");
    else if (!allowNoName) Fail(nameTok->line, "expected a name here");

    // Array sizes: int a[2][3] is an array of 2 arrays of 3 ints, so build from the right
    int dims[4], dimCount = 0;
    while (Accept("["))
    {
        if (dimCount >= 4) Fail(*line, "too many array dimensions");
        if (Accept("]")) { dims[dimCount++] = -1; continue; }   // size from the initialiser
        Node *e = ParseExpr();
        long n = ConstEval(e);
        if (n <= 0) Fail(*line, "an array needs a positive size");
        dims[dimCount++] = (int)n;
        Expect("]");
    }
    for (int i = dimCount - 1; i >= 0; i--) t = ArrayOf(t, dims[i] < 0 ? 0 : dims[i]);
    return t;
}

// Compile-time constant (array sizes, case labels)
static long ConstEval(Node *n)
{
    switch (n->kind)
    {
        case N_NUM: return n->ival;
        case N_UNARY:
            if (strcmp(n->op, "-") == 0) return -ConstEval(n->a);
            if (strcmp(n->op, "+") == 0) return ConstEval(n->a);
            break;
        case N_BINARY:
        {
            long a = ConstEval(n->a), b = ConstEval(n->b);
            if (strcmp(n->op, "+") == 0) return a + b;
            if (strcmp(n->op, "-") == 0) return a - b;
            if (strcmp(n->op, "*") == 0) return a*b;
            if (strcmp(n->op, "/") == 0 && b) return a/b;
            if (strcmp(n->op, "%") == 0 && b) return a%b;
        } break;
        case N_SIZEOF_TYPE: return n->type->size;
        case N_CAST: return ConstEval(n->a);
        default: break;
    }
    Fail(n->line, "this must be a constant number (like 10 or a #define)");
    return 0;
}

static int AddStringLiteral(const char *s, int len, int line)
{
    // Reuse an identical literal
    for (int a = literalStart; a + len < literalEnd; a++)
        if (memcmp(&mem[a], s, (size_t)len) == 0 && mem[a + len] == 0) return a;
    if (literalEnd + len + 1 > TEMP_BASE) Fail(line, "too much string data");
    int addr = literalEnd;
    memcpy(&mem[addr], s, (size_t)len);
    mem[addr + len] = 0;
    memset(&memInit[addr], 1, (size_t)len + 1);
    literalEnd += len + 1;
    return addr;
}

// ---- Expressions, lowest precedence last ----
static Node *ParsePrimary(void)
{
    Tok *t = Peek();
    int line = t->line;

    if (t->kind == TK_INT || t->kind == TK_CHAR)
    {
        pos++;
        Node *n = NewNode(N_NUM, line);
        n->ival = t->ival;
        n->type = (t->kind == TK_CHAR) ? &tInt : (t->isLong || t->ival > 2147483647L) ? &tLong : &tInt;
        return n;
    }
    if (t->kind == TK_FLOAT)
    {
        pos++;
        Node *n = NewNode(N_FNUM, line);
        n->fval = t->fval;
        return n;
    }
    if (t->kind == TK_STRING)
    {
        pos++;
        Node *n = NewNode(N_STR, line);
        n->strAddr = AddStringLiteral(t->str, t->strLen, line);
        n->type = ArrayOf(&tChar, t->strLen + 1);
        return n;
    }
    if (t->kind == TK_IDENT)
    {
        pos++;
        if (strcmp(t->text, "NULL") == 0 || strcmp(t->text, "nullptr") == 0)
        {
            Node *n = NewNode(N_CAST, line);
            n->type = PtrTo(&tVoid);
            n->a = NewNode(N_NUM, line);
            n->a->type = &tInt;
            return n;
        }
        if (strcmp(t->text, "true") == 0 || strcmp(t->text, "false") == 0)
        {
            Node *n = NewNode(N_NUM, line);
            n->ival = (t->text[0] == 't');
            n->type = &tInt;
            return n;
        }
        if (strcmp(t->text, "stdin") == 0)
        {
            Node *n = NewNode(N_CAST, line);
            n->type = PtrTo(&tVoid);
            n->a = NewNode(N_NUM, line);
            n->a->type = &tInt;
            n->a->ival = 1;
            return n;
        }
        if (strcmp(t->text, "RAND_MAX") == 0)
        {
            Node *n = NewNode(N_NUM, line);
            n->ival = 32767;
            n->type = &tInt;
            return n;
        }
        if (Is("("))
        {
            pos++;
            Node *n = NewNode(N_CALL, line);
            snprintf(n->name, CI_NAME_LEN, "%s", t->text);
            Node head = { 0 }, *tail = &head;
            if (!Accept(")"))
            {
                do { tail->next = ParseAssign(); tail = tail->next; } while (Accept(","));
                Expect(")");
            }
            n->a = head.next;
            return n;
        }
        Node *n = NewNode(N_IDENT, line);
        snprintf(n->name, CI_NAME_LEN, "%s", t->text);
        return n;
    }
    if (Accept("("))
    {
        Node *n = ParseExpr();
        Expect(")");
        return n;
    }
    if (t->kind == TK_EOF) Fail(line, "the program ends in the middle of an expression");
    Fail(line, "expected a value but found '%s'", t->text);
    return NULL;
}

static Node *ParsePostfix(void)
{
    Node *n = ParsePrimary();
    for (;;)
    {
        int line = Peek()->line;
        if (Accept("["))
        {
            Node *m = NewNode(N_INDEX, line);
            m->a = n; m->b = ParseExpr();
            Expect("]");
            n = m;
        }
        else if (Accept(".") || (Is("->") && pos++ >= 0))
        {
            bool arrow = (toks[pos - 1].text[0] == '-');
            Node *m = NewNode(arrow ? N_ARROW : N_MEMBER, line);
            m->a = n;
            Tok *f = Peek();
            if (f->kind != TK_IDENT) Fail(line, "expected a field name after %s", arrow ? "->" : ".");
            snprintf(m->name, CI_NAME_LEN, "%s", f->text);
            pos++;
            n = m;
        }
        else if (Accept("++")) { Node *m = NewNode(N_POSTINC, line); m->a = n; n = m; }
        else if (Accept("--")) { Node *m = NewNode(N_POSTDEC, line); m->a = n; n = m; }
        else if (Is("(")) Fail(line, "only calling a function by its name is supported");
        else return n;
    }
}

static bool IsCastAhead(void)
{
    if (!Is("(")) return false;
    int save = pos;
    pos++;
    bool r = IsTypeStart();
    pos = save;
    return r;
}

static CiType *ParseTypeName(void)
{
    CiType *t = ParseBaseType();
    char name[CI_NAME_LEN];
    int line;
    t = ParseDeclarator(t, name, true, &line);
    return t;
}

static Node *ParseUnary(void)
{
    int line = Peek()->line;
    if (Accept("++")) { Node *n = NewNode(N_PREINC, line); n->a = ParseUnary(); return n; }
    if (Accept("--")) { Node *n = NewNode(N_PREDEC, line); n->a = ParseUnary(); return n; }
    if (Accept("*")) { Node *n = NewNode(N_DEREF, line); n->a = ParseUnary(); return n; }
    if (Accept("&")) { Node *n = NewNode(N_ADDR, line); n->a = ParseUnary(); return n; }
    if (Is("-") || Is("+") || Is("!") || Is("~"))
    {
        Node *n = NewNode(N_UNARY, line);
        snprintf(n->op, sizeof(n->op), "%.3s", Peek()->text);
        pos++;
        n->a = ParseUnary();
        return n;
    }
    if (Accept("sizeof"))
    {
        if (IsCastAhead())
        {
            pos++;
            Node *n = NewNode(N_SIZEOF_TYPE, line);
            n->type = ParseTypeName();
            Expect(")");
            return n;
        }
        Node *n = NewNode(N_SIZEOF_EXPR, line);
        n->a = ParseUnary();
        return n;
    }
    if (IsCastAhead())
    {
        pos++;
        Node *n = NewNode(N_CAST, line);
        n->type = ParseTypeName();
        Expect(")");
        n->a = ParseUnary();
        return n;
    }
    return ParsePostfix();
}

static int Precedence(const char *op)
{
    static const struct { const char *op; int prec; } table[] = {
        { "*", 10 }, { "/", 10 }, { "%", 10 }, { "+", 9 }, { "-", 9 }, { "<<", 8 }, { ">>", 8 },
        { "<", 7 }, { ">", 7 }, { "<=", 7 }, { ">=", 7 }, { "==", 6 }, { "!=", 6 },
        { "&", 5 }, { "^", 4 }, { "|", 3 }, { "&&", 2 }, { "||", 1 }, { NULL, 0 }
    };
    for (int i = 0; table[i].op; i++) if (strcmp(table[i].op, op) == 0) return table[i].prec;
    return -1;
}

static Node *ParseBinary(int minPrec)
{
    Node *left = ParseUnary();
    for (;;)
    {
        Tok *t = Peek();
        if (t->kind != TK_PUNCT) return left;
        int prec = Precedence(t->text);
        if (prec < minPrec || prec < 0) return left;
        int line = t->line;
        char op[4];
        snprintf(op, sizeof(op), "%.3s", t->text);
        pos++;
        Node *right = ParseBinary(prec + 1);
        Node *n;
        if (strcmp(op, "&&") == 0) n = NewNode(N_LOGAND, line);
        else if (strcmp(op, "||") == 0) n = NewNode(N_LOGOR, line);
        else { n = NewNode(N_BINARY, line); snprintf(n->op, sizeof(n->op), "%s", op); }
        n->a = left; n->b = right;
        left = n;
    }
}

static Node *ParseConditional(void)
{
    Node *cond = ParseBinary(1);
    if (Is("?"))
    {
        int line = Peek()->line;
        pos++;
        Node *n = NewNode(N_COND, line);
        n->a = cond;
        n->b = ParseExpr();
        Expect(":");
        n->c = ParseConditional();
        return n;
    }
    return cond;
}

static Node *ParseAssign(void)
{
    Node *left = ParseConditional();
    static const char *ops[] = { "=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "^=", NULL };
    for (int i = 0; ops[i]; i++)
    {
        if (Is(ops[i]))
        {
            int line = Peek()->line;
            pos++;
            Node *n = NewNode(N_ASSIGN, line);
            snprintf(n->op, sizeof(n->op), "%s", ops[i]);
            n->a = left;
            n->b = ParseAssign();
            return n;
        }
    }
    return left;
}

static Node *ParseExpr(void)
{
    Node *n = ParseAssign();
    while (Is(","))
    {
        int line = Peek()->line;
        pos++;
        Node *c = NewNode(N_COMMA, line);
        c->a = n; c->b = ParseAssign();
        n = c;
    }
    return n;
}

static Node *ParseInitializer(void)
{
    if (Is("{"))
    {
        Node *list = NewNode(N_INITLIST, Peek()->line);
        pos++;
        Node head = { 0 }, *tail = &head;
        while (!Accept("}"))
        {
            if (Is(".")) Fail(Peek()->line, "designated initialisers (.field = ...) are not supported");
            tail->next = ParseInitializer();
            tail = tail->next;
            if (!Accept(",")) { Expect("}"); break; }
        }
        list->a = head.next;
        return list;
    }
    return ParseAssign();
}

// A declaration statement: one N_DECL per declared name, chained with next
static Node *ParseDeclaration(void)
{
    int line = Peek()->line;
    if (Accept("typedef"))
    {
        CiType *base = ParseBaseType();
        do
        {
            char name[CI_NAME_LEN];
            int l;
            CiType *t = ParseDeclarator(base, name, false, &l);
            if (typedefCount >= MAX_TYPEDEFS) Fail(l, "too many typedefs");
            snprintf(typedefs[typedefCount].name, CI_NAME_LEN, "%s", name);
            typedefs[typedefCount++].type = t;
        } while (Accept(","));
        Expect(";");
        return NewNode(N_EMPTY, line);
    }

    CiType *base = ParseBaseType();
    bool isStatic = sawStatic;
    if (Accept(";")) return NewNode(N_EMPTY, line);   // just "struct X { ... };"

    Node head = { 0 }, *tail = &head;
    do
    {
        char name[CI_NAME_LEN];
        int l;
        CiType *t = ParseDeclarator(base, name, false, &l);
        Node *d = NewNode(N_DECL, l);
        snprintf(d->name, CI_NAME_LEN, "%s", name);
        d->ival = isStatic;         // static local: one copy that keeps its value between calls
        if (Accept("=")) d->a = ParseInitializer();

        // int a[] = {...} or char s[] = "..." : the size comes from the initialiser
        if (t->kind == CI_ARRAY && t->len == 0)
        {
            int n = 0;
            if (d->a && d->a->kind == N_INITLIST) for (Node *e = d->a->a; e; e = e->next) n++;
            else if (d->a && d->a->kind == N_STR) n = d->a->type->len;
            else Fail(l, "%s[] needs an initialiser so the size is known", name);
            t = ArrayOf(t->base, n);
        }
        if (t->kind == CI_VOID) Fail(l, "a variable cannot have type void");
        if (t->kind == CI_STRUCT && !structs[t->structIndex].complete) Fail(l, "struct %s is used before it is defined", structs[t->structIndex].name);
        d->type = t;
        tail->next = d;
        tail = d;
    } while (Accept(","));
    Expect(";");

    if (head.next == tail) return tail;
    Node *block = NewNode(N_BLOCK, line);   // several names: a block that does not open a scope
    block->a = head.next;
    block->ival = 1;                        // 1 = no new scope
    return block;
}

static Node *ParseBlock(void)
{
    Node *n = NewNode(N_BLOCK, Peek()->line);
    Expect("{");
    Node head = { 0 }, *tail = &head;
    while (!Accept("}"))
    {
        if (Peek()->kind == TK_EOF) Fail(n->line, "missing '}' for the '{' on line %d", n->line);
        tail->next = ParseStatement();
        while (tail->next) tail = tail->next;
    }
    n->a = head.next;
    return n;
}

static Node *ParseStatement(void)
{
    Tok *t = Peek();
    int line = t->line;

    if (Is("{")) return ParseBlock();
    if (Accept(";")) return NewNode(N_EMPTY, line);
    if (Is("typedef") || IsTypeStart()) return ParseDeclaration();

    if (Accept("if"))
    {
        Node *n = NewNode(N_IF, line);
        Expect("(");
        n->a = ParseExpr();
        Expect(")");
        n->b = ParseStatement();
        if (Accept("else")) n->c = ParseStatement();
        return n;
    }
    if (Accept("while"))
    {
        Node *n = NewNode(N_WHILE, line);
        Expect("(");
        n->a = ParseExpr();
        Expect(")");
        n->b = ParseStatement();
        return n;
    }
    if (Accept("do"))
    {
        Node *n = NewNode(N_DO, line);
        n->b = ParseStatement();
        int whileLine = Peek()->line;
        Expect("while");
        Expect("(");
        n->a = ParseExpr();
        n->a->line = whileLine;
        Expect(")");
        Expect(";");
        return n;
    }
    if (Accept("for"))
    {
        Node *n = NewNode(N_FOR, line);
        Expect("(");
        if (Accept(";")) n->a = NULL;
        else if (IsTypeStart()) n->a = ParseDeclaration();
        else { n->a = NewNode(N_EXPR, line); n->a->a = ParseExpr(); Expect(";"); }
        if (!Is(";")) n->b = ParseExpr();
        Expect(";");
        if (!Is(")")) n->c = ParseExpr();
        Expect(")");
        n->d = ParseStatement();
        return n;
    }
    if (Accept("return"))
    {
        Node *n = NewNode(N_RETURN, line);
        if (!Is(";")) n->a = ParseExpr();
        Expect(";");
        return n;
    }
    if (Accept("break")) { Expect(";"); return NewNode(N_BREAK, line); }
    if (Accept("continue")) { Expect(";"); return NewNode(N_CONTINUE, line); }
    if (Accept("switch"))
    {
        Node *n = NewNode(N_SWITCH, line);
        Expect("(");
        n->a = ParseExpr();
        Expect(")");
        n->b = ParseStatement();
        return n;
    }
    if (Accept("case"))
    {
        Node *n = NewNode(N_CASE, line);
        n->ival = ConstEval(ParseConditional());
        Expect(":");
        return n;
    }
    if (Accept("default")) { Expect(":"); return NewNode(N_DEFAULT, line); }
    if (Is("goto")) Fail(line, "goto is not supported");
    if (Is("else")) Fail(line, "'else' without a matching 'if' (check the braces above)");

    Node *n = NewNode(N_EXPR, line);
    n->a = ParseExpr();
    Expect(";");
    return n;
}

static Func *FindFunc(const char *name)
{
    for (int i = 0; i < funcCount; i++) if (strcmp(funcs[i].name, name) == 0) return &funcs[i];
    return NULL;
}

static void ParseProgram(void)
{
    while (Peek()->kind != TK_EOF)
    {
        int line = Peek()->line;
        if (Accept(";")) continue;
        if (Is("typedef")) { ParseDeclaration(); continue; }
        if (!IsTypeStart()) Fail(line, "expected a function, a global variable or a struct here, found '%s'", Peek()->text);

        int start = pos;
        CiType *base = ParseBaseType();
        if (Accept(";")) continue;                      // struct definition

        char name[CI_NAME_LEN];
        int l;
        CiType *t = ParseDeclarator(base, name, false, &l);
        if (Is("("))
        {
            // A function definition or prototype
            pos++;
            Func f = { 0 };
            snprintf(f.name, CI_NAME_LEN, "%s", name);
            f.ret = t;
            f.line = l;
            if (!(Is("void") && IsAt(1, ")")) && !Is(")"))
            {
                do
                {
                    if (Is("...")) Fail(l, "functions with ... are not supported");
                    CiType *pb = ParseBaseType();
                    char pname[CI_NAME_LEN];
                    int pl;
                    CiType *pt = ParseDeclarator(pb, pname, true, &pl);
                    if (pt->kind == CI_ARRAY) pt = PtrTo(pt->base);      // int a[] as a parameter is int *a
                    if (f.paramCount >= 8) Fail(pl, "too many parameters (8 at most)");
                    snprintf(f.paramNames[f.paramCount], CI_NAME_LEN, "%s", pname);
                    f.paramTypes[f.paramCount++] = pt;
                } while (Accept(","));
            }
            else Accept("void");
            Expect(")");

            Func *existing = FindFunc(name);
            if (Accept(";"))
            {
                if (!existing && funcCount < MAX_FUNCS) funcs[funcCount++] = f;
                continue;
            }
            f.body = ParseBlock();
            for (int i = 0; i < f.paramCount; i++)
                if (f.paramNames[i][0] == '\0') Fail(f.line, "parameter %d of %s needs a name", i + 1, name);
            if (existing && existing->body) Fail(l, "function %s is defined twice", name);
            if (existing) *existing = f;
            else if (funcCount < MAX_FUNCS) funcs[funcCount++] = f;
            else Fail(l, "too many functions");
            continue;
        }

        // Global variable(s): re-parse as a normal declaration
        (void)t;
        pos = start;
        Node *d = ParseDeclaration();
        Node *list = (d->kind == N_BLOCK) ? d->a : d;
        for (Node *n = list; n; )
        {
            Node *next = n->next;
            n->next = NULL;
            if (n->kind == N_DECL)
            {
                if (globalTail) globalTail->next = n; else globalDecls = n;
                globalTail = n;
            }
            n = next;
        }
    }
}

//----------------------------------------------------------------------------------
// 3. Runner
//----------------------------------------------------------------------------------
typedef struct Var { char name[CI_NAME_LEN]; CiType *type; int addr; int frame; } Var;
#define MAX_VARS 512
static Var vars[MAX_VARS];
static int varCount = 0;
static int globalVarCount = 0;

typedef struct Frame { const char *func; int firstVar; int line; int spAtStart; } Frame;
#define MAX_DEPTH 200
static Frame frames[MAX_DEPTH];
static int frameCount = 0;

#define MAX_BLOCKS 256
static CiBlock blocks[MAX_BLOCKS];
static int blockCount = 0;
static int heapTop = CI_HEAP_BASE;
static int sp = CI_STACK_BASE;

static const char *input = "";       // text for scanf/fgets/getchar
static int inputPos = 0;
static char *output = NULL;
static int outputLen = 0, outputCap = 0;
static char note[96] = "";
static int stepCap = 0;
static unsigned randState = 1;
static unsigned garbageState = 1;      // separate from rand(), so garbage does not change rand()'s numbers

typedef struct Val { CiType *t; long i; double d; int addr; } Val;   // addr: arrays and structs

typedef enum Flow { FLOW_NORMAL, FLOW_BREAK, FLOW_CONTINUE, FLOW_RETURN } Flow;
static Val returnValue;

static void Out(const char *s, int n)
{
    if (outputLen + n + 1 > outputCap)
    {
        int cap = outputCap ? outputCap*2 : 1024;
        while (cap < outputLen + n + 1) cap *= 2;
        char *o = Alloc((size_t)cap);
        if (output) memcpy(o, output, (size_t)outputLen);
        output = o;
        outputCap = cap;
    }
    memcpy(output + outputLen, s, (size_t)n);
    outputLen += n;
    output[outputLen] = '\0';
    if (outputLen > 20000) Fail(curLine, "the program printed too much (over 20000 characters)");
}

// ---- Snapshots ----
static void Snap(int line)
{
    if (run->stepCount >= CI_MAX_STEPS)
    {
        run->truncated = true;
        Fail(line, "stopped after %d steps: this may be an infinite loop (a loop whose condition never becomes false)", CI_MAX_STEPS);
    }
    if (run->stepCount == stepCap)
    {
        int cap = stepCap ? stepCap*2 : 256;
        CiSnapshot *n = Alloc(sizeof(CiSnapshot)*(size_t)cap);
        if (run->steps) memcpy(n, run->steps, sizeof(CiSnapshot)*(size_t)run->stepCount);
        run->steps = n;
        stepCap = cap;
    }
    if (frameCount > 0) frames[frameCount - 1].line = line;

    CiSnapshot *s = &run->steps[run->stepCount++];
    s->line = line;
    snprintf(s->note, sizeof(s->note), "%s", note);
    note[0] = '\0';

    s->frameCount = frameCount;
    s->frames = Alloc(sizeof(CiFrame)*(size_t)(frameCount ? frameCount : 1));
    s->varCount = varCount;
    s->globalCount = globalVarCount;
    s->vars = Alloc(sizeof(CiVar)*(size_t)(varCount ? varCount : 1));
    for (int i = 0; i < varCount; i++)
    {
        snprintf(s->vars[i].name, CI_NAME_LEN, "%s", vars[i].name);
        s->vars[i].type = vars[i].type;
        s->vars[i].addr = vars[i].addr;
    }
    for (int f = 0; f < frameCount; f++)
    {
        snprintf(s->frames[f].func, CI_NAME_LEN, "%s", frames[f].func);
        s->frames[f].line = frames[f].line;
        s->frames[f].firstVar = frames[f].firstVar;
        int end = (f + 1 < frameCount) ? frames[f + 1].firstVar : varCount;
        s->frames[f].varCount = end - frames[f].firstVar;
    }

    s->blockCount = blockCount;
    s->blocks = Alloc(sizeof(CiBlock)*(size_t)(blockCount ? blockCount : 1));
    memcpy(s->blocks, blocks, sizeof(CiBlock)*(size_t)blockCount);
    s->outputLen = outputLen;

    s->stackUsed = sp - CI_STACK_BASE;
    s->heapUsed = heapTop - CI_HEAP_BASE;
    s->dataUsed = literalEnd - CI_DATA_BASE;          // globals, statics and string literals
    s->stack = Alloc((size_t)s->stackUsed + 1);     s->stackInit = Alloc((size_t)s->stackUsed + 1);
    s->heap = Alloc((size_t)s->heapUsed + 1);       s->heapInit = Alloc((size_t)s->heapUsed + 1);
    s->data = Alloc((size_t)s->dataUsed);           s->dataInit = Alloc((size_t)s->dataUsed);
    memcpy(s->stack, &mem[CI_STACK_BASE], (size_t)s->stackUsed);  memcpy(s->stackInit, &memInit[CI_STACK_BASE], (size_t)s->stackUsed);
    memcpy(s->heap, &mem[CI_HEAP_BASE], (size_t)s->heapUsed);     memcpy(s->heapInit, &memInit[CI_HEAP_BASE], (size_t)s->heapUsed);
    memcpy(s->data, &mem[CI_DATA_BASE], (size_t)s->dataUsed);     memcpy(s->dataInit, &memInit[CI_DATA_BASE], (size_t)s->dataUsed);
}

// ---- Checked memory access ----
static const Var *VarAt(int addr)
{
    for (int i = varCount - 1; i >= 0; i--)
        if (addr >= vars[i].addr && addr < vars[i].addr + vars[i].type->size) return &vars[i];
    return NULL;
}

static const Var *NearestVarBelow(int addr)
{
    const Var *best = NULL;
    for (int i = 0; i < varCount; i++)
        if (vars[i].addr <= addr && (best == NULL || vars[i].addr > best->addr)) best = &vars[i];
    return best;
}

static CiBlock *BlockAt(int addr)
{
    for (int i = blockCount - 1; i >= 0; i--)
        if (addr >= blocks[i].addr && addr < blocks[i].addr + blocks[i].size) return &blocks[i];
    return NULL;
}

static void CheckAccess(int addr, int size, bool write)
{
    const char *what = write ? "write to" : "read";
    if (addr >= 0 && addr < CI_NULL_END)
        Fail(curLine, "NULL pointer dereference: tried to %s address %d. The pointer is NULL (or was never set), so there is nothing there.", what, addr);
    if (addr < 0 || addr + size > CI_MEM_SIZE)
        Fail(curLine, "tried to %s address %d, which is outside all memory. The pointer holds a garbage value (was it ever set?).", what, addr);

    if (addr < CI_STACK_END)
    {
        const Var *v = VarAt(addr);
        if (v == NULL)
        {
            if (addr >= sp) Fail(curLine, "tried to %s address %d, which belonged to a function that has already returned. A pointer to a local variable is useless after its function ends (dangling pointer).", what, addr);
            const Var *near = NearestVarBelow(addr);
            if (near) Fail(curLine, "tried to %s address %d, just past the end of %s (%d bytes at %d). Is an index out of range?", what, addr, near->name, near->type->size, near->addr);
            Fail(curLine, "tried to %s address %d, which is not inside any variable", what, addr);
        }
        if (addr + size > v->addr + v->type->size)
            Fail(curLine, "out of bounds: tried to %s past the end of %s (%d bytes at %d). Is an index out of range?", what, v->name, v->type->size, v->addr);
        return;
    }

    if (addr < CI_HEAP_END)
    {
        CiBlock *b = BlockAt(addr);
        if (b == NULL)
        {
            CiBlock *prev = NULL;
            for (int i = 0; i < blockCount; i++) if (blocks[i].addr <= addr && (!prev || blocks[i].addr > prev->addr)) prev = &blocks[i];
            if (prev) Fail(curLine, "heap overflow: tried to %s address %d, past the end of the %d-byte block at %d (malloc'd on line %d). Did you allocate enough?", what, addr, prev->size, prev->addr, prev->allocLine);
            Fail(curLine, "tried to %s address %d on the heap, but nothing was allocated there", what, addr);
        }
        if (b->freeLine) Fail(curLine, "use after free: tried to %s the block at %d, which was freed on line %d. After free() the memory is no longer yours.", what, b->addr, b->freeLine);
        if (addr + size > b->addr + b->size) Fail(curLine, "heap overflow: tried to %s past the end of the %d-byte block at %d. Did you allocate enough?", what, b->size, b->addr);
        return;
    }

    if (addr >= literalStart && addr < literalEnd)
    {
        if (write)
        {
            int start = addr;
            while (start > literalStart && mem[start - 1] != 0) start--;
            Fail(curLine, "string literals are read-only: tried to change the text \"%.40s\". Copy it into a char array first (char s[] = \"...\").", (const char *)&mem[start]);
        }
        return;
    }
    if (addr >= TEMP_BASE) return;     // temporaries for struct return values
    const Var *v = VarAt(addr);
    if (v == NULL || addr + size > v->addr + v->type->size) Fail(curLine, "tried to %s address %d, which is not inside any variable", what, addr);
}

static bool IsInit(int addr, int size)
{
    for (int i = 0; i < size; i++) if (!memInit[addr + i]) return false;
    return true;
}

static void StoreRaw(int addr, const void *src, int size)
{
    CheckAccess(addr, size, true);
    memcpy(&mem[addr], src, (size_t)size);
    memset(&memInit[addr], 1, (size_t)size);
}

// Loads a scalar of type t from addr (with a warning if it never got a value)
static Val Load(int addr, CiType *t)
{
    Val v = { t, 0, 0, addr };
    if (t->kind == CI_ARRAY || t->kind == CI_STRUCT) { v.addr = addr; return v; }
    CheckAccess(addr, t->size, false);
    if (!IsInit(addr, t->size))
    {
        const Var *var = VarAt(addr);
        if (var) Warn(curLine, "line %d reads %s before it was given a value, so it holds garbage", curLine, var->name);
        else Warn(curLine, "line %d reads memory that was never given a value (garbage)", curLine);
    }
    switch (t->kind)
    {
        case CI_BOOL: v.i = mem[addr] != 0; break;
        case CI_CHAR: v.i = (signed char)mem[addr]; break;
        case CI_INT: { int32_t x; memcpy(&x, &mem[addr], 4); v.i = x; } break;
        case CI_LONG: case CI_PTR: { int64_t x; memcpy(&x, &mem[addr], 8); v.i = (long)x; } break;
        case CI_FLOAT: { float x; memcpy(&x, &mem[addr], 4); v.d = x; } break;
        case CI_DOUBLE: { double x; memcpy(&x, &mem[addr], 8); v.d = x; } break;
        default: break;
    }
    return v;
}

static Val MakeInt(long i, CiType *t) { Val v = { t, i, 0, 0 }; return v; }
static Val MakeDouble(double d) { Val v = { &tDouble, 0, d, 0 }; return v; }

static double AsDouble(Val v) { return IsFloating(v.t) ? v.d : (double)v.i; }
static long AsLong(Val v) { return IsFloating(v.t) ? (long)v.d : v.i; }
static bool Truthy(Val v) { return IsFloating(v.t) ? v.d != 0 : v.i != 0; }

// Converts a value to type t, as an assignment or initialisation would
static Val Convert(Val v, CiType *t, int line)
{
    Val r = { t, 0, 0, 0 };
    switch (t->kind)
    {
        case CI_BOOL: r.i = Truthy(v); return r;
        case CI_CHAR: r.i = (signed char)AsLong(v); return r;
        case CI_INT: r.i = (int32_t)AsLong(v); return r;
        case CI_LONG: r.i = AsLong(v); return r;
        case CI_FLOAT: r.d = (float)AsDouble(v); return r;
        case CI_DOUBLE: r.d = AsDouble(v); return r;
        case CI_PTR:
            if (IsFloating(v.t)) Fail(line, "cannot store a decimal number in a pointer");
            if (IsInteger(v.t) && v.i != 0) Warn(line, "line %d stores a plain number in a pointer (%s); did you forget & or *?", line, t->name);
            r.i = v.i;
            return r;
        case CI_STRUCT:
            if (v.t->kind != CI_STRUCT || v.t->structIndex != t->structIndex) Fail(line, "cannot store a %s in a %s", v.t->name, t->name);
            r.addr = v.addr;
            return r;
        default:
            Fail(line, "cannot store a value of type %s here", t->name);
    }
    return r;
}

static void Store(int addr, CiType *t, Val v)
{
    switch (t->kind)
    {
        case CI_BOOL: case CI_CHAR: { unsigned char x = (unsigned char)v.i; StoreRaw(addr, &x, 1); } break;
        case CI_INT: { int32_t x = (int32_t)v.i; StoreRaw(addr, &x, 4); } break;
        case CI_LONG: { int64_t x = v.i; StoreRaw(addr, &x, 8); } break;
        case CI_PTR:
        {
            int64_t x = v.i;
            StoreRaw(addr, &x, 8);
            // Guess what a heap block holds from the first pointer type it is stored in
            CiBlock *b = (v.i >= CI_HEAP_BASE && v.i < CI_HEAP_END) ? BlockAt((int)v.i) : NULL;
            if (b && b->hint == NULL && b->addr == v.i && t->base->kind != CI_VOID) b->hint = t->base;
        } break;
        case CI_FLOAT: { float x = (float)v.d; StoreRaw(addr, &x, 4); } break;
        case CI_DOUBLE: { double x = v.d; StoreRaw(addr, &x, 8); } break;
        case CI_STRUCT:
        {
            CheckAccess(v.addr, t->size, false);
            unsigned char tmp[4096];
            if (t->size > (int)sizeof(tmp)) Fail(curLine, "struct too large");
            memcpy(tmp, &mem[v.addr], (size_t)t->size);
            unsigned char init[4096];
            memcpy(init, &memInit[v.addr], (size_t)t->size);
            CheckAccess(addr, t->size, true);
            memcpy(&mem[addr], tmp, (size_t)t->size);
            memcpy(&memInit[addr], init, (size_t)t->size);
        } break;
        default: Fail(curLine, "cannot store a value of type %s", t->name);
    }
}

// ---- Variables ----
static Var *FindVar(const char *name)
{
    int first = frameCount ? frames[frameCount - 1].firstVar : globalVarCount;
    for (int i = varCount - 1; i >= first; i--) if (strcmp(vars[i].name, name) == 0) return &vars[i];
    for (int i = globalVarCount - 1; i >= 0; i--) if (strcmp(vars[i].name, name) == 0) return &vars[i];
    return NULL;
}

// Fills fresh stack memory with garbage, like a real stack
static void Garbage(int addr, int size)
{
    for (int i = 0; i < size; i++)
    {
        garbageState = garbageState*1103515245u + 12345u;
        mem[addr + i] = (unsigned char)(garbageState >> 16);
        memInit[addr + i] = 0;
    }
}

static Var *DeclareVar(const char *name, CiType *t, int line, bool global)
{
    if (varCount >= MAX_VARS) Fail(line, "too many variables");
    Var *v = &vars[varCount++];
    snprintf(v->name, CI_NAME_LEN, "%s", name);
    v->type = t;
    v->frame = global ? -1 : frameCount - 1;
    if (global)
    {
        int addr = AlignUp(dataTop, t->align);
        v->addr = addr;
        dataTop = addr + t->size;
        memset(&mem[addr], 0, (size_t)t->size);       // globals start at zero
        memset(&memInit[addr], 1, (size_t)t->size);
        globalVarCount = varCount;
    }
    else
    {
        int addr = AlignUp(sp, t->align);
        if (addr + t->size > CI_STACK_END)
            Fail(line, "stack overflow: the stack is full (%d bytes). Too many nested calls (recursion without a base case?) or a very large local array.", CI_STACK_END - CI_STACK_BASE);
        v->addr = addr;
        sp = addr + t->size;
        Garbage(addr, t->size);
    }
    return v;
}

static void Initialise(int addr, CiType *t, Node *init);

// A static local lives in the globals area and is initialised only the first time; each call
// makes the name visible in its frame again, pointing at that same memory.
static void DeclareStatic(Node *n)
{
    if (varCount >= MAX_VARS) Fail(n->line, "too many variables");
    bool first = (n->strAddr == 0);
    if (first)
    {
        int addr = AlignUp(dataTop, n->type->align);
        if (addr + n->type->size > CI_DATA_BASE + 1024) Fail(n->line, "too many global and static variables for the Visualizer");
        dataTop = addr + n->type->size;
        memset(&mem[addr], 0, (size_t)n->type->size);
        memset(&memInit[addr], 1, (size_t)n->type->size);
        n->strAddr = addr;
    }
    Var *v = &vars[varCount++];
    snprintf(v->name, CI_NAME_LEN, "%s", n->name);
    v->type = n->type;
    v->addr = n->strAddr;
    v->frame = frameCount - 1;
    if (first && n->a) Initialise(v->addr, v->type, n->a);
}

// ---- Expressions ----
static Val Eval(Node *n);
static Flow Exec(Node *n);

typedef struct LVal { int addr; CiType *t; } LVal;

static const CiField *FindField(CiType *st, const char *name, int line)
{
    const CiStruct *s = &structs[st->structIndex];
    for (int i = 0; i < s->fieldCount; i++) if (strcmp(s->fields[i].name, name) == 0) return &s->fields[i];
    Fail(line, "struct %s has no field called %s", s->name, name);
    return NULL;
}

static LVal EvalLVal(Node *n)
{
    curLine = n->line;
    switch (n->kind)
    {
        case N_IDENT:
        {
            Var *v = FindVar(n->name);
            if (v == NULL)
            {
                if (FindFunc(n->name)) Fail(n->line, "%s is a function; call it with %s(...)", n->name, n->name);
                Fail(n->line, "'%s' is not declared (check the spelling, and that it is declared before this line)", n->name);
            }
            return (LVal){ v->addr, v->type };
        }
        case N_DEREF:
        {
            Val p = Eval(n->a);
            if (!IsPtr(p.t)) Fail(n->line, "* can only be used on a pointer, but this is a %s", p.t->name);
            if (p.t->base->kind == CI_VOID) Fail(n->line, "cannot use * on a void pointer; cast it to a real pointer type first");
            curLine = n->line;
            return (LVal){ (int)p.i, p.t->base };
        }
        case N_INDEX:
        {
            Val base = Eval(n->a);
            Val idx = Eval(n->b);
            curLine = n->line;
            if (IsPtr(idx.t) && IsInteger(base.t)) { Val t = base; base = idx; idx = t; }
            if (!IsPtr(base.t)) Fail(n->line, "[ ] can only be used on an array or a pointer");
            if (!IsInteger(idx.t)) Fail(n->line, "an array index must be a whole number");
            CiType *elem = base.t->base;
            // Arrays: a clear message for an index out of range
            if (n->a->kind == N_IDENT || n->a->kind == N_MEMBER || n->a->kind == N_ARROW || n->a->kind == N_INDEX)
            {
                LVal arr = EvalLVal(n->a);
                curLine = n->line;
                if (arr.t->kind == CI_ARRAY && (idx.i < 0 || idx.i >= arr.t->len))
                    Fail(n->line, "index %ld is out of range: the array has %d elements, so valid indexes are 0 to %d", idx.i, arr.t->len, arr.t->len - 1);
            }
            return (LVal){ (int)(base.i + idx.i*elem->size), elem };
        }
        case N_MEMBER:
        {
            LVal s = EvalLVal(n->a);
            if (s.t->kind != CI_STRUCT) Fail(n->line, ". is used on a struct; for a pointer to a struct use ->");
            const CiField *f = FindField(s.t, n->name, n->line);
            return (LVal){ s.addr + f->offset, f->type };
        }
        case N_ARROW:
        {
            Val p = Eval(n->a);
            curLine = n->line;
            if (!IsPtr(p.t) || p.t->base->kind != CI_STRUCT)
            {
                if (p.t->kind == CI_STRUCT) Fail(n->line, "-> is used on a pointer to a struct; for a struct itself use .");
                Fail(n->line, "-> needs a pointer to a struct");
            }
            const CiField *f = FindField(p.t->base, n->name, n->line);
            if (p.i >= 0 && p.i < CI_NULL_END) Fail(n->line, "NULL pointer dereference: %s is NULL here, so ->%s has nothing to read. Check for NULL before using ->.", n->a->kind == N_IDENT ? n->a->name : "the pointer", n->name);
            return (LVal){ (int)p.i + f->offset, f->type };
        }
        case N_STR: return (LVal){ n->strAddr, n->type };
        case N_CALL:
        {
            Val v = Eval(n);
            if (v.t->kind == CI_STRUCT) return (LVal){ v.addr, v.t };
        } break;
        default: break;
    }
    Fail(n->line, "this is not something you can assign to or take the address of");
    return (LVal){ 0, NULL };
}

// Value of an lvalue: arrays decay to a pointer to their first element
static Val LoadLVal(LVal lv)
{
    if (lv.t->kind == CI_ARRAY) return MakeInt(lv.addr, PtrTo(lv.t->base));
    return Load(lv.addr, lv.t);
}

static CiType *ArithResult(CiType *a, CiType *b)
{
    if (IsFloating(a) || IsFloating(b)) return &tDouble;
    if (a->kind == CI_LONG || b->kind == CI_LONG) return &tLong;
    return &tInt;
}

static Val Normalize(long i, CiType *t)
{
    if (t->kind == CI_INT) return MakeInt((int32_t)i, &tInt);
    return MakeInt(i, t);
}

static Val Binary(const char *op, Val a, Val b, int line)
{
    // Pointer arithmetic
    if (IsPtr(a.t) || IsPtr(b.t))
    {
        if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0)
        {
            if (IsPtr(a.t) && IsPtr(b.t))
            {
                if (op[0] == '+') Fail(line, "two pointers cannot be added");
                int size = a.t->base->size ? a.t->base->size : 1;
                return MakeInt((a.i - b.i)/size, &tLong);
            }
            Val p = IsPtr(a.t) ? a : b, k = IsPtr(a.t) ? b : a;
            if (!IsInteger(k.t)) Fail(line, "only whole numbers can be added to a pointer");
            if (op[0] == '-' && !IsPtr(a.t)) Fail(line, "a pointer cannot be subtracted from a number");
            int size = p.t->base->kind == CI_VOID ? 1 : p.t->base->size;
            return MakeInt(p.i + (op[0] == '+' ? k.i : -k.i)*size, p.t);
        }
        long x = a.i, y = b.i;
        if (strcmp(op, "==") == 0) return MakeInt(x == y, &tInt);
        if (strcmp(op, "!=") == 0) return MakeInt(x != y, &tInt);
        if (strcmp(op, "<") == 0) return MakeInt(x < y, &tInt);
        if (strcmp(op, ">") == 0) return MakeInt(x > y, &tInt);
        if (strcmp(op, "<=") == 0) return MakeInt(x <= y, &tInt);
        if (strcmp(op, ">=") == 0) return MakeInt(x >= y, &tInt);
        Fail(line, "operator %s cannot be used with a pointer", op);
    }
    if (!IsArith(a.t) || !IsArith(b.t)) Fail(line, "operator %s needs numbers (got %s and %s)", op, a.t->name, b.t->name);

    CiType *rt = ArithResult(a.t, b.t);
    if (IsFloating(rt))
    {
        double x = AsDouble(a), y = AsDouble(b);
        if (strcmp(op, "+") == 0) return MakeDouble(x + y);
        if (strcmp(op, "-") == 0) return MakeDouble(x - y);
        if (strcmp(op, "*") == 0) return MakeDouble(x*y);
        if (strcmp(op, "/") == 0) return MakeDouble(x/y);
        if (strcmp(op, "==") == 0) return MakeInt(x == y, &tInt);
        if (strcmp(op, "!=") == 0) return MakeInt(x != y, &tInt);
        if (strcmp(op, "<") == 0) return MakeInt(x < y, &tInt);
        if (strcmp(op, ">") == 0) return MakeInt(x > y, &tInt);
        if (strcmp(op, "<=") == 0) return MakeInt(x <= y, &tInt);
        if (strcmp(op, ">=") == 0) return MakeInt(x >= y, &tInt);
        Fail(line, "operator %s needs whole numbers, not decimals", op);
    }
    long x = a.i, y = b.i;
    if (strcmp(op, "+") == 0) return Normalize(x + y, rt);
    if (strcmp(op, "-") == 0) return Normalize(x - y, rt);
    if (strcmp(op, "*") == 0) return Normalize(x*y, rt);
    if (strcmp(op, "/") == 0) { if (y == 0) Fail(line, "division by zero"); return Normalize(x/y, rt); }
    if (strcmp(op, "%") == 0) { if (y == 0) Fail(line, "division by zero (in %%)"); return Normalize(x%y, rt); }
    if (strcmp(op, "<<") == 0) return Normalize(x << y, rt);
    if (strcmp(op, ">>") == 0) return Normalize(x >> y, rt);
    if (strcmp(op, "&") == 0) return Normalize(x & y, rt);
    if (strcmp(op, "|") == 0) return Normalize(x | y, rt);
    if (strcmp(op, "^") == 0) return Normalize(x ^ y, rt);
    if (strcmp(op, "==") == 0) return MakeInt(x == y, &tInt);
    if (strcmp(op, "!=") == 0) return MakeInt(x != y, &tInt);
    if (strcmp(op, "<") == 0) return MakeInt(x < y, &tInt);
    if (strcmp(op, ">") == 0) return MakeInt(x > y, &tInt);
    if (strcmp(op, "<=") == 0) return MakeInt(x <= y, &tInt);
    if (strcmp(op, ">=") == 0) return MakeInt(x >= y, &tInt);
    Fail(line, "unknown operator %s", op);
    return MakeInt(0, &tInt);
}

// Reads a NUL-terminated string from simulated memory (checked)
static int ReadString(int addr, char *out, int cap)
{
    // Find where the array (or block) holding the text ends, to explain a missing '\0' clearly
    int end = -1;
    const char *holder = NULL;
    if (addr >= CI_STACK_BASE && addr < CI_STACK_END) { const Var *v = VarAt(addr); if (v) { end = v->addr + v->type->size; holder = v->name; } }
    else if (addr >= CI_HEAP_BASE && addr < CI_HEAP_END) { CiBlock *b = BlockAt(addr); if (b && !b->freeLine) { end = b->addr + b->size; holder = "the malloc'd block"; } }

    int n = 0;
    for (;;)
    {
        if (end >= 0 && addr + n >= end)
            Fail(curLine, "this text has no '\\0' before the end of %s, so C cannot tell where the string stops. Was it ever given a string value (strcpy, \"...\", or input)?", holder);
        CheckAccess(addr + n, 1, false);
        char c = (char)mem[addr + n];
        if (!memInit[addr + n]) Warn(curLine, "line %d uses a string that has no '\\0' at the end yet (garbage)", curLine);
        if (c == '\0') break;
        if (n < cap - 1) out[n] = c;
        n++;
        if (n > 10000) Fail(curLine, "this string never ends: there is no '\\0'");
    }
    out[n < cap ? n : cap - 1] = '\0';
    return n;
}

static Val CallFunc(Func *f, Val *args, int argc, int line);

static int Malloc(int size, int line)
{
    if (size <= 0) Fail(line, "malloc(%d): the size must be positive", size);
    if (blockCount >= MAX_BLOCKS) Fail(line, "too many malloc calls for the Visualizer (%d)", MAX_BLOCKS);
    int addr = AlignUp(heapTop, 8);
    if (addr + size > CI_HEAP_END) Fail(line, "out of memory: the heap is full (%d bytes). Is memory freed?", CI_HEAP_END - CI_HEAP_BASE);
    heapTop = addr + size + 8;   // a gap between blocks, so overflows land outside any block
    CiBlock *b = &blocks[blockCount++];
    memset(b, 0, sizeof(*b));
    b->addr = addr; b->size = size; b->allocLine = line;
    Garbage(addr, size);
    return addr;
}

static void Free(long p, int line)
{
    if (p == 0) return;
    CiBlock *b = BlockAt((int)p);
    if (b == NULL || b->addr != p)
    {
        if (b) Fail(line, "free(%ld): this points into the middle of a block. Pass the pointer that malloc returned.", p);
        Fail(line, "free(%ld): this memory did not come from malloc. Only free what malloc/calloc/realloc gave you.", p);
    }
    if (b->freeLine) Fail(line, "double free: the block at %ld was already freed on line %d", p, b->freeLine);
    b->freeLine = line;
}

// scanf on the input text: %d %i %ld %f %lf %c %s (with widths), spaces, and literal characters
static Val Scanf(Val *args, int argc, int line)
{
    if (argc < 1 || !IsPtr(args[0].t)) Fail(line, "scanf needs a format string first");
    char fmt[512];
    ReadString((int)args[0].i, fmt, sizeof(fmt));
    int argi = 1, assigned = 0;
    char readText[80] = "";

    for (const char *p = fmt; *p; p++)
    {
        if (isspace((unsigned char)*p)) { while (isspace((unsigned char)input[inputPos])) inputPos++; continue; }
        if (*p != '%' || p[1] == '%')
        {
            if (*p == '%') p++;
            if (input[inputPos] != *p) break;
            inputPos++;
            continue;
        }
        p++;
        bool suppress = false;
        if (*p == '*') { suppress = true; p++; }
        int width = 0;
        while (isdigit((unsigned char)*p)) width = width*10 + (*p++ - '0');
        bool isLong = false;
        while (*p == 'l' || *p == 'h') { if (*p == 'l') isLong = true; p++; }
        char conv = *p;
        if (conv == '\0') break;

        if (conv != 'c') while (isspace((unsigned char)input[inputPos])) inputPos++;
        if (input[inputPos] == '\0')
        {
            if (assigned == 0 && argi == 1)
                Fail(line, "scanf is waiting for input, but the Input box is empty (or all of it has been read). Type the input your program expects into the Input box.");
            break;
        }

        Val target = { 0 };
        if (!suppress)
        {
            if (argi >= argc) Fail(line, "scanf: %%%c has no matching argument", conv);
            target = args[argi++];
            if (!IsPtr(target.t)) Fail(line, "scanf needs the address of the variable: write &x, not x");
        }

        const char *start = &input[inputPos];
        char *end;
        char piece[64];
        switch (conv)
        {
            case 'd': case 'i': case 'u':
            {
                long v = strtol(start, &end, 10);
                if (end == start) goto stop;
                inputPos += (int)(end - start);
                if (!suppress)
                {
                    CiType *t = target.t->base;
                    if (IsFloating(t)) Fail(line, "scanf: %%d needs the address of an int, but this is a %s (use %%lf for double)", t->name);
                    Store((int)target.i, (isLong || t->kind == CI_LONG) ? &tLong : (t->kind == CI_CHAR ? &tChar : &tInt), MakeInt(v, &tLong));
                    assigned++;
                }
                snprintf(piece, sizeof(piece), "%ld ", v);
            } break;
            case 'f': case 'e': case 'g':
            {
                double v = strtod(start, &end);
                if (end == start) goto stop;
                inputPos += (int)(end - start);
                if (!suppress)
                {
                    CiType *t = target.t->base;
                    if (!IsFloating(t)) Fail(line, "scanf: %%%s needs the address of a %s variable, but this is a %s", isLong ? "lf" : "f", isLong ? "double" : "float", t->name);
                    if (t->kind == CI_DOUBLE && !isLong) Fail(line, "scanf: use %%lf (not %%f) to read a double");
                    Store((int)target.i, t, MakeDouble(v));
                    assigned++;
                }
                snprintf(piece, sizeof(piece), "%g ", v);
            } break;
            case 'c':
            {
                char c = input[inputPos++];
                if (!suppress) { StoreRaw((int)target.i, &c, 1); assigned++; }
                snprintf(piece, sizeof(piece), "'%c' ", c == '\n' ? ' ' : c);
            } break;
            case 's':
            {
                int k = 0;
                while (input[inputPos] && !isspace((unsigned char)input[inputPos]) && (width == 0 || k < width))
                {
                    char c = input[inputPos++];
                    if (!suppress) StoreRaw((int)target.i + k, &c, 1);
                    if (k < 40) piece[k] = c;
                    k++;
                }
                if (!suppress) { char zero = 0; StoreRaw((int)target.i + k, &zero, 1); assigned++; }
                piece[k < 40 ? k : 40] = '\0';
                strcat(piece, " ");
            } break;
            default: Fail(line, "scanf: %%%c is not supported", conv);
        }
        if (strlen(readText) + strlen(piece) < sizeof(readText) - 1) strcat(readText, piece);
    }
stop:
    if (readText[0]) snprintf(note, sizeof(note), "scanf read %s", readText);
    else snprintf(note, sizeof(note), "scanf could not read a value (the input does not match the format)");
    return MakeInt(assigned, &tInt);
}

static Val Builtin(Node *n, bool *handled)
{
    *handled = true;
    int line = n->line;
    Val args[8];
    int argc = 0;
    for (Node *a = n->a; a; a = a->next)
    {
        if (argc >= 8) Fail(line, "too many arguments");
        args[argc++] = Eval(a);
    }
    curLine = line;
    #define NEED(k) do { if (argc != (k)) Fail(line, "%s needs %d argument%s", n->name, (k), (k) == 1 ? "" : "s"); } while (0)

    if (strcmp(n->name, "printf") == 0)
    {
        if (argc < 1 || !IsPtr(args[0].t)) Fail(line, "printf needs a format string first");
        char fmt[1024];
        ReadString((int)args[0].i, fmt, sizeof(fmt));
        char buf[2048];
        int used = 0, argi = 1, count = 0;
        for (const char *p = fmt; *p; p++)
        {
            if (*p != '%') { Out(p, 1); count++; continue; }
            const char *start = p++;
            if (*p == '%') { Out("%", 1); count++; continue; }
            while (*p && strchr("-+ #0", *p)) p++;
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
            bool isLong = false;
            while (*p == 'l' || *p == 'h' || *p == 'z') { if (*p != 'h') isLong = true; p++; }
            char conv = *p;
            if (conv == '\0') break;
            char spec[32];
            int specLen = 0;
            for (const char *q = start; q < p && specLen < 20; q++) if (*q != 'l' && *q != 'h' && *q != 'z') spec[specLen++] = *q;
            spec[specLen] = '\0';
            if (argi >= argc) Fail(line, "printf: %%%c has no matching argument", conv);
            Val a = args[argi++];
            char piece[1100];
            switch (conv)
            {
                case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
                {
                    if (IsFloating(a.t)) Fail(line, "printf: %%%c expects a whole number, but this argument is a decimal (use %%f)", conv);
                    char s2[40]; snprintf(s2, sizeof(s2), "%sl%c", spec, conv);
                    snprintf(piece, sizeof(piece), s2, isLong || a.t->kind == CI_LONG || IsPtr(a.t) ? a.i : (long)(int32_t)a.i);
                } break;
                case 'c':
                {
                    char s2[40]; snprintf(s2, sizeof(s2), "%sc", spec);
                    snprintf(piece, sizeof(piece), s2, (int)(char)AsLong(a));
                } break;
                case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
                {
                    if (!IsFloating(a.t)) Fail(line, "printf: %%%c expects a decimal number, but this argument is a %s (use %%d, or write 2.0)", conv, a.t->name);
                    char s2[40]; snprintf(s2, sizeof(s2), "%s%c", spec, conv);
                    snprintf(piece, sizeof(piece), s2, a.d);
                } break;
                case 's':
                {
                    if (!IsPtr(a.t)) Fail(line, "printf: %%s expects a string (a char pointer or array), but this argument is a %s", a.t->name);
                    char str[1024];
                    ReadString((int)a.i, str, sizeof(str));
                    char s2[40]; snprintf(s2, sizeof(s2), "%ss", spec);
                    snprintf(piece, sizeof(piece), s2, str);
                } break;
                case 'p': snprintf(piece, sizeof(piece), "%ld", a.i); break;
                default: Fail(line, "printf: %%%c is not supported", conv);
            }
            int len = (int)strlen(piece);
            Out(piece, len);
            count += len;
            (void)buf; (void)used;
        }
        return MakeInt(count, &tInt);
    }
    if (strcmp(n->name, "puts") == 0)
    {
        NEED(1);
        char s[1024];
        int len = ReadString((int)args[0].i, s, sizeof(s));
        Out(s, len < (int)sizeof(s) ? len : (int)sizeof(s) - 1);
        Out("\n", 1);
        return MakeInt(0, &tInt);
    }
    if (strcmp(n->name, "putchar") == 0)
    {
        NEED(1);
        char c = (char)AsLong(args[0]);
        Out(&c, 1);
        return MakeInt(c, &tInt);
    }
    if (strcmp(n->name, "malloc") == 0 || strcmp(n->name, "calloc") == 0)
    {
        bool isCalloc = n->name[0] == 'c';
        NEED(isCalloc ? 2 : 1);
        long size = isCalloc ? AsLong(args[0])*AsLong(args[1]) : AsLong(args[0]);
        int addr = Malloc((int)size, line);
        if (isCalloc) { memset(&mem[addr], 0, (size_t)size); memset(&memInit[addr], 1, (size_t)size); }
        snprintf(note, sizeof(note), "%s(%ld) returned %d", n->name, size, addr);
        return MakeInt(addr, PtrTo(&tVoid));
    }
    if (strcmp(n->name, "realloc") == 0)
    {
        NEED(2);
        long old = args[0].i, size = AsLong(args[1]);
        int addr = Malloc((int)size, line);
        if (old != 0)
        {
            CiBlock *b = BlockAt((int)old);
            if (b == NULL || b->addr != old || b->freeLine) Fail(line, "realloc: the old pointer did not come from malloc (or was freed)");
            int copy = b->size < size ? b->size : (int)size;
            memcpy(&mem[addr], &mem[old], (size_t)copy);
            memcpy(&memInit[addr], &memInit[old], (size_t)copy);
            blocks[blockCount - 1].hint = b->hint;
            Free(old, line);
        }
        snprintf(note, sizeof(note), "realloc moved the data to %d", addr);
        return MakeInt(addr, PtrTo(&tVoid));
    }
    if (strcmp(n->name, "free") == 0)
    {
        NEED(1);
        if (!IsPtr(args[0].t) && !(IsInteger(args[0].t) && args[0].i == 0)) Fail(line, "free needs a pointer");
        Free(args[0].i, line);
        if (args[0].i) snprintf(note, sizeof(note), "freed the block at %ld", args[0].i);
        return MakeInt(0, &tVoid);
    }
    if (strcmp(n->name, "strlen") == 0)
    {
        NEED(1);
        char s[2];
        return MakeInt(ReadString((int)args[0].i, s, 1), &tLong);
    }
    if (strcmp(n->name, "strcpy") == 0 || strcmp(n->name, "strcat") == 0 || strcmp(n->name, "strncpy") == 0)
    {
        bool isN = n->name[3] == 'n';
        NEED(isN ? 3 : 2);
        char src[4096];
        int len = ReadString((int)args[1].i, src, sizeof(src));
        int dst = (int)args[0].i;
        if (n->name[3] == 'c' && n->name[4] == 'a')   // strcat: append after the existing text
        {
            char d[2];
            dst += ReadString(dst, d, 1);
        }
        if (isN)
        {
            long max = AsLong(args[2]);
            for (long i = 0; i < max; i++) { char c = (i < len) ? src[i] : '\0'; StoreRaw(dst + (int)i, &c, 1); }
        }
        else
        {
            for (int i = 0; i <= len; i++) StoreRaw(dst + i, &src[i], 1);
        }
        return args[0];
    }
    if (strcmp(n->name, "strcmp") == 0)
    {
        NEED(2);
        char a[4096], b[4096];
        ReadString((int)args[0].i, a, sizeof(a));
        ReadString((int)args[1].i, b, sizeof(b));
        int r = strcmp(a, b);
        return MakeInt(r < 0 ? -1 : r > 0 ? 1 : 0, &tInt);
    }
    if (strcmp(n->name, "memset") == 0)
    {
        NEED(3);
        unsigned char c = (unsigned char)AsLong(args[1]);
        for (long i = 0; i < AsLong(args[2]); i++) StoreRaw((int)args[0].i + (int)i, &c, 1);
        return args[0];
    }
    if (strcmp(n->name, "memcpy") == 0)
    {
        NEED(3);
        long count = AsLong(args[2]);
        for (long i = 0; i < count; i++)
        {
            CheckAccess((int)args[1].i + (int)i, 1, false);
            unsigned char c = mem[args[1].i + i];
            StoreRaw((int)args[0].i + (int)i, &c, 1);
        }
        return args[0];
    }
    if (strcmp(n->name, "abs") == 0) { NEED(1); long x = AsLong(args[0]); return MakeInt(x < 0 ? -x : x, &tInt); }
    if (strcmp(n->name, "sqrt") == 0) { NEED(1); return MakeDouble(sqrt(AsDouble(args[0]))); }
    if (strcmp(n->name, "fabs") == 0) { NEED(1); return MakeDouble(fabs(AsDouble(args[0]))); }
    if (strcmp(n->name, "pow") == 0) { NEED(2); return MakeDouble(pow(AsDouble(args[0]), AsDouble(args[1]))); }
    if (strcmp(n->name, "rand") == 0) { randState = randState*1103515245u + 12345u; return MakeInt((randState >> 16) & 32767, &tInt); }
    if (strcmp(n->name, "srand") == 0) { NEED(1); randState = (unsigned)AsLong(args[0]); return MakeInt(0, &tVoid); }
    if (strcmp(n->name, "exit") == 0) { NEED(1); Fail(line, "the program called exit(%ld)", AsLong(args[0])); }
    if (strcmp(n->name, "scanf") == 0) return Scanf(args, argc, line);
    if (strcmp(n->name, "getchar") == 0)
    {
        NEED(0);
        if (input[inputPos] == '\0') return MakeInt(-1, &tInt);
        return MakeInt((unsigned char)input[inputPos++], &tInt);
    }
    if (strcmp(n->name, "fgets") == 0)
    {
        NEED(3);
        long max = AsLong(args[1]);
        if (input[inputPos] == '\0') { snprintf(note, sizeof(note), "fgets: no more input"); return MakeInt(0, PtrTo(&tChar)); }
        int dst = (int)args[0].i, k = 0;
        while (k < max - 1 && input[inputPos] != '\0')
        {
            char c = input[inputPos++];
            StoreRaw(dst + k++, &c, 1);
            if (c == '\n') break;
        }
        char zero = 0;
        StoreRaw(dst + k, &zero, 1);
        snprintf(note, sizeof(note), "fgets read %d character%s", k, k == 1 ? "" : "s");
        return args[0];
    }

    *handled = false;
    return MakeInt(0, &tVoid);
}

static Val Eval(Node *n)
{
    curLine = n->line;
    switch (n->kind)
    {
        case N_NUM: return MakeInt(n->ival, n->type);
        case N_FNUM: return MakeDouble(n->fval);
        case N_STR: return MakeInt(n->strAddr, PtrTo(&tChar));
        case N_IDENT: case N_INDEX: case N_MEMBER: case N_ARROW: case N_DEREF:
        {
            LVal lv = EvalLVal(n);
            return LoadLVal(lv);
        }
        case N_ADDR:
        {
            if (n->a->kind == N_IDENT && FindFunc(n->a->name) && !FindVar(n->a->name)) Fail(n->line, "taking the address of a function is not supported");
            LVal lv = EvalLVal(n->a);
            return MakeInt(lv.addr, PtrTo(lv.t));
        }
        case N_ASSIGN:
        {
            LVal lv = EvalLVal(n->a);
            if (lv.t->kind == CI_ARRAY) Fail(n->line, "an array cannot be assigned with =; copy it element by element (or use strcpy for strings)");
            Val v = Eval(n->b);
            curLine = n->line;
            if (n->op[0] != '=')
            {
                char op[4];
                snprintf(op, sizeof(op), "%.*s", (int)strlen(n->op) - 1, n->op);
                v = Binary(op, Load(lv.addr, lv.t), v, n->line);
            }
            if (lv.t->kind == CI_PTR && IsPtr(v.t) && v.t->base->kind != CI_VOID && lv.t->base->kind != CI_VOID && v.t->base->size != lv.t->base->size)
                Warn(n->line, "line %d stores a %s in a %s", n->line, v.t->name, lv.t->name);
            Val c = Convert(v, lv.t, n->line);
            Store(lv.addr, lv.t, c);
            return c;
        }
        case N_UNARY:
        {
            Val v = Eval(n->a);
            switch (n->op[0])
            {
                case '-': if (IsFloating(v.t)) return MakeDouble(-v.d); return Normalize(-v.i, ArithResult(v.t, &tInt));
                case '+': return v;
                case '!': return MakeInt(!Truthy(v), &tInt);
                case '~': return Normalize(~v.i, ArithResult(v.t, &tInt));
            }
        } break;
        case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
        {
            LVal lv = EvalLVal(n->a);
            Val old = Load(lv.addr, lv.t);
            curLine = n->line;
            bool inc = (n->kind == N_PREINC || n->kind == N_POSTINC);
            Val nv = Binary(inc ? "+" : "-", old, MakeInt(1, &tInt), n->line);
            nv = Convert(nv, lv.t, n->line);
            Store(lv.addr, lv.t, nv);
            return (n->kind == N_PREINC || n->kind == N_PREDEC) ? nv : old;
        }
        case N_BINARY:
        {
            Val a = Eval(n->a);
            Val b = Eval(n->b);
            return Binary(n->op, a, b, n->line);
        }
        case N_LOGAND: { if (!Truthy(Eval(n->a))) return MakeInt(0, &tInt); return MakeInt(Truthy(Eval(n->b)), &tInt); }
        case N_LOGOR: { if (Truthy(Eval(n->a))) return MakeInt(1, &tInt); return MakeInt(Truthy(Eval(n->b)), &tInt); }
        case N_COND: return Truthy(Eval(n->a)) ? Eval(n->b) : Eval(n->c);
        case N_COMMA: Eval(n->a); return Eval(n->b);
        case N_CAST:
        {
            Val v = Eval(n->a);
            if (n->type->kind == CI_VOID) return MakeInt(0, &tVoid);
            if (n->type->kind == CI_PTR) { Val r = MakeInt(AsLong(v), n->type); return r; }
            return Convert(v, n->type, n->line);
        }
        case N_SIZEOF_TYPE: return MakeInt(n->type->size, &tLong);
        case N_SIZEOF_EXPR:
        {
            // sizeof needs the type without running the expression: evaluate on a copy of the state
            Node *e = n->a;
            if (e->kind == N_IDENT) { Var *v = FindVar(e->name); if (v) return MakeInt(v->type->size, &tLong); }
            if (e->kind == N_STR) return MakeInt(e->type->size, &tLong);
            if (e->kind == N_DEREF || e->kind == N_INDEX || e->kind == N_MEMBER || e->kind == N_ARROW) { LVal lv = EvalLVal(e); return MakeInt(lv.t->size, &tLong); }
            Val v = Eval(e);
            return MakeInt(v.t->size, &tLong);
        }
        case N_CALL:
        {
            Func *f = FindFunc(n->name);
            if (f == NULL)
            {
                bool handled;
                Val r = Builtin(n, &handled);
                if (handled) return r;
                Fail(n->line, "there is no function called %s", n->name);
            }
            Val args[8];
            int argc = 0;
            for (Node *a = n->a; a; a = a->next)
            {
                if (argc >= 8) Fail(n->line, "too many arguments");
                args[argc++] = Eval(a);
            }
            curLine = n->line;
            if (argc != f->paramCount) Fail(n->line, "%s takes %d argument%s but is given %d", f->name, f->paramCount, f->paramCount == 1 ? "" : "s", argc);
            return CallFunc(f, args, argc, n->line);
        }
        case N_INITLIST: Fail(n->line, "a { ... } list can only be used to initialise a variable");
        default: break;
    }
    Fail(n->line, "cannot evaluate this expression");
    return MakeInt(0, &tInt);
}

// Initialises a variable of type t at addr from an initialiser (expression, string or { list })
static void Initialise(int addr, CiType *t, Node *init)
{
    curLine = init->line;
    if (t->kind == CI_ARRAY)
    {
        if (init->kind == N_STR && t->base->kind == CI_CHAR)
        {
            int len = init->type->len;      // includes the '\0'
            if (len > t->len + 1) Fail(init->line, "the text is longer than the array (%d characters)", t->len);
            for (int i = 0; i < t->len; i++) { char c = (i < len) ? (char)mem[init->strAddr + i] : 0; StoreRaw(addr + i, &c, 1); }
            if (len == t->len + 1) Warn(init->line, "line %d: the array is exactly as long as the text, so there is no room for the '\\0'", init->line);
            return;
        }
        if (init->kind != N_INITLIST) Fail(init->line, "an array is initialised with { ... }");
        int i = 0;
        for (Node *e = init->a; e; e = e->next, i++)
        {
            if (i >= t->len) Fail(init->line, "too many values for an array of %d", t->len);
            Initialise(addr + i*t->base->size, t->base, e);
        }
        for (; i < t->len; i++)      // the rest become zero
        {
            unsigned char zero[4096] = { 0 };
            if (t->base->size > (int)sizeof(zero)) Fail(init->line, "array element too large");
            StoreRaw(addr + i*t->base->size, zero, t->base->size);
        }
        return;
    }
    if (t->kind == CI_STRUCT && init->kind == N_INITLIST)
    {
        const CiStruct *s = &structs[t->structIndex];
        unsigned char zero[4096] = { 0 };
        StoreRaw(addr, zero, t->size);       // unlisted fields (and padding) are zero
        int i = 0;
        for (Node *e = init->a; e; e = e->next, i++)
        {
            if (i >= s->fieldCount) Fail(init->line, "too many values for struct %s", s->name);
            Initialise(addr + s->fields[i].offset, s->fields[i].type, e);
        }
        return;
    }
    if (init->kind == N_INITLIST)
    {
        if (init->a && !init->a->next) { Initialise(addr, t, init->a); return; }
        Fail(init->line, "{ ... } is for arrays and structs");
    }
    Val v = Eval(init);
    curLine = init->line;
    Store(addr, t, Convert(v, t, init->line));
}

static Val CallFunc(Func *f, Val *args, int argc, int line)
{
    if (f->body == NULL) Fail(line, "%s is declared but never defined (it has no body)", f->name);
    if (frameCount >= MAX_DEPTH) Fail(line, "stack overflow: more than %d calls in progress. Does the recursion reach its base case?", MAX_DEPTH);

    // Struct arguments: keep a copy, because the new frame's memory may overlap temporaries
    Frame *fr = &frames[frameCount++];
    fr->func = f->name;
    fr->firstVar = varCount;
    fr->line = f->line;
    fr->spAtStart = sp;
    int savedTemp = tempTop;

    for (int i = 0; i < argc; i++)
    {
        Var *v = DeclareVar(f->paramNames[i], f->paramTypes[i], f->line, false);
        curLine = line;
        Store(v->addr, v->type, Convert(args[i], f->paramTypes[i], line));
    }

    Snap(f->line);      // shows the new frame with its parameters

    returnValue = MakeInt(0, &tVoid);
    Flow fl = Exec(f->body);
    Val ret = (fl == FLOW_RETURN) ? returnValue : MakeInt(0, &tVoid);

    if (fl != FLOW_RETURN && f->ret->kind != CI_VOID && strcmp(f->name, "main") != 0)
        Warn(line, "%s ended without return, so the value it gives back is garbage", f->name);

    // Struct results are copied to a temporary area that survives popping this frame
    if (f->ret->kind == CI_STRUCT && fl == FLOW_RETURN)
    {
        int tmp = AlignUp(tempTop, 8);
        if (tmp + f->ret->size > CI_MEM_SIZE) tmp = TEMP_BASE;
        memcpy(&mem[tmp], &mem[ret.addr], (size_t)f->ret->size);
        memcpy(&memInit[tmp], &memInit[ret.addr], (size_t)f->ret->size);
        ret.addr = tmp;
        tempTop = tmp + f->ret->size;
    }
    else tempTop = savedTemp;

    varCount = fr->firstVar;
    sp = fr->spAtStart;
    frameCount--;

    if (f->ret->kind != CI_VOID && fl == FLOW_RETURN && f->ret->kind != CI_STRUCT)
    {
        char vbuf[40];
        if (IsFloating(ret.t)) snprintf(vbuf, sizeof(vbuf), "%g", ret.d); else snprintf(vbuf, sizeof(vbuf), "%ld", ret.i);
        snprintf(note, sizeof(note), "%s returned %s", f->name, vbuf);
    }
    return ret;
}

// ---- Statements ----
static Flow ExecList(Node *list)
{
    for (Node *s = list; s; s = s->next)
    {
        Flow f = Exec(s);
        if (f != FLOW_NORMAL) return f;
    }
    return FLOW_NORMAL;
}

static Flow Exec(Node *n)
{
    if (n == NULL) return FLOW_NORMAL;
    curLine = n->line;
    switch (n->kind)
    {
        case N_EMPTY: return FLOW_NORMAL;
        case N_BLOCK:
        {
            if (n->ival == 1)       // several declarations on one line: one step for the whole line
            {
                Snap(n->line);
                for (Node *d = n->a; d; d = d->next)
                {
                    curLine = d->line;
                    if (d->ival) { DeclareStatic(d); continue; }
                    Var *v = DeclareVar(d->name, d->type, d->line, false);
                    if (d->a) Initialise(v->addr, v->type, d->a);
                }
                return FLOW_NORMAL;
            }
            int saveVars = varCount, saveSp = sp;
            Flow f = ExecList(n->a);
            varCount = saveVars;
            sp = saveSp;
            return f;
        }
        case N_DECL:
        {
            Snap(n->line);
            curLine = n->line;
            if (n->ival) { DeclareStatic(n); return FLOW_NORMAL; }
            Var *v = DeclareVar(n->name, n->type, n->line, false);
            if (n->a) Initialise(v->addr, v->type, n->a);
            return FLOW_NORMAL;
        }
        case N_EXPR:
            Snap(n->line);
            Eval(n->a);
            return FLOW_NORMAL;
        case N_IF:
            Snap(n->line);
            if (Truthy(Eval(n->a))) return Exec(n->b);
            return Exec(n->c);
        case N_WHILE:
            for (;;)
            {
                Snap(n->line);
                if (!Truthy(Eval(n->a))) return FLOW_NORMAL;
                Flow f = Exec(n->b);
                if (f == FLOW_BREAK) return FLOW_NORMAL;
                if (f == FLOW_RETURN) return f;
            }
        case N_DO:
            for (;;)
            {
                Flow f = Exec(n->b);
                if (f == FLOW_BREAK) return FLOW_NORMAL;
                if (f == FLOW_RETURN) return f;
                Snap(n->a->line);
                if (!Truthy(Eval(n->a))) return FLOW_NORMAL;
            }
        case N_FOR:
        {
            int saveVars = varCount, saveSp = sp;
            Flow result = FLOW_NORMAL;
            if (n->a)
            {
                if (n->a->kind == N_EXPR) { Snap(n->line); Eval(n->a->a); }
                else
                {
                    // Declarations in the for header: run them without their own snapshot line change
                    Node *list = (n->a->kind == N_BLOCK) ? n->a->a : n->a;
                    Snap(n->line);
                    for (Node *d = list; d; d = d->next)
                    {
                        if (d->kind != N_DECL) continue;
                        Var *v = DeclareVar(d->name, d->type, d->line, false);
                        if (d->a) Initialise(v->addr, v->type, d->a);
                    }
                }
            }
            for (;;)
            {
                if (n->b)
                {
                    Snap(n->line);
                    if (!Truthy(Eval(n->b))) break;
                }
                Flow f = Exec(n->d);
                if (f == FLOW_BREAK) break;
                if (f == FLOW_RETURN) { result = f; break; }
                if (n->c) { curLine = n->line; Eval(n->c); }
                if (!n->b) Snap(n->line);
            }
            varCount = saveVars;
            sp = saveSp;
            return result;
        }
        case N_RETURN:
            Snap(n->line);
            returnValue = n->a ? Eval(n->a) : MakeInt(0, &tVoid);
            // Convert to the function's return type
            {
                const char *fname = frames[frameCount - 1].func;
                Func *f = FindFunc(fname);
                if (f && f->ret->kind != CI_VOID)
                {
                    if (!n->a) Fail(n->line, "%s must return a value", fname);
                    if (f->ret->kind == CI_STRUCT) returnValue = Convert(returnValue, f->ret, n->line);
                    else returnValue = Convert(returnValue, f->ret, n->line);
                    // Returning the address of a local variable: warn, it is a classic bug
                    if (f->ret->kind == CI_PTR && returnValue.i >= frames[frameCount - 1].spAtStart && returnValue.i < CI_STACK_END)
                        Warn(n->line, "line %d returns the address of a local variable of %s; it stops existing when %s returns (dangling pointer)", n->line, fname, fname);
                }
                else if (f && n->a) Fail(n->line, "%s is void, so it cannot return a value", fname);
            }
            return FLOW_RETURN;
        case N_BREAK: Snap(n->line); return FLOW_BREAK;
        case N_CONTINUE: Snap(n->line); return FLOW_CONTINUE;
        case N_SWITCH:
        {
            Snap(n->line);
            long value = AsLong(Eval(n->a));
            if (n->b->kind != N_BLOCK) return FLOW_NORMAL;
            Node *start = NULL, *def = NULL;
            for (Node *s = n->b->a; s; s = s->next)
            {
                if (s->kind == N_CASE && s->ival == value && !start) start = s;
                if (s->kind == N_DEFAULT) def = s;
            }
            if (!start) start = def;
            if (!start) return FLOW_NORMAL;
            int saveVars = varCount, saveSp = sp;
            Flow f = FLOW_NORMAL;
            for (Node *s = start; s; s = s->next)
            {
                if (s->kind == N_CASE || s->kind == N_DEFAULT) continue;
                f = Exec(s);
                if (f != FLOW_NORMAL) break;
            }
            varCount = saveVars;
            sp = saveSp;
            if (f == FLOW_BREAK) return FLOW_NORMAL;
            return f;
        }
        case N_CASE: case N_DEFAULT: return FLOW_NORMAL;
        default:
            Fail(n->line, "cannot run this statement");
    }
    return FLOW_NORMAL;
}

//----------------------------------------------------------------------------------
// Public functions
//----------------------------------------------------------------------------------
static void ResetState(void)
{
    toks = NULL; tokCount = 0; tokCap = 0;
    macroCount = 0;
    structCount = 0;
    memset(structs, 0, sizeof(structs));
    memset(structTypes, 0, sizeof(structTypes));
    funcCount = 0;
    memset(funcs, 0, sizeof(funcs));
    typedefCount = 0;
    globalDecls = globalTail = NULL;
    pos = 0;
    memset(mem, 0, sizeof(mem));
    memset(memInit, 0, sizeof(memInit));
    dataTop = CI_DATA_BASE;
    literalStart = literalEnd = 0;
    tempTop = TEMP_BASE;
    varCount = 0; globalVarCount = 0;
    frameCount = 0;
    blockCount = 0;
    heapTop = CI_HEAP_BASE;
    sp = CI_STACK_BASE;
    output = NULL; outputLen = 0; outputCap = 0;
    note[0] = '\0';
    stepCap = 0;
    randState = 1;
    garbageState = 2024;
    curLine = 0;
    running = false;
}

bool CiExecute(const char *source, const char *in, CiRun *r)
{
    memset(r, 0, sizeof(*r));
    run = r;
    arena = NULL;
    ResetState();
    input = in ? in : "";
    inputPos = 0;

    if (setjmp(failJump) != 0)
    {
        if (running)
        {
            // Show the moment of the error as the last step
            snprintf(note, sizeof(note), "stopped here");
            if (run->stepCount < CI_MAX_STEPS + 1 && !run->truncated)
            {
                int saveCount = run->stepCount;
                if (saveCount < CI_MAX_STEPS) Snap(run->errorLine);
            }
        }
        goto done;
    }

    // String literals go after the globals, so lex and parse first with literals at a provisional place
    literalStart = literalEnd = CI_DATA_BASE + 1024;     // globals get the first 1024 bytes of the data region
    Lex(source, 1, false);
    {
        Tok eof = { 0 };
        eof.kind = TK_EOF;
        eof.line = tokCount ? toks[tokCount - 1].line : 1;
        PushTok(eof);
    }
    ParseProgram();

    Func *mainFunc = FindFunc("main");
    if (mainFunc == NULL || mainFunc->body == NULL) Fail(1, "the program needs a main function: int main(void) { ... }");

    running = true;
    for (Node *d = globalDecls; d; d = d->next)
    {
        curLine = d->line;
        Var *v = DeclareVar(d->name, d->type, d->line, true);
        if (dataTop > CI_DATA_BASE + 1024) Fail(d->line, "too many global variables for the Visualizer");
        if (d->a) Initialise(v->addr, v->type, d->a);
    }

    {
        Val args[1];
        CallFunc(mainFunc, args, 0, mainFunc->line);
    }

    // Finished: report memory that was never freed
    for (int i = 0; i < blockCount; i++)
        if (!blocks[i].freeLine) { run->leakedBlocks++; run->leakedBytes += blocks[i].size; }
    snprintf(note, sizeof(note), "%s", run->leakedBlocks ? "main returned, but some memory was never freed" : "main returned");
    if (run->stepCount < CI_MAX_STEPS) Snap(0);

done:
    running = false;
    run->output = output ? output : Dup("", 0);
    run->outputLen = outputLen;
    run->structs = Alloc(sizeof(CiStruct)*(size_t)(structCount ? structCount : 1));
    memcpy(run->structs, structs, sizeof(CiStruct)*(size_t)structCount);
    run->structCount = structCount;
    run->arena = arena;
    arena = NULL;
    return !run->compileError;
}

void CiFree(CiRun *r)
{
    Chunk *c = r->arena;
    while (c) { Chunk *next = c->next; free(c); c = next; }
    memset(r, 0, sizeof(*r));
}

int CiRegion(int addr)
{
    if (addr >= CI_STACK_BASE && addr < CI_STACK_END) return 1;
    if (addr >= CI_HEAP_BASE && addr < CI_HEAP_END) return 2;
    if (addr >= CI_DATA_BASE && addr < CI_MEM_SIZE) return 3;
    return 0;
}

bool CiSnapRead(const CiSnapshot *s, int addr, int size, unsigned char *out, bool *initialised)
{
    const unsigned char *src = NULL, *init = NULL;
    int base = 0, used = 0;
    switch (CiRegion(addr))
    {
        case 1: src = s->stack; init = s->stackInit; base = CI_STACK_BASE; used = s->stackUsed; break;
        case 2: src = s->heap; init = s->heapInit; base = CI_HEAP_BASE; used = s->heapUsed; break;
        case 3: src = s->data; init = s->dataInit; base = CI_DATA_BASE; used = s->dataUsed; break;
        default: return false;
    }
    if (addr - base + size > used) return false;
    memcpy(out, src + (addr - base), (size_t)size);
    bool all = true;
    for (int i = 0; i < size; i++) if (!init[addr - base + i]) all = false;
    if (initialised) *initialised = all;
    return true;
}

void CiFormatValue(const CiRun *r, const CiSnapshot *s, int addr, const CiType *t, char *out, int cap)
{
    (void)r;
    unsigned char b[8] = { 0 };
    bool init = true;
    if (t->kind == CI_ARRAY || t->kind == CI_STRUCT || t->size > 8) { snprintf(out, (size_t)cap, "..."); return; }
    if (!CiSnapRead(s, addr, t->size, b, &init)) { snprintf(out, (size_t)cap, "-"); return; }
    if (!init) { snprintf(out, (size_t)cap, "?"); return; }
    switch (t->kind)
    {
        case CI_BOOL: snprintf(out, (size_t)cap, "%s", b[0] ? "true" : "false"); break;
        case CI_CHAR:
        {
            signed char c = (signed char)b[0];
            if (c == 0) snprintf(out, (size_t)cap, "'\\0'");
            else if (c == '\n') snprintf(out, (size_t)cap, "'\\n'");
            else if (c >= 32 && c < 127) snprintf(out, (size_t)cap, "'%c'", c);
            else snprintf(out, (size_t)cap, "%d", c);
        } break;
        case CI_INT: { int32_t x; memcpy(&x, b, 4); snprintf(out, (size_t)cap, "%d", x); } break;
        case CI_LONG: { int64_t x; memcpy(&x, b, 8); snprintf(out, (size_t)cap, "%lld", (long long)x); } break;
        case CI_FLOAT: { float x; memcpy(&x, b, 4); snprintf(out, (size_t)cap, "%g", x); } break;
        case CI_DOUBLE: { double x; memcpy(&x, b, 8); snprintf(out, (size_t)cap, "%g", x); } break;
        case CI_PTR: { int64_t x; memcpy(&x, b, 8); if (x == 0) snprintf(out, (size_t)cap, "NULL"); else snprintf(out, (size_t)cap, "%lld", (long long)x); } break;
        default: snprintf(out, (size_t)cap, "?"); break;
    }
}
