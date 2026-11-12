#!/usr/bin/env python3
"""Generate src/teptris/schema/idna_tables.h from the Unicode sources.

Inputs (fetched once, committed nowhere):
  - IdnaMappingTable.txt (UTS #46, nontransitional reading)
  - ArabicShaping.txt (Joining_Type)
  - Scripts.txt (Greek/Hebrew/Hiragana/Katakana/Han context-rule sets)
  - unicodedata (ccc, canonical decompositions, NFC composition oracle)

Run: python3 scripts/gen_idna_tables.py /tmp/idna-src
"""
import sys
import unicodedata as ud

SRC = sys.argv[1] if len(sys.argv) > 1 else "/tmp/idna-src"

ST_VALID, ST_IGNORED, ST_MAPPED, ST_DIS = 0, 1, 2, 3

# ---------------------------------------------------------------- UTS-46 map
entries = []  # (start, end, status, mapped_str)
for line in open(f"{SRC}/IdnaMapping.txt", encoding="utf-8"):
    line = line.split("#")[0].strip()
    if not line:
        continue
    fields = [f.strip() for f in line.split(";")]
    cp_field, status = fields[0], fields[1]
    mapped = fields[2] if len(fields) > 2 else ""
    flags = fields[-1] if len(fields) > 3 else ""
    if ".." in cp_field:
        a, b = [int(x, 16) for x in cp_field.split("..")]
    else:
        a = b = int(cp_field, 16)
    if status == "deviation":
        # deviation: valid in nontransitional processing — the listed
        # mapping applies only in transitional processing, so drop it
        st, mapped = ST_VALID, ""
    elif status == "valid":
        # NV8/XV8: the character is excluded by IDNA2008 — this
        # engine validates to IDNA2008 conformance (the unified
        # suite's stance), so UTS-46-only "valid" rows stay out
        st = ST_DIS if flags in ("NV8", "XV8") else ST_VALID
    elif status == "ignored":
        st = ST_IGNORED
    elif status == "mapped":
        st = ST_MAPPED
    elif status == "disallowed_STD3_valid":
        st = ST_VALID  # non-STD3 processing
    elif status == "disallowed_STD3_mapped":
        st = ST_MAPPED
    elif status == "disallowed":
        st = ST_DIS
    else:
        raise ValueError(status)
    targets = "".join(chr(int(x, 16)) for x in mapped.split() if x)
    entries.append((a, b, st, targets))

# merge adjacent ranges with equal (status, mapped)
merged = []
for e in entries:
    if merged and merged[-1][1] == e[0] - 1 and \
       merged[-1][2] == e[2] and merged[-1][3] == e[3]:
        merged[-1] = (merged[-1][0], e[1], e[2], e[3])
    else:
        merged.append(e)

pool = []
pool_off = 0
rows = []
for a, b, st, targets in merged:
    enc = targets.encode("utf-8")
    if enc:
        rows.append((a, b, st, pool_off, len(enc)))
        pool.append(enc)
        pool_off += len(enc)
    else:
        rows.append((a, b, st, 0, 0))

pool_c = "".join("\\%03o" % byte for enc in pool for byte in enc)

# ---------------------------------------------------------------- bidi classes
CLASS_CODES = {"L": 1, "R": 2, "AL": 3, "AN": 4, "EN": 5, "ES": 6,
               "CS": 7, "ET": 8, "ON": 9, "BN": 10, "NSM": 11}

def cls(cp):
    return CLASS_CODES.get(ud.bidirectional(chr(cp)), 0)

bidi_rows = []
run_start, run_val = None, cls(0)
for cp in range(0x110000):
    v = cls(cp)
    if run_start is None:
        run_start, run_val = cp, v
    elif v != run_val:
        bidi_rows.append((run_start, cp - 1, run_val))
        run_start, run_val = cp, v
bidi_rows.append((run_start, 0x10FFFF, run_val))

# ---------------------------------------------------------------- joining type
JT = {"C": 1, "D": 2, "L": 3, "R": 4, "U": 5, "T": 6}
jt = {}
for line in open(f"{SRC}/ArabicShaping.txt", encoding="utf-8"):
    line = line.split("#")[0].strip()
    if not line or line.startswith("@") or ";" not in line:
        continue
    fields = [f.strip() for f in line.split(";")]
    if len(fields) < 3:
        continue
    jt[int(fields[0], 16)] = JT[fields[2]]

