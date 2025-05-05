#include "../read_mem.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>

//#define CAPSTONE_DISAS

#ifdef CAPSTONE_DISAS
#include <capstone/capstone.h>
#endif

static read_mem_page_t page;

int main(int argc, char *argv[])
{

    if (argc != 2) {
        printf("usage: %s <addr>\n", argv[0]);
        return 1;
    }
    int fd = open("/proc/" PROC_READMEM, O_RDONLY);



    unsigned long addr=strtoul(argv[1],0,16);
    page.addr = addr;
    unsigned long offset = page.addr&0xfff;
    page.addr &= ~0xfffUL;

    if (ioctl(fd, REQ_READ_PAGE, &page) !=0) {
        fprintf(stderr, "Can't read 0x%lx. Output will be 00's\n", page.addr);
    }

#ifdef CAPSTONE_DISAS
    csh h;
    cs_open(CS_ARCH_X86, CS_MODE_64, &h);
    cs_insn *insn;
    int n = cs_disasm(h, page.data + offset, 0x1000 - offset, addr, 0, &insn);
    for (int i = 0; i < n; ++i) {
        printf("%lx\t", insn[i].address);
        for (int j = 0 ; j < insn[i].size; ++j) {
            printf("%02x ", insn[i].bytes[j]);
        }
        printf("\t%s %s\n",insn[i].mnemonic, insn[i].op_str);
    }
    cs_free(insn, n);
    cs_close(&h);
#else
    // alternatively pipe it to have objdump deal with it.
    fflush(stdout);
    write(STDOUT_FILENO, page.data+offset, 0x1000-offset);
#endif

    return 0;
}
