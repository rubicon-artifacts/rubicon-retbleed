# Rubicon-Enhanced Retbleed: Massaging Source Code and Proof of Concept

This directory contains the source code for the massaging functionality used in the Rubicon-enhanced Retbleed attack. The massaging code is designed to position the `/etc/shadow` file at a predictable location in physical memory, facilitating more efficient exploitation of the Retbleed vulnerability. The implementation includes two key components:

- **`massage_shadow`**: Handles the eviction and massaging of the shadow file.
- **`cache_files`**: Manages file caching after eviction to enhance massaging precision.

## File Dependency Extraction

Since dynamically loaded files vary by system, we provide a script, `extract_files.py`, to identify and process these dependencies. The script uses `strace` to determine which files are loaded into the cache when running `expiry`. It then updates `cache_files.c` with the appropriate files.

To use the script:
```bash
strace -o strace.log expiry -c
python extract_files.py strace.log
```

## Compilation

To compile the code, simply run:
```bash
make
```

## Proof of Concept (PoC)

We provide a hardware-independent proof of concept for the massaging code. This version replaces the physical address leaking primitive with a pagemap-based implementation (requires root privileges). The PoC is compatible with any Linux system that has the necessary permissions.

To execute the PoC:
```bash
sudo ./poc
```

The PoC will attempt to massage the `/etc/shadow` file into a predictable physical memory location. After execution, it will output a hint to the physical address of the `/etc/shadow` file. You can verify this address using the kernel module provided in the `kmod_read_mem/` directory.

## Verifying the Physical Address

To verify the physical address of the `/etc/shadow` file:

1. Compile the kernel module and load it:
  ```bash
  make -C ../kmod_read_mem
  sudo insmod ../kmod_read_mem/read_mem.ko
  ```

2. Compile the user-space counterpart of the module:
  ```bash
  make -C ../kmod_read_mem/user
  ```

3. Run the user-space program:
  ```bash
  ../kmod_read_mem/user/find_secret
  ```

The program will print the physical address of the `/etc/shadow` file. Compare this address with the one output by the PoC to confirm successful massaging.