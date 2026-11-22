#!/usr/bin/env python3
"""workflow_lint: CI never fetches the game, and the workflows stay simple to audit.

    python3 tools/release/workflow_lint.py [--repo DIR]

Over .github/workflows/*.yml and the scripts CI runs (tools/release/ci_*.sh):
  - no step may download Steam content: steamcmd, DepotDownloader, download_depot, app_update,
    steam:// URLs, Steam CDN hosts, SA2's app or depot ids, the game's executable name;
  - every `uses:` is pinned by major version (owner/action@vN), first-party actions only
    (actions/*), so no third-party code runs with the token;
  - no secret but GITHUB_TOKEN;
  - each workflow parses as YAML (when PyYAML is installed) and declares `permissions:`.
Prints WORKFLOW_LINT_OK, or one line per problem and exits 1.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FORBIDDEN = [
    (r"steamcmd", "steamcmd"),
    (r"depotdownloader", "DepotDownloader"),
    (r"download_depot", "download_depot"),
    (r"\bapp_update\b", "steamcmd's app_update"),
    (r"steam://", "a steam:// URL"),
    (r"steamcontent\.com|steampipe|cdn\.steamstatic|steamcdn|steamusercontent", "a Steam content host"),
    (r"\b213610\b|\b213611\b|\b213612\b", "SA2's Steam app/depot id"),
    (r"sonic2app", "the game's executable"),
    (r"steamapps", "a Steam library path"),
]
USES = re.compile(r"^\s*-?\s*uses:\s*(\S+)")
SECRET = re.compile(r"secrets\.([A-Za-z_][A-Za-z0-9_]*)")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    a = ap.parse_args(argv)
    wf = sorted((a.repo / ".github" / "workflows").glob("*.y*ml"))
    scripts = sorted((a.repo / "tools" / "release").glob("ci_*.sh"))
    problems = []
    if not wf:
        problems.append(".github/workflows: no workflow")
    try:
        import yaml  # noqa: PLC0415
    except ImportError:
        yaml = None
    for p in wf + scripts:
        rel = p.relative_to(a.repo)
        text = p.read_text()
        for n, line in enumerate(text.splitlines(), 1):
            for pat, what in FORBIDDEN:
                if re.search(pat, line, re.I):
                    problems.append(f"{rel}:{n}: {what}: {line.strip()[:120]}")
            if p.suffix in (".yml", ".yaml"):
                m = USES.match(line)
                if m:
                    ref = m.group(1).strip("'\"")
                    if not re.fullmatch(r"actions/[A-Za-z0-9_.-]+@v\d+", ref):
                        problems.append(f"{rel}:{n}: `uses: {ref}`: only actions/*@vN (pinned by major)")
                for s in SECRET.findall(line):
                    if s != "GITHUB_TOKEN":
                        problems.append(f"{rel}:{n}: secrets.{s}: only GITHUB_TOKEN is used")
        if p.suffix in (".yml", ".yaml"):
            if not re.search(r"^permissions:", text, re.M):
                problems.append(f"{rel}: no top-level permissions:")
            if yaml is not None:
                try:
                    doc = yaml.safe_load(text)
                    if not isinstance(doc, dict) or "jobs" not in doc:
                        problems.append(f"{rel}: no jobs")
                except yaml.YAMLError as e:
                    problems.append(f"{rel}: not valid YAML: {e}")
    for line in problems:
        print(f"REFUSED {line}")
    if problems:
        print(f"WORKFLOW_LINT_FAILED {len(problems)} problem(s)")
        return 1
    print(f"WORKFLOW_LINT_OK {len(wf)} workflow(s), {len(scripts)} script(s)"
          + ("" if yaml else " (PyYAML missing: YAML not parsed)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
