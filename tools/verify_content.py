#!/usr/bin/env python3
"""Check (and optionally fix) the code examples in the lesson files.

Every @code block is compiled. If the block has an @output section, the program is also
run and its real output is compared with what the file says.

  python3 tools/verify_content.py              check every lesson in src/resources/content
  python3 tools/verify_content.py FILE...      check specific files
  python3 tools/verify_content.py --fix        rewrite wrong @output blocks with the real output

Snippets without a main() are wrapped in one automatically, so lessons can show just the
interesting lines. Needs gcc (for "c" blocks) and g++ (for "cpp" blocks).
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile

C_WRAPPER = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdbool.h>\nint main(void) {\n%s\n    return 0;\n}\n"
CPP_WRAPPER = "#include <iostream>\n#include <cstring>\nusing namespace std;\nint main() {\n%s\n    return 0;\n}\n"


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


def build_source(code, lang):
    if "main" in code:
        return code + "\n"
    return (C_WRAPPER if lang == "c" else CPP_WRAPPER) % code


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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*")
    parser.add_argument("--fix", action="store_true", help="rewrite @output blocks that differ from the real output")
    args = parser.parse_args()

    files = args.files
    if not files:
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "resources", "content")
        files = sorted(f for f in glob.glob(os.path.join(root, "*.txt")) if os.path.basename(f) != "sections.txt")

    problems = sum(check_file(path, args.fix) for path in files)
    print("\n%s" % ("All examples check out." if problems == 0 else "%d problem(s) found." % problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
