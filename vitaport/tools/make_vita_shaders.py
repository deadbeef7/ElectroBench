#!/usr/bin/env python3
"""Generate the PS Vita (vitaGL) copies of the scene 2 + scene 4 shaders.

The desktop shaders are GLSL 3.30 core. vitaGL feeds shader source to
SceShaccCg through its own GLSL translator, which:

  * deletes the #version and #extension directives,
  * and rewrites `attribute` / `varying` declarations into Cg semantics.

It does *not* understand 3.30 syntax, so the port ships GLSL 1.00-style copies
of the same math. This script is the single source of truth for that rewrite:
run it after touching a source shader, and it regenerates vitaport/shaders/.

    python3 vitaport/tools/make_vita_shaders.py

Every rewrite below is a mechanical, auditable rule (see RULES); the script
prints what it changed per file, and tools/check_vita_shaders.sh validates the
result with glslangValidator as GLSL ES 1.00.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "shaders")
# VITA_SHADER_OUT lets tools/check_shaders.sh regenerate into a scratch tree and
# diff it against the committed one.
DST = os.environ.get("VITA_SHADER_OUT") or os.path.join(ROOT, "vitaport", "shaders")

# scene 2 (ocean) and scene 4 (power lines) only, plus the shared HUD pair.
SETS = {
    "ps14": ["sea_vert.glsl", "sea_frag.glsl", "sky_vert.glsl", "sky_frag.glsl",
             "hud_vert.glsl", "hud_frag.glsl"],
    "pole": ["sky_vert.glsl", "sky_frag.glsl", "object_vert.glsl", "object_frag.glsl",
             "hud_frag.glsl"],
}

HEADER = """// GENERATED from shaders/{rel} by vitaport/tools/make_vita_shaders.py -- do not edit.
// GLSL 1.00-style rewrite for vitaGL/SceShaccCg (see that script's RULES); the
// math is identical to the desktop shader except for the documented rewrites.
"""


def split_args(text, start):
    """Split a call's argument list starting at the '(' index `start`.

    Returns (args, end_index_of_matching_paren). Parens inside nested calls are
    respected, so `clamp(1.5 + d * 0.0012, 1.0, 5.0)` yields 3 args.
    """
    assert text[start] == "("
    depth = 0
    cur = []
    args = []
    i = start + 1
    while i < len(text):
        c = text[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            if depth == 0:
                args.append("".join(cur))
                return args, i
            depth -= 1
        if c == "," and depth == 0:
            args.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    raise ValueError("unbalanced parentheses")


def rewrite_call(text, name, fn):
    """Rewrite every `name(...)` call in `text` through fn(name, args)."""
    out = []
    i = 0
    while True:
        m = re.compile(r"\b" + name + r"\s*\(").search(text, i)
        if not m:
            out.append(text[i:])
            break
        out.append(text[i:m.start()])
        args, end = split_args(text, m.end() - 1)
        out.append(fn(args))
        i = end + 1
    return "".join(out)


def check_attribute_order(rel, src):
    """vitaGL binds vertex streams to shader attributes in DECLARATION order, so
    a vertex shader is only safe if its layout(location) indices are 0..N-1
    ascending -- that is what makes the scenes' glVertexAttribPointer(n, ...)
    calls line up. The desktop shaders satisfy this; assert it so a future edit
    cannot quietly break the port in a way no host test would catch."""
    locs = [int(m) for m in re.findall(r"^layout\(location = (\d+)\) in ", src, re.M)]
    if locs and locs != list(range(len(locs))):
        raise SystemExit("%s: attribute locations %s are not 0..N-1 in order; vitaGL "
                         "binds by declaration order" % (rel, locs))
    return len(locs)


def translate(rel, src, report):
    """Apply the rewrite rules to one shader. `report` collects change counts."""
    is_frag = rel.endswith("_frag.glsl")
    text = src
    if not is_frag:
        n = check_attribute_order(rel, src)
        if n:
            report["attributes in location order"] = n

    def sub(pattern, repl, label, flags=0):
        nonlocal text
        text, n = re.subn(pattern, repl, text, flags=flags)
        if n:
            report[label] = report.get(label, 0) + n

    # 1. version directive. vitaGL strips it; ES 1.00 is the closest language
    #    level to what the translator accepts, and keeps the file validatable.
    sub(r"^#version 330 core", "#version 100", "version->100")
    if is_frag:
        sub(r"^(#version 100\n)", r"\1precision mediump float;\n", "fragment default precision")

    # 2. attribute locations -> plain attribute declarations. Declaration ORDER is
    #    preserved: vitaGL binds vertex streams to attributes in declaration order,
    #    which already matches the glVertexAttribPointer indices in both scenes.
    sub(r"^layout\(location = \d+\) (in|out) ", lambda m: "attribute " if m.group(1) == "in" else "varying ",
        "layout(location) dropped", flags=re.M)

    # 3. vertex/fragment stage interfaces.
    if is_frag:
        sub(r"^flat in ", "varying ", "flat in -> varying", flags=re.M)
        sub(r"^in ", "varying ", "frag in -> varying", flags=re.M)
    else:
        sub(r"^flat out ", "varying ", "flat out -> varying", flags=re.M)
        sub(r"^out ", "varying ", "vert out -> varying", flags=re.M)

    # 4. fragment output: declare nothing, write gl_FragColor. The desktop shaders
    #    have exactly one out variable; find its name from the declaration.
    #    (Both scene shaders write a single vec4 named fragColor.)
    if is_frag:
        m = re.search(r"^out (?:lowp |mediump |highp )?(vec4|vec3|float) (\w+)\s*;", text, re.M)
        if m:
            name = m.group(2)
            text = text[:m.start()] + text[m.end():]
            report["out %s -> gl_FragColor" % name] = 1
            text, n = re.subn(r"\b" + name + r"\b", "gl_FragColor", text)
            report["gl_FragColor writes"] = n

    # 5. texture lookups. ES 1.00 has texture2D/textureCube only, so the
    #    sampler type of the first argument decides which one is emitted.
    samplers = dict((name, kind) for kind, name in
                    re.findall(r"uniform sampler(2D|Cube)\s+(\w+)", src))

    def lookup(args):
        fn = "textureCube" if samplers.get(args[0].strip()) == "Cube" else "texture2D"
        return "%s(%s)" % (fn, ", ".join(x.strip() for x in args[:2]))

    if "texture(" in text:
        text = rewrite_call(text, "texture", lookup)
        report["texture() -> texture2D/textureCube"] = text.count("texture2D(") + text.count("textureCube(")
    # textureLod(sampler, coord, lod): ES 1.00 has no LOD bias in fragment shaders.
    if "textureLod(" in text:
        text = rewrite_call(text, "textureLod", lookup)
        report["textureLod -> texture2D/textureCube (LOD dropped)"] = 1

    # 6. derivatives. GLSL 1.00 needs the OES extension and vitaGL's translator
    #    provides dFdx/dFdy as ddx/ddy; fwidth() is a macro built from those two
    #    in desktop GLSL too, so this is exactly equivalent.
    if re.search(r"\bfwidth\s*\(", text):
        text = rewrite_call(text, "fwidth",
                            lambda a: "(abs(dFdx(%s)) + abs(dFdy(%s)))" % (a[0].strip(), a[0].strip()))
        report["fwidth -> abs(dFdx)+abs(dFdy)"] = 1
        if "GL_OES_standard_derivatives" not in text:
            text = text.replace("#version 100\n",
                                "#version 100\n#extension GL_OES_standard_derivatives : enable\n", 1)
            report["OES_standard_derivatives enabled"] = 1

    # 6b. No implicit int -> float conversion in GLSL 1.00. Loop counters are the
    #     only ints that reach float arithmetic here (`i * 7.3`, `i * 13.0`).
    sub(r"\b([ijk])\s*([*/])\s*(\d+\.\d+)", r"float(\1) \2 \3", "int loop var promoted", flags=re.M)
    sub(r"(\d+\.\d+)\s*([*/])\s*\b([ijk])\b", r"\1 \2 float(\3)", "int loop var promoted", flags=re.M)

    # 7. GLSL 1.00 has no mat3(mat4) constructor.
    sub(r"mat3\(\s*(u\w+)\s*\)", r"mat3(\1[0].xyz, \1[1].xyz, \1[2].xyz)", "mat3(mat4) expanded")

    # 8. `flat` material id: SceGxm has no flat varyings, so vMat arrives
    #    interpolated. Every triangle is built with one material, so the
    #    interpolated value equals the flat one; comparisons become nearest-id
    #    tests so a rounding ulp cannot fall through to the wrong branch.
    if "kMat" in text:
        sub(r"\bm == (kMat\w+)", r"abs(m - \1) < 0.5", "material == -> nearest id")
        sub(r"\bm != (kMat\w+)", r"abs(m - \1) >= 0.5", "material != -> nearest id")

    return HEADER.format(rel=rel) + text


def main():
    failures = 0
    for d, names in SETS.items():
        for name in names:
            rel = "%s/%s" % (d, name)
            with open(os.path.join(SRC, rel)) as f:
                src = f.read()
            report = {}
            out = translate(rel, src, report)
            dst = os.path.join(DST, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "w") as f:
                f.write(out)
            detail = ", ".join("%s x%d" % (k, v) for k, v in sorted(report.items()))
            print("%-28s %s" % (rel, detail))


if __name__ == "__main__":
    sys.exit(main())
