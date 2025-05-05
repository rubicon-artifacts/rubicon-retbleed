# Copyright (C) 2025 Matej Bölcskei, ETH Zurich
# Licensed under the GNU General Public License as published by the Free Software Foundation, version 3.
# See LICENSE or <https://www.gnu.org/licenses/gpl-3.0.html> for details.

# SPDX-License-Identifier: GPL-3.0-only

import re
import argparse

def parse_strace_file(filename):
    opened_files = set()
    fd_stack = {}

    openat_pattern = re.compile(
        r'openat\(\s*(\d+|AT_FDCWD)\s*,\s*"([^"]+)"[^)]*\)\s*=\s*(-?\d+)(?:\s+(\w+)\s+\(([^)]+)\))?'
    )
    close_pattern = re.compile(r'close\(\s*(\d+)\s*\)')
    read_pattern = re.compile(r'read\(\s*(\d+)\s*,')
    mmap_pattern = re.compile(r'mmap\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+,\s*(\d+),')

    with open(filename) as f:
        for line in f:
            match = openat_pattern.search(line)
            if match:
                fd = match.group(1)
                path = match.group(2)
                ret_code = match.group(3)
                error_code = match.group(4)
                print(f"openat: fd: {fd}, path: {path}, ret_code: {ret_code}, error_code: {error_code}")

                if ret_code == "-1":
                    continue

                if path.startswith("/"):
                    fd_stack[ret_code] = path
                else:
                    if fd in fd_stack:
                        path = fd_stack[fd] + path
                        fd_stack[ret_code] = path
                    elif fd == "AT_FDCWD":
                        path = "CWD/" + path
                        fd_stack[ret_code] = path
                    else:
                        print(f"Warning: {fd} not found in stack, path: {path}")

                continue

            match = close_pattern.search(line)
            if match:
                fd = match.group(1)
                print(f"close: fd: {fd}")
                if fd in fd_stack:
                    del fd_stack[fd]
                continue

            match = read_pattern.search(line)
            if match:
                fd = match.group(1)
                print(f"read: fd: {fd}")
                if fd in fd_stack:
                    opened_files.add(fd_stack[fd])
                continue

            match = mmap_pattern.search(line)
            if match:
                fd = match.group(1)
                print(f"mmap: fd: {fd}")
                if fd in fd_stack:
                    opened_files.add(fd_stack[fd])
                continue

    return sorted(opened_files)

def generate_c_code(paths):
    c_code = (
        '#define _GNU_SOURCE\n'
        '\n'
        '#include "cache_files.h"\n'
        '\n'
        '#include <stdio.h>\n'
        '#include <stdlib.h>\n'
        '#include <fcntl.h>\n'
        '#include <unistd.h>\n'
        '#include <sys/stat.h>\n'
        '#include <sys/mman.h>\n'
        '\n'
        f'#define NUM_FILES {len(paths)}\n'
        'int fds[NUM_FILES];\n'
        'size_t sizes[NUM_FILES];\n'
        'void *ptrs[NUM_FILES];\n'
        '\n'
        'void cache_files() {\n'
        '    struct stat st;\n'
    )

    for i, file in enumerate(paths):
        c_code += f'    stat("{file}", &st);\n'
        c_code += f'    sizes[{i}] = st.st_size;\n'
        c_code += f'    fds[{i}] = open("{file}", O_RDONLY);\n'
        c_code += f'    if (fds[{i}] < 0) {{ perror("open: {file}"); exit(EXIT_FAILURE); }}\n'
        c_code += f'    ptrs[{i}] = mmap(NULL, sizes[{i}], PROT_READ | PROT_EXEC, MAP_SHARED | MAP_POPULATE, fds[{i}], 0);\n'
        
    c_code += (
        '}\n'
        '\n'
        'void close_files() {\n'
        '    for (int i = 0; i < NUM_FILES; ++i) {\n'
        '        if (ptrs[i] != MAP_FAILED) munmap(ptrs[i], sizes[i]);\n'
        '        if (fds[i] >= 0) close(fds[i]);\n'
        '    }\n'
        '}\n'
    )
    return c_code


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Extract opened file paths from strace log")
    parser.add_argument("strace_file", help="Path to strace output")

    args = parser.parse_args()
    paths = parse_strace_file(args.strace_file)
    for path in paths:
        print(path)
    c_code = generate_c_code(paths)
    with open("cache_files.c", "w") as f:
        f.write(c_code)