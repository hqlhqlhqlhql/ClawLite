#!/usr/bin/env python3
"""Generate the five benchmark figures for ClawLite.

This script is designed for the course report / defense deck.
It can run in two modes:
1) Demo mode (default): generate smooth synthetic data in milliseconds.
2) CSV mode: if you later prepare benchmark CSVs, pass them via --data-dir.

Output:
  --out-dir default: ./figures
  5 PNG files, one figure per benchmark.

Notes:
- Each benchmark is a separate figure (no subplots).
- Chart 1 uses a twin y-axis because the task asks for probe count and latency.
- The script keeps the visuals readable even when the real data is only at ms scale.
"""

from __future__ import annotations

import argparse
import csv
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

import matplotlib.pyplot as plt


plt.rcParams.setdefault("figure.dpi", 160)
plt.rcParams.setdefault("savefig.dpi", 200)
plt.rcParams.setdefault("axes.unicode_minus", False)
# Prefer generic sans-serif; avoid hard-coding a specific font that may not exist.
plt.rcParams.setdefault("font.family", ["DejaVu Sans", "sans-serif"])


@dataclass
class Series:
    x: list[float]
    y: list[float]
    label: str


@dataclass
class FigureSpec:
    name: str
    title: str
    xlabel: str
    ylabel: str
    out_name: str


def _ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def _save(fig: plt.Figure, out: Path) -> None:
    fig.tight_layout()
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)


def _read_csv_series(path: Path, x_col: str, y_col: str) -> Optional[Series]:
    if not path.exists():
        return None
    xs, ys = [], []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if x_col not in row or y_col not in row:
                return None
            xs.append(float(row[x_col]))
            ys.append(float(row[y_col]))
    if not xs:
        return None
    return Series(xs, ys, y_col)


def _read_multi_csv(path: Path, x_col: str, y_cols: list[str]) -> Optional[tuple[list[float], dict[str, list[float]]]]:
    if not path.exists():
        return None
    xs: list[float] = []
    series = {name: [] for name in y_cols}
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if x_col not in row:
                return None
            xs.append(float(row[x_col]))
            for name in y_cols:
                if name not in row:
                    return None
                series[name].append(float(row[name]))
    if not xs:
        return None
    return xs, series


# ---------------------------------------------------------------------------
# Demo data generators (all values are intentionally at ms scale)
# ---------------------------------------------------------------------------

def _demo_chart1() -> dict[str, list[float]]:
    x = [64, 128, 256, 512, 1024, 2048, 4096]
    linear_probes = [n * 0.52 for n in x]
    binary_probes = [math.log2(n) + 1.5 for n in x]
    linear_ms = [0.42 + n * 0.0088 for n in x]
    binary_ms = [0.78 + math.log2(n) * 0.06 for n in x]
    return {
        "x": x,
        "linear_probes": linear_probes,
        "binary_probes": binary_probes,
        "linear_ms": linear_ms,
        "binary_ms": binary_ms,
    }


def _demo_chart2() -> dict[str, list[float]]:
    x = [100, 500, 1_000, 5_000, 10_000, 50_000, 100_000]
    heap_ms = [0.9 + 0.000018 * n * math.log2(20 + 1) for n in x]
    sort_ms = [1.5 + 0.000010 * n * math.log2(n) for n in x]
    heap_ms = [round(v, 3) for v in heap_ms]
    sort_ms = [round(v, 3) for v in sort_ms]
    return {"x": x, "heap_ms": heap_ms, "sort_ms": sort_ms}


def _demo_chart3() -> dict[str, list[float]]:
    x = [1_000, 3_000, 10_000, 30_000, 100_000]
    brute_ms = [1.2 + 0.00016 * n for n in x]
    inverted_ms = [0.7 + 0.000025 * math.sqrt(n) * 100 for n in x]
    fts5_ms = [0.55 + 0.000018 * math.log2(n) * 100 for n in x]
    return {
        "x": x,
        "brute_ms": [round(v, 3) for v in brute_ms],
        "inverted_ms": [round(v, 3) for v in inverted_ms],
        "fts5_ms": [round(v, 3) for v in fts5_ms],
    }


def _demo_chart4() -> dict[str, list[float]]:
    x = [0, 10, 20, 50, 100, 200, 500, 1000, 2000]
    hit_rate = [0.06, 0.15, 0.23, 0.37, 0.52, 0.66, 0.79, 0.88, 0.93]
    return {"x": x, "hit_rate": hit_rate}


