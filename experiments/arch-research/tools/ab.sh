#!/bin/bash
# ab.sh data_dir rounds bin1 bin2 ...: interleaved runs, prints median wall/user per binary.
S=/tmp/claude-0/-home-user/dad15a98-0c5f-5b36-9467-3f49e569bad7/scratchpad
data=$1; n=$2; shift 2
for b in "$@"; do : > $S/out/ab.$(basename $b); done
for i in $(seq $n); do for b in "$@"; do $S/tools/rt $b --data $data --out $S/out/ab-$(basename $b) 2>> $S/out/ab.$(basename $b); done; done

for b in "$@"; do f=$S/out/ab.$(basename $b); echo "$(basename $b) wall med $(sort -k2 -n $f | awk '{w[NR]=$2} END{printf "%.4f min %.4f", w[int((NR+1)/2)], w[1]}') user med $(sort -k6 -n $f | awk '{u[NR]=$6} END{printf "%.3f min %.3f", u[int((NR+1)/2)], u[1]}') $(sha256sum < $S/out/ab-$(basename $b)/results.txt | cut -c1-12)"; done
