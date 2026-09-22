#!/usr/bin/env python3
"""Interleaved A/B benchmark comparison (TODO.impl/09 companion).

Compares two bench_parse JSON result sets and prints per-shape medians,
deltas, ratio-vs-best-competitor, and the per-round values so the
round-to-round scatter is visible. Exit code is always 0: the
ship/revert call is a human decision against the scatter, not a
threshold this script can own.

usage: bench_ab.py [--ref sweep.json] <a.json ...> <b.json ...>
                   (equal a/b counts)

--ref: a full-matrix bench_parse run for the ratio columns. The CI lane
runs teptris-only A/B rounds (competitors are identical pinned sources
in both trees) plus one full sweep passed here; without --ref the
ratios come from competitor entries inside each round file (dev
machine, where every round carries the full matrix).
"""
import argparse
import json
import statistics


def agg(data, i, ref):
    vals, rats = [], []
    for d in data:
        f = d["files"][i]
        t = f["libs"]["teptris"]["mb_per_s"]
        vals.append(t)
        denom = best_competitor(ref["files"][i] if ref else f)
        rats.append(t / denom if denom == denom else float("nan"))
    return vals, rats


def best_competitor(f):
    best = [x["mb_per_s"] for k, x in f["libs"].items() if k != "teptris"]
    return max(best) if best else float("nan")


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("-h", "--help", action="help")
    ap.add_argument("--ref", metavar="sweep.json",
                    help="full-matrix sweep for the ratio columns")
    ap.add_argument("jsons", nargs="+", metavar="json")
    args = ap.parse_args()
    n = len(args.jsons) // 2
    if n < 1 or len(args.jsons) % 2:
        ap.error("need equal a/b counts")
    A = [json.load(open(p)) for p in args.jsons[:n]]
    B = [json.load(open(p)) for p in args.jsons[n:]]
    ref = json.load(open(args.ref)) if args.ref else None
    shapes = [f["name"] for f in A[0]["files"]]
    print(f"{'shape':<20} {'A med':>6} {'B med':>6} {'delta':>7}  {'rA':>5} {'rB':>5}")
    for i, s in enumerate(shapes):
        av, ar = agg(A, i, ref)
        bv, br = agg(B, i, ref)
        ma, mb = statistics.median(av), statistics.median(bv)
        ra = statistics.median(ar)
        rbb = statistics.median(br)
        rtxt = f"{ra:5.2f} {rbb:5.2f}" if ra == ra and rbb == rbb else "  n/a   n/a"
        print(f"{s:<20} {ma:6.0f} {mb:6.0f} {mb / ma:6.1%}  {rtxt}")
        print(f"{'':>20} A={','.join(f'{v:.0f}' for v in av)}"
              f"  B={','.join(f'{v:.0f}' for v in bv)}")
    src = f"--ref {args.ref}" if ref else "per-round competitor entries"
    print(f"\n(rA/rB = teptris vs best competitor, {src}; per-round values")
    print(" shown so scatter is visible — deltas inside the A/B round")
    print(" spread are noise, not signal)")


if __name__ == "__main__":
    main()