def _demo_chart5() -> dict[str, list[float]]:
    x = list(range(1, 31))
    incremental_ms = [8.8 + 0.12 * t + 0.05 * math.log2(t + 1) for t in x]
    full_ms = [11.5 + 0.85 * t + 0.18 * math.log2(t + 1) for t in x]
    return {
        "x": x,
        "incremental_ms": [round(v, 3) for v in incremental_ms],
        "full_ms": [round(v, 3) for v in full_ms],
    }


# ---------------------------------------------------------------------------
# Plot helpers
# ---------------------------------------------------------------------------

def plot_chart1(data: dict[str, list[float]], out: Path) -> None:
    fig, ax1 = plt.subplots(figsize=(8.8, 5.2))
    ax2 = ax1.twinx()

    x = data["x"]
    ax1.plot(x, data["linear_probes"], marker="o", linewidth=2, label="Linear scan probes")
    ax1.plot(x, data["binary_probes"], marker="o", linewidth=2, label="Prefix sum + binary probes")
    ax2.plot(x, data["linear_ms"], marker="s", linestyle="--", linewidth=2, label="Linear scan latency")
    ax2.plot(x, data["binary_ms"], marker="s", linestyle="--", linewidth=2, label="Prefix sum + binary latency")

    ax1.set_xscale("log", base=2)
    ax1.set_xlabel("Skill count")
    ax1.set_ylabel("Probe count")
    ax2.set_ylabel("Latency (ms)")
    ax1.set_title("A1: Prefix sum + binary search vs linear scan")
    ax1.grid(True, alpha=0.28)

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper left", frameon=False)
    _save(fig, out)


def plot_chart2(data: dict[str, list[float]], out: Path) -> None:
    fig, ax = plt.subplots(figsize=(8.8, 5.2))
    x = data["x"]
    ax.plot(x, data["heap_ms"], marker="o", linewidth=2, label="Heap Top-K")
    ax.plot(x, data["sort_ms"], marker="o", linewidth=2, label="Full sort")
    ax.set_xscale("log")
    ax.set_xlabel("Chunk count")
    ax.set_ylabel("Latency (ms)")
    ax.set_title("B1: Heap Top-K vs full sort")
    ax.grid(True, alpha=0.28)
    ax.legend(frameon=False)
    _save(fig, out)


def plot_chart3(data: dict[str, list[float]], out: Path) -> None:
    fig, ax = plt.subplots(figsize=(8.8, 5.2))
    x = data["x"]
    ax.plot(x, data["brute_ms"], marker="o", linewidth=2, label="Brute-force scan")
    ax.plot(x, data["inverted_ms"], marker="o", linewidth=2, label="Handwritten inverted index")
    ax.plot(x, data["fts5_ms"], marker="o", linewidth=2, label="FTS5")
    ax.set_xscale("log")
    ax.set_xlabel("Corpus size (chunks)")
    ax.set_ylabel("Query latency (ms)")
    ax.set_title("B3: Inverted index vs brute force vs FTS5")
    ax.grid(True, alpha=0.28)
    ax.legend(frameon=False)
    _save(fig, out)


def plot_chart4(data: dict[str, list[float]], out: Path) -> None:
    fig, ax = plt.subplots(figsize=(8.8, 5.2))
    x = data["x"]
    ax.plot(x, [v * 100 for v in data["hit_rate"]], marker="o", linewidth=2)
    ax.set_xscale("log")
    ax.set_xlabel("Cache size")
    ax.set_ylabel("Hit rate (%)")
    ax.set_title("B2: LRU cache hit rate vs cache size")
    ax.grid(True, alpha=0.28)
    _save(fig, out)


def plot_chart5(data: dict[str, list[float]], out: Path) -> None:
    fig, ax = plt.subplots(figsize=(8.8, 5.2))
    x = data["x"]
    ax.plot(x, data["incremental_ms"], marker="o", linewidth=2, label="Incremental history")
    ax.plot(x, data["full_ms"], marker="o", linewidth=2, label="Full history pass-through")
    ax.set_xlabel("Dialogue turns")
    ax.set_ylabel("Single-turn latency (ms)")
    ax.set_title("C2: Incremental history vs full pass-through")
    ax.grid(True, alpha=0.28)
    ax.legend(frameon=False)
    _save(fig, out)


# ---------------------------------------------------------------------------
# Optional CSV overrides
# ---------------------------------------------------------------------------

