#!/usr/bin/env python3
"""Per-component behavior report from .ts.txt event logs.

Usage:
    python3 scripts/analyze_ts.py <run-dir> [<run-dir> ...]

See /home/ubuntu/.claude/plans/replicated-tinkering-horizon.md for the
metric catalog. Stdlib only.
"""

import argparse
import math
import os
import sys
from collections import defaultdict, namedtuple
from glob import glob

OP_KINDS = ("PUSH", "PULL", "PULL_WAIT", "FLUSH", "FLUSH_WAIT")

# Pipeline order for display: DAQ → DIST → SIRT → DEN. Unknown components
# sort after these alphabetically.
COMPONENT_ORDER = ("daq", "dist", "sirt", "den")


def component_sort_key(comp):
    c = comp.lower()
    if c in COMPONENT_ORDER:
        return (0, COMPONENT_ORDER.index(c))
    return (1, c)

Op = namedtuple("Op", ["kind", "topic", "start_ns", "end_ns", "data_size", "event_id", "rank"])


# ---------- parsing ----------

def parse_attrs(s):
    out = {}
    for tok in s.split(","):
        tok = tok.strip()
        if not tok or "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        out[k] = v
    return out


def parse_ts_file(path):
    """Return a list of (ts_ns, evtype, attrs) sorted by ts."""
    events = []
    warnings = 0
    with open(path) as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            parts = line.split(None, 2)
            if len(parts) < 2:
                warnings += 1
                continue
            try:
                ts = int(parts[0])
            except ValueError:
                warnings += 1
                continue
            evtype = parts[1]
            attrs = parse_attrs(parts[2]) if len(parts) > 2 else {}
            events.append((ts, evtype, attrs))
    if warnings:
        print(f"warning: {path}: skipped {warnings} malformed line(s)", file=sys.stderr)
    # Files are normally already in order; sort defensively.
    events.sort(key=lambda e: e[0])
    # Detect timestamp unit (ns/µs/ms/s) by magnitude and normalize to ns.
    if events:
        ref = events[0][0]
        if ref >= 10 ** 17:
            scale = 1                       # already ns
        elif ref >= 10 ** 14:
            scale = 1_000                   # µs → ns
        elif ref >= 10 ** 11:
            scale = 1_000_000               # ms → ns
        elif ref >= 10 ** 8:
            scale = 1_000_000_000           # s  → ns
        else:
            scale = 1
        if scale != 1:
            events = [(t * scale, ev, a) for (t, ev, a) in events]
    return events


def pair_events(events, rank):
    """Pair START/END into Op tuples (per single rank's event list)."""
    open_op = {}  # kind -> (start_ns, attrs_at_start)
    ops = []
    for ts, evtype, attrs in events:
        if evtype.endswith("_START"):
            kind = evtype[: -len("_START")]
            if kind not in OP_KINDS:
                continue
            open_op[kind] = (ts, attrs)
        elif evtype.endswith("_END"):
            kind = evtype[: -len("_END")]
            if kind not in OP_KINDS:
                continue
            if kind not in open_op:
                continue  # stray END; ignore
            start_ts, start_attrs = open_op.pop(kind)
            topic = start_attrs.get("topic") or attrs.get("topic", "?")
            # data_size may appear on start (PUSH) or end (PULL_WAIT).
            ds = start_attrs.get("data_size") or attrs.get("data_size")
            ds = int(ds) if ds is not None else None
            eid = attrs.get("event_id") or start_attrs.get("event_id")
            eid = int(eid) if eid is not None else None
            ops.append(Op(kind, topic, start_ts, ts, ds, eid, rank))
    return ops


# ---------- per-run aggregation ----------

