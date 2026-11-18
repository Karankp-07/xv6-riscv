# Process Checkpoint and Restore for xv6-riscv

An implementation of process state checkpointing and restoration in the MIT xv6-riscv operating system, inspired by Linux CRIU (Checkpoint/Restore in Userspace).

This subsystem enables a running user process to snapshot its complete execution state (CPU registers, virtual memory pages, and open file descriptors) to an on-disk image, and later resurrect that process on demand or automatically across system power cuts and reboots.

## Motivation and System Overview

Processes in standard xv6 exist only ephemerally in volatile memory: when an application exits, crashes, or the machine shuts down, all computational progress is lost. In modern enterprise operating systems and cloud environments, process persistence is essential for:
- Live container and virtual machine migration across physical nodes.
- Long-running computational checkpointing in scientific high-performance computing.
- Non-disruptive kernel upgrades and fault recovery.

While xv6 is intentionally minimal, this project introduces a full-stack process persistence subsystem spanning three core operating system layers:
1. Low-level RISC-V hardware state preservation via the process trapframe.
2. Virtual memory introspection and dynamic page table re-mapping.
3. Direct filesystem serialization and deserialization via kernel inode operations.

## Modified and Created Files

To implement this subsystem, the following files were created or modified:

### Kernel Subsystems
- `kernel/checkpoint.h` (Created): Defines the on-disk binary image specification (`XV6C` magic, versioning, header metadata, open file descriptor representations, and sequential page record layout).
- `kernel/checkpoint.c` (Created): Core persistence engine containing:
  - `sys_checkpoint`: Walks the RISC-V page table, serializes CPU register state (`trapframe`), and streams mapped virtual memory pages directly to disk inodes.
  - `sys_restore`: Validates snapshot integrity, unmaps existing process pages (`uvmunmap`), allocates fresh physical memory frames (`kalloc`), maps them into the page table (`mappages`), and restores user registers.
- `kernel/syscall.h` (Modified): Allocates system call indices `SYS_checkpoint` (24) and `SYS_restore` (25).
- `kernel/syscall.c` (Modified): Registers system call prototypes and connects them into the kernel syscall dispatch table.
- `Makefile` (Modified): Integrates `kernel/checkpoint.o` into kernel linking (`OBJS`) and packages user-space utilities into `fs.img`.

### User Space and Boot System
- `user/user.h` (Modified): Exposes user-space function declarations for `checkpoint()` and `restore()`.
- `user/usys.pl` (Modified): Emits entry macros generating RISC-V assembly `ecall` trampolines.
- `user/cptest.c` (Created): A long-running computational workload that performs periodic checkpointing every five iterations, continuing execution without voluntary termination.
- `user/restore.c` (Created): Standalone shell utility (`restore <snapshot_file>`) allowing manual resurrection of any saved process snapshot.
- `user/init.c` (Modified): Enhanced the operating system boot sequence to inspect the filesystem for saved checkpoints (`snap.chk`) and automatically restore suspended workloads before launching the shell.

## System Architecture

The checkpoint mechanism introduces a custom binary image format identified by the magic identifier `0x43365658` (`XV6C` in little-endian ASCII).

### On-Disk Image Layout

```text
+-------------------------------------------------------+
| Header: struct xv6_chk_header (magic, version, sz,   |
|         pid, name, num_pages, file descriptors)       |
+-------------------------------------------------------+
| CPU Context: struct trapframe (36 RISC-V registers)   |
+-------------------------------------------------------+
| Page Record 0: [8-byte VA] [4096-byte Page Content]   |
+-------------------------------------------------------+
| Page Record 1: [8-byte VA] [4096-byte Page Content]   |
+-------------------------------------------------------+
| ...                                                   |
+-------------------------------------------------------+
| Page Record N: [8-byte VA] [4096-byte Page Content]   |
+-------------------------------------------------------+
```

### Checkpoint Workflow (`sys_checkpoint`)

1. Path resolution: Resolves the target snapshot filepath in user space and either truncates an existing file or allocates a new inode via the kernel directory lookup interface.
2. Page count audit: Traverses the active user virtual address space `[0, p->sz)` using `walkaddr()` to identify all currently mapped physical pages.
3. Header serialization: Writes `xv6_chk_header` containing process metadata, process memory boundary `sz`, and open file descriptor state.
4. Register capture: Serializes the `trapframe` containing user general-purpose registers (`a0`-`a7`, `s0`-`s11`, `t0`-`t6`), stack pointer (`sp`), and program counter (`epc`).
5. Memory streaming: For each valid page table entry, streams the 8-byte virtual address followed directly by 4096 bytes of physical memory to the disk inode. Physical addresses are read directly from kernel virtual memory mapping to avoid placing 4KB buffers on the constrained kernel stack.

### Restoration Workflow (`sys_restore`)

