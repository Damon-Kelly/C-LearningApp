/**********************************************************************************************
*
*   C Learning App - coding challenges (see challenge.h)
*
**********************************************************************************************/

#include "challenge.h"
#include "cinterp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//----------------------------------------------------------------------------------
// Loading
//----------------------------------------------------------------------------------
static void Trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
    size_t k = 0;
    while (s[k] == ' ' || s[k] == '\t') k++;
    if (k) memmove(s, s + k, strlen(s + k) + 1);
}

// Reads lines until one that starts with one of the stop words; returns them joined with '\n'
static char *ReadBlock(char **lines, int lineCount, int *i, const char *stop1, const char *stop2)
{
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) return NULL;
    buf[0] = '\0';
    for (; *i < lineCount; (*i)++)
    {
        const char *l = lines[*i];
        if (strncmp(l, stop1, strlen(stop1)) == 0 || (stop2 && strncmp(l, stop2, strlen(stop2)) == 0)) break;
        size_t n = strlen(l);
        if (len + n + 2 > cap) { while (len + n + 2 > cap) cap *= 2; char *nb = realloc(buf, cap); if (!nb) break; buf = nb; }
        memcpy(buf + len, l, n);
        len += n;
        buf[len++] = '\n';
        buf[len] = '\0';
    }
    while (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    return buf;
}

void UnloadChallenges(ChallengeSet *set)
{
    for (int i = 0; i < set->count; i++)
    {
        Challenge *c = &set->items[i];
        free(c->text); free(c->starter); free(c->solution); free(c->helpers);
        for (int t = 0; t < c->testCount; t++) { free(c->tests[t].code); free(c->tests[t].expect); }
    }
    memset(set, 0, sizeof(*set));
}

