#!/usr/bin/env python3
"""field_check: the 1.0.3 picker field's structure, checked on the BUILT skin (design §7).

  python3 tools/field_check.py "build/skin/Dream Static Sounds - VST - MPC Synthdrums"

The field is the framework's own device-proven popup widget (PolyForce shPopField / vendored
shadow_skin.py): two "Toggle Switch" actions, components exactly [Focus, Value label on its own
"Text" handle], NO Button component (a Button fires the def's actions twice per tap: open, then
instantly shut), and each placement remaps Data -> the hidden "__open" flag and Text -> the enum's
own parameter. Exits 1 on any violation."""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from skin import INDEX, OPEN_INDEX, POPUPS   # noqa: E402  (the nine pickers)


def main():
    folder = sys.argv[1]
    tui = json.load(open(os.path.join(folder, "Plugin Skins", "TUI.json")))
    defs = {e["key"]: e["value"] for e in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    subs = [v for k, v in defs.items() if "|" in k]        # the sub-page defs hold the placements
    fields = sorted(k for k in defs if k.startswith("sdField_"))
    fails = []
    if len(fields) != len(POPUPS):
        fails.append("expected %d sdField_ defs, found %d" % (len(POPUPS), len(fields)))
    want_actions = sorted([("Mouse Down", "Toggle Switch"), ("Enter Pressed", "Toggle Switch")])
    for k in fields:
        d = defs[k]
        acts = sorted((a["onAction"], a["handler"]) for a in d.get("actions", []))
        if acts != want_actions:
            fails.append("%s: actions %r != the Toggle Switch pair" % (k, acts))
        types = [(c["componentData"]["type"], c["componentData"]["data"].get("handleName"))
                 for c in d.get("componentsData", [])]
        if len(types) != 2 or ("Focus", None) not in types or ("Label", "Text") not in types:
            fails.append("%s: components %r (want exactly Focus + Label@Text, no Button)" % (k, types))
    # placements: each field's Data -> its __open flag, Text -> the enum's own param; the 1.0.2
    # separate "<key> value" kids and the old sdKey field defs must be gone
    kids = [c for s in subs for c in s.get("componentsData", [])
            if c["componentData"]["type"].startswith("sdField_")]
    for key, _cols, _cw in POPUPS:
        mine = [c for c in kids if c["componentData"]["name"] == key]
        if len(mine) != 1:
            fails.append("%s: expected exactly one field placement, found %d" % (key, len(mine)))
        else:
            m = {e["key"]: e["value"] for e in mine[0]["handle remapping"]["map"]}
            if m.get("Data") != "Parameter %d" % OPEN_INDEX[key] or m.get("Text") != "Parameter %d" % INDEX[key]:
                fails.append("%s: remap %r (want Data->%d __open, Text->%d enum)"
                             % (key, m, OPEN_INDEX[key], INDEX[key]))
        if [c for s in subs for c in s.get("componentsData", [])
                if c["componentData"]["name"] == "%s value" % key]:
            fails.append("%s: the old separate value kid is still placed" % key)
        if any(k.startswith("sdKey_p4_") and k.endswith(key) for k in defs):
            fails.append("%s: the old sdKey field def is still defined" % key)
    for f in fails:
        print("FAIL", f)
    print("field_check: %d failures" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
