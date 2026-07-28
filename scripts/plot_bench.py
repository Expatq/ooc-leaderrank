#!/usr/bin/env python3
import argparse
import collections
import csv
import statistics
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load(path):
    groups = collections.defaultdict(list)
    with open(path) as file:
        for row in csv.DictReader(file):
            key = (row["dataset"], row["tool"], int(row["threads"]))
            groups[key].append(row)
    medians = {}
    for key, rows in groups.items():
        walls = sorted(float(row["wall_s"]) for row in rows)
        rss = sorted(int(row["peak_rss_kib"]) for row in rows if row["peak_rss_kib"])
        iters = [int(row["iterations"]) for row in rows if row["iterations"]]
        medians[key] = {
            "wall_s": statistics.median(walls),
            "peak_rss_kib": statistics.median(rss) if rss else None,
            "iterations": iters[0] if iters else None,
        }
    return medians


def series(medians, dataset, tool):
    threads = sorted(t for (d, tl, t) in medians if d == dataset and tl == tool)
    return threads, [medians[(dataset, tool, t)] for t in threads]


def save(figure, path):
    figure.tight_layout()
    figure.savefig(path, dpi=150)
    plt.close(figure)
    print(f"wrote {path}")


def main():
    parser = argparse.ArgumentParser(description="Plot thread benchmark results.")
    parser.add_argument("--csv", default="data/bench/results.csv",
                        help="CSV written by bench_threads.sh")
    parser.add_argument("--out-dir", default="data/bench",
                        help="directory for PNG files")
    parser.add_argument("--budget-mib", type=float, default=None,
                        help="draw the configured memory budget on the RSS chart")
    args = parser.parse_args()

    if not Path(args.csv).is_file():
        parser.error(f"input file not found: {args.csv}")
    medians = load(args.csv)
    if not medians:
        parser.error(f"no benchmark rows found in {args.csv}")
    Path(args.out_dir).mkdir(parents=True, exist_ok=True)
    datasets = sorted({d for (d, _, _) in medians})
    tools = ["prepare", "rank"]

    figure, axes = plt.subplots(1, 2, figsize=(11, 4.5))
    for axis, tool in zip(axes, tools):
        for dataset in datasets:
            threads, points = series(medians, dataset, tool)
            if not threads:
                continue
            base = points[0]["wall_s"]
            axis.plot(threads, [base / p["wall_s"] for p in points], marker="o", label=dataset)
        limit = max((t for (_, _, t) in medians), default=1)
        axis.plot([1, limit], [1, limit], linestyle="--", color="gray", label="ideal")
        axis.set_title(f"lr-{tool}: speedup vs T")
        axis.set_xlabel("threads")
        axis.set_ylabel("speedup vs T=1")
        axis.grid(True, alpha=0.3)
        axis.legend()
    save(figure, f"{args.out_dir}/speedup.png")

    figure, axes = plt.subplots(1, 2, figsize=(11, 4.5))
    for axis, tool in zip(axes, tools):
        for dataset in datasets:
            threads, points = series(medians, dataset, tool)
            if not threads:
                continue
            base = points[0]["wall_s"]
            axis.plot(threads, [base / p["wall_s"] / t for t, p in zip(threads, points)], marker="o", label=dataset)
        axis.set_title(f"lr-{tool}: efficiency")
        axis.set_xlabel("threads")
        axis.set_ylabel("speedup / T")
        axis.set_ylim(0, 1.1)
        axis.grid(True, alpha=0.3)
        axis.legend()
    save(figure, f"{args.out_dir}/efficiency.png")

    figure, axis = plt.subplots(figsize=(6.5, 4.5))
    for dataset in datasets:
        threads, points = series(medians, dataset, "rank")
        if not threads or points[0]["iterations"] is None:
            continue
        axis.plot(threads, [p["wall_s"] / p["iterations"] * 1000 for p in points], marker="o", label=dataset)
    axis.set_title("lr-rank: time per iteration")
    axis.set_xlabel("threads")
    axis.set_ylabel("ms / iteration")
    axis.grid(True, alpha=0.3)
    axis.legend()
    save(figure, f"{args.out_dir}/iteration_time.png")

    figure, axis = plt.subplots(figsize=(6.5, 4.5))
    for dataset in datasets:
        for tool, style in zip(tools, ["-o", "--s"]):
            threads, points = series(medians, dataset, tool)
            if not threads or points[0]["peak_rss_kib"] is None:
                continue
            axis.plot(threads, [p["peak_rss_kib"] / 1024 for p in points], style, label=f"{dataset} {tool}")
    if args.budget_mib is not None:
        axis.axhline(args.budget_mib, color="red", linestyle=":", label="budget")
    axis.set_title("peak RSS")
    axis.set_xlabel("threads")
    axis.set_ylabel("MiB")
    axis.grid(True, alpha=0.3)
    axis.legend()
    save(figure, f"{args.out_dir}/peak_rss.png")


if __name__ == "__main__":
    main()
