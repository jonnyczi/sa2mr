#!/usr/bin/env python3
"""Deterministic remill lift sample, with lift_all.py --to-next-entry / lift_arms.py's exact
remill arguments. Writes jobs.json: [{name, kind, args:[...]}] (args hold game bytes: scratch only)."""
import bisect, json, sys
sys.path.insert(0, "/work/tools/s3/whole_image")
import common as C
out = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 200
fmap = C.by_address()
starts = sorted(fmap)
img = C.memory_image()
step = len(starts) / N
picks = [starts[int(i * step)] for i in range(N)]
jobs = []
for va in picks:
    i = bisect.bisect_right(starts, va)
    end = starts[i] if i < len(starts) else va + fmap[va]["size"]
    raw = img[va - C.IMAGE_BASE:end - C.IMAGE_BASE]
    jobs.append({"name": f"sub_{va:08X}", "kind": "map", "bytes": len(raw),
                 "args": ["--arch", "x86", "--os", "windows", "--address", f"{va:#x}",
                          "--bytes", raw.hex()]})
arms = [json.loads(l) for l in open(C.GEN / "s3/arms/arms.next.jsonl") if l.strip()]
arms.sort(key=lambda r: int(r["arm"], 16))
for r in [arms[int(i * len(arms) / 5)] for i in range(5)]:
    owner, end = (int(x, 16) for x in r["anchor"])
    entry = int(r["arm"], 16)
    raw = img[owner - C.IMAGE_BASE:end - C.IMAGE_BASE]
    jobs.append({"name": f"arm_{entry:08X}", "kind": "arm", "bytes": len(raw),
                 "args": ["--arch", "x86", "--os", "windows", "--address", f"{owner:#x}",
                          "--entry_address", f"{entry:#x}", "--bytes", raw.hex()]})
json.dump(jobs, open(out, "w"))
print(len(jobs), "jobs, bytes total", sum(j["bytes"] for j in jobs), "max", max(j["bytes"] for j in jobs))
