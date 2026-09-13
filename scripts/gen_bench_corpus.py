#!/usr/bin/env python3
"""Deterministic benchmark corpus for teptris (TODO.impl/09).

Fixed seed: identical bytes on every run. Shapes mirror the yeptris
item-18 matrix (scalar-heavy, table-heavy, deep, arrays, datetimes,
mixed, real-world-ish). All files stay within TOML 1.0 as supported by
every reference library in the matrix (tomlc99 v1.0, toml11 v4.2,
tomlplusplus v3.4).
"""
import os
import random
import sys

random.seed(42)
OUT = sys.argv[1] if len(sys.argv) > 1 else "bench-corpus"
os.makedirs(OUT, exist_ok=True)

WORDS = ("alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo "
         "lima mike november oscar papa quebec romeo sierra tango uniform "
         "victor whiskey xray yankee zulu").split()


def write(name, text):
    path = os.path.join(OUT, name)
    with open(path, "w") as f:
        f.write(text)
    print(f"{name}: {len(text)} bytes")


def sentence(n):
    return " ".join(random.choice(WORDS) for _ in range(n))


# --- scalar shapes ---------------------------------------------------------
write("scalar_int.toml",
      "".join(f"k{i} = {random.randint(-2**40, 2**40)}\n" for i in range(60000)))

write("scalar_float.toml",
      "".join(f"k{i} = {random.uniform(-1e6, 1e6):.6f}e{random.randint(-3, 3)}\n"
              for i in range(60000)))

write("scalar_string.toml",
      "".join(f'k{i} = "{sentence(random.randint(3, 8))}"\n' for i in range(30000)))

# --- table-heavy -----------------------------------------------------------
parts = []
for i in range(15000):
    parts.append(f"[section_{i}]")
    parts.append(f'name = "sec{i}"')
    parts.append(f"count = {i}")
    parts.append(f"ratio = {i}.25")
    parts.append(f"enabled = {'true' if i % 2 == 0 else 'false'}")
    parts.append("")
write("table_heavy.toml", "\n".join(parts))

# --- deep tables -----------------------------------------------------------
parts = []
for i in range(4000):
    branch = f"br_{i % 40}"
    parts.append(f"[deep.root_{i % 100}.{branch}.node_{i}]")
    parts.append(f"id = {i}")
    parts.append(f'label = "{sentence(2)}"')
    parts.append("")
write("deep_tables.toml", "\n".join(parts))

# --- arrays ----------------------------------------------------------------
parts = []
for i in range(300):
    nums = ", ".join(str(random.randint(0, 10**6)) for _ in range(256))
    parts.append(f"arr_{i} = [{nums}]")
    parts.append("")
for i in range(100):
    strs = ", ".join(f'"{sentence(3)}"' for _ in range(64))
    parts.append(f"strs_{i} = [{strs}]")
    parts.append("")
for i in range(50):
    mixed = (f"{random.randint(0, 999)}, {random.uniform(0, 1):.4f}, "
             f'"{random.choice(WORDS)}", '
             f"{'true' if random.random() < 0.5 else 'false'}")
    parts.append(f"mixed_{i} = [{mixed}, {mixed}, {mixed}]")
    parts.append("")
write("array_heavy.toml", "\n".join(parts))

# --- datetimes -------------------------------------------------------------
parts = []
for i in range(40000):
    y, mo, d = 2024, random.randint(1, 12), random.randint(1, 28)
    h, mi, s = random.randint(0, 23), random.randint(0, 59), random.randint(0, 59)
    kind = i % 6
    if kind == 0:
        v = f"{y:04d}-{mo:02d}-{d:02d}T{h:02d}:{mi:02d}:{s:02d}Z"
    elif kind == 1:
        v = f"{y:04d}-{mo:02d}-{d:02d}T{h:02d}:{mi:02d}:{s:02d}+07:30"
    elif kind == 2:
        v = f"{y:04d}-{mo:02d}-{d:02d}T{h:02d}:{mi:02d}:{s:02d}.{random.randint(100000, 999999)}"
    elif kind == 3:
        v = f"{y:04d}-{mo:02d}-{d:02d}"
    elif kind == 4:
        v = f"{h:02d}:{mi:02d}:{s:02d}.{random.randint(1, 999):03d}"
    else:
        v = f"{y:04d}-{mo:02d}-{d:02d} {h:02d}:{mi:02d}:{s:02d}"
    parts.append(f"t{i} = {v}")
write("datetime_heavy.toml", "\n".join(parts) + "\n")

# --- mixed realistic -------------------------------------------------------
parts = [
    "# Mixed realistic document",
    'title = "Service Configuration"',
    'version = "3.1.4"',
    "debug = false",
    "max_connections = 4096",
    "timeout_seconds = 30.5",
    "startup = 2026-09-12T08:00:00Z",
    "",
    "[server]",
    'host = "0.0.0.0"',
    "port = 8080",
    "ports = [8080, 8081, 8082]",
    'motd = """',
    "Welcome to the mixed",
    "benchmark document. Lines are folded manually.",
    '"""',
    "",
    "[server.tls]",
    "enabled = true",
    'cert = "/etc/ssl/cert.pem"',
    'ciphers = ["TLS1.3", "TLS1.2"]',
    "",
]
for i in range(3000):
    parts.append(f"[[items]]")
    parts.append(f"id = {i}")
    parts.append(f'name = "{sentence(random.randint(1, 4))}"')
    parts.append(f"price = {random.uniform(1, 500):.2f}")
    parts.append(f"tags = [\"{random.choice(WORDS)}\", \"{random.choice(WORDS)}\"]")
    parts.append(f"meta = {{ created = 2026-01-{random.randint(10, 28)}, weight = {random.uniform(0, 10):.3f} }}")
    parts.append(f"available = {'true' if random.random() < 0.8 else 'false'}")
    parts.append("")
parts += [
    "[owners.primary]",
    "name = \"Ada\"",
    "since = 2019-04-01",
    "roles = [\"admin\", \"deploy\"]",
    "",
    "[owners.backup]",
    "name = \"Grace\"",
    "since = 2020-11-30",
    "",
]
write("mixed.toml", "\n".join(parts))

# --- cargo-like ------------------------------------------------------------
parts = [
    "[package]",
    'name = "bench-crate"',
    'version = "0.7.2"',
    'edition = "2021"',
    'description = "A benchmark-shaped cargo manifest"',
    "",
]
for i in range(4000):
    dep = f"{random.choice(WORDS)}-{i}"
    parts.append(f"[dependencies.{dep}]")
    parts.append(f'version = "{random.randint(0, 9)}.{random.randint(0, 20)}.{random.randint(0, 9)}"')
    parts.append(f'features = ["{random.choice(WORDS)}", "{random.choice(WORDS)}"]')
    parts.append(f"optional = {'true' if i % 3 == 0 else 'false'}")
    parts.append("")
write("cargo_like.toml", "\n".join(parts))
