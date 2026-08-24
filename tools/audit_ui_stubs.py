#!/usr/bin/env python3
"""Count interface elements that do nothing, so MARKDOWN/UI_STUBS_AUDIT.md can be checked.

Run from the repo root:

    python tools/audit_ui_stubs.py            # print the counts
    python tools/audit_ui_stubs.py --json     # machine-readable, for diffing

Three separate failures, because "does nothing" means something different for each:

  MENU STUB   an action built through MenuBuilder's addStub()/addStubDisabled() helpers,
              or an addAction() whose overload carries no receiver and whose result is
              never connected or compared against a menu exec() result.

  NO HANDLER  a button with nothing connected to it, no menu attached and no default
              action. Clicking does nothing, and unlike a disabled control it does not
              look like it.

  NO SINK     an input control whose value nothing ever reads. It may be connected — the
              Project Properties fields all mark the dialog dirty, so Apply lights up —
              and still never reach the model.

The counts are a tripwire, not a verdict: every candidate needs reading before it goes in
the report. The known false-positive shapes are listed in the audit's own §6, and the ones
this script already filters are noted inline below.
"""

import argparse
import io
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

INPUTS = ("QCheckBox", "QRadioButton", "QComboBox", "QSpinBox", "QDoubleSpinBox",
          "QSlider", "QLineEdit", "QPlainTextEdit", "QTextEdit", "QDial",
          "QDateTimeEdit", "QTimeEdit", "QFontComboBox", "QKeySequenceEdit")
BUTTONS = ("QPushButton", "QToolButton", "QCommandLinkButton")
READERS = ("value", "isChecked", "checkState", "currentText", "currentIndex", "currentData",
           "text", "toPlainText", "sliderPosition", "dateTime", "time", "keySequence",
           "currentFont", "isDown")


def load_sources(root):
    out = {}
    for dirpath, _dirs, files in os.walk(os.path.join(root, "src")):
        for fn in files:
            if fn.endswith((".cpp", ".h")):
                p = os.path.join(dirpath, fn)
                rel = os.path.relpath(p, root).replace("\\", "/")
                out[rel] = open(p, encoding="utf-8", errors="replace").read()
    return out


def menu_stubs(sources):
    """MenuBuilder marks its own unfinished entries; they are the reliable count."""
    stub = re.compile(r"\baddStub(Disabled)?\s*\(\s*\w+\s*,\s*QObject::tr\(\s*\"([^\"]*)\"")
    rows = []
    for rel, text in sources.items():
        for i, line in enumerate(text.splitlines(), 1):
            m = stub.search(line)
            if m:
                rows.append({"file": rel, "line": i, "label": m.group(2),
                             "kind": "DISABLED" if m.group(1) else "DEAD"})
    return rows


def call_args(text, open_paren):
    depth = 0
    for i in range(open_paren, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1:i]
    return ""


def unwired_actions(sources):
    """addAction() overloads that leave the action unconnected."""
    wired_hint = re.compile(r"\[|&\w+::|\bwindow\b\s*,|\bthis\b\s*,|\bqApp\b\s*,|SLOT\s*\(")
    text_arg = re.compile(r"(?:QObject::)?tr\s*\(\s*(?:QStringLiteral\()?\"([^\"]*)\"")
    rows = []
    for rel, text in sources.items():
        if not rel.endswith(".cpp"):
            continue
        connects = "\n".join(re.findall(r"connect\s*\((?:[^;]|\n)*?;", text))
        compares = " ".join(re.findall(r"(?:==|!=)\s*(\w+)", text))
        for m in re.finditer(r"addAction\s*\(", text):
            args = call_args(text, m.end() - 1)
            label = text_arg.search(args)
            if not label:
                continue  # group->addAction(existing): not a creation
            if wired_hint.search(args):
                continue
            before = text[max(0, m.start() - 120):m.start()]
            vm = re.search(r"(\w+)\s*=\s*[^=;]*$", before)
            var = vm.group(1) if vm else None
            if var and (re.search(r"\b" + re.escape(var) + r"\b", connects)
                        or re.search(r"\b" + re.escape(var) + r"\b", compares)):
                continue
            # A deliberate setEnabled(false) is a "not yet", which reads honestly on
            # screen; an unwired action that still looks clickable does not.
            after = text[m.start():m.start() + 400]
            kind = "DEAD"
            if var and re.search(re.escape(var) + r"\s*->\s*setEnabled\s*\(\s*false", after):
                kind = "DISABLED"
            rows.append({"file": rel, "line": text[:m.start()].count("\n") + 1,
                         "label": label.group(1), "kind": kind})
    return rows