def load_run(run_dir):
    """Return {component: {ranks: [...], ops: [...], per_rank_ops: {r: [...]}}, wall_ns: ...}."""
    paths = sorted(glob(os.path.join(run_dir, "*.ts.txt")))
    if not paths:
        return None
    components = defaultdict(lambda: {"ranks": [], "ops": [], "per_rank_ops": {}})
    for p in paths:
        base = os.path.basename(p)
        try:
            comp, rank_str, _, _ = base.split(".")
            rank = int(rank_str)
        except ValueError:
            print(f"warning: skipping {p}: filename not <comp>.<rank>.ts.txt", file=sys.stderr)
            continue
        events = parse_ts_file(p)
        if not events:
            continue
        ops = pair_events(events, rank)
        components[comp]["ranks"].append(rank)
        components[comp]["ops"].extend(ops)
        components[comp]["per_rank_ops"][rank] = ops
    # Compute per-component wall-clock.
    for comp, d in components.items():
        d["ranks"].sort()
        if d["ops"]:
            d["wall_ns"] = max(op.end_ns for op in d["ops"]) - min(op.start_ns for op in d["ops"])
        else:
            d["wall_ns"] = 0
    return dict(components)


# ---------- stats ----------

def percentile(sorted_vals, p):
    """Linear-interpolation percentile on a pre-sorted list."""
    if not sorted_vals:
        return float("nan")
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    k = (len(sorted_vals) - 1) * p / 100.0
    lo = int(math.floor(k))
    hi = int(math.ceil(k))
    if lo == hi:
        return sorted_vals[lo]
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def basic_stats(samples):
    """Return dict with n, min, max, mean, median, stddev, sum, cv, skew, p50,p90,p95,p99,p99.9."""
    n = len(samples)
    if n == 0:
        return {"n": 0}
    s = sorted(samples)
    total = sum(s)
    mean = total / n
    if n > 1:
        var = sum((x - mean) ** 2 for x in s) / (n - 1)
        sd = math.sqrt(var)
    else:
        sd = 0.0
    cv = (sd / mean) if mean else float("nan")
    if n > 2 and sd > 0:
        m3 = sum((x - mean) ** 3 for x in s) / n
        skew = m3 / (sd ** 3)
    else:
        skew = float("nan")
    return {
        "n": n,
        "min": s[0],
        "max": s[-1],
        "mean": mean,
        "median": percentile(s, 50),
        "stddev": sd,
        "sum": total,
        "cv": cv,
        "skew": skew,
        "p50": percentile(s, 50),
        "p90": percentile(s, 90),
        "p95": percentile(s, 95),
        "p99": percentile(s, 99),
        "p99.9": percentile(s, 99.9),
    }


def gaps(values_ns):
    """Inter-event gaps in ns from a list of sorted timestamps."""
    vs = sorted(values_ns)
    return [vs[i + 1] - vs[i] for i in range(len(vs) - 1)]


def cadence_stats(samples_ns):
    """Cadence reports min/max/mean/median/p95/stddev only."""
    full = basic_stats(samples_ns)
    if full["n"] == 0:
        return full
    return {k: full[k] for k in ("n", "min", "max", "mean", "median", "p95", "stddev")}


# ---------- formatting helpers ----------

def fmt_dur_ns(ns):
    """Format a duration (given in ns) in human-friendly units."""
    if ns != ns:  # NaN
        return "nan"
    if ns < 1_000:
        return f"{ns:.0f}ns"
    if ns < 1_000_000:
        return f"{ns / 1_000:.2f}µs"
    if ns < 1_000_000_000:
        return f"{ns / 1_000_000:.2f}ms"
    return f"{ns / 1_000_000_000:.3f}s"


def fmt_bytes(n):
    if n is None:
        return "-"
    for unit, scale in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if n >= scale:
            return f"{n / scale:.2f} {unit}"
    return f"{n} B"


def fmt_pct(x):
    return "nan" if x != x else f"{x:.1f}%"


# ---------- per-run report computation ----------

