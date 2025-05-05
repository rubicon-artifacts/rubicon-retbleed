// safely read memory..
#include <linux/proc_fs.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/memblock.h>

#include "./read_mem.h"

static struct proc_dir_entry *procfs_file;

static read_mem_page_t p;

char read_this[100];

#define READ_MAX (get_num_physpages() << 12)

static long handle_ioctl(struct file *filp, unsigned int req,
                         unsigned long argp)
{
    unsigned long pa;
    int nerror = 0;
    char test = 0;
    u64 error_start;
    int found = 0;
    if (req == REQ_CLFLUSH) {
        asm("lfence");

        if (copy_from_user(&p, (void *)argp, 0x8) != 0) {
            return -EFAULT;
        }

        if (get_kernel_nofault(test, (void *)p.addr) != 0) {
            return -EFAULT;
        }

        // it's mapped we can safely try to flush it.
        asm volatile("mfence\n\t"
                     "clflushopt (%0)" ::"r"(p.addr));
    }

    if (req == REQ_READ_PAGE || req == REQ_READ_PHYS) {
        asm("lfence");
        if (copy_from_user(&p, (void *)argp, 0x8) != 0) {
            return -EFAULT;
        }
        if (req == REQ_READ_PHYS) {
            p.addr += page_offset_base;
        }

        if (get_kernel_nofault(test, (void *)p.addr) != 0) {
            return -EFAULT;
        }
        pr_info("Read from %lx\n", p.addr);
        /* memcpy(&p, (void*)(p.addr&~0xfffUL), 0x1000); */
        if (copy_to_user((void *)argp, (void *)(p.addr & ~0xfffUL), 0x1000) !=
            0) {
            return -EFAULT;
        }
    }
    if (req == REQ_SCAN_PHYSMAP) {
        if (copy_from_user(&p, (void *)argp, 100) != 0) {
            return -EFAULT;
        }

        if (p.needle_len > sizeof(p.needle)) {
            return -EFAULT;
        }

        pr_info("physmap @ 0x%lx maximum %lx\n", page_offset_base, READ_MAX);
        pr_info("needle: '%s', len %u\n", p.needle, p.needle_len);

        // there is usually a 2-4 GiB gap
        for (u64 i = 0x0; i < READ_MAX + (3UL << 30); i += 0x1000) {
            if (i >= 0x90000000 && i < 0x100000000)
                continue;

            p.addr = page_offset_base + i;
            if (get_kernel_nofault(read_this, (void *)p.addr) != 0) {
                if (nerror == 0) {
                    error_start = p.addr;
                }
                nerror += 1;
                if (copy_to_user((void *)argp, (void *)&p, 0x8) != 0) {
                    return -EFAULT;
                }
            } else if (nerror != 0) {
                pr_info("Non-readable 0x%llx -- 0x%llx", error_start,
                        error_start + (nerror << 12));

                pr_info("phys         0x%016llx -- 0x%016llx",
                        error_start - page_offset_base,
                        error_start + (nerror << 12) - page_offset_base);

                nerror = 0;
            }

            if (memcmp(read_this, p.needle, p.needle_len) == 0) {
                found += 1;
                pa = p.addr - page_offset_base;
                pr_info("(%d) found at @ 0x%lx (pa=%lx) \n", found, p.addr, pa);
                if (copy_to_user((void *)argp, &pa, 8) != 0) {
                    return -EFAULT;
                }
                return 0;
            }
        }
    }
    return 0;
}

static struct proc_ops pops = {
    .proc_ioctl = handle_ioctl,
    .proc_open = nonseekable_open,
    .proc_lseek = no_llseek,
};

static void mod_exit(void)
{
    proc_remove(procfs_file);
}

static int mod_init(void)
{
    procfs_file = proc_create(PROC_READMEM, 0, NULL, &pops);
    return 0;
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_LICENSE("GPL");
