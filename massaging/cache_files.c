/*
 * Copyright (C) 2025 Matej Bölcskei, ETH Zurich
 * Licensed under the GNU General Public License as published by the Free Software Foundation, version 3.
 * See LICENSE or <https://www.gnu.org/licenses/gpl-3.0.html> for details.
 * 
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define _GNU_SOURCE

#include "cache_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define NUM_FILES 10
int fds[NUM_FILES];
size_t sizes[NUM_FILES];
void *ptrs[NUM_FILES];

void cache_files() {
    struct stat st;
    stat("/etc/ld.so.cache", &st);
    sizes[0] = st.st_size;
    fds[0] = open("/etc/ld.so.cache", O_RDONLY);
    if (fds[0] < 0) { perror("open: /etc/ld.so.cache"); exit(EXIT_FAILURE); }
    ptrs[0] = mmap(NULL, sizes[0], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[0], 0);
    stat("/etc/nsswitch.conf", &st);
    sizes[1] = st.st_size;
    fds[1] = open("/etc/nsswitch.conf", O_RDONLY);
    if (fds[1] < 0) { perror("open: /etc/nsswitch.conf"); exit(EXIT_FAILURE); }
    ptrs[1] = mmap(NULL, sizes[1], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[1], 0);
    stat("/etc/passwd", &st);
    sizes[2] = st.st_size;
    fds[2] = open("/etc/passwd", O_RDONLY);
    if (fds[2] < 0) { perror("open: /etc/passwd"); exit(EXIT_FAILURE); }
    ptrs[2] = mmap(NULL, sizes[2], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[2], 0);
    stat("/lib/x86_64-linux-gnu/libc.so.6", &st);
    sizes[3] = st.st_size;
    fds[3] = open("/lib/x86_64-linux-gnu/libc.so.6", O_RDONLY);
    if (fds[3] < 0) { perror("open: /lib/x86_64-linux-gnu/libc.so.6"); exit(EXIT_FAILURE); }
    ptrs[3] = mmap(NULL, sizes[3], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[3], 0);
    stat("/lib/x86_64-linux-gnu/libnss_sss.so.2", &st);
    sizes[4] = st.st_size;
    fds[4] = open("/lib/x86_64-linux-gnu/libnss_sss.so.2", O_RDONLY);
    if (fds[4] < 0) { perror("open: /lib/x86_64-linux-gnu/libnss_sss.so.2"); exit(EXIT_FAILURE); }
    ptrs[4] = mmap(NULL, sizes[4], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[4], 0);
    stat("/lib/x86_64-linux-gnu/libnss_systemd.so.2", &st);
    sizes[5] = st.st_size;
    fds[5] = open("/lib/x86_64-linux-gnu/libnss_systemd.so.2", O_RDONLY);
    if (fds[5] < 0) { perror("open: /lib/x86_64-linux-gnu/libnss_systemd.so.2"); exit(EXIT_FAILURE); }
    ptrs[5] = mmap(NULL, sizes[5], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[5], 0);
    stat("/proc/self/loginuid", &st);
    sizes[6] = st.st_size;
    fds[6] = open("/proc/self/loginuid", O_RDONLY);
    if (fds[6] < 0) { perror("open: /proc/self/loginuid"); exit(EXIT_FAILURE); }
    ptrs[6] = mmap(NULL, sizes[6], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[6], 0);
    stat("/proc/sys/kernel/random/boot_id", &st);
    sizes[7] = st.st_size;
    fds[7] = open("/proc/sys/kernel/random/boot_id", O_RDONLY);
    if (fds[7] < 0) { perror("open: /proc/sys/kernel/random/boot_id"); exit(EXIT_FAILURE); }
    ptrs[7] = mmap(NULL, sizes[7], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[7], 0);
    stat("/usr/lib/locale/locale-archive", &st);
    sizes[8] = st.st_size;
    fds[8] = open("/usr/lib/locale/locale-archive", O_RDONLY);
    if (fds[8] < 0) { perror("open: /usr/lib/locale/locale-archive"); exit(EXIT_FAILURE); }
    ptrs[8] = mmap(NULL, sizes[8], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[8], 0);
    stat("/var/lib/sss/mc/passwd", &st);
    sizes[9] = st.st_size;
    fds[9] = open("/var/lib/sss/mc/passwd", O_RDONLY);
    if (fds[9] < 0) { perror("open: /var/lib/sss/mc/passwd"); exit(EXIT_FAILURE); }
    ptrs[9] = mmap(NULL, sizes[9], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[9], 0);
}

void close_files() {
    for (int i = 0; i < NUM_FILES; ++i) {
        if (ptrs[i] != MAP_FAILED) munmap(ptrs[i], sizes[i]);
        if (fds[i] >= 0) close(fds[i]);
    }
}
