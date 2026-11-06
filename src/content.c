#include "content.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//----------------------------------------------------------------------------------
// Section list state
//----------------------------------------------------------------------------------
static SectionDef sections[MAX_SECTIONS];
static int sectionCount = 0;
static char contentError[256] = "";

int GetSectionCount(void) { return sectionCount; }
const char *GetContentError(void) { return contentError; }

const SectionDef *GetSection(int index)
{
    if (index < 0 || index >= sectionCount) return NULL;
    return &sections[index];
}

//----------------------------------------------------------------------------------
// Small helpers
//----------------------------------------------------------------------------------
static char *ReadWholeFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }

    char *data = malloc((size_t)size + 1);
    if (data == NULL) { fclose(f); return NULL; }

    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);
    data[got] = '\0';
    return data;
}

static char *DupString(const char *s)
{
    size_t n = strlen(s);
    char *copy = malloc(n + 1);
    if (copy != NULL) memcpy(copy, s, n + 1);
    return copy;
}

static void CopyString(char *dst, int dstSize, const char *src)
{
    snprintf(dst, (size_t)dstSize, "%s", src);
}

// Trims both ends in place and returns the start of the trimmed text
static char *Trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

// Copies the next line (without \r\n) into line. Returns the start of the following
// line, or NULL when this was the last one.
static const char *NextLine(const char *p, char *line, int size, bool *tooLong)
{
    int n = 0;
    *tooLong = false;

    while (*p != '\0' && *p != '\n')
    {
        if (n < size - 1) line[n++] = *p;
        else *tooLong = true;
        p++;
    }
    while (n > 0 && line[n - 1] == '\r') n--;
    line[n] = '\0';

    return (*p == '\n') ? p + 1 : NULL;
}

