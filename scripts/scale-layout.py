#!/usr/bin/env python3
"""Make LVGL layout coordinates scale-aware (run once per file; idempotent).

Integer geometry values become Jinja substitutions, e.g. `y: 62` ->
`y: ${ (62 * ui_scale) | round | int }`. With the panel's ui_scale of 1 the
resolved values are unchanged; the rectangular panels set ui_scale per model.
"""
import re
import sys
from pathlib import Path

KEYS = ("x", "y", "width", "height", "radius", "pad_all", "pad_top", "pad_bottom",
        "pad_left", "pad_right", "pad_row", "pad_column", "arc_width", "border_width",
        "outline_width", "shadow_width", "text_line_space")
PATTERN = re.compile(r"^(\s*(?:- )?(?:%s)):\s*(-?\d+)\s*$" % "|".join(KEYS))

for name in sys.argv[1:]:
    path = Path(name)
    changed = 0
    out = []
    for line in path.read_text().split("\n"):
        match = PATTERN.match(line)
        if match and int(match.group(2)) not in (0,):
            line = f"{match.group(1)}: ${{ ({match.group(2)} * (ui_scale | float)) | round | int }}"
            changed += 1
        out.append(line)
    path.write_text("\n".join(out))
    print(f"{name}: {changed} values")