1. Image verification: Reads and verifies header magic and format version to reject corrupted or invalid files.
2. Address space teardown: Safely unmaps and frees all existing user memory pages (`uvmunmap`) of the host process.
3. Page table reconstruction: Iterates through each serialized page record, allocates a fresh physical frame via `kalloc()`, reads page data directly from disk into that frame, and installs the mapping into `p->pagetable` via `mappages()` with full user permissions (`PTE_R | PTE_W | PTE_X | PTE_U`).
4. Register restoration: Copies the saved user register state into `p->trapframe` while explicitly preserving the host kernel execution context fields (`kernel_satp`, `kernel_sp`, `kernel_trap`, `kernel_hartid`).
5. Seamless continuation: Sets `p->trapframe->a0 = 1` so that the resumed execution path perceives `checkpoint()` returning with a restore indicator, and returns to user space through the trampoline.

## System Call Interface

### `int checkpoint(const char *path)`
Captures the execution state of the calling process and writes it to the file specified by `path`. Returns `0` on initial save, or `-1` on error.

### `int restore(const char *path)`
Replaces the address space and register context of the calling process with the image stored at `path`. On success, execution branches directly to the instruction following the original checkpoint call, where `checkpoint()` returns `1`; on error, returns `-1`.

## Prerequisites and Toolchain Setup

To compile and execute xv6-riscv, you need the RISC-V 64-bit cross-compiler and QEMU emulator.

### Ubuntu / Debian
```bash
sudo apt update
sudo apt install git build-essential gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf qemu-system-misc
```

### Arch Linux
```bash
sudo pacman -S riscv64-elf-gcc riscv64-elf-binutils qemu-system-riscv
```

### macOS (via Homebrew)
```bash
brew tap riscv/riscv
brew install riscv-tools qemu
```

## Compilation and Execution

### 1. Build the Kernel and Filesystem
From the project root directory, run:
```bash
make clean
make
```

### 2. Launch xv6 in QEMU
Start the QEMU emulator with:
```bash
make qemu
```
To exit QEMU at any point, press `Ctrl-A` followed by `X`.

## End-to-End Power-Cut and Auto-Recovery Walkthrough

### Phase 1: Start the Workload
In the xv6 shell, run `cptest`:
```text
$ cptest
cptest running (pid 3)
  count = 1  (uptime 2)
  count = 2  (uptime 27)
  count = 3  (uptime 52)
  count = 4  (uptime 77)
  count = 5  (uptime 102)
checkpoint: saved 'cptest' (3 pages)
checkpoint saved at count 5 (safe to power off anytime)
  count = 6  (uptime 127)
  count = 7  (uptime 152)
```

### Phase 2: Pull the Power Plug (`Ctrl-A X`)
While `cptest` is actively printing around count 7, press `Ctrl-A` then `X`.
QEMU immediately terminates. The virtual CPU clock stops instantly, exactly simulating a sudden power failure or machine crash mid-computation.

### Phase 3: Reboot and Observe Automatic Recovery
Relaunch QEMU:
```bash
make qemu
```

During initialization, `init` detects the existing snapshot file on the disk image and automatically restores the interrupted job:
```text
xv6 kernel is booting

hart 1 starting
hart 2 starting
init: found saved snapshot 'snap.chk', auto-resuming process
loading snap.chk ...
restore: resumed 'cptest' at epc=0x44c
resumed from checkpoint at count 5
  count = 6  (uptime 26)
  count = 7  (uptime 51)
  count = 8  (uptime 76)
  count = 9  (uptime 101)
  count = 10  (uptime 126)
checkpoint: saved 'restore' (3 pages)
checkpoint saved at count 10 (safe to power off anytime)
  count = 11  (uptime 151)
  ...
  count = 20  (uptime 376)
checkpoint: saved 'restore' (3 pages)
checkpoint saved at count 20 (safe to power off anytime)
cptest finished successfully. snapshot removed.
init: starting sh
$
```

### Observations and Verification Points
1. True zero-overhead resumption: Execution continues from the exact loop counter and stack frame active at the latest checkpoint (`count = 5`), skipping already completed work.
2. Temporal dislocation: The system uptime resets on reboot, demonstrating that execution resumed within an entirely new kernel instance.
3. Continued fault tolerance: The restored process continues taking subsequent checkpoints (at count 10, 15, and 20). If power is cut a second time, it will resume from the newest snapshot.
4. Clean cleanup: Upon reaching count 20, the workload unlinks `snap.chk` so future reboots boot directly into the shell without restoring stale work.

## Key Technical Challenges Solved

### Kernel Stack Conservation
The xv6 kernel allocates exactly one 4096-byte page per process kernel stack. Attempting to allocate page-sized structs or buffers on the stack triggers instant page faults. This was resolved by streaming memory pages directly between disk inodes and allocated heap frames (`kalloc()`), keeping stack allocation strictly bounded to scalar descriptors.

### Trapframe Isolation
A direct copy of `struct trapframe` overwrites the active kernel stack pointer and kernel page table pointers (`kernel_satp`, `kernel_sp`), corrupting subsequent trap handling. The restore procedure selectively preserves the current CPU hardware contexts while restoring user general-purpose registers and program counters.