def _maybe_load_csvs(data_dir: Path) -> tuple[dict[str, list[float]], ...]:
    # Chart 1: a1.csv with columns: skill_count, linear_probes, binary_probes, linear_ms, binary_ms
    c1 = {
        "x": [], "linear_probes": [], "binary_probes": [], "linear_ms": [], "binary_ms": [],
    }
    p = data_dir / "a1.csv"
    if p.exists():
        with p.open("r", encoding="utf-8", newline="") as f:
            r = csv.DictReader(f)
            for row in r:
                c1["x"].append(float(row["skill_count"]))
                c1["linear_probes"].append(float(row["linear_probes"]))
                c1["binary_probes"].append(float(row["binary_probes"]))
                c1["linear_ms"].append(float(row["linear_ms"]))
                c1["binary_ms"].append(float(row["binary_ms"]))

    # Chart 2: b1.csv columns: chunk_count, heap_ms, sort_ms
    c2 = {"x": [], "heap_ms": [], "sort_ms": []}
    p = data_dir / "b1.csv"
    if p.exists():
        with p.open("r", encoding="utf-8", newline="") as f:
            r = csv.DictReader(f)
            for row in r:
                c2["x"].append(float(row["chunk_count"]))
                c2["heap_ms"].append(float(row["heap_ms"]))
                c2["sort_ms"].append(float(row["sort_ms"]))

    # Chart 3: b3.csv columns: corpus_size, brute_ms, inverted_ms, fts5_ms
    c3 = {"x": [], "brute_ms": [], "inverted_ms": [], "fts5_ms": []}
    p = data_dir / "b3.csv"
    if p.exists():
        with p.open("r", encoding="utf-8", newline="") as f:
            r = csv.DictReader(f)
            for row in r:
                c3["x"].append(float(row["corpus_size"]))
                c3["brute_ms"].append(float(row["brute_ms"]))
                c3["inverted_ms"].append(float(row["inverted_ms"]))
                c3["fts5_ms"].append(float(row["fts5_ms"]))

    # Chart 4: b2.csv columns: cache_size, hit_rate
    c4 = {"x": [], "hit_rate": []}
    p = data_dir / "b2.csv"
    if p.exists():
        with p.open("r", encoding="utf-8", newline="") as f:
            r = csv.DictReader(f)
            for row in r:
                c4["x"].append(float(row["cache_size"]))
                c4["hit_rate"].append(float(row["hit_rate"]))

    # Chart 5: c1.csv columns: turns, incremental_ms, full_ms
    c5 = {"x": [], "incremental_ms": [], "full_ms": []}
    p = data_dir / "c1.csv"
    if p.exists():
        with p.open("r", encoding="utf-8", newline="") as f:
            r = csv.DictReader(f)
            for row in r:
                c5["x"].append(float(row["turns"]))
                c5["incremental_ms"].append(float(row["incremental_ms"]))
                c5["full_ms"].append(float(row["full_ms"]))

    return c1, c2, c3, c4, c5


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate the five ClawLite benchmark figures.")
    parser.add_argument("--out-dir", type=Path, default=Path("figures"), help="Output directory")
    parser.add_argument("--data-dir", type=Path, default=None, help="Optional CSV directory for real benchmark data")
    args = parser.parse_args()

    _ensure_dir(args.out_dir)

    # Start with demo data, then override each chart if a CSV exists.
    c1 = _demo_chart1()
    c2 = _demo_chart2()
    c3 = _demo_chart3()
    c4 = _demo_chart4()
    c5 = _demo_chart5()

    if args.data_dir is not None and args.data_dir.exists():
        csvs = _maybe_load_csvs(args.data_dir)
        if csvs[0]["x"]:
            c1 = csvs[0]
        if csvs[1]["x"]:
            c2 = csvs[1]
        if csvs[2]["x"]:
            c3 = csvs[2]
        if csvs[3]["x"]:
            c4 = csvs[3]
        if csvs[4]["x"]:
            c5 = csvs[4]

    plot_chart1(c1, args.out_dir / "figure_1_a_prefix_binary_vs_linear.png")
    plot_chart2(c2, args.out_dir / "figure_2_b_heap_vs_sort.png")
    plot_chart3(c3, args.out_dir / "figure_3_b_inverted_vs_scan_vs_fts5.png")
    plot_chart4(c4, args.out_dir / "figure_4_b_lru_hit_rate_vs_cache_size.png")
    plot_chart5(c5, args.out_dir / "figure_5_c_history_incremental_vs_full.png")

    print(f"Saved 5 figures to: {args.out_dir.resolve()}")


if __name__ == "__main__":
    main()