def compute_run_report(components):
    """Return a structured dict of all stats for one run."""
    report = {"components": {}, "cross": {}}
    for comp, d in components.items():
        cr = {"ranks": d["ranks"], "wall_ns": d["wall_ns"], "topics": {}, "compute_gap": None,
              "totals": {}}
        # ---- group ops by (topic, kind) for B/D/F ----
        by_topic = defaultdict(lambda: defaultdict(list))  # topic -> kind -> [Op]
        for op in d["ops"]:
            by_topic[op.topic][op.kind].append(op)

        # ---- per-topic A,B,D,F ----
        for topic, by_kind in by_topic.items():
            t = {"ops": {}, "pushed_sizes": [], "pulled_sizes": [], "event_ids": set()}
            for kind, ops in by_kind.items():
                durations_ns = [op.end_ns - op.start_ns for op in ops]
                stats = basic_stats(durations_ns)  # ns
                # cadence
                if kind == "PUSH":
                    starts = sorted(op.start_ns for op in ops)
                    cad = cadence_stats(gaps(starts))
                elif kind == "PULL_WAIT":
                    ends = sorted(op.end_ns for op in ops)
                    cad = cadence_stats(gaps(ends))
                elif kind == "FLUSH":
                    starts = sorted(op.start_ns for op in ops)
                    cad = cadence_stats(gaps(starts))
                else:
                    cad = None
                t["ops"][kind] = {"dur": stats, "cadence": cad}
                # payload accumulation
                if kind == "PUSH":
                    t["pushed_sizes"].extend(op.data_size for op in ops if op.data_size is not None)
                if kind == "PULL_WAIT":
                    t["pulled_sizes"].extend(op.data_size for op in ops if op.data_size is not None)
                    t["event_ids"].update(op.event_id for op in ops if op.event_id is not None)
            t["bytes_pushed"] = sum(t["pushed_sizes"])
            t["bytes_pulled"] = sum(t["pulled_sizes"])
            t["pushed_size_stats"] = basic_stats(t["pushed_sizes"])
            t["pulled_size_stats"] = basic_stats(t["pulled_sizes"])
            t["distinct_pushed"] = _count_if_low_cardinality(t["pushed_sizes"])
            t["distinct_pulled"] = _count_if_low_cardinality(t["pulled_sizes"])
            t["n_event_ids"] = len(t["event_ids"])
            cr["topics"][topic] = t

        # ---- C: component-level breakdown ----
        kind_sums_ns = defaultdict(int)
        for op in d["ops"]:
            kind_sums_ns[op.kind] += op.end_ns - op.start_ns
        wall = d["wall_ns"]
        # For multi-rank components, the wall is span across all ranks; sums
        # include all ranks. To express the fractions per-rank then averaged,
        # we compute per-rank and average.
        per_rank_fracs = defaultdict(list)  # kind -> [frac per rank]
        per_rank_idle = []
        per_rank_blocked = []
        per_rank_active = []
        all_gaps_ns = []
        for rank, ops in d["per_rank_ops"].items():
            if not ops:
                continue
            rank_wall = max(op.end_ns for op in ops) - min(op.start_ns for op in ops)
            sums = defaultdict(int)
            for op in ops:
                sums[op.kind] += op.end_ns - op.start_ns
            for k in OP_KINDS:
                per_rank_fracs[k].append(sums[k] / rank_wall if rank_wall else 0.0)
            total_op = sum(sums.values())
            per_rank_idle.append((rank_wall - total_op) / rank_wall if rank_wall else 0.0)
            per_rank_blocked.append(sums["PULL_WAIT"] + sums["FLUSH_WAIT"])
            per_rank_active.append(sums["PUSH"] + sums["PULL"] + sums["FLUSH"])
            # compute/own-code gaps (between sequential op END and next START)
            ops_sorted = sorted(ops, key=lambda o: o.start_ns)
            for i in range(len(ops_sorted) - 1):
                g = ops_sorted[i + 1].start_ns - ops_sorted[i].end_ns
                if g > 0:
                    all_gaps_ns.append(g)
        cr["frac"] = {k: (sum(per_rank_fracs[k]) / len(per_rank_fracs[k])
                          if per_rank_fracs[k] else 0.0) for k in OP_KINDS}
        cr["idle_frac"] = sum(per_rank_idle) / len(per_rank_idle) if per_rank_idle else 0.0
        cr["blocked_ns"] = sum(per_rank_blocked)
        cr["active_ns"] = sum(per_rank_active)
        cr["compute_gap"] = basic_stats(all_gaps_ns)

        report["components"][comp] = cr

    # ---- G: cross-component metrics ----
    report["cross"] = compute_cross(components)
    # G35: pipeline imbalance for SIRT on dist_sirt
    if "sirt" in components:
        sirt_pw_mean_per_rank = {}
        for rank, ops in components["sirt"]["per_rank_ops"].items():
            durs = [op.end_ns - op.start_ns for op in ops
                    if op.kind == "PULL_WAIT" and op.topic == "dist_sirt"]
            if durs:
                sirt_pw_mean_per_rank[rank] = sum(durs) / len(durs)
        if len(sirt_pw_mean_per_rank) >= 2:
            vals = list(sirt_pw_mean_per_rank.values())
            mean = sum(vals) / len(vals)
            imbalance = (max(vals) - min(vals)) / mean if mean else 0.0
            report["sirt_imbalance"] = {
                "per_rank_mean_ns": sirt_pw_mean_per_rank,
                "imbalance": imbalance,
            }
    return report


