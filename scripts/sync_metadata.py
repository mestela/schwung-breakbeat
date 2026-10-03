#!/usr/bin/env python3
"""Keep the DSP's fallback metadata in sync with src/module.json."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
manifest = json.loads((ROOT / "src/module.json").read_text())
levels = {}
flat = []
seen = set()
for key, level in manifest["capabilities"]["ui_hierarchy"]["levels"].items():
    params = [{k: v for k, v in entry.items() if k != "default"}
              for entry in level["params"]]
    levels[key] = {"name": level.get("name", level.get("label", key)),
                   "knobs": level["knobs"], "params": params}
    for item in params:
        if "key" in item and item["key"] not in seen:
            seen.add(item["key"])
            flat.append({**{k: v for k, v in item.items() if k != "label"},
                         "name": item.get("label", item["key"])})


def function(name, value):
    compact = json.dumps(value, separators=(",", ":"), ensure_ascii=True)
    chunks = "\n".join("        " + json.dumps(compact[i:i + 110])
                       for i in range(0, len(compact), 110))
    return (f"static void {name}(const breakbeat_t *bb, char *out, int out_len) {{\n"
            f"    (void)bb;\n    snprintf(out, out_len, \"%s\",\n{chunks});\n}}\n\n")


source_path = ROOT / "src/dsp/breakbeat.c"
source = source_path.read_text()
start = source.index("static void build_ui_hierarchy(")
end = source.index("/* Create the filepath browser", start)
hierarchy = {"modes": None, "levels": levels, "params": flat}
# The host's UI response buffer is 8192 bytes. Page-local definitions and
# chain_params already describe these controls; repeating them in the flat
# hierarchy list would truncate the response after adding eight anchors.
hierarchy["params"] = [item for item in flat
                       if not item["key"].startswith("anchor_")
                       and not item["key"].startswith("retrig_")
                       and item["key"] not in ("attack_ms", "decay_ms", "status",
                                               "A_sample_path", "A_sample_length", "B_sample_path",
                                               "mode_a", "mode_b", "edit_a", "edit_b",
                                               "early_end")]
replacement = function("build_ui_hierarchy", hierarchy) + function("build_chain_params", flat)
source_path.write_text(source[:start] + replacement + source[end:])