def jval(cp):
    return jt.get(cp, 5)  # default U

join_rows = []
run_start, run_val = None, jval(0)
for cp in range(0x110000):
    v = jval(cp)
    if run_start is None:
        run_start, run_val = cp, v
    elif v != run_val:
        join_rows.append((run_start, cp - 1, run_val))
        run_start, run_val = cp, v
join_rows.append((run_start, 0x10FFFF, run_val))

# ---------------------------------------------------------------- scripts
WANT = {"Greek": 1, "Hebrew": 2, "Hiragana": 3, "Katakana": 4, "Han": 5}
script_rows = []
for line in open(f"{SRC}/Scripts.txt", encoding="utf-8"):
    line = line.split("#")[0].strip()
    if not line:
        continue
    cp_field, script = [f.strip() for f in line.split(";")]
    if script not in WANT:
        continue
    if ".." in cp_field:
        a, b = [int(x, 16) for x in cp_field.split("..")]
    else:
        a = b = int(cp_field, 16)
    script_rows.append((a, b, WANT[script]))
script_rows.sort()

# ---------------------------------------------------------------- ccc + virama
def cval(cp):
    return ud.combining(chr(cp))

ccc_rows = []
run_start, run_val = None, 0
for cp in range(0x110000):
    v = cval(cp)
    if run_start is None:
        run_start, run_val = cp, v
    elif v != run_val:
        if run_val:
            ccc_rows.append((run_start, cp - 1, run_val))
        run_start, run_val = cp, v
if run_val:
    ccc_rows.append((run_start, 0x10FFFF, run_val))
viramas = [cp for cp in range(0x110000) if cval(cp) == 9]

# gc=M (UTS-46 validity criterion 6: no leading combining mark)
mark_rows = []
run_start = None
for cp in range(0x110000):
    is_mark = ud.category(chr(cp)) in ("Mn", "Mc", "Me")
    if run_start is None:
        run_start, run_val = cp, is_mark
    elif is_mark != run_val:
        if run_val:
            mark_rows.append((run_start, cp - 1))
        run_start, run_val = cp, is_mark
if run_val:
    mark_rows.append((run_start, 0x10FFFF))

# ---------------------------------------------------------------- NFC tables
def canon_decomp(cp):
    d = ud.decomposition(chr(cp))
    if not d or d.startswith("<"):
        return None
    return [int(x, 16) for x in d.split()]

decomp_rows = []   # (cp, seq_off, seq_len) over nfc_seq_pool (fully expanded)
comp_rows = []     # (first, second, composed)
seq_pool = []
seq_off = 0
for cp in range(0x110000):
    d = canon_decomp(cp)
    if d is None:
        continue
    changed = True
    while changed:
        changed = False
        nd = []
        for x in d:
            s = canon_decomp(x)
            if s:
                nd.extend(s)
                changed = True
            else:
                nd.append(x)
        d = nd
    decomp_rows.append((cp, seq_off, len(d)))
    seq_pool.extend(d)
    seq_off += len(d)
    if len(d) == 2:
        a, b = chr(d[0]), chr(d[1])
        if ud.normalize("NFC", a + b) == chr(cp):
            comp_rows.append((d[0], d[1], cp))

# ---------------------------------------------------------------- emit
def hexcp(x):
    return "0x%04X" % x if x < 0x110000 else "0x%06X" % x