def _count_if_low_cardinality(values, max_k=10):
    if not values:
        return None
    distinct = {}
    for v in values:
        distinct[v] = distinct.get(v, 0) + 1
        if len(distinct) > max_k:
            return None
    return distinct


def compute_cross(components):
    """Per-topic cross-component join (timestamp-order match)."""
    out = {}
    # collect, per topic, all producer pushes and all consumer pulls
    pushes = defaultdict(list)   # topic -> [Op]
    pulls = defaultdict(list)    # topic -> [Op]
    pushers_components = defaultdict(set)   # topic -> {component}
    pullers_components = defaultdict(set)
    for comp, d in components.items():
        for op in d["ops"]:
            if op.kind == "PUSH":
                pushes[op.topic].append(op)
                pushers_components[op.topic].add(comp)
            elif op.kind == "PULL_WAIT":
                pulls[op.topic].append(op)
                pullers_components[op.topic].add(comp)
    for topic in set(pushes) | set(pulls):
        prod = sorted(pushes.get(topic, []), key=lambda o: o.start_ns)
        cons = sorted(pulls.get(topic, []), key=lambda o: o.end_ns)
        if not prod or not cons:
            out[topic] = {
                "skew": len(prod) - len(cons),
                "producers": sorted(pushers_components[topic]),
                "consumers": sorted(pullers_components[topic]),
                "matched": 0,
            }
            continue
        n = min(len(prod), len(cons))
        e2e_ns = [cons[i].end_ns - prod[i].start_ns for i in range(n)]
        q_ns = [cons[i].end_ns - prod[i].end_ns for i in range(n)]
        # steady-state: drop first/last 10%
        lo, hi = int(n * 0.1), int(math.ceil(n * 0.9))
        steady = e2e_ns[lo:hi] if hi > lo else e2e_ns
        out[topic] = {
            "producers": sorted(pushers_components[topic]),
            "consumers": sorted(pullers_components[topic]),
            "matched": n,
            "skew": len(prod) - len(cons),
            "e2e": basic_stats(e2e_ns),
            "queue": basic_stats(q_ns),
            "first_e2e_ns": e2e_ns[0] if e2e_ns else float("nan"),
            "steady_e2e": basic_stats(steady),
        }
    return out


# ---------- printing ----------

