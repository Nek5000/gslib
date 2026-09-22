#!/usr/bin/env python3
"""
viz_flow.py - visualize crystal-router DATA FLOW from verbose logs.

The router (src/crystal.c, crystal_set_verbose(cr,2)) prints one record per
round per rank:

  CRFLOW round=<r> rank=<id> targ=<t> recvn=<n> send=<S> keep=<K> recv=<R> hold=<H>

  send=S : uints shipped to rank t this round (S=0 -> this rank received only)
  keep=K : uints kept locally after the split
  recv=R : uints received this round
  hold=H : uints held after the round (= K + R)
  held entering the round = K + S

This renders, per network mode:
  <out>_timeline.png : x=round, y=rank; node size ~ data held entering the
                       round; arrows (round,rank)->(round+1,targ) width ~ send.
                       Reads left->right as data funnels through the network.
  <out>_rounds.png   : one circular network panel per round; arrows = that
                       round's sends (width ~ amount), node size ~ data held.

It also prints a text table (held per rank per round) + the edge list, so it is
useful without opening the images.

Usage:
  mpirun --use-hwthread-cpus -n 16 ./crystal_test2 -E 16 -n 2 -v 2>&1 | \
      python3 viz_flow.py -o flow16_all2one
  python3 viz_flow.py mylog.txt -o flow
"""

import sys
import re
import argparse
from collections import defaultdict

RE_FLOW = re.compile(
    r"CRFLOW\s+round=(\d+)\s+rank=(\d+)\s+targ=(\d+)\s+recvn=(-?\d+)\s+"
    r"send=(\d+)\s+keep=(\d+)\s+recv=(\d+)\s+hold=(\d+)"
)

UINT = 4  # bytes per uint, for MiB display

# CR <rank> tag=<T> S  peer=<p> size=<bytes> off=<uints> left=<uints>
# (one line per send chunk on the batched path; send tag = 2*round+1)
RE_CHUNK = re.compile(
    r"^CR\s+(\d+)\s+tag=(\d+)\s+S\s+peer=(\d+)\s+size=(\d+)")


