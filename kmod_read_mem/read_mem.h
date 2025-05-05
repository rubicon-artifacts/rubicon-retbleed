#define PROC_READMEM "readmem"
#define REQ_READ_PHYS 0x233335
#define REQ_READ_PAGE 0x133335
#define REQ_CLFLUSH    0x2a0000
#define REQ_SCAN_PHYSMAP 0x6a0000

typedef struct read_mem_page {
  union {
    struct {
        unsigned long addr; // read-request and result
        unsigned int needle_len;
        char needle[100];
    };
    unsigned char data[0x1000]; // response
  };
} read_mem_page_t;