def print_run(label, components, report):
    print(f"========== {label} ==========")
    for comp in sorted(report["components"], key=component_sort_key):
        cr = report["components"][comp]
        ranks_note = (f" ({len(cr['ranks'])} ranks pooled)"
                      if len(cr["ranks"]) > 1 else "")
        wall_s = cr["wall_ns"] / 1e9
        # quick header line: pushes / pulls / blocked%/idle%
        total_pushes = sum(t["ops"].get("PUSH", {"dur": {"n": 0}})["dur"].get("n", 0)
                           for t in cr["topics"].values())
        total_pulls = sum(t["ops"].get("PULL_WAIT", {"dur": {"n": 0}})["dur"].get("n", 0)
                          for t in cr["topics"].values())
        total_pushed = sum(t["bytes_pushed"] for t in cr["topics"].values())
        total_pulled = sum(t["bytes_pulled"] for t in cr["topics"].values())
        # blocked/idle reported as time fractions averaged across ranks
        # blocked-as-fraction-of-wall: use blocked_ns / (wall * n_ranks)
        n_ranks = max(1, len(cr["ranks"]))
        blocked_pct = (cr["blocked_ns"] / (cr["wall_ns"] * n_ranks) * 100.0
                       if cr["wall_ns"] else 0.0)
        idle_pct = cr["idle_frac"] * 100.0
        print(f"[{comp.upper()}{ranks_note}]  runtime={wall_s:.3f}s  "
              f"pushes={total_pushes}({fmt_bytes(total_pushed)})  "
              f"pulls={total_pulls}({fmt_bytes(total_pulled)})  "
              f"blocked={blocked_pct:.1f}%  idle={idle_pct:.1f}%")
        # time breakdown
        breakdown = "  Time spent: " + "  ".join(
            f"{k}={cr['frac'][k] * 100:.1f}%" for k in OP_KINDS if cr['frac'][k] > 0)
        print(breakdown)
        # compute gap
        cg = cr["compute_gap"]
        if cg.get("n"):
            print(f"  Compute/own-code gap (op-END→next-op-START): "
                  f"n={cg['n']} mean={fmt_dur_ns(cg['mean'])} "
                  f"median={fmt_dur_ns(cg['median'])} "
                  f"p95={fmt_dur_ns(cg['p95'])} max={fmt_dur_ns(cg['max'])} "
                  f"sum={fmt_dur_ns(cg['sum'])}")
        # per-topic detail
        for topic in sorted(cr["topics"]):
            t = cr["topics"][topic]
            print(f"  Topic {topic}:")
            for kind in OP_KINDS:
                if kind not in t["ops"]:
                    continue
                opd = t["ops"][kind]
                s = opd["dur"]
                if s["n"] == 0:
                    continue
                print(f"    {kind:<11} n={s['n']:<5} "
                      f"total={fmt_dur_ns(s['sum'])}  "
                      f"min={fmt_dur_ns(s['min'])}  "
                      f"mean={fmt_dur_ns(s['mean'])}  "
                      f"median={fmt_dur_ns(s['median'])}  "
                      f"max={fmt_dur_ns(s['max'])}  "
                      f"std={fmt_dur_ns(s['stddev'])}  "
                      f"cv={s['cv']:.2f}  skew={s['skew']:.2f}")
                print(f"                p50={fmt_dur_ns(s['p50'])} "
                      f"p90={fmt_dur_ns(s['p90'])} "
                      f"p95={fmt_dur_ns(s['p95'])} "
                      f"p99={fmt_dur_ns(s['p99'])} "
                      f"p99.9={fmt_dur_ns(s['p99.9'])}")
                cad = opd["cadence"]
                if cad and cad.get("n"):
                    print(f"                cadence: n={cad['n']} "
                          f"mean={fmt_dur_ns(cad['mean'])} "
                          f"median={fmt_dur_ns(cad['median'])} "
                          f"p95={fmt_dur_ns(cad['p95'])} "
                          f"min={fmt_dur_ns(cad['min'])} "
                          f"max={fmt_dur_ns(cad['max'])} "
                          f"std={fmt_dur_ns(cad['stddev'])}")
            # payloads
            if t["pushed_sizes"]:
                ps = t["pushed_size_stats"]
                line = (f"    Payload pushed: total={fmt_bytes(t['bytes_pushed'])} "
                        f"n={ps['n']} min={fmt_bytes(ps['min'])} "
                        f"mean={fmt_bytes(int(ps['mean']))} "
                        f"max={fmt_bytes(ps['max'])} "
                        f"std={ps['stddev']:.1f}")
                if t["distinct_pushed"] is not None:
                    line += "  sizes=" + str(t["distinct_pushed"])
                print(line)
            if t["pulled_sizes"]:
                ps = t["pulled_size_stats"]
                line = (f"    Payload pulled: total={fmt_bytes(t['bytes_pulled'])} "
                        f"n={ps['n']} min={fmt_bytes(ps['min'])} "
                        f"mean={fmt_bytes(int(ps['mean']))} "
                        f"max={fmt_bytes(ps['max'])} "
                        f"std={ps['stddev']:.1f}")
                if t["distinct_pulled"] is not None:
                    line += "  sizes=" + str(t["distinct_pulled"])
                print(line)
            if t["n_event_ids"]:
                print(f"    Distinct event_ids observed: {t['n_event_ids']}")
        print()

    # cross-component
    if report["cross"]:
        print("Cross-component (timestamp-order push↔pull match):")
        for topic in sorted(report["cross"]):
            x = report["cross"][topic]
            prods = ",".join(x["producers"]) or "-"
            cons = ",".join(x["consumers"]) or "-"
            head = f"  {topic}  prod=[{prods}] cons=[{cons}]  skew={x['skew']:+d}"
            if x.get("matched"):
                e2e = x["e2e"]
                q = x["queue"]
                steady = x["steady_e2e"]
                head += f"  matched={x['matched']}"
                print(head)
                print(f"    e2e_latency:   mean={fmt_dur_ns(e2e['mean'])} "
                      f"median={fmt_dur_ns(e2e['median'])} "
                      f"p95={fmt_dur_ns(e2e['p95'])} "
                      f"p99={fmt_dur_ns(e2e['p99'])} "
                      f"min={fmt_dur_ns(e2e['min'])} max={fmt_dur_ns(e2e['max'])}")
                print(f"    queue_latency: mean={fmt_dur_ns(q['mean'])} "
                      f"median={fmt_dur_ns(q['median'])} "
                      f"p95={fmt_dur_ns(q['p95'])} "
                      f"p99={fmt_dur_ns(q['p99'])}")
                print(f"    first_event_latency={fmt_dur_ns(x['first_e2e_ns'])}  "
                      f"steady_mean={fmt_dur_ns(steady['mean']) if steady['n'] else 'n/a'}")
            else:
                print(head + "  (no matched events)")
    if "sirt_imbalance" in report:
        si = report["sirt_imbalance"]
        per_rank = "  ".join(f"rank{r}={fmt_dur_ns(v)}"
                             for r, v in sorted(si["per_rank_mean_ns"].items()))
        print(f"SIRT imbalance (dist_sirt PULL_WAIT mean per rank): "
              f"{per_rank}  (max-min)/mean={si['imbalance'] * 100:.1f}%")
    print()