static const char *BaseName(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// Growable string buffer
typedef struct Buf { char *data; int len; int cap; } Buf;

static void BufReset(Buf *b)
{
    b->len = 0;
    if (b->data != NULL) b->data[0] = '\0';
}

static void BufAppend(Buf *b, const char *s, int n)
{
    if (b->len + n + 1 > b->cap)
    {
        int newCap = (b->cap == 0) ? 256 : b->cap;
        while (newCap < b->len + n + 1) newCap *= 2;

        char *grown = realloc(b->data, (size_t)newCap);
        if (grown == NULL) return;
        b->data = grown;
        b->cap = newCap;
    }

    memcpy(b->data + b->len, s, (size_t)n);
    b->len += n;
    b->data[b->len] = '\0';
}

// Returns a malloc'd copy of the buffer without trailing blank space
static char *TakeBuf(Buf *b)
{
    while (b->len > 0 && isspace((unsigned char)b->data[b->len - 1])) b->data[--b->len] = '\0';
    return DupString(b->data != NULL ? b->data : "");
}

static int CountLines(const char *text)
{
    if (text == NULL || text[0] == '\0') return 0;
    int lines = 1;
    for (const char *c = text; *c != '\0'; c++) if (*c == '\n') lines++;
    return lines;
}

// Splits "a | b | c" in place on '|' and trims each field
static int SplitFields(char *s, char **fields, int max)
{
    int n = 0;
    fields[n++] = s;
    for (char *c = s; *c != '\0' && n < max; c++)
    {
        if (*c == '|') { *c = '\0'; fields[n++] = c + 1; }
    }
    for (int i = 0; i < n; i++) fields[i] = Trim(fields[i]);
    return n;
}

//----------------------------------------------------------------------------------
// Section list parser
//----------------------------------------------------------------------------------
bool LoadManifest(const char *path)
{
    sectionCount = 0;
    contentError[0] = '\0';
    memset(sections, 0, sizeof(sections));

    char *text = ReadWholeFile(path);
    if (text == NULL)
    {
        snprintf(contentError, sizeof(contentError), "Could not open %s", path);
        return false;
    }

    const char *file = BaseName(path);
    const char *p = text;
    int lineNo = 0;
    bool ok = true;

    while (p != NULL && ok)
    {
        char line[512];
        bool tooLong;
        p = NextLine(p, line, sizeof(line), &tooLong);
        lineNo++;

        char *s = Trim(line);
        if (s[0] == '\0' || s[0] == '#') continue;

        if (s[0] != '@')
        {
            snprintf(contentError, sizeof(contentError), "%s:%d: expected a line starting with @section or @topic", file, lineNo);
            ok = false;
            break;
        }

        char *rest = s + 1;
        while (*rest != '\0' && !isspace((unsigned char)*rest)) rest++;
        bool hasRest = (*rest != '\0');
        if (hasRest) *rest++ = '\0';
        const char *keyword = s + 1;

        char *fields[3];
        int fieldCount = SplitFields(rest, fields, 3);

        if (strcmp(keyword, "section") == 0)
        {
            if (!hasRest || fields[0][0] == '\0') { snprintf(contentError, sizeof(contentError), "%s:%d: @section needs a name", file, lineNo); ok = false; break; }
            if (sectionCount >= MAX_SECTIONS) { snprintf(contentError, sizeof(contentError), "%s:%d: too many sections (max %d)", file, lineNo, MAX_SECTIONS); ok = false; break; }

            SectionDef *sec = &sections[sectionCount++];
            CopyString(sec->name, sizeof(sec->name), fields[0]);
            if (fieldCount > 1) CopyString(sec->description, sizeof(sec->description), fields[1]);
        }
        else if (strcmp(keyword, "topic") == 0)
        {
            if (sectionCount == 0) { snprintf(contentError, sizeof(contentError), "%s:%d: @topic must come after a @section", file, lineNo); ok = false; break; }
            SectionDef *sec = &sections[sectionCount - 1];
            if (sec->topicCount >= MAX_TOPICS_PER_SECTION) { snprintf(contentError, sizeof(contentError), "%s:%d: too many topics in one section (max %d)", file, lineNo, MAX_TOPICS_PER_SECTION); ok = false; break; }
            if (fieldCount < 2 || fields[0][0] == '\0' || fields[1][0] == '\0') { snprintf(contentError, sizeof(contentError), "%s:%d: @topic needs: file | title | summary", file, lineNo); ok = false; break; }

            TopicRef *topic = &sec->topics[sec->topicCount++];
            CopyString(topic->file, sizeof(topic->file), fields[0]);
            CopyString(topic->title, sizeof(topic->title), fields[1]);
            if (fieldCount > 2) CopyString(topic->summary, sizeof(topic->summary), fields[2]);
        }
        else
        {
            snprintf(contentError, sizeof(contentError), "%s:%d: unknown directive @%s", file, lineNo, keyword);
            ok = false;
        }
    }

    free(text);
    return ok;
}

//----------------------------------------------------------------------------------
// Lesson parser
//----------------------------------------------------------------------------------
typedef enum Mode { MODE_NONE, MODE_TEXT, MODE_CODE, MODE_OUTPUT } Mode;

typedef struct Parser {
    Lesson *lesson;
    Page *page;
    Item *codeItem;         // the code item being filled while in MODE_CODE / MODE_OUTPUT
    Question *question;
    Step *step;
    Mode mode;
    Buf buf;
    const char *file;
    int lineNo;             // 0 while validating
    char *err;
    int errSize;
    bool failed;
} Parser;

static void Fail(Parser *p, const char *fmt, ...)
{
    if (p->failed) return;
    p->failed = true;

    char msg[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (p->lineNo > 0) snprintf(p->err, (size_t)p->errSize, "%s:%d: %s", p->file, p->lineNo, msg);
    else snprintf(p->err, (size_t)p->errSize, "%s: %s", p->file, msg);
}

// Prose: single line breaks join into one paragraph, a blank line starts a new paragraph,
// and lines beginning with "- " stay on their own line.
static void AddTextLine(Buf *b, const char *line)
{
    const char *t = line;
    while (*t == ' ' || *t == '\t') t++;

    if (*t == '\0')
    {
        if (b->len > 0 && b->data[b->len - 1] != '\n') BufAppend(b, "\n\n", 2);
        return;
    }

    if (b->len > 0 && b->data[b->len - 1] != '\n')
    {
        if (t[0] == '-' && t[1] == ' ') BufAppend(b, "\n", 1);
        else BufAppend(b, " ", 1);
    }
    BufAppend(b, t, (int)strlen(t));
}

static Item *NewItem(Parser *p, ItemKind kind)
{
    if (p->page->itemCount >= MAX_ITEMS)
    {
        Fail(p, "too many text/code blocks on one page (max %d)", MAX_ITEMS);
        return NULL;
    }
    Item *item = &p->page->items[p->page->itemCount++];
    item->kind = kind;
    return item;
}

static void EndBlock(Parser *p)
{
    switch (p->mode)
    {
        case MODE_TEXT:
        {
            Item *item = NewItem(p, ITEM_TEXT);
            if (item != NULL) item->text = TakeBuf(&p->buf);
        } break;
        case MODE_CODE:
        {
            p->codeItem->text = TakeBuf(&p->buf);
        } break;
        case MODE_OUTPUT:
        {
            p->codeItem->output = TakeBuf(&p->buf);
        } break;
        default: break;
    }

    p->mode = MODE_NONE;
    BufReset(&p->buf);
}

static bool NeedPage(Parser *p, PageType type, const char *directive)
{
    if (p->page == NULL) { Fail(p, "@%s must come after a @page", directive); return false; }
    if (p->page->type != type)
    {
        static const char *names[] = { "explain", "trace", "predict", "quiz" };
        Fail(p, "@%s can only be used on a %s page", directive, names[type]);
        return false;
    }
    return true;
}

static void HandleDirective(Parser *p, char *line)
{
    char *rest = line + 1;
    while (*rest != '\0' && !isspace((unsigned char)*rest)) rest++;
    if (*rest != '\0') *rest++ = '\0';
    rest = Trim(rest);
    const char *kw = line + 1;

    // Inside a block only @end (and @output inside code) are allowed
    if (p->mode != MODE_NONE)
    {
        if (strcmp(kw, "end") == 0) { EndBlock(p); return; }
        if (strcmp(kw, "output") == 0 && p->mode == MODE_CODE)
        {
            p->codeItem->text = TakeBuf(&p->buf);
            BufReset(&p->buf);
            p->mode = MODE_OUTPUT;
            return;
        }
        Fail(p, "unexpected @%s inside a block (missing @end?)", kw);
        return;
    }

    Lesson *lesson = p->lesson;

    if (strcmp(kw, "topic") == 0) CopyString(lesson->title, sizeof(lesson->title), rest);
    else if (strcmp(kw, "summary") == 0) CopyString(lesson->summary, sizeof(lesson->summary), rest);
    else if (strcmp(kw, "page") == 0)
    {
        if (lesson->pageCount >= MAX_PAGES) { Fail(p, "too many pages (max %d)", MAX_PAGES); return; }

        char *title = rest;
        while (*title != '\0' && !isspace((unsigned char)*title)) title++;
        if (*title != '\0') *title++ = '\0';
        title = Trim(title);

        PageType type;
        if (strcmp(rest, "explain") == 0) type = PAGE_EXPLAIN;
        else if (strcmp(rest, "trace") == 0) type = PAGE_TRACE;
        else if (strcmp(rest, "predict") == 0) type = PAGE_PREDICT;
        else if (strcmp(rest, "quiz") == 0) type = PAGE_QUIZ;
        else { Fail(p, "unknown page type '%s' (use explain, trace, predict or quiz)", rest); return; }

        p->page = &lesson->pages[lesson->pageCount++];
        p->page->type = type;
        CopyString(p->page->title, sizeof(p->page->title), title);
        p->question = NULL;
        p->step = NULL;
    }
    else if (strcmp(kw, "text") == 0)
    {
        if (p->page == NULL) { Fail(p, "@text must come after a @page"); return; }
        if (p->page->type == PAGE_QUIZ || p->page->type == PAGE_TRACE) { Fail(p, "@text is not used on %s pages", p->page->type == PAGE_QUIZ ? "quiz" : "trace"); return; }
        p->mode = MODE_TEXT;
        BufReset(&p->buf);
    }
    else if (strcmp(kw, "code") == 0)
    {
        if (p->page == NULL) { Fail(p, "@code must come after a @page"); return; }
        if (p->page->type == PAGE_QUIZ) { Fail(p, "@code is not supported on quiz pages"); return; }

        Item *item = NewItem(p, ITEM_CODE);
        if (item == NULL) return;
        CopyString(item->lang, sizeof(item->lang), rest[0] != '\0' ? rest : "c");
        p->codeItem = item;
        p->mode = MODE_CODE;
        BufReset(&p->buf);
    }
    else if (strcmp(kw, "step") == 0)
    {
        if (!NeedPage(p, PAGE_TRACE, "step")) return;
        if (p->page->stepCount >= MAX_STEPS) { Fail(p, "too many steps (max %d)", MAX_STEPS); return; }
        p->step = &p->page->steps[p->page->stepCount++];
        p->step->line = atoi(rest);
    }
    else if (strcmp(kw, "cell") == 0)
    {
        if (p->step == NULL) { Fail(p, "@cell must come after a @step"); return; }
        if (p->step->cellCount >= MAX_CELLS) { Fail(p, "too many cells in one step (max %d)", MAX_CELLS); return; }

        Cell *cell = &p->step->cells[p->step->cellCount];
        if (sscanf(rest, "%23s %23s %23s %d", cell->name, cell->type, cell->value, &cell->addr) != 4)
        {
            Fail(p, "@cell needs: name type value address   (example: @cell a int 5 1000)");
            return;
        }
        p->step->cellCount++;
    }
    else if (strcmp(kw, "note") == 0)
    {
        if (p->step == NULL) { Fail(p, "@note must come after a @step"); return; }
        CopyString(p->step->note, sizeof(p->step->note), rest);
    }
    else if (strcmp(kw, "option") == 0)
    {
        if (!NeedPage(p, PAGE_PREDICT, "option")) return;
        if (p->page->optionCount >= MAX_OPTIONS) { Fail(p, "too many options (max %d)", MAX_OPTIONS); return; }
        p->page->options[p->page->optionCount++] = DupString(rest);
    }
    else if (strcmp(kw, "answer") == 0)
    {
        if (!NeedPage(p, PAGE_PREDICT, "answer")) return;
        p->page->correct = atoi(rest) - 1;
    }
    else if (strcmp(kw, "why") == 0)
    {
        if (p->page != NULL && p->page->type == PAGE_PREDICT) p->page->why = DupString(rest);
        else if (p->page != NULL && p->page->type == PAGE_QUIZ && p->question != NULL) p->question->why = DupString(rest);
        else Fail(p, "@why must come after an @answer (predict page) or a @q (quiz page)");
    }
    else if (strcmp(kw, "q") == 0)
    {
        if (!NeedPage(p, PAGE_QUIZ, "q")) return;
        if (p->page->questionCount >= MAX_QUESTIONS) { Fail(p, "too many questions (max %d)", MAX_QUESTIONS); return; }
        p->question = &p->page->questions[p->page->questionCount++];
        p->question->prompt = DupString(rest);
    }
    else if (strcmp(kw, "opt") == 0)
    {
        if (p->question == NULL) { Fail(p, "@opt must come after a @q"); return; }
        if (p->question->optionCount >= MAX_OPTIONS) { Fail(p, "too many options (max %d)", MAX_OPTIONS); return; }
        p->question->options[p->question->optionCount++] = DupString(rest);
    }
    else if (strcmp(kw, "correct") == 0)
    {
        if (p->question == NULL) { Fail(p, "@correct must come after a @q"); return; }
        p->question->correct = atoi(rest) - 1;
    }
    else if (strcmp(kw, "end") == 0) Fail(p, "@end without a matching @text or @code");
    else Fail(p, "unknown directive @%s", kw);
}

static const Item *FirstCodeItem(const Page *page)
{
    for (int i = 0; i < page->itemCount; i++) if (page->items[i].kind == ITEM_CODE) return &page->items[i];
    return NULL;
}

static int FindCellByName(const Step *step, const char *name)
{
    for (int i = 0; i < step->cellCount; i++) if (strcmp(step->cells[i].name, name) == 0) return i;
    return -1;
}

// Checks the finished lesson so authoring mistakes show up as messages, not odd screens
static void Validate(Parser *p)
{
    p->lineNo = 0;
    Lesson *lesson = p->lesson;

    if (lesson->title[0] == '\0') { Fail(p, "missing @topic title"); return; }
    if (lesson->pageCount == 0) { Fail(p, "the lesson has no pages"); return; }

    for (int i = 0; i < lesson->pageCount && !p->failed; i++)
    {
        const Page *page = &lesson->pages[i];
        const Item *code = FirstCodeItem(page);

        switch (page->type)
        {
            case PAGE_EXPLAIN:
            {
                if (page->itemCount == 0) Fail(p, "page %d (explain) is empty", i + 1);
            } break;
            case PAGE_TRACE:
            {
                if (code == NULL) { Fail(p, "page %d (trace) needs a @code block", i + 1); break; }
                int lines = CountLines(code->text);
                if (lines > MAX_TRACE_CODE_LINES) { Fail(p, "page %d (trace) code has %d lines (max %d)", i + 1, lines, MAX_TRACE_CODE_LINES); break; }
                if (page->stepCount == 0) { Fail(p, "page %d (trace) has no @step", i + 1); break; }

                for (int s = 0; s < page->stepCount && !p->failed; s++)
                {
                    const Step *step = &page->steps[s];
                    if (step->line < 0 || step->line > lines) { Fail(p, "page %d, step %d: line %d is outside the code (1-%d)", i + 1, s + 1, step->line, lines); break; }

                    for (int c = 0; c < step->cellCount; c++)
                    {
                        const char *value = step->cells[c].value;
                        if (value[0] == '-' && value[1] == '>' && FindCellByName(step, value + 2) < 0)
                        {
                            Fail(p, "page %d, step %d: pointer %s points at '%s', which is not a cell in that step", i + 1, s + 1, step->cells[c].name, value + 2);
                            break;
                        }
                    }
                }
            } break;
            case PAGE_PREDICT:
            {
                if (code == NULL) { Fail(p, "page %d (predict) needs a @code block", i + 1); break; }
                if (CountLines(code->text) > MAX_PREDICT_CODE_LINES) { Fail(p, "page %d (predict) code is longer than %d lines", i + 1, MAX_PREDICT_CODE_LINES); break; }
                if (code->output == NULL) { Fail(p, "page %d (predict) code needs an @output block (run tools/verify_content.py --fix)", i + 1); break; }
                if (page->optionCount < 2) { Fail(p, "page %d (predict) needs at least 2 @option lines", i + 1); break; }
                if (page->correct < 0 || page->correct >= page->optionCount) Fail(p, "page %d (predict) has a missing or invalid @answer", i + 1);
            } break;
            case PAGE_QUIZ:
            {
                if (page->questionCount == 0) { Fail(p, "page %d (quiz) has no @q questions", i + 1); break; }
                for (int q = 0; q < page->questionCount && !p->failed; q++)
                {
                    const Question *question = &page->questions[q];
                    if (question->optionCount < 2) Fail(p, "page %d, question %d needs at least 2 @opt lines", i + 1, q + 1);
                    else if (question->correct < 0 || question->correct >= question->optionCount) Fail(p, "page %d, question %d has a missing or invalid @correct", i + 1, q + 1);
                }
            } break;
        }
    }
}

bool LoadLesson(const char *path, Lesson *lesson, char *err, int errSize)
{
    err[0] = '\0';

    char *text = ReadWholeFile(path);
    if (text == NULL)
    {
        snprintf(err, (size_t)errSize, "Could not open %s", path);
        return false;
    }

    Parser parser = { 0 };
    parser.lesson = lesson;
    parser.file = BaseName(path);
    parser.err = err;
    parser.errSize = errSize;

    const char *p = text;
    while (p != NULL && !parser.failed)
    {
        char line[512];
        bool tooLong;
        p = NextLine(p, line, sizeof(line), &tooLong);
        parser.lineNo++;

        if (tooLong) { Fail(&parser, "line is longer than %d characters", (int)sizeof(line) - 1); break; }

        if (parser.mode == MODE_CODE || parser.mode == MODE_OUTPUT)
        {
            if (line[0] == '@') HandleDirective(&parser, line);
            else
            {
                BufAppend(&parser.buf, line, (int)strlen(line));
                BufAppend(&parser.buf, "\n", 1);
            }
            continue;
        }

        char *s = line;
        while (*s == ' ' || *s == '\t') s++;

        if (parser.mode == MODE_TEXT)
        {
            if (s[0] == '@') HandleDirective(&parser, s);
            else AddTextLine(&parser.buf, s);
            continue;
        }

        if (s[0] == '\0' || s[0] == '#') continue;

        if (s[0] != '@') { Fail(&parser, "expected a line starting with @ (found text outside a block)"); break; }
        HandleDirective(&parser, s);
    }

    if (!parser.failed && parser.mode != MODE_NONE) { Fail(&parser, "a @text or @code block was never closed with @end"); }
    if (!parser.failed) Validate(&parser);

    free(parser.buf.data);
    free(text);
    return !parser.failed;
}

void UnloadLesson(Lesson *lesson)
{
    for (int i = 0; i < lesson->pageCount; i++)
    {
        Page *page = &lesson->pages[i];

        for (int j = 0; j < page->itemCount; j++)
        {
            free(page->items[j].text);
            free(page->items[j].output);
        }
        for (int j = 0; j < page->optionCount; j++) free(page->options[j]);
        free(page->why);

        for (int j = 0; j < page->questionCount; j++)
        {
            Question *q = &page->questions[j];
            free(q->prompt);
            for (int k = 0; k < q->optionCount; k++) free(q->options[k]);
            free(q->why);
        }
    }

    memset(lesson, 0, sizeof(*lesson));
}
