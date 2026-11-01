#!/bin/bash
# mem.sh image data_dir: runs the image like the harness and prints parent-cgroup peak memory (MB).
S=/tmp/claude-0/-home-user/dad15a98-0c5f-5b36-9467-3f49e569bad7/scratchpad
img=$1; data=$2; cat $data/* > /dev/null; out=$S/out/mem-$img; rm -rf $out; mkdir -p $out; chmod 777 $out
pg=sdtest-$RANDOM; mkdir /sys/fs/cgroup/memory/$pg; echo 1 > /sys/fs/cgroup/memory/$pg/memory.use_hierarchy 2>/dev/null
cid=$(docker run -d --cgroup-parent=/$pg --user=65534:65534 --cpuset-cpus=${CPUS:-0-3} --memory=8g --memory-swap=8g --network=none --read-only --tmpfs /tmp:rw,size=1g -v $data:/data:ro -v $out:/out:rw $img)
docker wait $cid >/dev/null
peak=$(cat /sys/fs/cgroup/memory/$pg/memory.max_usage_in_bytes)
docker rm $cid >/dev/null; rmdir /sys/fs/cgroup/memory/$pg 2>/dev/null
echo "$img peak_MB $(python3 -c "print(round($peak/1048576,2))") $(sha256sum < $out/results.txt | cut -c1-12)"