# ---------- across-run aggregate ----------

def collect_aggregate(reports):
    """Pool sample-level metrics that need re-percentiling, and gather
    per-run scalars whose mean ± stddev across runs we will print."""
    agg = {
        "comps": defaultdict(lambda: {
            "wall_s": [],
            "frac": defaultdict(list),
            "idle_pct": [],
            "topics": defaultdict(lambda: {
                "ops": defaultdict(lambda: {"per_run_mean_ns": [], "per_run_n": [],
                                            "pooled_dur_ns": []}),
                "bytes_pushed": [],
                "bytes_pulled": [],
                "pushed_n": [],
                "pulled_n": [],
            }),
        }),
        "cross": defaultdict(lambda: {
            "e2e_mean_per_run_ns": [],
            "queue_mean_per_run_ns": [],
            "first_e2e_per_run_ns": [],
            "pooled_e2e_ns": [],
            "skew_per_run": [],
            "matched_per_run": [],
        }),
    }
    for rep in reports:
        for comp, cr in rep["components"].items():
            ac = agg["comps"][comp]
            ac["wall_s"].append(cr["wall_ns"] / 1e9)
            ac["idle_pct"].append(cr["idle_frac"] * 100)
            for k in OP_KINDS:
                ac["frac"][k].append(cr["frac"][k] * 100)
            for topic, t in cr["topics"].items():
                at = ac["topics"][topic]
                at["bytes_pushed"].append(t["bytes_pushed"])
                at["bytes_pulled"].append(t["bytes_pulled"])
                at["pushed_n"].append(t["pushed_size_stats"].get("n", 0))
                at["pulled_n"].append(t["pulled_size_stats"].get("n", 0))
                for kind, opd in t["ops"].items():
                    s = opd["dur"]
                    if s["n"]:
                        at["ops"][kind]["per_run_mean_ns"].append(s["mean"])
                        at["ops"][kind]["per_run_n"].append(s["n"])
        for topic, x in rep["cross"].items():
            ax = agg["cross"][topic]
            ax["skew_per_run"].append(x["skew"])
            ax["matched_per_run"].append(x.get("matched", 0))
            if x.get("matched"):
                ax["e2e_mean_per_run_ns"].append(x["e2e"]["mean"])
                ax["queue_mean_per_run_ns"].append(x["queue"]["mean"])
                ax["first_e2e_per_run_ns"].append(x["first_e2e_ns"])
    return agg


