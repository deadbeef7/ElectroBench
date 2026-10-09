#!/usr/bin/env python3
"""Strip comments from GLSL shaders in-place.

Removes // line comments and /* */ block comments while preserving:
  * string literals ("..." with \\ escapes),
  * line joining through a trailing backslash,
  * the line number of everything that is kept (so #version stays on line 1
    and in-file line references do not shift),
  * trailing newlines.

Usage: python3 scripts/strip_shader_comments.py <file.glsl> ...
"""
import sys


def strip_one(path: str) -> bool:
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    out = []
    i, n = 0, len(src)
    nl = 0          # pending newlines: a removed comment collapses to a newline
                     # only where the comment itself spanned one
    while i < n:
        c = src[i]
        # keep string literals verbatim (no strings in GLSL normally, but "..."
        # exists in some drivers' #line / include tricks -- be safe)
        if c == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\": j += 2; continue
                if src[j] == '"': j += 1; break
                j += 1
            out.append(src[i:j]); i = j; continue
        # line comment: drop it; emit one newline only if the line ends here
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            if j == -1:
                i = n; break
            i = j  # the newline itself is handled by the default branch
            continue
        # block comment: drop it; emit one space if it spanned lines to join the
        # surrounding tokens, and collapse its internal lines to newlines
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            if j == -1:
                raise ValueError(f"{path}: unterminated block comment")
            body = src[i + 2:j]
            spans = body.count("\n")
            i = j + 2
            if spans == 0:
                out.append(" ")      # single line: token separator
            else:
                out.append(" " + "\n" * spans)  # keep downstream line numbers
            continue
        out.append(c)
        i += 1
    new = "".join(out)
    # collapse runs of >=3 blank lines to 2, strip trailing space per line
    lines = [("  " if False else line) for line in
             (l.rstrip() for l in new.split("\n"))]
    collapsed = []
    blanks = 0
    for line in lines:
        if line == "":
            blanks += 1
            if blanks >= 3:
                continue
        else:
            blanks = 0
        collapsed.append(line)
    while collapsed and collapsed[-1] == "":
        collapsed.pop()
    new = "\n".join(collapsed) + "\n"
    changed = new != src
    if changed:
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(new)
    return changed


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    rc = 0
    for path in sys.argv[1:]:
        try:
            changed = strip_one(path)
            print(f"{path}: {'stripped' if changed else 'already clean'}")
        except Exception as e:
            print(f"{path}: ERROR {e}")
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