bool LoadChallenges(const char *path, ChallengeSet *set, char *err, int errSize)
{
    memset(set, 0, sizeof(*set));
    FILE *f = fopen(path, "rb");
    if (f == NULL) { snprintf(err, (size_t)errSize, "cannot open %s", path); return false; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc((size_t)size + 1);
    if (data == NULL) { fclose(f); snprintf(err, (size_t)errSize, "out of memory"); return false; }
    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);
    data[got] = '\0';

    // Split into lines (in place)
    int lineCount = 0, lineCap = 256;
    char **lines = malloc(sizeof(char *)*(size_t)lineCap);
    for (char *p = data; p && *p; )
    {
        if (lineCount == lineCap) { lineCap *= 2; lines = realloc(lines, sizeof(char *)*(size_t)lineCap); }
        lines[lineCount++] = p;
        char *nl = strchr(p, '\n');
        if (nl) { *nl = '\0'; if (nl > p && nl[-1] == '\r') nl[-1] = '\0'; p = nl + 1; } else p = NULL;
    }

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    bool ok = true;
    Challenge *c = NULL;

    #define FAIL(...) do { int k_ = snprintf(err, (size_t)errSize, "%s:%d: ", name, i + 1); snprintf(err + k_, (size_t)(errSize - k_), __VA_ARGS__); ok = false; goto done; } while (0)

    for (int i = 0; i < lineCount; i++)
    {
        char *l = lines[i];
        if (l[0] == '#' || l[0] == '\0') continue;
        if (l[0] != '@') FAIL("text outside a block (missing @text?)");

        if (strncmp(l, "@challenge ", 11) == 0)
        {
            if (set->count >= MAX_CHALLENGES) FAIL("too many challenges (at most %d per file)", MAX_CHALLENGES);
            c = &set->items[set->count++];
            snprintf(c->title, sizeof(c->title), "%s", l + 11);
            Trim(c->title);
            c->level = 1;
            c->line = i + 1;
            continue;
        }
        if (c == NULL) FAIL("@challenge must come first");

        if (strncmp(l, "@level", 6) == 0) { c->level = atoi(l + 6); if (c->level < 1 || c->level > 3) FAIL("@level is 1, 2 or 3"); }
        else if (strncmp(l, "@hint ", 6) == 0) { snprintf(c->hint, sizeof(c->hint), "%s", l + 6); Trim(c->hint); }
        else if (strcmp(l, "@noleaks") == 0) c->noLeaks = true;
        else if (strncmp(l, "@forbid ", 8) == 0)
        {
            if (c->forbidCount >= MAX_FORBID) FAIL("too many @forbid lines");
            char *bar = strchr(l + 8, '|');
            if (bar == NULL) FAIL("@forbid text | reason");
            *bar = '\0';
            snprintf(c->forbid[c->forbidCount], sizeof(c->forbid[0]), "%s", l + 8);
            Trim(c->forbid[c->forbidCount]);
            snprintf(c->forbidWhy[c->forbidCount], sizeof(c->forbidWhy[0]), "%s", bar + 1);
            Trim(c->forbidWhy[c->forbidCount]);
            c->forbidCount++;
        }
        else if (strcmp(l, "@text") == 0 || strcmp(l, "@starter") == 0 || strcmp(l, "@solution") == 0 || strcmp(l, "@helpers") == 0)
        {
            char kind = l[1];
            i++;
            char *block = ReadBlock(lines, lineCount, &i, "@end", NULL);
            if (i >= lineCount) FAIL("missing @end");
            if (kind == 't') c->text = block;
            else if (kind == 's' && l[2] == 't') c->starter = block;
            else if (kind == 's') c->solution = block;
            else c->helpers = block;
        }
        else if (strncmp(l, "@test", 5) == 0 || strncmp(l, "@hidden", 7) == 0)
        {
            if (c->testCount >= MAX_CHALLENGE_TESTS) FAIL("too many tests (at most %d)", MAX_CHALLENGE_TESTS);
            ChallengeTest *t = &c->tests[c->testCount++];
            t->hidden = (l[1] == 'h');
            snprintf(t->name, sizeof(t->name), "%s", l + (t->hidden ? 7 : 5));
            Trim(t->name);
            t->line = i + 1;
            i++;
            t->code = ReadBlock(lines, lineCount, &i, "@expect", "@end");
            if (i >= lineCount || strcmp(lines[i], "@expect") != 0) FAIL("test \"%s\" needs @expect and then @end", t->name);
            i++;
            t->expect = ReadBlock(lines, lineCount, &i, "@end", NULL);
            if (i >= lineCount) FAIL("missing @end after @expect");
        }
        else FAIL("unknown directive %s", l);
    }

    for (int k = 0; k < set->count; k++)
    {
        Challenge *ch = &set->items[k];
        int i = ch->line - 1;
        if (!ch->text || !ch->starter || !ch->solution) FAIL("challenge \"%s\" needs @text, @starter and @solution", ch->title);
        if (ch->testCount == 0) FAIL("challenge \"%s\" has no tests", ch->title);
    }

done:
    #undef FAIL
    free(lines);
    free(data);
    if (!ok) UnloadChallenges(set);
    return ok;
}

//----------------------------------------------------------------------------------
// Grading
//----------------------------------------------------------------------------------
static int CountLines(const char *s)
{
    int n = 1;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}

int ChallengeProgram(const Challenge *c, const char *code, int test, char *out, int cap)
{
    // The student's code comes first, so error line numbers match what they see in the editor
    int n = snprintf(out, (size_t)cap, "%s\n", code);
    int userLines = CountLines(code);
    if (c->helpers && n < cap) n += snprintf(out + n, (size_t)(cap - n), "\n%s\n", c->helpers);
    if (n < cap) n += snprintf(out + n, (size_t)(cap - n), "\nint main(void) {\n");
    const char *p = c->tests[test].code;
    while (*p && n < cap - 40)
    {
        n += snprintf(out + n, (size_t)(cap - n), "    ");
        while (*p && *p != '\n' && n < cap - 40) out[n++] = *p++;
        out[n++] = '\n';
        if (*p == '\n') p++;
    }
    snprintf(out + n, (size_t)(cap - n), "    return 0;\n}\n");
    return userLines;
}