def m_s(values, fmt=lambda v: f"{v:.2f}"):
    n = len(values)
    if n == 0:
        return "n/a"
    if n == 1:
        return fmt(values[0])
    mean = sum(values) / n
    var = sum((v - mean) ** 2 for v in values) / (n - 1)
    sd = math.sqrt(var)
    return f"{fmt(mean)} ± {fmt(sd)}"


def m_s_dur_ns(values_ns):
    return m_s(values_ns, fmt=fmt_dur_ns)


def m_s_bytes(values):
    return m_s(values, fmt=lambda v: fmt_bytes(int(v)))


def print_aggregate(reports):
    n = len(reports)
    print(f"========== aggregate (mean ± stddev across {n} runs) ==========")
    agg = collect_aggregate(reports)
    for comp in sorted(agg["comps"], key=component_sort_key):
        ac = agg["comps"][comp]
        print(f"[{comp.upper()}] "
              f"runtime={m_s(ac['wall_s'], lambda v: f'{v:.3f}s')}  "
              f"idle={m_s(ac['idle_pct'], lambda v: f'{v:.1f}%')}")
        frac_line = "  Time spent: " + "  ".join(
            f"{k}={m_s(ac['frac'][k], lambda v: f'{v:.1f}%')}"
            for k in OP_KINDS if any(v > 0 for v in ac['frac'][k]))
        print(frac_line)
        for topic in sorted(ac["topics"]):
            at = ac["topics"][topic]
            print(f"  Topic {topic}:  "
                  f"bytes_pushed={m_s_bytes(at['bytes_pushed'])}  "
                  f"bytes_pulled={m_s_bytes(at['bytes_pulled'])}  "
                  f"n_push={m_s(at['pushed_n'], lambda v: f'{v:.1f}')}  "
                  f"n_pull={m_s(at['pulled_n'], lambda v: f'{v:.1f}')}")
            for kind in OP_KINDS:
                if kind not in at["ops"]:
                    continue
                op = at["ops"][kind]
                if not op["per_run_mean_ns"]:
                    continue
                print(f"    {kind:<11} "
                      f"per-run mean of mean dur = {m_s_dur_ns(op['per_run_mean_ns'])}  "
                      f"per-run n = {m_s(op['per_run_n'], lambda v: f'{v:.1f}')}")
        print()
    if agg["cross"]:
        print("Cross-component (across-run aggregates):")
        for topic in sorted(agg["cross"]):
            ax = agg["cross"][topic]
            print(f"  {topic}:  skew={m_s(ax['skew_per_run'], lambda v: f'{v:+.1f}')}  "
                  f"matched={m_s(ax['matched_per_run'], lambda v: f'{v:.1f}')}")
            if ax["e2e_mean_per_run_ns"]:
                print(f"    e2e_latency:   per-run mean = {m_s_dur_ns(ax['e2e_mean_per_run_ns'])}")
                print(f"    queue_latency: per-run mean = {m_s_dur_ns(ax['queue_mean_per_run_ns'])}")
                print(f"    first-event:   per-run = {m_s_dur_ns(ax['first_e2e_per_run_ns'])}")
        print()


# ---------- main ----------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("run_dir", nargs="+", help="One or more directories containing *.ts.txt files")
    args = ap.parse_args(argv)

    reports = []
    for d in args.run_dir:
        if not os.path.isdir(d):
            print(f"error: {d}: not a directory", file=sys.stderr)
            continue
        comps = load_run(d)
        if not comps:
            print(f"warning: {d}: no *.ts.txt files found", file=sys.stderr)
            continue
        label = os.path.basename(os.path.abspath(d)) or d
        report = compute_run_report(comps)
        print_run(label, comps, report)
        reports.append(report)

    if len(reports) > 1:
        print_aggregate(reports)


if __name__ == "__main__":
    main()
