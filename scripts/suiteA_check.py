#!/usr/bin/env python3
"""Definition A — tombi's exact suite definition — through teptris.
Suite: json-schema-org/JSON-Schema-Test-Suite pinned at tombi's
1d82f70b87bba8c6f5d47b6b7e256401215d635b. Required files only (no
optional/), drafts 7 / 2019-09 / 2020-12, object roots, no nulls
(the only skip class), dialect injected when missing, localhost
remotes rewritten to file:// under remotes/."""
import json, subprocess, sys, tempfile, os, copy

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEPTRIS = os.environ.get("TEPTRIS_CLI", REPO + "/build/cli/teptris")
SUITE = os.environ.get(
    "SUITE_DIR",
    "/tmp/suiteA")  # the suite clone at tombi's pin (tests/ parent)
DRAFTS = ["draft7", "draft2019-09", "draft2020-12"]
DIALECT = {
    "draft7": "http://json-schema.org/draft-07/schema#",
    "draft2019-09": "https://json-schema.org/draft/2019-09/schema#",
    "draft2020-12": "https://json-schema.org/draft/2020-12/schema#",
}

def toml_key(k):
    # TOML bare keys are ASCII-only (python's isalnum is not)
    if k and all(("a" <= c <= "z") or ("A" <= c <= "Z") or ("0" <= c <= "9")
                 or c in "-_" for c in k) and not k[0].isdigit():
        return k
    return json.dumps(k)

def scalar(v):
    if isinstance(v, bool): return "true" if v else "false"
    if isinstance(v, (int, float)): return json.dumps(v)
    if isinstance(v, str): return json.dumps(v, ensure_ascii=False)
    raise ValueError

def inline(v):
    if isinstance(v, list):
        return "[" + ", ".join(inline(x) for x in v) + "]"
    if isinstance(v, dict):
        if not v: return "{}"
        return "{" + ", ".join(f"{toml_key(k)} = {inline(x)}" for k, x in v.items()) + "}"
    return scalar(v)

def to_toml(obj):
    return "".join(f"{toml_key(k)} = {inline(v)}\n" for k, v in obj.items())

def representable(v):
    if v is None: return False
    if isinstance(v, dict): return all(representable(x) for x in v.values())
    if isinstance(v, list): return all(representable(x) for x in v)
    return True

def inject_dialect(schema, draft):
    if isinstance(schema, dict) and "$schema" not in schema:
        schema = dict(schema)
        schema["$schema"] = DIALECT[draft]
    return schema

def rewrite_remotes(node):
    if isinstance(node, dict):
        return {k: rewrite_remotes(v) for k, v in node.items()}
    if isinstance(node, list):
        return [rewrite_remotes(x) for x in node]
    if isinstance(node, str) and node.startswith("http://localhost:"):
        parts = node.split("/", 3)
        return "file://" + SUITE + "/remotes/" + (parts[3] if len(parts) > 3 else "")
    return node

def run_case(schema, data):
    d = tempfile.mkdtemp()
    sp = os.path.join(d, "s.json")
    open(sp, "w").write(json.dumps(schema))
    tp = os.path.join(d, "d.toml")
    open(tp, "w").write(to_toml(data))
    r = subprocess.run([TEPTRIS, "validate", "--schema", sp, tp],
                       capture_output=True, text=True)
    if r.returncode == 2:
        return -1  # schema compile rejection
    return r.returncode

total = passed = skipped = rejected = 0
failures = []
for draft in DRAFTS:
    pdir = os.path.join(SUITE, "tests", draft)
    for fname in sorted(os.listdir(pdir)):
        if not fname.endswith(".json") or "/optional" in fname:
            continue
        path = os.path.join(pdir, fname)
        for case in json.load(open(path)):
            schema = rewrite_remotes(inject_dialect(case["schema"], draft))
            for t in case["tests"]:
                data = t["data"]
                if not (isinstance(data, dict) and representable(data)):
                    skipped += 1
                    continue
                total += 1
                rc = run_case(schema, data)
                if rc == -1:
                    rejected += 1
                    failures.append(("REJECT", draft, fname, case["description"], t["description"]))
                    continue
                if (rc == 0) == t["valid"]:
                    passed += 1
                else:
                    failures.append(("WRONG", draft, fname, case["description"], t["description"],
                                     "want valid=%s" % t["valid"]))

print(f"passed {passed}/{total}, skipped(non-representable) {skipped}, schema-rejected {rejected}")
from collections import Counter
c = Counter()
for f in failures:
    c[(f[0], f[2])] += 1
for k, n in c.most_common(30):
    print(n, k)
import json as _j
with open("/tmp/suiteA_failures.json", "w") as fh:
    _j.dump(failures, fh, indent=1)