// Copy of the code with comments and the contents of strings/chars blanked, for @forbid
static void CodeOnly(const char *src, char *out, int cap)
{
    int n = 0;
    for (const char *p = src; *p && n < cap - 1; )
    {
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') { p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++; if (*p) p += 2; continue; }
        if (*p == '"' || *p == '\'')
        {
            char q = *p++;
            out[n++] = q;
            while (*p && *p != q && *p != '\n') { if (*p == '\\' && p[1]) p++; p++; }
            if (*p == q) { out[n++] = q; p++; }
            continue;
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
}

// Output comparison that ignores spaces at line ends and blank lines at the end
static void Normalize(const char *s, char *out, int cap)
{
    int n = 0;
    for (const char *p = s; *p && n < cap - 1; p++)
    {
        if (*p == '\r') continue;
        if (*p == '\n') { while (n > 0 && out[n - 1] == ' ') n--; }
        out[n++] = *p;
    }
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' ')) n--;
    out[n] = '\0';
}

void GradeChallenge(const Challenge *c, const char *code, GradeResult *g)
{
    memset(g, 0, sizeof(*g));

    // Rules first
    static char stripped[16384];
    CodeOnly(code, stripped, sizeof(stripped));
    for (int i = 0; i < c->forbidCount; i++)
    {
        if (strstr(stripped, c->forbid[i]))
        {
            g->ruleBroken = true;
            snprintf(g->message, sizeof(g->message), "Your code uses \"%s\". %s", c->forbid[i], c->forbidWhy[i]);
            for (int t = 0; t < c->testCount; t++) snprintf(g->tests[t].message, sizeof(g->tests[t].message), "not run: %s", c->forbidWhy[i]);
            return;
        }
    }

    static char program[20000];
    for (int t = 0; t < c->testCount; t++)
    {
        TestResult *r = &g->tests[t];
        int userLines = ChallengeProgram(c, code, t, program, sizeof(program));
        CiRun run;
        CiExecute(program, "", &run);

        if (run.compileError)
        {
            // The same compile error would repeat for every test: report it once
            g->compileError = true;
            if (run.errorLine <= userLines)
            {
                g->errorLine = run.errorLine;
                snprintf(g->message, sizeof(g->message), "Line %d of your code: %s", run.errorLine, run.error);
            }
            else snprintf(g->message, sizeof(g->message), "Your code does not work with the tests: %s. Check the function's name, return type and parameters against the task.", run.error);
            for (int k = 0; k < c->testCount; k++) snprintf(g->tests[k].message, sizeof(g->tests[k].message), "not run: the code does not compile");
            CiFree(&run);
            return;
        }

        snprintf(r->got, sizeof(r->got), "%s", run.output ? run.output : "");
        if (run.runtimeError)
        {
            char where[64];
            if (run.errorLine > 0 && run.errorLine <= userLines) snprintf(where, sizeof(where), "line %d of your code", run.errorLine);
            else snprintf(where, sizeof(where), "while the test was using your function");
            snprintf(r->message, sizeof(r->message), "Stopped (%s): %s", where, run.error);
        }
        else
        {
            char want[2048], got[2048];
            Normalize(c->tests[t].expect, want, sizeof(want));
            Normalize(run.output, got, sizeof(got));
            if (strcmp(want, got) != 0)
            {
                // First line that differs, for the Expected / Your output view
                int line = 1;
                const char *a = want, *b = got;
                while (*a && *a == *b) { if (*a == '\n') line++; a++; b++; }
                r->wrongOutput = true;
                r->diffLine = line;
                if (c->tests[t].hidden) snprintf(r->message, sizeof(r->message), "Wrong result (this is a hidden test, so the details are not shown)");
                else if (got[0] == '\0') snprintf(r->message, sizeof(r->message), "Your code printed nothing.");
                else snprintf(r->message, sizeof(r->message), "The output is different from line %d.", line);
            }
            else if (c->noLeaks && run.leakedBlocks > 0)
                snprintf(r->message, sizeof(r->message), "Right result, but %d block%s (%d bytes) from malloc %s never freed: a memory leak", run.leakedBlocks, run.leakedBlocks == 1 ? "" : "s", run.leakedBytes, run.leakedBlocks == 1 ? "was" : "were");
            else r->passed = true;
        }
        if (r->passed) g->passedCount++;
        CiFree(&run);
    }
}

const char *ChallengeKey(const char *file, int index)
{
    static char key[96];
    snprintf(key, sizeof(key), "%s#%d", file, index + 1);
    return key;
}
