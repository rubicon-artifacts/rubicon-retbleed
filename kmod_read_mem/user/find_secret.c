#include "../read_mem.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>

static read_mem_page_t page = {
    .needle = "root:$",
    .needle_len = sizeof("root:$") - 1
};

int main(int argc, char *argv[])
{
    int fd = open("/proc/" PROC_READMEM, O_RDONLY);
    int err;
    err = ioctl(fd, REQ_SCAN_PHYSMAP, &page);
    if (err == -135) {
        fprintf(stderr, "Can't read 0x%lx.\n", page.addr);
    } else if (err !=0) {
        fprintf(stderr, "Can't read 0x%lx (err=%d). Output will be 0's\n", page.addr, err);
    }

    printf("%lx\n", page.addr);

    // alternatively pipe it to have objdump deal with it.
    //
    /* fflush(stdout); */
    /* write(STDOUT_FILENO, page.data, 0x1000); */

    return 0;
}
