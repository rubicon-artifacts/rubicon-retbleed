/*
 * Copyright (C) 2025 Matej Bölcskei, ETH Zurich
 * Licensed under the GNU General Public License as published by the Free Software Foundation, version 3.
 * See LICENSE or <https://www.gnu.org/licenses/gpl-3.0.html> for details.
 * 
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "massage_shadow.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <err.h>

uintptr_t va_to_phys(void *vaddr)
{
  unsigned long pa_with_flags;
  long va = (long)vaddr;

  int fd_pagemap = open("/proc/self/pagemap", O_RDONLY);
  if (fd_pagemap <= 0) {
      err(1, "hm. weird.");
  }

  lseek(fd_pagemap, ((long) (~0xfffUL)&va)>>9, SEEK_SET);
  if (read(fd_pagemap, &pa_with_flags, 8) == 0) {
      fprintf(stderr, "problem reading pagemap, are you root?\n");
      return 0;
  }

  close(fd_pagemap);

  return (uintptr_t)(pa_with_flags<<12 | (va & 0xfff));
}

int main(void) {
  void *contiguous_ptr = NULL;
  uintptr_t contiguous_pa;
  uintptr_t shadow_hint;
  if (allocate_contiguous_massage_shadow(va_to_phys, 0, &contiguous_ptr, &contiguous_pa, &shadow_hint)) {
    fprintf(stderr, "Failed to allocate contiguous memory\n");
    return -1;
  }
  printf("Physical address: %lx\n", shadow_hint);
  return 0;
}