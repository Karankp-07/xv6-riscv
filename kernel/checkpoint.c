// kernel/checkpoint.c
// Process checkpoint and restore for xv6.
//
// sys_checkpoint() serializes the calling process to a file:
//   header -> trapframe -> pages (each as: 8-byte VA + 4096-byte data)
//
// sys_restore() reads that file and replaces the calling process's
// address space and registers so it resumes where the snapshot was taken.
//
// Important: we never put a full 4096-byte page buffer on the kernel stack
// since the kernel stack is only one page (4096 bytes) itself. For page
// data we either write directly from the physical address or kalloc() a
// temporary frame on the heap.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "defs.h"
#include "stat.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "checkpoint.h"

// Write `len` bytes from kernel buffer `src` into inode `ip` at `*off`.
static int
chk_write(struct inode *ip, uint *off, void *src, uint len)
{
  if (writei(ip, 0, (uint64)src, *off, len) != (int)len)
    return -1;
  *off += len;
  return 0;
}

// Read `len` bytes from inode `ip` at `*off` into kernel buffer `dst`.
static int
chk_read(struct inode *ip, uint *off, void *dst, uint len)
{
  if (readi(ip, 0, (uint64)dst, *off, len) != (int)len)
    return -1;
  *off += len;
  return 0;
}

// sys_checkpoint(const char *path)
//
// Saves the calling process to a snapshot file at `path`.
// Returns 0 on success, -1 on error.
uint64
sys_checkpoint(void)
{
  char path[MAXPATH];
  struct proc *p = myproc();
  struct inode *ip;
  uint off = 0;
  uint64 va, num_pages;
  struct xv6_chk_header hdr;
  int i;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  ip = namei(path);
  if (ip) {
    ilock(ip);
    itrunc(ip);
  } else {
    char ppath[MAXPATH];
    char name[DIRSIZ];
    struct inode *dp;

    memmove(ppath, path, MAXPATH);
    dp = nameiparent(ppath, name);
    if (!dp) {
      end_op();
      return -1;
    }
    ilock(dp);
    ip = ialloc(dp->dev, T_FILE);
    if (!ip) {
      iunlockput(dp);
      end_op();
      return -1;
    }
    ilock(ip);
    ip->nlink = 1;
    iupdate(ip);
    if (dirlink(dp, name, ip->inum) < 0) {
      ip->nlink = 0;
      iupdate(ip);
      iunlockput(ip);
      iunlockput(dp);
      end_op();
      return -1;
    }
    iunlockput(dp);
  }

  // Count mapped pages first so the header has the right number.
  num_pages = 0;
  for (va = 0; va < p->sz; va += PGSIZE) {
    if (walkaddr(p->pagetable, va) != 0)
      num_pages++;
  }

  memset(&hdr, 0, sizeof(hdr));
  hdr.magic     = XV6_CHK_MAGIC;
  hdr.version   = XV6_CHK_VERSION;
  hdr.pid       = p->pid;
  hdr.sz        = p->sz;
  hdr.num_pages = num_pages;
  memmove(hdr.name, p->name, 16);

  for (i = 0; i < NOFILE; i++) {
    struct file *f = p->ofile[i];
    if (!f) { hdr.fds[i].valid = 0; continue; }
    hdr.fds[i].valid    = 1;
    hdr.fds[i].type     = f->type;
    hdr.fds[i].readable = f->readable;
    hdr.fds[i].writable = f->writable;
    hdr.fds[i].off      = f->off;
  }

  if (chk_write(ip, &off, &hdr, sizeof(hdr)) < 0)
    goto err;

  if (chk_write(ip, &off, p->trapframe, sizeof(struct trapframe)) < 0)
    goto err;

  // Write each mapped page as: [8-byte VA][4096-byte data].
  // We pass the physical address directly to writei (user_dst=0 means
  // it treats the address as a kernel VA, which works because xv6 maps
  // all physical RAM at its physical address in the kernel page table).
  for (va = 0; va < p->sz; va += PGSIZE) {
    uint64 pa = walkaddr(p->pagetable, va);
    if (pa == 0)
      continue;

    if (chk_write(ip, &off, &va, sizeof(uint64)) < 0)
      goto err;
    if (writei(ip, 0, pa, off, PGSIZE) != PGSIZE)
      goto err;
    off += PGSIZE;
  }

  iunlockput(ip);
  end_op();
  printk("checkpoint: saved '%s' (%ld pages)\n", p->name, num_pages);
  return 0;

err:
  iunlockput(ip);
  end_op();
  printk("checkpoint: write error\n");
  return -1;
}

