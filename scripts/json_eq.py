"""Structural comparison of toml-test JSON with the official runner's
semantics (corpus json.go):
  - floats compare NUMERICALLY (any round-trip spelling; nan == nan)
  - datetimes parse to their instants (space/t/z normalized like
    datetimeRepl, fractions normalized to nanoseconds)
  - everything else compares as exact strings (bools case-insensitive)

The previous exact-equality comparison was stricter than the contract
and failed numerically-identical encodings ("300.0" vs "300").
"""
import json
import re
import sys

DT = re.compile(
    r"^(\d{4})-(\d{2})-(\d{2})[Tt ](\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?"
    r"(Z|z|[+-]\d{2}:\d{2})?$")
D = re.compile(r"^(\d{4})-(\d{2})-(\d{2})$")
T = re.compile(r"^(\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?$")


def _frac(ns_str):
    if not ns_str:
        return 0
    return int(ns_str.ljust(9, "0")[:9])


def _days_from_civil(y, m, d):
    y -= m <= 2
    era = (y if y >= 0 else y - 399) // 400
    yoe = y - era * 400
    doy = (153 * (m - 3 if m > 2 else m + 9) + 2) // 5 + d - 1
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468


def _instant(g):
    y, mo, d, h, mi, s, ns, off = g
    days = _days_from_civil(int(y), int(mo), int(d))
    secs = days * 86400 + int(h) * 3600 + int(mi) * 60 + int(s)
    if off and off not in ("Z", "z"):
        sign = 1 if off[0] == "+" else -1
        oh, om = int(off[1:3]), int(off[4:6])
        secs -= sign * (oh * 3600 + om * 60)
    return secs, _frac(ns)


def parse_dt(kind, v):
    if kind == "datetime":
        m = DT.match(v)
        return ("dt",) + _instant(m.groups()) if m else None
    if kind == "datetime-local":
        m = DT.match(v)
        if not m or m.group(8):
            return None
        g = m.groups()
        return ("dtl",) + g[:6] + (_frac(g[6]),)
    if kind == "date-local":
        m = D.match(v)
        return ("d",) + m.groups() if m else None
    if kind == "time-local":
        m = T.match(v)
        return ("t", m.group(1), m.group(2), m.group(3), _frac(m.group(4))) \
            if m else None
    return None


def leaf_eq(t, a, b):
    if t == "float":
        a, b = a.lower(), b.lower()
        if a.endswith("nan") or b.endswith("nan"):
            return a.lstrip("+-") == b.lstrip("+-")
        try:
            return float(a) == float(b)
        except ValueError:
            return False
    if t in ("datetime", "datetime-local", "date-local", "time-local"):
        return parse_dt(t, a) == parse_dt(t, b)
    if t == "bool":
        return a.lower() == b.lower()
    return a == b


def eq(a, b):
    if isinstance(a, dict) and isinstance(b, dict):
        if set(a) != set(b):
            return False
        if set(a) == {"type", "value"}:
            return a["type"] == b["type"] and leaf_eq(a["type"], a["value"],
                                                      b["value"])
        return all(eq(v, b[k]) for k, v in a.items())
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(eq(x, y) for x, y in zip(a, b))
    return a == b


got = json.load(open(sys.argv[1]))
want = json.load(open(sys.argv[2]))
sys.exit(0 if eq(got, want) else 1)
