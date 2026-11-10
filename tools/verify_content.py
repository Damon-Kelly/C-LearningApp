#!/usr/bin/env python3
"""Check (and optionally fix) the code examples in the lesson files.

Every @code block is compiled. If the block has an @output section, the program is also
run and its real output is compared with what the file says.

  python3 tools/verify_content.py              check every lesson in src/resources/content
  python3 tools/verify_content.py FILE...      check specific files
  python3 tools/verify_content.py --fix        rewrite wrong @output blocks with the real output

Snippets without a main() are wrapped in one automatically, so lessons can show just the
interesting lines. Needs gcc (for "c" blocks) and g++ (for "cpp" blocks).

Exam files (exam_*.txt) are checked too:
  - every @accept of a @typed question is put in place of ____ in the code, then compiled and
    run with the @test block, and the output must equal @expect
  - a multiple-choice question that shows code is run, and its @correct option must be the
    only option equal to the real output
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile

C_WRAPPER = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdbool.h>\nint main(void) {\n%s\n    return 0;\n}\n"
CPP_WRAPPER = "#include <iostream>\n#include <string>\n#include <vector>\n#include <cstring>\nusing namespace std;\nint main() {\n%s\n    return 0;\n}\n"


class Block:
    def __init__(self, lang, code_start, line_no):
        self.lang = lang
        self.line_no = line_no            # line of the @code directive (1-based, for messages)
        self.code_start = code_start      # index of the first code line
        self.code_end = None              # index just after the last code line
        self.out_start = None             # index of the first output line, if any
        self.out_end = None


def parse_blocks(lines):
    blocks, current, mode = [], None, None
    for i, raw in enumerate(lines):
        line = raw.rstrip("\n")
        stripped = line.strip()
        if mode is None:
            if stripped.startswith("@code"):
                parts = stripped.split()
                lang = parts[1] if len(parts) > 1 else "c"
                current = Block(lang, i + 1, i + 1)
                mode = "code"
        elif stripped == "@end":
            if mode == "code":
                current.code_end = i
            else:
                current.out_end = i
            blocks.append(current)
            current, mode = None, None
        elif stripped == "@output" and mode == "code":
            current.code_end = i
            current.out_start = i + 1
            mode = "output"
    return blocks


C_INCLUDES = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdbool.h>\n"
CPP_INCLUDES = "#include <iostream>\n#include <string>\n#include <vector>\n#include <cstring>\n#include <cstdlib>\nusing namespace std;\n"


def build_source(code, lang):
    if "main" in code:
        # Full program: add the standard headers only if the snippet has no #include of its own
        if "#include" in code:
            return code + "\n"
        return (C_INCLUDES if lang == "c" else CPP_INCLUDES) + code + "\n"
    return (C_WRAPPER if lang == "c" else CPP_WRAPPER) % code


def compile_and_run_source(source, lang):
    """Compile a complete program as it is. Returns (ok, stdout_or_error)."""
    compiler, ext, std = ("gcc", ".c", "-std=c11") if lang == "c" else ("g++", ".cpp", "-std=c++17")
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "example" + ext)
        exe = os.path.join(tmp, "example")
        with open(src, "w") as f:
            f.write(source)
        build = subprocess.run([compiler, std, "-w", "-o", exe, src], capture_output=True, text=True)
        if build.returncode != 0:
            return False, "does not compile:\n" + build.stderr.strip()
        try:
            result = subprocess.run([exe], capture_output=True, text=True, timeout=5, stdin=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            return False, "ran for more than 5 seconds"
        return True, result.stdout


def compile_and_run(code, lang, run):
    compiler, ext, std = ("gcc", ".c", "-std=c11") if lang == "c" else ("g++", ".cpp", "-std=c++17")
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "example" + ext)
        exe = os.path.join(tmp, "example")
        with open(src, "w") as f:
            f.write(build_source(code, lang))
        build = subprocess.run([compiler, std, "-w", "-o", exe, src], capture_output=True, text=True)
        if build.returncode != 0:
            return False, "does not compile:\n" + build.stderr.strip()
        if not run:
            return True, None
        try:
            result = subprocess.run([exe], capture_output=True, text=True, timeout=5, stdin=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            return False, "ran for more than 5 seconds"
        return True, result.stdout


def check_file(path, fix):
    with open(path) as f:
        lines = f.readlines()

    problems, fixes = 0, []
    for block in parse_blocks(lines):
        code = "".join(lines[block.code_start:block.code_end]).rstrip("\n")
        has_output = block.out_start is not None
        ok, result = compile_and_run(code, block.lang, run=has_output)
        where = "%s:%d" % (os.path.basename(path), block.line_no)

        if not ok:
            print("FAIL %s  %s" % (where, result))
            problems += 1
        elif not has_output:
            print("ok   %s  (compiles, no output block)" % where)
        else:
            expected = "".join(lines[block.out_start:block.out_end]).rstrip()
            actual = result.rstrip()
            if expected == actual:
                print("ok   %s  output matches" % where)
            else:
                print("FAIL %s  output differs" % where)
                print("       file says: %r" % expected)
                print("       real run:  %r" % actual)
                if fix:
                    fixes.append((block, actual))
                else:
                    problems += 1

    if fixes:
        # Apply from the bottom so earlier line numbers stay valid
        for block, actual in sorted(fixes, key=lambda item: -item[0].out_start):
            new_lines = [l + "\n" for l in actual.split("\n")] if actual else []
            lines[block.out_start:block.out_end] = new_lines
        with open(path, "w") as f:
            f.writelines(lines)
        print("fixed %d output block(s) in %s" % (len(fixes), os.path.basename(path)))

    return problems


# ---------------------------------------------------------------------------
# Exam files
# ---------------------------------------------------------------------------
def normalize(answer):
    """Same rule as NormalizeAnswer in content.c."""
    out = "".join(answer.split()).lower()
    return out[:-1] if out.endswith(";") else out


def parse_exam(lines):
    """Returns a list of question dicts: kind, line, prompt, lang, code, options, correct, accepts, test, expect."""
    questions, q, mode = [], None, None
    buf = []
    for i, raw in enumerate(lines):
        line = raw.rstrip("\n")
        stripped = line.strip()
        if mode == "code":
            if stripped == "@end":
                q["code"] = "\n".join(buf).rstrip("\n")
                mode = None
            else:
                buf.append(line)
            continue
        if mode == "test":
            if stripped == "@expect":
                q["test"] = "\n".join(buf).strip("\n")
                buf, mode = [], "expect"
            elif stripped == "@end":        # @test without @expect
                q["test"] = "\n".join(buf).strip("\n")
                mode = None
            else:
                buf.append(line)
            continue
        if mode == "expect":
            if stripped == "@end":
                q["expect"] = "\n".join(buf).rstrip()
                mode = None
            else:
                buf.append(line)
            continue

        if not stripped.startswith("@"):
            continue
        parts = stripped[1:].split(None, 1)
        kw, rest = parts[0], (parts[1] if len(parts) > 1 else "")
        if kw in ("q", "typed"):
            q = {"kind": "choice" if kw == "q" else "typed", "line": i + 1, "prompt": rest, "lang": "c",
                 "code": None, "options": [], "correct": None, "accepts": [], "test": None, "expect": None}
            questions.append(q)
        elif kw == "code" and q is not None:
            q["lang"] = rest.split()[0] if rest.split() else "c"
            buf, mode = [], "code"
        elif kw == "opt" and q is not None:
            q["options"].append(rest)
        elif kw == "correct" and q is not None:
            q["correct"] = int(rest) - 1
        elif kw == "accept" and q is not None:
            q["accepts"].append(rest)
        elif kw == "test" and q is not None:
            buf, mode = [], "test"
    return questions


def typed_program(q, answer):
    code = q["code"].replace("____", answer)
    test = q["test"] or ""
    if "main" in code or "main" in test:
        body = code + "\n" + test + "\n"
        return (C_INCLUDES if q["lang"] == "c" else CPP_INCLUDES) + body
    return (C_WRAPPER if q["lang"] == "c" else CPP_WRAPPER) % (code + "\n" + test)


def check_exam(path):
    with open(path) as f:
        lines = f.readlines()

    name = os.path.basename(path)
    problems = 0
    questions = parse_exam(lines)

    for q in questions:
        where = "%s:%d" % (name, q["line"])

        if q["kind"] == "typed":
            if q["code"] is None or "____" not in q["code"]:
                print("FAIL %s  typed question needs a @code block containing ____" % where)
                problems += 1
                continue
            if q["test"] is None or q["expect"] is None:
                print("warn %s  no @test/@expect: the accepted answers are not checked by running" % where)
                continue
            bad = 0
            seen = {}
            for answer in q["accepts"]:
                norm = normalize(answer)
                if norm in seen:
                    print("FAIL %s  @accept %r is the same answer as %r once spaces and case are ignored" % (where, answer, seen[norm]))
                    bad += 1
                    continue
                seen[norm] = answer
                ok, result = compile_and_run_source(typed_program(q, answer), q["lang"])
                if not ok:
                    print("FAIL %s  @accept %r  %s" % (where, answer, result))
                    bad += 1
                elif result.rstrip() != q["expect"]:
                    print("FAIL %s  @accept %r prints %r, expected %r" % (where, answer, result.rstrip(), q["expect"]))
                    bad += 1
            if bad == 0:
                print("ok   %s  typed: %d accepted answer(s) behave correctly" % (where, len(q["accepts"])))
            problems += bad
        else:
            if q["code"] is None:
                print("ok   %s  choice (no code)" % where)
                continue
            source = (C_WRAPPER if q["lang"] == "c" else CPP_WRAPPER) % q["code"] if "main" not in q["code"] else build_source(q["code"], q["lang"])
            ok, result = compile_and_run_source(source, q["lang"])
            if not ok:
                print("FAIL %s  %s" % (where, result))
                problems += 1
                continue
            matches = [i for i, opt in enumerate(q["options"]) if opt.strip() == result.strip()]
            if matches != [q["correct"]]:
                print("FAIL %s  real output is %r; options equal to it: %s; @correct is option %s" % (
                    where, result.strip(), [m + 1 for m in matches] or "none", (q["correct"] + 1) if q["correct"] is not None else "missing"))
                problems += 1
            else:
                print("ok   %s  choice: option %d matches the real output" % (where, q["correct"] + 1))
    return problems


def is_exam_file(path):
    with open(path) as f:
        for raw in f:
            s = raw.strip()
            if s and not s.startswith("#"):
                return s.startswith("@exam")
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*")
    parser.add_argument("--fix", action="store_true", help="rewrite @output blocks that differ from the real output")
    args = parser.parse_args()

    files = args.files
    if not files:
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "resources", "content")
        files = sorted(f for f in glob.glob(os.path.join(root, "*.txt")) if os.path.basename(f) != "sections.txt")

    problems = sum(check_exam(path) if is_exam_file(path) else check_file(path, args.fix) for path in files)
    print("\n%s" % ("All examples check out." if problems == 0 else "%d problem(s) found." % problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
