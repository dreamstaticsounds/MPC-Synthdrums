#!/usr/bin/env python3
"""gen_vst's skin_post hook (vst.json "skin_post"): stamp the skin folder's version.xml with this port's
version. The framework's write_skin hardcodes <version>1.0.0.0</version> in it (shadow_skin.py) -- right for
the 1.0.0 release, stale for every later one; the real version lives in vst.json ("version": 1001 reads
"1.0.1.0", the same major.minor.patch.build form MPC shows). Run by gen_vst after the skin build, with the
skin folder as its argument."""
import json
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
v = json.load(open(os.path.join(here, "..", "vst.json")))["version"]
ver = "%d.%d.%d.0" % (v // 1000, (v // 100) % 10, v % 10)
path = os.path.join(sys.argv[1], "version.xml")
txt = open(path).read()          # read before truncating: open(path, "w") would empty it first
open(path, "w").write(re.sub(r"<version>[^<]*</version>", "<version>%s</version>" % ver, txt))
print("version.xml: %s" % ver)
