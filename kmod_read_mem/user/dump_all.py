import os
import sys
import subprocess

CMD_KSTART = "sudo grep -m1 \ _text$ /proc/kallsyms | cut -d\  -f1"
CMD_KEND   = "sudo grep -m1 \ _end$ /proc/kallsyms | cut -d\  -f1"

START = int(subprocess.check_output(CMD_KSTART, shell=True), 16)
STOP  = int(subprocess.check_output(CMD_KEND, shell=True), 16)

fname = f"{hex(START)}.bin"
if (os.path.exists(fname)):
    print(f"{fname} exists!")
    exit(1)

for x in range(START, STOP, 0x1000):
    print(f"Dump...{hex(x)}", end="\r")
    if(os.system('./main {} >> {}'.format(hex(x), fname)) != 0):
        print("\nFailed")
        sys.exit(1)
print("\ndone")
