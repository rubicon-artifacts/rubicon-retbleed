This thing will will dump the page of the given address and dump the bytes from
the page-offset until the end of the page to stdout


```bash
make main
```

### Usage

Disassemble some code chunk:
```bash
$ sudo grep T\ __x86_indirect_thunk_rax /proc/kallsyms
ffffffffb9c016c0 T __x86_indirect_thunk_rax

$ ./main ffffffffb9c016c0 > bin

$ ./bin2x64.sh 0xffffffffb9c016c0 bin | less
```

Dump the entire code section:
```
python ./dump_all.py
```

Dump hex:
```
./dump_hex.sh 0xffffffffb9c016c0 
```