out = []
out.append("/* Generated by scripts/gen_idna_tables.py - do not edit. */")
out.append("#pragma once")
out.append("")
out.append("/* UTS #46 statuses */")
out.append("#define IDNA_VALID 0")
out.append("#define IDNA_IGNORED 1")
out.append("#define IDNA_MAPPED 2")
out.append("#define IDNA_DISALLOWED 3")
out.append("")
out.append("/* bidi classes (0 = uninterested) */")
out.append("enum { B_NONE = 0, B_L = 1, B_R = 2, B_AL = 3, B_AN = 4,")
out.append("       B_EN = 5, B_ES = 6, B_CS = 7, B_ET = 8, B_ON = 9,")
out.append("       B_BN = 10, B_NSM = 11 };")
out.append("")
out.append("/* joining types (default U) */")
out.append("enum { J_U = 5, J_R = 4, J_L = 3, J_D = 2, J_T = 6 };")
out.append("")
out.append("/* context-rule scripts */")
out.append("enum { SC_GREEK = 1, SC_HEBREW = 2, SC_HIRAGANA = 3,")
out.append("       SC_KATAKANA = 4, SC_HAN = 5 };")
out.append("")
out.append("static const unsigned char idna_map_pool[] = \"%s\";" % pool_c)
out.append("")
out.append("typedef struct { unsigned first, last; unsigned char st;")
out.append("                 unsigned off, len; } idna_map_row;")
out.append("static const idna_map_row idna_map[] = {")
for a, b, st, off, ln in rows:
    out.append("    {%s,%s,%d,%u,%u}," % (hexcp(a), hexcp(b), st, off, ln))
out.append("};")
out.append("")
out.append("typedef struct { unsigned first, last; unsigned char v; }")
out.append("    idna_range_row;")
out.append("static const idna_range_row idna_bidi[] = {")
for a, b, v in bidi_rows:
    out.append("    {%s,%s,%d}," % (hexcp(a), hexcp(b), v))
out.append("};")
out.append("")
out.append("static const idna_range_row idna_join[] = {")
for a, b, v in join_rows:
    out.append("    {%s,%s,%d}," % (hexcp(a), hexcp(b), v))
out.append("};")
out.append("")
out.append("static const idna_range_row idna_script[] = {")
for a, b, v in script_rows:
    out.append("    {%s,%s,%d}," % (hexcp(a), hexcp(b), v))
out.append("};")
out.append("")
out.append("static const idna_range_row idna_ccc[] = {")
for a, b, v in ccc_rows:
    out.append("    {%s,%s,%d}," % (hexcp(a), hexcp(b), v))
out.append("};")
out.append("")
out.append("static const unsigned idna_virama[] = {")
out.append("    " + ",".join(hexcp(c) for c in viramas))
out.append("};")
out.append("")
out.append("typedef struct { unsigned first, last; } idna_mark_row;")
out.append("static const idna_mark_row idna_mark[] = {")
for a, b in mark_rows:
    out.append("    {%s,%s}," % (hexcp(a), hexcp(b)))
out.append("};")
out.append("")
out.append("static const unsigned idna_seq_pool[] = {")
out.append("    " + ",".join(str(x) for x in seq_pool))
out.append("};")
out.append("")
out.append("typedef struct { unsigned cp; unsigned short off, len; }")
out.append("    idna_decomp_row;")
out.append("static const idna_decomp_row idna_decomp[] = {")
for cp, off, ln in decomp_rows:
    out.append("    {%s,%u,%u}," % (hexcp(cp), off, ln))
out.append("};")
out.append("")
out.append("typedef struct { unsigned a, b, cp; } idna_comp_row;")
out.append("static const idna_comp_row idna_comp[] = {")
for a, b, cp in comp_rows:
    out.append("    {%s,%s,%s}," % (hexcp(a), hexcp(b), hexcp(cp)))
out.append("};")
out.append("")
out.append("static const size_t idna_map_len = %d;" % len(rows))
out.append("static const size_t idna_bidi_len = %d;" % len(bidi_rows))
out.append("static const size_t idna_join_len = %d;" % len(join_rows))
out.append("static const size_t idna_script_len = %d;" % len(script_rows))
out.append("static const size_t idna_ccc_len = %d;" % len(ccc_rows))
out.append("static const size_t idna_virama_len = %d;" % len(viramas))
out.append("static const size_t idna_mark_len = %d;" % len(mark_rows))
out.append("static const size_t idna_decomp_len = %d;" % len(decomp_rows))
out.append("static const size_t idna_comp_len = %d;" % len(comp_rows))
out.append("")

path = "src/teptris/schema/idna_tables.h"
with open(path, "w") as f:
    f.write("\n".join(out))
print("wrote", path)
print("map rows:", len(rows), "bidi:", len(bidi_rows), "join:",
      len(join_rows), "script:", len(script_rows), "ccc:", len(ccc_rows),
      "virama:", len(viramas), "decomp:", len(decomp_rows),
      "comp:", len(comp_rows))
