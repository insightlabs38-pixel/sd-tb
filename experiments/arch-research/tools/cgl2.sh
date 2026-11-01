#!/bin/bash
# cgcost.sh src.cpp [data]: deterministic cost proxy from cachegrind on one core (full seed 1 by default).
S=/tmp/claude-0/-home-user/dad15a98-0c5f-5b36-9467-3f49e569bad7/scratchpad
src=$1; data=${2:-$S/data/full-s1}; b=$S/cg/$(basename $src .cpp).cgbin
g++ -std=c++20 -O3 -march=x86-64-v3 -DNDEBUG -pthread -g $src -o $b 2>/dev/null
taskset -c 0 valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes --D1=49152,12,64 --LL=${LLSIZE:-655360},10,64 --cachegrind-out-file=$b.out $b --data $data --out $S/out/cg 2>&1 |
python3 -c "
import sys,re
v={}
for l in sys.stdin:
  m=re.search(r'(I *refs|D1  misses|LLd misses|Mispredicts):\s+([\d,]+)',l)
  if m: v[m.group(1).split()[0]]=int(m.group(2).replace(',',''))
c=v['I']+10*v['D1']+80*v['LLd']+15*v['Mispredicts']
print('$(basename $src)', 'Ir %.3fG D1 %.1fM LL %.1fM BrMiss %.1fM  cost %.3fG' % (v['I']/1e9, v['D1']/1e6, v['LLd']/1e6, v['Mispredicts']/1e6, c/1e9))
"
