#!/usr/bin/env python3
"""Regenerate src/teptris/schema/unicode_props.c from the Python
unicode database: the general categories (long + short names, shared
tables), plus Any / ASCII / White_Space / Alphabetic / Uppercase /
Lowercase for the schema regex's \\p{...} support."""
import os
import unicodedata

MAX_CP = 0x110000
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "teptris", "schema",
                   "unicode_props.c")


def ranges_for(pred):
    out = []
    start = None
    for cp in range(MAX_CP):
        if pred(cp):
            if start is None:
                start = cp
        else:
            if start is not None:
                out.append((start, cp - 1))
                start = None
    if start is not None:
        out.append((start, MAX_CP - 1))
    return out


groups = {
    "Letter": ("L", {"Lu", "Ll", "Lt", "Lm", "Lo"}),
    "Uppercase_Letter": ("Lu", {"Lu"}),
    "Lowercase_Letter": ("Ll", {"Ll"}),
    "Titlecase_Letter": ("Lt", {"Lt"}),
    "Modifier_Letter": ("Lm", {"Lm"}),
    "Other_Letter": ("Lo", {"Lo"}),
    "Mark": ("M", {"Mn", "Mc", "Me"}),
    "Nonspacing_Mark": ("Mn", {"Mn"}),
    "Spacing_Mark": ("Mc", {"Mc"}),
    "Enclosing_Mark": ("Me", {"Me"}),
    "Number": ("N", {"Nd", "Nl", "No"}),
    "Decimal_Number": ("Nd", {"Nd"}),
    "Letter_Number": ("Nl", {"Nl"}),
    "Other_Number": ("No", {"No"}),
    "Punctuation": ("P", {"Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po"}),
    "Connector_Punctuation": ("Pc", {"Pc"}),
    "Dash_Punctuation": ("Pd", {"Pd"}),
    "Open_Punctuation": ("Ps", {"Ps"}),
    "Close_Punctuation": ("Pe", {"Pe"}),
    "Initial_Punctuation": ("Pi", {"Pi"}),
    "Final_Punctuation": ("Pf", {"Pf"}),
    "Other_Punctuation": ("Po", {"Po"}),
    "Symbol": ("S", {"Sm", "Sc", "Sk", "So"}),
    "Math_Symbol": ("Sm", {"Sm"}),
    "Currency_Symbol": ("Sc", {"Sc"}),
    "Modifier_Symbol": ("Sk", {"Sk"}),
    "Other_Symbol": ("So", {"So"}),
    "Separator": ("Z", {"Zs", "Zl", "Zp"}),
    "Space_Separator": ("Zs", {"Zs"}),
    "Line_Separator": ("Zl", {"Zl"}),
    "Paragraph_Separator": ("Zp", {"Zp"}),
    "Control": ("Cc", {"Cc"}),
    "Format": ("Cf", {"Cf"}),
    "Private_Use": ("Co", {"Co"}),
}

member_tables = {}
tables = {}
for long_name, (short, members) in groups.items():
    key = frozenset(members)
    if key not in member_tables:
        member_tables[key] = ranges_for(
            lambda cp: unicodedata.category(chr(cp)) in members)
    tables[long_name] = member_tables[key]
    tables[short] = member_tables[key]

tables["White_Space"] = ranges_for(lambda cp: chr(cp).isspace())
tables["Alphabetic"] = sorted(set(tables["L"]) | set(tables["Nl"]))
tables["Uppercase"] = tables["Lu"]
tables["Lowercase"] = tables["Ll"]
tables["Any"] = [(0, MAX_CP - 1)]
tables["ASCII"] = [(0, 0x7F)]

names = sorted(tables.keys())
uniq = {}
sym_of = {}
for name in names:
    key = tuple(tables[name])
    if key not in uniq:
        uniq[key] = f"uc_{len(uniq)}_ranges"
    sym_of[name] = uniq[key]

lines = [
    "/* Unicode property ranges for \\p{...} (ECMA-262 unicode mode):",
    " * generated from the Unicode character database by",
    " * scripts/gen_unicode_props.py - edit the script, not this file. */",
    '#include "unicode_props.h"',
    "",
]
for key, sym in uniq.items():
    lines.append(f"static const teptris_uc_range {sym}[] = {{")
    lines.append("\n".join(f"    {{0x{lo:X}, 0x{hi:X}}}," for lo, hi in key))
    lines.append("};")
    lines.append("")
lines.append("const teptris_uc_prop teptris_unicode_props[] = {")
for name in names:
    lines.append(f'    {{"{name}", {sym_of[name]}, {len(tables[name])}}},')
lines.append("};")
lines.append("")
lines.append(f"const int teptris_n_unicode_props = {len(names)};")
with open(OUT, "w") as f:
    f.write("\n".join(lines) + "\n")
print(f"{len(names)} properties, {len(uniq)} tables written")