def controls(sources):
    """Buttons with no handler, inputs with no reader."""
    joined = "\n".join(sources.values())
    all_connects = "\n".join(re.findall(r"connect\s*\((?:[^;]|\n)*?;", joined))
    decl = re.compile(r"\b(m_\w+|\w+)\s*=\s*new\s+(" + "|".join(INPUTS + BUTTONS) + r")\b")

    # A local variable is only visible in its own file, so searching every file for the
    # name lets an unrelated `closeBtn` in some other dialog vouch for a dead one here —
    # which is exactly how one went unnoticed. Members (m_*) do cross files through the
    # header, so those keep the wide search.
    file_connects = {rel: "\n".join(re.findall(r"connect\s*\((?:[^;]|\n)*?;", t))
                     for rel, t in sources.items()}

    def connected(name, rel):
        hay = all_connects if name.startswith("m_") else file_connects.get(rel, "")
        return bool(re.search(r"\b" + re.escape(name) + r"\b", hay))

    def read(name, rel):
        hay = joined if name.startswith("m_") else sources.get(rel, "")
        esc = re.escape(name)
        return any(re.search(esc + r"\s*(?:->|\.)\s*" + r + r"\s*\(", hay) for r in READERS)

    no_handler, no_sink = [], []
    seen = {"inputs": 0, "buttons": 0}
    for rel, text in sources.items():
        if not rel.endswith(".cpp"):
            continue
        for m in decl.finditer(text):
            var, kind = m.group(1), m.group(2)
            line = text[:m.start()].count("\n") + 1
            near = text[m.start():m.start() + 700]
            if kind in BUTTONS:
                seen["buttons"] += 1
                if connected(var, rel):
                    continue
                # A menu or a default action does the wiring instead, and a button handed
                # out through a pointer parameter is wired by its caller. setMenu can
                # come dozens of lines below the declaration, so it is looked for in the
                # whole file rather than in a window around it.
                if re.search(re.escape(var) + r"\s*->\s*setMenu\s*\(", text) \
                        or "setDefaultAction" in near \
                        or re.search(r"\*\s*\w+\s*=\s*" + re.escape(var) + r"\s*;", text) \
                        or re.search(r"return\s+" + re.escape(var) + r"\s*;", near) \
                        or re.search(r"addButton\s*\(\s*" + re.escape(var), near):
                    continue
                no_handler.append({"file": rel, "line": line, "name": var, "class": kind})
            else:
                seen["inputs"] += 1
                # Connected counts as alive here: a slider wired to a lambda that uses the
                # argument is doing its job without anyone calling ->value() on it, and
                # that shape is common enough that flagging it buries the real cases.
                #
                # The cost is a blind spot the tool cannot close: a control connected only
                # to a "mark dirty" helper looks alive to this check and is not. Project
                # Properties is full of them, and they are listed in the audit by hand.
                if connected(var, rel) or read(var, rel):
                    continue
                # Built by a factory helper or a lambda and handed back: the caller owns
                # the wiring, so judging it here would blame the wrong place.
                if re.search(r"return\s+" + re.escape(var) + r"\s*;", near):
                    continue
                no_sink.append({"file": rel, "line": line, "name": var, "class": kind})
    return seen, no_handler, no_sink


def discarded_buttons(sources):
    """Buttons built straight into a layout call, with no pointer kept.

        layout->addWidget(IconFactory::toolButton(this, tr("Start Preview"), ...));

    The button is on screen and clickable, and nothing can ever connect to it because
    nothing holds it. The factory sets one shared objectName, so findChild cannot recover
    it either.
    """
    call = re.compile(r"addWidget\s*\(\s*IconFactory::toolButton\s*\(")
    label = re.compile(r"tr\(\s*\"([^\"]*)\"")
    rows = []
    for rel, text in sources.items():
        if not rel.endswith(".cpp"):
            continue
        for m in call.finditer(text):
            args = call_args(text, text.index("(", m.end() - 1))
            lm = label.search(args)
            rows.append({"file": rel, "line": text[:m.start()].count("\n") + 1,
                         "label": lm.group(1) if lm else "?"})
    return rows


def orphan_ui(root, sources):
    """.ui files the build compiles that no source includes."""
    joined = "\n".join(sources.values())
    out = []
    ui_dir = os.path.join(root, "ui")
    if not os.path.isdir(ui_dir):
        return out
    for name in sorted(os.listdir(ui_dir)):
        if not name.endswith(".ui"):
            continue
        base = name[:-3]
        if re.search(r"ui_" + re.escape(base) + r"\.h|Ui::" + re.escape(base) + r"\b", joined):
            continue
        try:
            widgets = sum(1 for w in ET.parse(os.path.join(ui_dir, name)).getroot().iter("widget"))
        except ET.ParseError:
            widgets = -1
        out.append({"file": "ui/" + name, "widgets": widgets})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    sources = load_sources(args.root)
    stubs = menu_stubs(sources)
    actions = unwired_actions(sources)
    seen, no_handler, no_sink = controls(sources)
    orphans = orphan_ui(args.root, sources)
    discarded = discarded_buttons(sources)

    report = {
        "menu_stubs": {"dead": sum(1 for s in stubs if s["kind"] == "DEAD"),
                       "disabled": sum(1 for s in stubs if s["kind"] == "DISABLED"),
                       "rows": stubs},
        "unwired_actions": {"count": len(actions), "rows": actions},
        "no_handler": {"count": len(no_handler), "rows": no_handler},
        "no_sink": {"count": len(no_sink), "rows": no_sink},
        "orphan_ui": {"count": len(orphans), "rows": orphans},
        "discarded_buttons": {"count": len(discarded), "rows": discarded},
        "scanned": seen,
    }

    if args.json:
        # Labels carry en dashes and ellipses, and a redirected stdout on Windows defaults
        # to the console code page, which cannot encode them.
        out = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", newline="\n")
        json.dump(report, out, indent=1, ensure_ascii=False)
        out.write("\n")
        out.flush()
        return

    print("menu stubs        %3d dead, %d disabled  (MenuBuilder helpers)"
          % (report["menu_stubs"]["dead"], report["menu_stubs"]["disabled"]))
    print("unwired actions   %3d" % len(actions))
    print("buttons, no handler %d of %d scanned" % (len(no_handler), seen["buttons"]))
    print("inputs, no reader   %d of %d scanned" % (len(no_sink), seen["inputs"]))
    print("discarded buttons   %d (built inline, no pointer kept)" % len(discarded))
    print("orphan .ui files    %d" % len(orphans))
    print()
    print("Candidates, not verdicts: read each one before changing "
          "MARKDOWN/UI_STUBS_AUDIT.md.")


if __name__ == "__main__":
    main()
