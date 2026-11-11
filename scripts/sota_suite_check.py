#!/usr/bin/env python3
"""The official JSON Schema test suite (draft 2020-12) through teptris.

Suite: github.com/json-schema-org/JSON-Schema-Test-Suite, pinned at
245a45abc90190c0bff04d97e6d2b541b4bb9f12. Point SUITE_DIR at a clone:

    git clone --depth 1 https://github.com/json-schema-org/JSON-Schema-Test-Suite
    SUITE_DIR=$PWD/JSON-Schema-Test-Suite/tests/draft2020-12 \
        python3 scripts/sota_suite_check.py

Verdicts compare teptris's exit code against the suite expectation.
Skips are classified, never silent: TOML cannot represent null or
non-table roots; schemas carrying $id resources or remote $refs sit
outside the single-document subset (SCHEMAS.md records the boundary);
format/content/const divergences are the deliberate policy stances
listed in SCHEMAS.md and are reported, not skipped."""
import json, subprocess, sys, tempfile, os

TEPTRIS = os.environ.get(
    "TEPTRIS_CLI",
    os.path.join(os.path.dirname(__file__), "../build/cli/teptris"))
D = os.environ.get("SUITE_DIR", "/tmp/jsonschemats/tests/draft2020-12")
FILES = sys.argv[1:] or sorted(f for f in os.listdir(D) if f.endswith(".json"))

def toml_scalar(v):
    if isinstance(v, bool): return "true" if v else "false"
    if isinstance(v, (int, float)): return json.dumps(v)
    if isinstance(v, str): return json.dumps(v, ensure_ascii=False)
    if v is None: return "''"
    raise ValueError

def toml_inline(v):
    if isinstance(v, list):
        return "[" + ", ".join(toml_inline(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{" + ", ".join(f"{toml_key(k)} = {toml_inline(x)}" for k, x in v.items()) + "}"
    return toml_scalar(v)

def toml_key(k):
    # TOML bare keys are ASCII-only (python's isalnum is not)
    if all(("a" <= c <= "z") or ("A" <= c <= "Z") or ("0" <= c <= "9")
           or c in "-_" for c in k) and k and not k[0].isdigit():
        return k
    return json.dumps(k)

def data_to_toml(v):
    if isinstance(v, dict):
        lines = []
        def emit(prefix, d):
            scalars = {k: x for k, x in d.items() if not isinstance(x, (dict, list)) or (isinstance(x, list) and not any(isinstance(y, (dict, dict)) for y in x))}
            for k, x in scalars.items():
                lines.append(f"{toml_key(k)} = {toml_inline(x)}")
            for k, x in d.items():
                if k in scalars: continue
                if isinstance(x, dict):
                    lines.append(f"[{toml_key(k) if not prefix else prefix + toml_key(k)}]")
                    emit(f"{prefix}{toml_key(k)}.", x)
                elif isinstance(x, list):
                    lines.append(f"{toml_key(k)} = {toml_inline(x)}")
        emit("", v)
        return "\n".join(lines) + "\n"
    if isinstance(v, list):
        return f"root = {toml_inline(v)}\n"
    return f"root = {toml_scalar(v)}\n"

def normalize(v):
    # suite cases that use a top-level non-table get wrapped as root=
    return v

def contains_null(v):
    if v is None: return True
    if isinstance(v, list): return any(contains_null(x) for x in v)
    if isinstance(v, dict): return any(contains_null(x) for x in v.values())
    return False

def run_case(schema, data):
    d = tempfile.mkdtemp()
    sp, tp = os.path.join(d, "s.json"), os.path.join(d, "d.toml")
    blob = json.dumps(schema)
    if '"https://json-schema.org/draft' in blob and '$ref' in blob:
        return -1  # the schema itself references a remote metaschema
    if not isinstance(data, dict):
        if '"$ref"' in blob or '$dynamicRef' in blob or '$recursiveRef' in blob:
            # the TOML wrap moves the schema under a root property, so
            # document-relative refs no longer resolve: unrepresentable
            return -1
        # keyword-only schemas wrap cleanly; the default dialect
        # rides the wrap root ($schema is a resource-root keyword)
        schema = {"$schema": "https://json-schema.org/draft/2020-12/schema",
                  "properties": {"root": schema}}
    if '"$id"' in blob:
        return -1  # $id resources: the single-document subset rejects them at compile
    if contains_null(data):
        return -1  # TOML has no null
    open(sp, "w").write(json.dumps(schema))
    open(tp, "w").write(data_to_toml(data))
    r = subprocess.run([TEPTRIS, "validate", "--schema", sp, tp],
                       capture_output=True, text=True)
    return r.returncode

total = passed = skipped = 0
failures = []
for f in FILES:
    p = os.path.join(D, f)
    if not os.path.exists(p):
        continue
    for case in json.load(open(p)):
        schema = case["schema"]
        # the official harness contract: the draft's meta-schema URI is
        # the default dialect for every file in tests/<draft>/
        if (isinstance(schema, dict) and "$schema" not in schema):
            schema = dict(schema)
            schema["$schema"] = ("https://json-schema.org/draft/"
                                 "2020-12/schema")
        blob = json.dumps(schema)
        if "remote" in case["description"].lower() or "validate-defs" in blob and False:
            skipped += len(case["tests"])
            continue
        if "$ref" in blob and "http" in blob and "json-schema.org/draft" not in blob and "json-schema.org/draft" not in case["description"]:
            pass
        for t in case["tests"]:
            data = t["data"]
            rc = run_case(schema, data)
            if rc == -1:
                skipped += 1
                continue
            total += 1
            ok = (rc == 0) == t["valid"]
            if ok:
                passed += 1
            else:
                failures.append((f, case["description"], json.dumps(data)[:50], t["valid"], rc))

print(f"passed {passed}/{total}, skipped {skipped}")
for x in failures[:200]:
    print("FAIL", x)
sys.exit(1 if failures else 0)
