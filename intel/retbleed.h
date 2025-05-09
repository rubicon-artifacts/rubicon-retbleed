#include <err.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define SHM_SZ (1ul<<21)
#define SHM_PATH "/dev/shm/tjo"

#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_BLUE   "\033[34m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_NC     "\033[0m"

#define COLOR_BG_RED        "\033[41m"
#define COLOR_BG_PRED       "\033[101m"
#define COLOR_BG_GRN        "\033[42m"
#define COLOR_BG_PGRN       "\033[102m"
#define COLOR_BG_YEL        "\033[43m"
#define COLOR_BG_PYEL       "\033[103m"
#define COLOR_BG_BLU        "\033[44m"
#define COLOR_BG_PBLU       "\033[104m"
#define COLOR_BG_MAG        "\033[45m"
#define COLOR_BG_PMAG       "\033[105m"
#define COLOR_BG_CYN        "\033[46m"
#define COLOR_BG_PCYN       "\033[106m"
#define COLOR_BG_WHT        "\033[47m"
#define COLOR_BG_PWHT       "\033[107m"

#define INFO(...)    printf("\r[" COLOR_BLUE "-" COLOR_NC"] " __VA_ARGS__);
#define SUCCESS(...) printf("\r[" COLOR_GREEN "*" COLOR_NC"] " __VA_ARGS__);
#define ERROR(...)   printf("\r[" COLOR_RED "!" COLOR_NC"] " __VA_ARGS__);
#define DEBUG(...)    printf("\r[" COLOR_YELLOW "d" COLOR_NC"] " __VA_ARGS__);
#define WARN DEBUG

#define MMAP_FLAGS (MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE)
#define PROT_RWX (PROT_READ | PROT_WRITE | PROT_EXEC)
#define PROT_RW (PROT_READ | PROT_WRITE)

typedef unsigned long u64;
typedef unsigned char u8;

#define MIN(x,n) (x > n ? n : x)
#define str(s) #s
#define xstr(s) str(s)
#define NOP asm volatile("nop")
#define NOPS_str(n) ".rept " xstr(n) "\n\t"\
    "nop\n\t"\
    ".endr\n\t"
#define NOPS(n) asm volatile(NOPS_str(n))

#define MAP_OR_DIE(...) do {\
    if (mmap(__VA_ARGS__) == MAP_FAILED) err(1, "mmap");\
} while(0)

static long va_to_phys(int fd, long va)
{
    unsigned long pa_with_flags;

    lseek(fd, ((long) (~0xfffUL)&va)>>9, SEEK_SET);
    read(fd, &pa_with_flags, 8);
    return pa_with_flags<<12 | (va & 0xfff);
}


/**
 * Descriptor of some memory, i.e., our reload buffer or probe buffer.
 */
struct mem_info {
    union {
        u64 va;
        u8* buf;
    };
    u64 kva;
    u64 pa;
};

#define NPHYS 8
struct rb_phys {
    u64 physmap; // we can store physmap here, why not.
    u64 phys[NPHYS]; // we can store pas of pages here. so that we can support
                     // many parallel cores

    // shadow_pa *hint*. it's not accurate, expect it to be off by +/- 4 pages.
    u64 shadow_pa;
};

static inline unsigned long get_ms() {
	static struct timeval tp;
	gettimeofday(&tp, 0);
	return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

static inline __attribute__((always_inline)) u64 rdtsc(void) {
	u64 lo, hi;
    asm volatile ("CPUID\n\t"
            "RDTSC\n\t"
            "movq %%rdx, %0\n\t"
            "movq %%rax, %1\n\t" : "=r" (hi), "=r" (lo)::
            "%rax", "%rbx", "%rcx", "%rdx");
	return (hi << 32) | lo;
}

static inline __attribute__((always_inline)) u64 rdtscp(void) {
    u64 lo, hi;
    asm volatile("RDTSCP\n\t"
            "movq %%rdx, %0\n\t"
            "movq %%rax, %1\n\t"
            "CPUID\n\t": "=r" (hi), "=r" (lo):: "%rax",
            "%rbx", "%rcx", "%rdx");
    return (hi << 32) | lo;
}

// flip to 1 when we SHOULD segfault and not crash the program
static int should_segfault = 0;
static sigjmp_buf env;
static void handle_segv(int sig, siginfo_t *si, void *unused)
{
    if (should_segfault) {
        siglongjmp(env, 12);
        return;
    };

    fprintf(stderr, "Not handling SIGSEGV\n");
    exit(sig);
}

static void inline setup_segv_handler() {
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = &handle_segv;
    sigaction (SIGSEGV, &sa, NULL);
}

static inline __attribute__((always_inline)) void reload_range(u8* base, long stride, int n, u64 *results) {
	__asm__ volatile("mfence\n");
	for (u64 k = 0; k < n; ++k) {
        u64 c = (k*13+9)&(n-1);
		unsigned volatile char *p = base + (stride * c);
		u64 t0 = rdtsc();
		*(volatile unsigned char *)p;
		u64 dt = rdtscp() - t0;
		if (dt < 130) results[c]++;
	}
}

static inline __attribute__((always_inline)) void flush_range(u8* start, long stride, int n) {
    for (u64 k = 0; k < n; ++k) {
        volatile void *p = start + k * stride;
        __asm__ volatile("clflushopt (%0)\n"::"r"(p));
        __asm__ volatile("clflushopt (%0)\n"::"r"(p));
    }
}

static size_t max_index(size_t *results, long max_byte) {
	size_t max = 0x0;
	for (size_t c = 0x1; c <= max_byte; ++c) {
		if (results[c] > results[max])
			max = c;
	}
	return max;
}

#include <fcntl.h>
static void *map_shared(char *path, void *ptr, u64 sz, int flags) {
    int fd_shm = open(path, flags, S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH|S_IRGRP|S_IWGRP);
    if (fd_shm < 0) {
        err(1, "open shm");
    }
    ftruncate(fd_shm, sz);
    if (mmap(ptr, sz, PROT_RW, MAP_SHARED_VALIDATE, fd_shm, 0) == MAP_FAILED) {
        err(1, "mmap %p", ptr);
    }
    return 0;
}


static void *
map_or_die(void *addr, u64 sz)
{
    if (mmap(addr, sz, PROT_RW, MMAP_FLAGS, -1, 0) == MAP_FAILED) {
        err(1, "mmap %p\n", addr);
    }
    return addr;
}

static void *
mmap_huge(void *addr, u64 sz)
{
    map_or_die(addr, sz);

    if (madvise(addr, 1UL<<21, MADV_HUGEPAGE) != 0) {
	    err(2, "madv %p", addr);
    }

    *(char*)addr=0x1; // map page.
    return addr;
}
