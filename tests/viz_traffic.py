#!/usr/bin/env python3
"""
viz_traffic.py - visualize crystal-router network traffic from verbose logs.

The crystal router (src/crystal.c), when run with crystal_set_verbose(cr, 2),
prints two kinds of lines that describe per-round, per-rank communication:

  crystal_exchange: rank = <id>  buf sizes  <data_n> <c0> <c1> <sum>
      <sum> = the rank's buffer size (in uints) AFTER that round's exchange.

  CR <id> tag=<t> <R1|R2|S> peer=<p> size=<bytes> off=<uints> left=<uints>
      one line per chunk of the batched (>CR_MAX_MSG) transfer path.

This script parses those lines and renders a PNG with two panels:
  (1) per-rank buffer size across router rounds  -> shows data concentrating
      on one rank (e.g. all-to-one), and
  (2) number of transfer chunks per rank         -> shows batching activity.

It also prints a short text summary so it is useful without opening the image.

Usage:
  mpirun -n 4 ./crystal_test2 -E 64 -n 2 -v 2>&1 | \
      python3 viz_traffic.py -o traffic.png
  python3 viz_traffic.py mylog.txt -o traffic.png
"""

import sys
import re
import argparse
from collections import defaultdict

# crystal_exchange: rank = 0  buf sizes  65856  65856  0  131712
RE_EXCH = re.compile(
    r"crystal_exchange:\s*rank\s*=\s*(\d+)\s+buf sizes\s+"
    r"(\d+)\s+(\d+)\s+(\d+)\s+(\d+)"
)
# CR 2 tag=1 S  peer=1 size=4096 off=0 left=8232
RE_CHUNK = re.compile(
    r"^CR\s+(\d+)\s+tag=(\d+)\s+(R1|R2|S)\s*peer=(\d+)\s+"
    r"size=(\d+)\s+off=(\d+)\s+left=(\d+)"
)


def parse(lines):
    """Return (rounds_by_rank, chunks_by_rank).

    rounds_by_rank[rank] = [sum_after_round0, sum_after_round1, ...]
      (rounds are emitted sequentially per rank, so append order == round order)
    chunks_by_rank[rank] = {'S': n, 'R1': n, 'R2': n}
    """
    rounds = defaultdict(list)
    chunks = defaultdict(lambda: defaultdict(int))
    for ln in lines:
        m = RE_EXCH.search(ln)
        if m:
            rank = int(m.group(1))
            buf_sum = int(m.group(5))
            rounds[rank].append(buf_sum)
            continue
        m = RE_CHUNK.match(ln.strip())
        if m:
            rank = int(m.group(1))
            kind = m.group(3)
            chunks[rank][kind] += 1
    return rounds, chunks


def text_summary(rounds, chunks):
    ranks = sorted(rounds)
    if not ranks:
        return ("No 'crystal_exchange' lines found. Run the test with -v and a "
                "library built so crystal_set_verbose emits level-2 traces.")
    nrounds = max((len(v) for v in rounds.values()), default=0)
    out = []
    out.append("Crystal-router traffic summary")
    out.append("  ranks=%d  rounds=%d" % (len(ranks), nrounds))
    # peak buffer per rank
    peak = {r: (max(rounds[r]) if rounds[r] else 0) for r in ranks}
    hot = max(peak, key=peak.get)
    out.append("  peak buffer (uints): " +
               "  ".join("r%d=%d" % (r, peak[r]) for r in ranks))
    out.append("  hottest rank = %d (peak %d uints, %.1f MiB as uint*4)"
               % (hot, peak[hot], peak[hot] * 4 / (1024 * 1024)))
    total_chunks = sum(sum(c.values()) for c in chunks.values())
    if total_chunks:
        out.append("  transfer chunks (batched path active):")
        for r in sorted(chunks):
            c = chunks[r]
            out.append("    rank %d: S=%d R1=%d R2=%d"
                       % (r, c.get('S', 0), c.get('R1', 0), c.get('R2', 0)))
    else:
        out.append("  transfer chunks: none (fast path; buffers below CR_MAX_MSG)")
    return "\n".join(out)


def make_png(rounds, chunks, path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        sys.stderr.write(
            "matplotlib not available; skipping PNG "
            "(pip install matplotlib). Text summary printed above.\n")
        return False

    ranks = sorted(rounds)
    nrounds = max((len(v) for v in rounds.values()), default=0)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.5))

    # Panel 1: per-rank buffer size across rounds
    for r in ranks:
        ys = rounds[r]
        xs = list(range(len(ys)))
        ax1.plot(xs, [y * 4 / (1024 * 1024) for y in ys],
                 marker="o", label="rank %d" % r)
    ax1.set_xlabel("router round")
    ax1.set_ylabel("buffer size (MiB)")
    ax1.set_title("Per-rank buffer size across rounds")
    ax1.grid(True, alpha=0.3)
    if len(ranks) <= 16:
        ax1.legend(fontsize=8, ncol=2)
    if nrounds:
        ax1.set_xticks(range(nrounds))

    # Panel 2: chunks per rank (batching activity), stacked S/R1/R2
    if chunks:
        crk = sorted(chunks)
        S = [chunks[r].get('S', 0) for r in crk]
        R1 = [chunks[r].get('R1', 0) for r in crk]
        R2 = [chunks[r].get('R2', 0) for r in crk]
        x = list(range(len(crk)))
        ax2.bar(x, S, label="send")
        ax2.bar(x, R1, bottom=S, label="recv1")
        ax2.bar(x, R2, bottom=[a + b for a, b in zip(S, R1)], label="recv2")
        ax2.set_xticks(x)
        ax2.set_xticklabels(["r%d" % r for r in crk])
        ax2.set_ylabel("transfer chunks")
        ax2.set_title("Batched-path chunks per rank")
        ax2.legend(fontsize=8)
    else:
        ax2.text(0.5, 0.5, "no batched chunks\n(fast path)",
                 ha="center", va="center", transform=ax2.transAxes)
        ax2.set_title("Batched-path chunks per rank")
    ax2.grid(True, alpha=0.3, axis="y")

    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logfile", nargs="?", default="-",
                    help="verbose log file (default: stdin)")
    ap.add_argument("-o", "--out", default="traffic.png",
                    help="output PNG path (default: traffic.png)")
    args = ap.parse_args()

    if args.logfile == "-":
        lines = sys.stdin.readlines()
    else:
        with open(args.logfile) as f:
            lines = f.readlines()

    rounds, chunks = parse(lines)
    print(text_summary(rounds, chunks))

    if rounds:
        if make_png(rounds, chunks, args.out):
            print("Wrote %s" % args.out)


if __name__ == "__main__":
    main()