def parse(lines):
    """recs[round][rank] = dict(targ,recvn,send,keep,recv,hold,held_in,nbatch).

    nbatch = number of send chunks this rank posted that round (>1 means the
    message exceeded CR_MAX_MSG and the batched/chunked path was used).
    """
    recs = defaultdict(dict)
    nbatch = defaultdict(lambda: defaultdict(int))  # [round][rank] -> #S chunks
    for ln in lines:
        m = RE_FLOW.search(ln)
        if m:
            rnd, rank, targ, recvn, send, keep, recv, hold = (int(x) for x in m.groups())
            recs[rnd][rank] = dict(targ=targ, recvn=recvn, send=send, keep=keep,
                                   recv=recv, hold=hold, held_in=keep + send)
            continue
        m = RE_CHUNK.match(ln.strip())
        if m:
            rank = int(m.group(1)); tag = int(m.group(2))
            nbatch[(tag - 1) // 2][rank] += 1
    # attach batch counts to the flow records
    for rnd in recs:
        for rank, d in recs[rnd].items():
            d["nbatch"] = nbatch.get(rnd, {}).get(rank, 1 if d["send"] > 0 else 0)
    return recs


def ranks_rounds(recs):
    rounds = sorted(recs)
    ranks = sorted({r for rnd in recs.values() for r in rnd})
    return ranks, rounds


def text_table(recs):
    ranks, rounds = ranks_rounds(recs)
    if not ranks:
        return ("No CRFLOW lines found. Run with -v against a lib whose "
                "crystal_set_verbose emits level-2 output (verbose+1).")
    out = ["Crystal-router data flow (uints held, entering each round)"]
    hdr = "rank |" + "".join(" r%-8d" % r for r in rounds) + "  final"
    out.append(hdr)
    out.append("-" * len(hdr))
    for rk in ranks:
        cells = []
        for rnd in rounds:
            d = recs[rnd].get(rk)
            cells.append(" %-9d" % (d["held_in"] if d else 0))
        # final held = hold of the last round this rank appears in
        last = None
        for rnd in reversed(rounds):
            if rk in recs[rnd]:
                last = recs[rnd][rk]["hold"]
                break
        out.append("%4d |%s  %d" % (rk, "".join(cells), last if last is not None else 0))
    # edges
    out.append("")
    out.append("edges (round: src -> dst (send_uints[ xNmsg if chunked])):")
    for rnd in rounds:
        parts = []
        for rk, d in sorted(recs[rnd].items()):
            if d["send"] > 0:
                s = "%d->%d(%d" % (rk, d["targ"], d["send"])
                s += ("x%dmsg)" % d["nbatch"]) if d.get("nbatch", 1) > 1 else ")"
                parts.append(s)
        out.append("  r%d: " % rnd + "  ".join(parts))
    return "\n".join(out)


def _import_mpl():
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        return plt
    except ImportError:
        sys.stderr.write("matplotlib not available; skipping PNGs "
                         "(pip install matplotlib).\n")
        return None


def _lw(send, smax):
    """line width scaled to the largest send in the run."""
    if smax <= 0:
        return 0.5
    return 0.6 + 4.4 * (send / smax)


def _msz(held, hmax):
    """marker area scaled to the largest holding."""
    if hmax <= 0:
        return 40.0
    return 40.0 + 460.0 * (held / hmax)


def _fmt(uints):
    """compact label for a count of uints, e.g. '64k' / '1.0M' (bytes=uints*4)."""
    b = uints * UINT
    for unit, scale in (("G", 1 << 30), ("M", 1 << 20), ("k", 1 << 10)):
        if b >= scale:
            v = b / scale
            return ("%.0f%sB" % (v, unit)) if v >= 10 else ("%.1f%sB" % (v, unit))
    return "%dB" % b


def make_timeline(recs, path, title, dpi=110):
    plt = _import_mpl()
    if plt is None:
        return False
    ranks, rounds = ranks_rounds(recs)
    nR = len(rounds)
    # global scales
    smax = max((d["send"] for rnd in recs.values() for d in rnd.values()), default=0)
    hmax = max((d["held_in"] for rnd in recs.values() for d in rnd.values()),
               default=0)
    # also include final holdings in the node scale
    finals = {}
    for rnd in rounds:
        for rk, d in recs[rnd].items():
            finals[rk] = d["hold"]
    hmax = max([hmax] + list(finals.values()) + [1])

    fig, ax = plt.subplots(figsize=(2.2 * (nR + 1) + 2, 0.42 * len(ranks) + 2))

    # columns 0..nR : col c shows "held entering round c"; last col = final held
    xcols = list(range(nR + 1))

    # arrows: (round r, rank) -> (round r+1, targ), width ~ send
    for i, rnd in enumerate(rounds):
        for rk, d in recs[rnd].items():
            if d["send"] > 0:
                ax.annotate(
                    "", xy=(i + 1, d["targ"]), xytext=(i, rk),
                    arrowprops=dict(arrowstyle="-|>", color="#c1121f",
                                    lw=_lw(d["send"], smax), alpha=0.8,
                                    shrinkA=6, shrinkB=6))
                # message-size (+ batch count if chunked) at the arrow midpoint
                mx, my = (i + 0.5), (rk + d["targ"]) / 2.0
                lbl = _fmt(d["send"])
                if d.get("nbatch", 1) > 1:
                    lbl += "\n%d msg" % d["nbatch"]
                ax.text(mx, my, lbl, fontsize=6, color="#7a0c14",
                        ha="center", va="center", zorder=5,
                        bbox=dict(boxstyle="round,pad=0.12", fc="white",
                                  ec="none", alpha=0.7))

    # nodes for entering-round columns (label = buffer held)
    for i, rnd in enumerate(rounds):
        xs, ys, ss = [], [], []
        for rk in ranks:
            d = recs[rnd].get(rk)
            held = d["held_in"] if d else 0
            xs.append(i); ys.append(rk); ss.append(_msz(held, hmax))
            if held > 0:
                ax.text(i, rk - 0.34, _fmt(held), fontsize=6, color="#10465e",
                        ha="center", va="bottom", zorder=5)
        ax.scatter(xs, ys, s=ss, c="#1f77b4", zorder=3, edgecolors="white",
                   linewidths=0.5)
    # final column (label = final buffer held)
    xs, ys, ss = [], [], []
    for rk in ranks:
        held = finals.get(rk, 0)
        xs.append(nR); ys.append(rk); ss.append(_msz(held, hmax))
        if held > 0:
            ax.text(nR, rk - 0.34, _fmt(held), fontsize=6, color="#1d5f56",
                    ha="center", va="bottom", zorder=5)
    ax.scatter(xs, ys, s=ss, c="#2a9d8f", zorder=3, edgecolors="white",
               linewidths=0.5)

    ax.set_xticks(xcols)
    ax.set_xticklabels(["r%d in" % r for r in rounds] + ["final"])
    ax.set_yticks(ranks)
    ax.set_yticklabels(["rank %d" % r for r in ranks])
    ax.set_xlabel("router round")
    ax.set_title(title + "\n(node size/label ~ data held; arrow width/label ~ "
                 "message sent; sizes in bytes)")
    ax.grid(True, axis="x", alpha=0.2)
    ax.set_ylim(min(ranks) - 1, max(ranks) + 1)
    ax.invert_yaxis()
    fig.tight_layout()
    fig.savefig(path, dpi=dpi)
    plt.close(fig)
    return True


def make_rounds(recs, path, title, dpi=110):
    plt = _import_mpl()
    if plt is None:
        return False
    import math
    ranks, rounds = ranks_rounds(recs)
    n = len(ranks)
    pos = {rk: (math.cos(2 * math.pi * i / n), math.sin(2 * math.pi * i / n))
           for i, rk in enumerate(ranks)}
    smax = max((d["send"] for rnd in recs.values() for d in rnd.values()), default=0)
    hmax = max((d["hold"] for rnd in recs.values() for d in rnd.values()), default=1)

    nR = len(rounds)
    ncol = min(nR, 4)
    nrow = (nR + ncol - 1) // ncol
    fig, axes = plt.subplots(nrow, ncol, figsize=(3.4 * ncol, 3.4 * nrow),
                             squeeze=False)

    for idx, rnd in enumerate(rounds):
        ax = axes[idx // ncol][idx % ncol]
        # edges (label = message size sent)
        for rk, d in recs[rnd].items():
            if d["send"] > 0 and rk in pos and d["targ"] in pos:
                x0, y0 = pos[rk]; x1, y1 = pos[d["targ"]]
                ax.annotate("", xy=(x1, y1), xytext=(x0, y0),
                            arrowprops=dict(arrowstyle="-|>", color="#c1121f",
                                            lw=_lw(d["send"], smax), alpha=0.75,
                                            connectionstyle="arc3,rad=0.15",
                                            shrinkA=8, shrinkB=8))
                mx, my = (x0 + x1) / 2.0, (y0 + y1) / 2.0
                lbl = _fmt(d["send"])
                if d.get("nbatch", 1) > 1:
                    lbl += " /%dmsg" % d["nbatch"]
                ax.text(mx, my, lbl, fontsize=5.5, color="#7a0c14",
                        ha="center", va="center", zorder=5,
                        bbox=dict(boxstyle="round,pad=0.1", fc="white",
                                  ec="none", alpha=0.7))
        # nodes: size/label ~ held after this round
        for rk in ranks:
            x, y = pos[rk]
            d = recs[rnd].get(rk)
            held = d["hold"] if d else 0
            ax.scatter([x], [y], s=_msz(held, hmax), c="#1f77b4", zorder=3,
                       edgecolors="white", linewidths=0.5)
            lbl = ("%d\n%s" % (rk, _fmt(held))) if held > 0 else str(rk)
            ax.text(x * 1.22, y * 1.22, lbl, ha="center", va="center",
                    fontsize=6)
        ax.set_title("round %d" % rnd, fontsize=10)
        ax.set_xlim(-1.4, 1.4); ax.set_ylim(-1.4, 1.4)
        ax.set_aspect("equal"); ax.axis("off")

    for j in range(nR, nrow * ncol):
        axes[j // ncol][j % ncol].axis("off")

    fig.suptitle(title + "  (node size/label ~ data held; arrow width/label ~ "
                 "message sent; sizes in bytes)", fontsize=11)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig(path, dpi=dpi)
    plt.close(fig)
    return True


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logfile", nargs="?", default="-",
                    help="verbose log file (default: stdin)")
    ap.add_argument("-o", "--out", default="flow",
                    help="output PNG prefix (default: flow -> flow_timeline.png,"
                         " flow_rounds.png)")
    ap.add_argument("-t", "--title", default="crystal-router data flow",
                    help="figure title")
    ap.add_argument("-d", "--dpi", type=int, default=110,
                    help="output PNG resolution in dots per inch (default: 110)")

    # No arguments and nothing piped in -> show help instead of blocking on stdin.
    if len(sys.argv) == 1 and sys.stdin.isatty():
        ap.print_help()
        return

    args = ap.parse_args()

    if args.dpi <= 0:
        ap.error("--dpi must be a positive integer")

    if args.logfile == "-":
        lines = sys.stdin.readlines()
    else:
        with open(args.logfile) as f:
            lines = f.readlines()

    recs = parse(lines)
    print(text_table(recs))
    if not recs:
        return

    tl = args.out + "_timeline.png"
    rd = args.out + "_rounds.png"
    if make_timeline(recs, tl, args.title, args.dpi):
        print("Wrote %s" % tl)
    if make_rounds(recs, rd, args.title, args.dpi):
        print("Wrote %s" % rd)


if __name__ == "__main__":
    main()
