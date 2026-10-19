#!/usr/bin/env python3
"""Differential oracle: teptris vs tomllib (stdlib TOML 1.0 reference).

For every input file both parsers must produce IDENTICAL value trees,
compared through teptris's natural-JSON view:

  teptris side  : `teptris to-json --natural` -> json.loads
  tomllib side  : parsed tree normalized to the same host shape
                  (datetimes rendered in teptris's canonical RFC 3339
                  forms, truncated to microsecond precision - the
                  engine keeps nanoseconds, Python cannot hold them;
                  nan/inf -> None like the natural JSON's lossy
                  mapping)

tomllib implements TOML 1.0 strictly. Inputs teptris accepts and
tomllib rejects are counted "1.1-only" (the draft grammar is a strict
1.0 superset - inline-table newlines, trailing commas, optional
seconds, \\e/\\xNN escapes) and skipped, not scored.

Exit 0 = zero divergences. Divergences print the file and a value-path
diff and exit 1.

Usage: differential.py <teptris-cli> <dir-or-file>...
"""
import json
import math
import re
import subprocess
import sys
from datetime import date, datetime, time, timedelta
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # py<3.11 dev hosts
    import tomli as tomllib

FRAC = re.compile(r"(\.\d+)")


def render_dt(v):
    """tomllib datetime -> teptris natural-JSON canonical string.
    teptris renders UTC as Z and strips trailing fraction zeros."""
    if isinstance(v, datetime):
        base = f"{v.year:04d}-{v.month:02d}-{v.day:02d}T"
        base += f"{v.hour:02d}:{v.minute:02d}:{v.second:02d}"
        frac = f"{v.microsecond:06d}".rstrip("0")
        if frac:
            base += f".{frac}"
        if v.tzinfo is not None:
            total = int(v.utcoffset().total_seconds())
            if total == 0:
                base += "Z"
            else:
                sign = "+" if total > 0 else "-"
                total = abs(total)
                base += f"{sign}{total // 3600:02d}:{(total % 3600) // 60:02d}"
        return base
    if isinstance(v, date):
        return f"{v.year:04d}-{v.month:02d}-{v.day:02d}"
    frac = f"{v.microsecond:06d}".rstrip("0")
    return f"{v.hour:02d}:{v.minute:02d}:{v.second:02d}" + (f".{frac}" if frac else "")


def canon_dt_strings(v):
    """Truncate RFC 3339-looking strings to microsecond fractions so a
    nanosecond datetime from teptris compares equal to tomllib's
    microsecond-holding value. Only applied to strings that carry a
    full-date 'T' time form - plain TOML strings never match the
    strict shape unless they really are RFC 3339 text."""
    if isinstance(v, dict):
        return {k: canon_dt_strings(x) for k, x in v.items()}
    if isinstance(v, list):
        return [canon_dt_strings(x) for x in v]
    if isinstance(v, str):
        m = re.fullmatch(
            r"(\d{4}-\d{2}-\d{2}[Tt]\d{2}:\d{2}:\d{2})(\.\d+)?"
            r"([Zz]|[+-]\d{2}:\d{2})?", v)
        if m:
            frac = (m.group(2) or "").rstrip("0")[:7]
            if frac == ".":
                frac = ""
            return m.group(1) + frac + (m.group(3) or "").upper().replace(
                "T", "T")
        m = re.fullmatch(r"(\d{2}:\d{2}:\d{2})(\.\d+)?", v)
        if m:
            frac = (m.group(2) or "").rstrip("0")[:7]
            if frac == ".":
                frac = ""
            return m.group(1) + frac
    return v


def norm_float(x):
    if isinstance(x, float) and not math.isfinite(x):
        return None
    return x


def norm_tomllib(v):
    if isinstance(v, bool):
        return v
    if isinstance(v, dict):
        return {k: norm_tomllib(x) for k, x in v.items()}
    if isinstance(v, list):
        return [norm_tomllib(x) for x in v]
    if isinstance(v, (datetime, date, time)):
        return render_dt(v)
    if isinstance(v, float):
        return norm_float(v)
    return v


def norm_json(v):
    if isinstance(v, dict):
        return {k: norm_json(x) for k, x in v.items()}
    if isinstance(v, list):
        return [norm_json(x) for x in v]
    if isinstance(v, float):
        return norm_float(v)
    return v


def diff_path(a, b, path, out):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                out.append(f"{path}.{k}: missing on teptris side")
            elif k not in b:
                out.append(f"{path}.{k}: missing on tomllib side")
            else:
                diff_path(a[k], b[k], f"{path}.{k}", out)
            if len(out) > 4:
                return
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append(f"{path}: len {len(a)} vs {len(b)}")
            return
        for i, (x, y) in enumerate(zip(a, b)):
            diff_path(x, y, f"{path}[{i}]", out)
            if len(out) > 4:
                return
    elif a != b and repr(a) != repr(b):
        out.append(f"{path}: teptris={a!r} tomllib={b!r}")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cli = Path(sys.argv[1]).resolve()
    files = []
    for arg in sys.argv[2:]:
        p = Path(arg)
        if p.is_dir():
            files.extend(sorted(p.rglob("*.toml")))
        elif p.is_file():
            files.append(p)
        else:
            print(f"differential: no such path {arg}", file=sys.stderr)
            return 2

    ok = skip11 = skippy = fail = 0
    for f in files:
        data = f.read_bytes()
        try:
            ref = tomllib.loads(data.decode("utf-8"))
        except (tomllib.TOMLDecodeError, UnicodeDecodeError,
                ValueError, OverflowError):
            skippy += 1
            continue
        try:
            out = subprocess.run(
                [str(cli), "to-json", "--natural", str(f)],
                capture_output=True, timeout=60)
        except subprocess.TimeoutExpired:
            print(f"DIVERGENCE {f}: teptris timeout")
            fail += 1
            continue
        if out.returncode != 0:
            print(f"DIVERGENCE {f}: teptris rejects, tomllib accepts "
                  f"({out.stderr.decode()[:120]})")
            fail += 1
            continue
        got = norm_json(json.loads(out.stdout.decode("utf-8")))
        exp = canon_dt_strings(norm_tomllib(ref))
        diffs = []
        diff_path(got, exp, "$", diffs)
        if diffs:
            print(f"DIVERGENCE {f}:")
            for d in diffs[:5]:
                print(f"  {d}")
            fail += 1
        else:
            ok += 1

    print(f"differential: {ok} agree, {skip11} 1.1-only-or-rejected, "
          f"{skippy} tomllib-cannot-represent, {fail} divergences "
          f"({len(files)} inputs)")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
