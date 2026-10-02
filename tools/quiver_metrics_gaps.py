#!/usr/bin/env python3
"""Summarise a Quiver metrics log and locate time the log does not account for.

Usage:
    quiver_metrics_gaps.py LOGFILE [--gap-ms 100] [--top 15]

Every record carries the milliseconds since metrics started as its last field.
Records that bracket a stall - "thumbnail fetch start" and "ready cell" for
instance - therefore measure the stall end to end, and any large gap between two
consecutive records is time spent in code that emitted nothing.

The trailing gap before the final record is the user sitting still, so the last
few records are not treated as a stall.
"""

import argparse
import collections
import sys

MARKER = "QUIVER-METRIC"


class Record:
    __slots__ = ("seq", "scope", "name", "value", "unit", "elapsed")

    def __init__(self, seq, scope, name, value, unit, elapsed):
        self.seq = seq
        self.scope = scope
        self.name = name
        self.value = value
        self.unit = unit
        self.elapsed = elapsed

    @property
    def key(self):
        return "%s/%s" % (self.scope, self.name)

    def __repr__(self):
        return "%s=%g%s@%g" % (self.key, self.value, self.unit, self.elapsed)


def load(path):
    records = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not line.startswith(MARKER):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 6:
                continue
            seq, scope, name, value, unit = parts[1:6]
            # the elapsed column was added later; older logs lack it
            elapsed = float(parts[6]) if len(parts) > 6 else float("nan")
            try:
                records.append(
                    Record(int(seq), scope, name, float(value), unit, elapsed)
                )
            except ValueError:
                continue
    return records


def has_timing(records):
    return any(r.elapsed == r.elapsed for r in records)


def describe(record):
    if record is None:
        return "(start of log)"
    return "#%d %s=%g%s @%.1fms" % (
        record.seq,
        record.key,
        record.value,
        record.unit,
        record.elapsed,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    parser.add_argument("--gap-ms", type=float, default=100.0,
                        help="report gaps at least this long (default: 100)")
    parser.add_argument("--top", type=int, default=15,
                        help="how many gaps to show (default: 15)")
    args = parser.parse_args()

    records = load(args.log)
    if not records:
        print("no %s records found in %s" % (MARKER, args.log))
        return 1

    print("loaded %d records from %s" % (len(records), args.log))
    if not has_timing(records):
        print("NOTE: log predates the timestamp column, so gaps cannot be found.")
        print("      durations below are still valid; recapture for gap analysis.")
        print()
        return summarise_only(records)

    span = records[-1].elapsed - records[0].elapsed
    print("wall span %.1f ms\n" % span)

    print("== unaccounted time: largest gaps between consecutive records ==")
    gaps = []
    for before, after in zip(records, records[1:]):
        delta = after.elapsed - before.elapsed
        if delta >= args.gap_ms:
            gaps.append((delta, before, after))
    gaps.sort(reverse=True, key=lambda item: item[0])

    if not gaps:
        print("  no gap of %.0f ms or more: every stall is inside a record"
              % args.gap_ms)
    else:
        for delta, before, after in gaps[:args.top]:
            print("  %8.1f ms   %s" % (delta, describe(before)))
            print("  %8s %s" % ("", describe(after)))
            print()

    brackets = report_brackets(records)
    summarise_only(records)
    return 0 if (gaps or brackets) else 0


def report_brackets(records):
    """Time between the icon cell's start/ready/paint marks, per fetch."""
    names = {
        "thumbnail fetch start": 0,
        "thumbnail ready": 1,
        "ready cell": 1,
        "thumbnail missing": 2,
        "missing cell": 2,
        "regressed cell": 3,
    }
    phases = collections.OrderedDict()
    current = None
    for record in records:
        if record.scope != "iconcell":
            continue
        if record.name not in names:
            continue
        if record.name == "thumbnail fetch start":
            current = {"cell": record.value,
                       "thumbnail fetch start": record.elapsed}
            continue
        if current is None:
            continue
        current[record.name] = record.elapsed
        if record.name in ("ready cell", "missing cell", "regressed cell"):
            total = record.elapsed - current["thumbnail fetch start"]
            current["total"] = total
            phases[record.seq] = current
            current = None

    if not phases:
        print("== icon cell fetch brackets: none in this log =\n")
        return 0

    ready = [d["total"] for d in phases.values() if "ready cell" in d]
    if ready:
        ready_sorted = sorted(ready)
        print("== icon cell fetch, start to ready ==")
        print("  %d fetches, worst %.1f ms, median %.1f ms, total %.1f ms"
              % (len(ready), ready_sorted[-1],
                 ready_sorted[len(ready_sorted) // 2], sum(ready)))
        print("  first ready for cell %g at %.1f ms\n"
              % (phases[next(iter(phases))]["cell"],
                 phases[next(iter(phases))]["total"]))
    else:
        print("== icon cell fetch brackets: no cell became ready =\n")
    return len(phases)


def summarise_only(records):
    """Worst and total for every sampled (timed) metric."""
    counts = collections.Counter()
    totals = collections.defaultdict(float)
    worst = collections.defaultdict(float)
    latest = {}
    counted = set()

    for record in records:
        counts[record.key] += 1
        if record.unit == "count":
            latest[record.key] = record.value
            continue
        if record.unit == "mark":
            continue
        totals[record.key] += record.value
        worst[record.key] = max(worst[record.key], record.value)
        counted.add(record.key)

    if counted:
        print("== slowest sampled metrics ==")
        for key in sorted(counted, key=lambda k: -worst[k])[:15]:
            print("  %-46s n=%-6d worst %9.2f ms  total %9.2f ms"
                  % (key, counts[key], worst[key], totals[key]))
        print()

    if latest:
        print("== cumulative counters ==")
        for key in sorted(latest):
            print("  %-46s %10.0f" % (key, latest[key]))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