// sys_restore(const char *path)
//
// Reads a snapshot file and replaces the calling process with it.
// On success execution resumes at the saved epc and checkpoint() appears
// to return 0 in the restored process (we set a0 = 0).
uint64
sys_restore(void)
{
  char path[MAXPATH];
  struct proc *p = myproc();
  struct inode *ip;
  uint off = 0;
  struct xv6_chk_header hdr;
  struct trapframe saved_tf;
  uint64 i, pg_va;
  char *mem;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  ip = namei(path);
  if (!ip) {
    end_op();
    printk("restore: not found: %s\n", path);
    return -1;
  }
  ilock(ip);

  if (chk_read(ip, &off, &hdr, sizeof(hdr)) < 0)
    goto err;

  if (hdr.magic != XV6_CHK_MAGIC) {
    printk("restore: not a valid checkpoint file\n");
    goto err;
  }
  if (hdr.version != XV6_CHK_VERSION) {
    printk("restore: unsupported version %d\n", hdr.version);
    goto err;
  }

  if (chk_read(ip, &off, &saved_tf, sizeof(saved_tf)) < 0)
    goto err;

  // Free the current address space before rebuilding from the snapshot.
  if (p->sz > 0)
    uvmunmap(p->pagetable, 0, p->sz / PGSIZE, 1);
  p->sz = 0;

  // For each saved page: read the VA, kalloc a frame, read the data into it,
  // then map it. We use kalloc here so the page data never sits on the kernel
  // stack (which is only 4096 bytes — same size as one page).
  for (i = 0; i < hdr.num_pages; i++) {
    if (chk_read(ip, &off, &pg_va, sizeof(uint64)) < 0)
      goto err_no_as;

    mem = kalloc();
    if (!mem)
      goto err_no_as;

    if (readi(ip, 0, (uint64)mem, off, PGSIZE) != PGSIZE) {
      kfree(mem);
      goto err_no_as;
    }
    off += PGSIZE;

    if (mappages(p->pagetable, pg_va, PGSIZE, (uint64)mem,
                 PTE_R | PTE_W | PTE_X | PTE_U) < 0) {
      kfree(mem);
      goto err_no_as;
    }
  }

  p->sz = hdr.sz;

  // Restore registers but keep the live kernel-side trapframe fields —
  // those belong to the current kernel execution context, not the saved user.
  uint64 k_satp   = p->trapframe->kernel_satp;
  uint64 k_sp     = p->trapframe->kernel_sp;
  uint64 k_trap   = p->trapframe->kernel_trap;
  uint64 k_hartid = p->trapframe->kernel_hartid;

  memmove(p->trapframe, &saved_tf, sizeof(struct trapframe));

  p->trapframe->kernel_satp   = k_satp;
  p->trapframe->kernel_sp     = k_sp;
  p->trapframe->kernel_trap   = k_trap;
  p->trapframe->kernel_hartid = k_hartid;

  // The restored process should see checkpoint() return 0 (success).
  p->trapframe->a0 = 0;

  iunlockput(ip);
  end_op();
  printk("restore: resumed '%s' at epc=0x%lx\n", hdr.name, p->trapframe->epc);
  return 0;

err:
  iunlockput(ip);
  end_op();
  return -1;

err_no_as:
  // Address space is gone, can't continue safely.
  iunlockput(ip);
  end_op();
  printk("restore: fatal, killing process\n");
  p->killed = 1;
  return -1;
}
