#!/bin/bash
#-----------------------------------------------------------------------------
# run_ct2.sh - drive crystal_test2 across all-to-all modes and produce a
#              network-traffic visualization.
#
# It builds gslib twice:
#   (a) NORMAL cap  -> correctness across modes at moderate size
#   (b) SMALL cap   -> forces the batched (>CR_MAX_MSG) transfer path at tiny
#                      size, cheaply proving the batched path works (this is the
#                      capability that failed before the crystal.c F1 fix).
#
# NOTE: `make CFLAGS=...` does NOT rebuild an up-to-date crystal.o, so we
#       explicitly remove src/crystal.o before the small-cap build.
#
# Usage:  cd tests && ./run_ct2.sh [np]
#-----------------------------------------------------------------------------
set -e

NP=${1:-4}
MPIRUN=${MPIRUN:-mpirun}
ROOT=$(cd "$(dirname "$0")/.." && pwd)   # gslib-dev
TESTDIR=$ROOT/tests
OUTDIR=$TESTDIR/ct2_out
mkdir -p "$OUTDIR"

echo "== gslib-dev: $ROOT"
echo "== np=$NP  outdir=$OUTDIR"

build_lib() { # $1=destdir  $2=extra CFLAGS
  rm -f "$ROOT/src/crystal.o"            # force crystal.c recompile w/ new cap
  make -C "$ROOT" DESTDIR="$1" CFLAGS="-O2 $2" >/dev/null
}

build_test() { # $1=libdir  $2=binname
  mpicc -O2 -g -I"$1/include/gslib" "$TESTDIR/crystal_test2.c" \
        -L"$1/lib" -lgs -lm -o "$TESTDIR/$2"
}

#--- (a) NORMAL cap: correctness across modes ------------------------------
echo; echo "== [1/4] normal-cap build + correctness across modes =="
NORMDIR=$OUTDIR/lib_normal
build_lib "$NORMDIR" ""
build_test "$NORMDIR" crystal_test2

for n in 0 1 2; do
  echo "-- network $n --"
  $MPIRUN -n "$NP" "$TESTDIR/crystal_test2" -E 256 -n "$n" \
    2>&1 | grep -E "crystal_test2:|Recv:|Error:|test successful|failure"
done

#--- (b) SMALL cap: batched path (F1 capability) ---------------------------
echo; echo "== [2/4] small-cap build (-DCR_MAX_MSG=4096UL): batched path =="
SCDIR=$OUTDIR/lib_smallcap
build_lib "$SCDIR" "-DCR_MAX_MSG=4096UL"
build_test "$SCDIR" crystal_test2_smallcap

echo "-- all-to-one, tiny nelt, forced chunking --"
$MPIRUN -n "$NP" "$TESTDIR/crystal_test2_smallcap" -E 64 -n 2 \
  2>&1 | grep -E "Recv:|Error:|test successful|failure"

#--- (c) traffic visualization from a verbose all-to-one run ---------------
echo; echo "== [3/4] verbose all-to-one -> traffic PNG =="
LOG=$OUTDIR/all2one_verbose.log
$MPIRUN -n "$NP" "$TESTDIR/crystal_test2_smallcap" -E 64 -n 2 -v \
  > "$LOG" 2>&1 || true
python3 "$TESTDIR/viz_traffic.py" "$LOG" -o "$OUTDIR/traffic.png" || true

#--- (d) 16-rank per-round DATA FLOW (normal cap, one msg/pairing/round) ----
# hwthreads let us oversubscribe to 16 ranks on a smaller core count.
echo; echo "== [4/4] 16-rank data-flow PNGs (all2one + chain) =="
NP16=16
HWT=${HWT:---use-hwthread-cpus}
build_test "$NORMDIR" crystal_test2         # ensure normal-cap binary present
for spec in "2:all2one" "0:chain"; do
  mode=${spec%%:*}; name=${spec##*:}
  FLOG=$OUTDIR/flow16_${name}.log
  echo "-- network $mode ($name), np=$NP16 --"
  $MPIRUN $HWT -n "$NP16" "$TESTDIR/crystal_test2" -E 16 -n "$mode" -v \
    > "$FLOG" 2>&1 || true
  grep -E "test successful|failure" "$FLOG" || true
  python3 "$TESTDIR/viz_flow.py" "$FLOG" -o "$OUTDIR/flow16_${name}" \
    -t "16 ranks, ${name}" | tail -3 || true
done

# small-cap all2one flow: same funnel, but arrows now labelled with chunk counts
echo "-- network 2 (all2one), np=$NP16, SMALL CAP (batch counts on arrows) --"
FLOG=$OUTDIR/flow16sc_all2one.log
$MPIRUN $HWT -n "$NP16" "$TESTDIR/crystal_test2_smallcap" -E 16 -n 2 -v \
  > "$FLOG" 2>&1 || true
python3 "$TESTDIR/viz_flow.py" "$FLOG" -o "$OUTDIR/flow16sc_all2one" \
  -t "16 ranks, all-to-one (small cap, CR_MAX_MSG=4kB)" | tail -3 || true

#--- restore the normal-cap lib as the installed one -----------------------
rm -f "$ROOT/src/crystal.o"
make -C "$ROOT" >/dev/null
echo; echo "== done. Artifacts in $OUTDIR:"
echo "     traffic.png, flow16_{all2one,chain}_{timeline,rounds}.png,"
echo "     flow16sc_all2one_{timeline,rounds}.png (small-cap, batch counts) (+ logs)."
echo "   Normal-cap lib reinstalled."
