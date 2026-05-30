#!/usr/bin/env python3
"""Plot A 模块 benchmark CSV.

输入格式：
skill_count,linear_median_us,linear_p95_us,binary_median_us,binary_p95_us
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("--out", type=Path, default=Path("skill_prompt_benchmark.png"))
    args = parser.parse_args()

    x = []
    linear = []
    binary = []

    reader = None
    for encoding in ("utf-8-sig", "utf-8", "utf-16"):
        try:
            with args.csv_path.open("r", encoding=encoding, newline="") as f:
                reader = list(csv.DictReader(f))
            break
        except UnicodeError:
            continue
    if reader is None:
        raise UnicodeError(f"Unable to decode CSV: {args.csv_path}")

    for row in reader:
        x.append(int(row["skill_count"]))
        linear.append(float(row["linear_median_us"]))
        binary.append(float(row["binary_median_us"]))

    plt.figure(figsize=(9, 5))
    plt.plot(x, linear, label="Linear scan (median)")
    plt.plot(x, binary, label="Prefix sum + binary search (median)")
    plt.xlabel("Skill count")
    plt.ylabel("Latency (us)")
    plt.title("Skill prompt truncation benchmark")
    plt.legend()
    plt.tight_layout()
    plt.savefig(args.out, dpi=180)
    print(args.out)


if __name__ == "__main__":
    main()
