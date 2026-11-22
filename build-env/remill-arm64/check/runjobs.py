#!/usr/bin/env python3
"""runjobs.py JOBS OUTDIR JOBS_N CMD... : run `CMD... <args> --ir_out OUTDIR/<name>.ll` per job.
CMD may contain {ARGS} for unshare-style shell wrapping (see the callers)."""
import concurrent.futures as cf, json, os, subprocess, sys, time
jobs = json.load(open(sys.argv[1])); outd = sys.argv[2]; n = int(sys.argv[3]); cmd = sys.argv[4:]
os.makedirs(outd, exist_ok=True)
def one(j):
    ll = f"{outd}/{j['name']}.ll"
    args = j["args"] + ["--ir_out", ll]
    if "{ARGS}" in " ".join(cmd):
        c = [x.replace("{ARGS}", " ".join(args)) for x in cmd]
    else:
        c = cmd + args
    t = time.time()
    p = subprocess.run(c, capture_output=True, text=True)
    return {"name": j["name"], "rc": p.returncode, "s": round(time.time() - t, 2), "err": p.stderr[-300:]}
t0 = time.time()
recs = []
with cf.ThreadPoolExecutor(n) as ex:
    for r in ex.map(one, jobs):
        recs.append(r)
json.dump(recs, open(f"{outd}/run.json", "w"), indent=0)
print(f"{len(recs)} jobs, rc!=0: {sum(r['rc'] != 0 for r in recs)}, wall {time.time()-t0:.1f}s, sum {sum(r['s'] for r in recs):.1f}s")
