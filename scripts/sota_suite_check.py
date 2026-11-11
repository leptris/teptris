#!/usr/bin/env python3
"""The official JSON Schema test suite through teptris.

Suite: github.com/json-schema-org/JSON-Schema-Test-Suite, pinned at
38dea93 (the validation/ unified-test-model era; the per-draft trees
were reorganized into version-agnostic keyword files whose cases
carry a `compatibility` field - this harness applies upstream's own
dialect_applies rule at target 2020-12). Point SUITE_DIR at a clone:

    git clone --depth 1 https://github.com/json-schema-org/JSON-Schema-Test-Suite
    SUITE_DIR=$PWD/JSON-Schema-Test-Suite/validation/tests \\
        python3 scripts/sota_suite_check.py

Verdicts compare teptris's exit code against the suite expectation.
Skips are classified, never silent: TOML cannot represent null or
non-table roots; schemas carrying $id resources or remote $refs sit
outside the single-document subset (SCHEMAS.md records the boundary);
format/content divergences are the deliberate policy stances listed
in SCHEMAS.md and are reported, not skipped.
"""
import glob, json, os, re, subprocess, sys, tempfile

TEPTRIS = os.environ.get(
    "TEPTRIS_CLI",
    os.path.join(os.path.dirname(__file__), "../build/cli/teptris"))
D = os.environ.get(
    "SUITE_DIR",
    "/tmp/jsonschemats/validation/tests")
FILES = sys.argv[1:] or sorted(glob.glob(os.path.join(D, "*.json")))


def dialect_applies(compatibility, target):
    # upstream's own rule (validation/check_validation_suite.py)
    is_valid = True
    for constraint in (compatibility.split(",") if compatibility else []):
        match = re.search(r"(?P<operator><=|>=|=)?(?P<version>\d+)", constraint)
        if not match:
            raise ValueError(f"Invalid compatibility string: {compatibility}")
        operator = match.group("operator") or ">="
        version = int(match.group("version"))
        if operator == ">=":
            is_valid = target >= version
        elif operator == "<=":
            is_valid = target <= version
        elif operator == "=":
            is_valid = target == version
        if target <= version:
            break
    return is_valid


def toml_scalar(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return json.dumps(v)
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if v is None:
        return "''"
    raise ValueError


def toml_inline(v):
    if isinstance(v, list):
        return "[" + ", ".join(toml_inline(x) for x in v) + "]"
    if isinstance(v, dict):
        return ("{" + ", ".join(
            f"{toml_key(k)} = {toml_inline(x)}" for k, x in v.items()) + "}")
    return toml_scalar(v)


def toml_key(k):
    if all(("a" <= c <= "z") or ("A" <= c <= "Z") or ("0" <= c <= "9")
           or c in "-_" for c in k) and k and not k[0].isdigit():
        return k
    return json.dumps(k)


def data_to_toml(v):
    if isinstance(v, dict):
        lines = []

        def emit(prefix, d):
            scalars = {k: x for k, x in d.items()
                       if not isinstance(x, (dict, list))
                       or (isinstance(x, list)
                           and not any(isinstance(y, (dict, dict)) for y in x))}
            for k, x in scalars.items():
                lines.append(f"{toml_key(k)} = {toml_inline(x)}")
            for k, x in d.items():
                if k in scalars:
                    continue
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


def contains_null(v):
    if v is None:
        return True
    if isinstance(v, list):
        return any(contains_null(x) for x in v)
    if isinstance(v, dict):
        return any(contains_null(x) for x in v.values())
    return False


def run_case(schema, data, dialect=None):
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
        # keyword-only schemas wrap cleanly; the case's chosen dialect
        # rides the wrap root ($schema is a resource-root keyword —
        # teptris reads the dialect from the ROOT resource only)
        schema = {"$schema": dialect or
                  "https://json-schema.org/draft/2020-12/schema",
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


total = passed = skipped = skipped_draft = skipped_external = 0
failures = []
for f in FILES:
    if not os.path.exists(f):
        continue
    suite = json.load(open(f))
    for case in suite["tests"]:
        compat = case.get("compatibility")
        if not dialect_applies(compat, 2020):
            skipped_draft += 1
            continue
        if "externalSchemas" in case:
            # the case registers metaschema resources by URI; the
            # single-document subset cannot feed them (the engine's
            # unresolvable-$schema fallback keeps all vocabularies,
            # which flips the $vocabulary semantics under test).
            # Classified, never silent - the $vocabulary gate itself
            # is unit-covered in-tree.
            skipped_external += 1
            continue
        schema = case.get("schema", {})
        if isinstance(schema, dict) and "$schema" not in schema:
            # the unified suite tags each case's format stance:
            # format-assertion cases run under the sanctioned
            # format-assertion dialect (our documented opt-in);
            # everything else rides the declared draft default
            tags = case.get("tags", [])
            dialect = ("https://json-schema.org/draft/2020-12/format-assertion"
                       if "format-assertion" in tags
                       else "https://json-schema.org/draft/2020-12/schema")
            if os.environ.get("SOTA_DEBUG"):
                print("DBG", case["description"][:40], "tags:", tags,
                      "->", dialect.rsplit('/', 1)[1], file=sys.stderr)
            schema = dict(schema)
            schema["$schema"] = dialect
        for t in case["tests"]:
            rc = run_case(schema, t["data"], dialect)
            if rc == -1:
                skipped += 1
                continue
            total += 1
            ok = (rc == 0) == t["valid"]
            if ok:
                passed += 1
            else:
                failures.append((os.path.basename(f), case["description"],
                                 json.dumps(t["data"])[:50], t["valid"], rc))

print(f"passed {passed}/{total}, skipped {skipped} "
      f"(other-draft cases not applicable to 2020-12: {skipped_draft}, "
      f"external-schema registration: {skipped_external})")
for x in failures[:200]:
    print("FAIL", x)
sys.exit(1 if failures else 0)
