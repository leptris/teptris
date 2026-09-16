#!/usr/bin/env python3
"""Interleaved A/B benchmark comparison (TODO.impl/09 companion).

Compares two bench_parse JSON result sets and prints per-shape medians,
deltas, ratio-vs-best-competitor, and the per-round values so the
round-to-round scatter is visible. Exit code is always 0: the
ship/revert call is a human decision against the scatter, not a
threshold this script can own.

usage: bench_ab.py <a.json ...> <b.json ...>   (equal counts)
"""
import json
import statistics
import sys


def agg(data, i):
    vals, rats = [], []
    for d in data:
        f = d["files"][i]
        t = f["libs"]["teptris"]["mb_per_s"]
        vals.append(t)
        # competitor libraries are external checkouts (dev machine only);
        # CI runs teptris-only and the ratio becomes n/a — the A/B delta
        # is the lane's product, ratios track the 4x mandate locally
        best = [x["mb_per_s"] for k, x in f["libs"].items() if k != "teptris"]
        rats.append(t / max(best) if best else float("nan"))
    return vals, rats


def main():
    n = (len(sys.argv) - 1) // 2
    if n < 1:
        sys.exit(__doc__)
    A = [json.load(open(p)) for p in sys.argv[1:1 + n]]
    B = [json.load(open(p)) for p in sys.argv[1 + n:]]
    shapes = [f["name"] for f in A[0]["files"]]
    print(f"{'shape':<20} {'A med':>6} {'B med':>6} {'delta':>7}  {'rA':>5} {'rB':>5}")
    for i, s in enumerate(shapes):
        av, ar = agg(A, i)
        bv, br = agg(B, i)
        ma, mb = statistics.median(av), statistics.median(bv)
        ra = statistics.median(ar)
        rb = statistics.median(br)
        rtxt = f"{ra:5.2f} {rb:5.2f}" if ra == ra and rb == rb else "  n/a   n/a"
        print(f"{s:<20} {ma:6.0f} {mb:6.0f} {mb / ma:6.1%}  {rtxt}")
        print(f"{'':>20} A={','.join(f'{v:.0f}' for v in av)}"
              f"  B={','.join(f'{v:.0f}' for v in bv)}")
    print("\n(rA/rB = teptris vs best competitor, median across rounds;")
    print(" per-round values shown so scatter is visible — deltas inside")
    print(" the A/B round spread are noise, not signal)")


if __name__ == "__main__":
    main()
