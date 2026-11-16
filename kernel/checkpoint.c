// kernel/checkpoint.c
// Process checkpoint and restore for xv6.
//
// sys_checkpoint() serializes the calling process to a file:
//   header -> trapframe -> pages
//
// sys_restore() reads that file and replaces the calling process's
// address space and registers so it resumes where the snapshot was taken.

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
// Bumps *off forward so callers can call this sequentially.
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
// The file is created if it doesn't exist, or truncated if it does.
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
  struct xv6_chk_page pg;
  int i;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  ip = namei(path);
  if (ip) {
    ilock(ip);
    itrunc(ip);
  } else {
    char parent_path[MAXPATH];
    char name[DIRSIZ];
    struct inode *dp;

    memmove(parent_path, path, MAXPATH);
    dp = nameiparent(parent_path, name);
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
    ip->major = 0;
    ip->minor = 0;
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

  // Count how many user pages are actually mapped so we can put
  // the right number in the header before writing any page data.
  num_pages = 0;
  for (va = 0; va < p->sz; va += PGSIZE) {
    if (walkaddr(p->pagetable, va) != 0)
      num_pages++;
  }

  // Build the header with process metadata and open file info.
  memset(&hdr, 0, sizeof(hdr));
  hdr.magic     = XV6_CHK_MAGIC;
  hdr.version   = XV6_CHK_VERSION;
  hdr.pid       = p->pid;
  hdr.sz        = p->sz;
  hdr.num_pages = num_pages;
  memmove(hdr.name, p->name, 16);

  for (i = 0; i < NOFILE; i++) {
    struct file *f = p->ofile[i];
    if (!f) {
      hdr.fds[i].valid = 0;
      continue;
    }
    hdr.fds[i].valid    = 1;
    hdr.fds[i].type     = f->type;
    hdr.fds[i].readable = f->readable;
    hdr.fds[i].writable = f->writable;
    hdr.fds[i].off      = f->off;
    hdr.fds[i].path[0]  = '\0';
  }

  if (chk_write(ip, &off, &hdr, sizeof(hdr)) < 0)
    goto err;

  // Save the full trapframe — this captures epc, sp, ra, and all
  // general-purpose registers exactly as they are at this moment.
  if (chk_write(ip, &off, p->trapframe, sizeof(struct trapframe)) < 0)
    goto err;

  // Walk the page table and dump every mapped user page to disk.
  for (va = 0; va < p->sz; va += PGSIZE) {
    uint64 pa = walkaddr(p->pagetable, va);
    if (pa == 0)
      continue;

    pg.va = va;
    memmove(pg.data, (void *)pa, PGSIZE);

    if (chk_write(ip, &off, &pg, sizeof(pg)) < 0)
      goto err;
  }

  iunlockput(ip);
  end_op();
  printk("checkpoint: saved '%s' (%lld pages)\n", p->name, num_pages);
  return 0;

err:
  iunlockput(ip);
  end_op();
  printk("checkpoint: write failed\n");
  return -1;
}

// sys_restore(const char *path)
//
// Reads a snapshot and replaces the calling process with it.
// On success, execution resumes at the saved epc and the restored
// process sees checkpoint() return 0 (we set a0 = 0 before returning).
// On failure, the process is killed if its address space was already torn down.
uint64
sys_restore(void)
{
  char path[MAXPATH];
  struct proc *p = myproc();
  struct inode *ip;
  uint off = 0;
  struct xv6_chk_header hdr;
  struct trapframe saved_tf;
  struct xv6_chk_page pg;
  uint64 i;
  char *mem;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  ip = namei(path);
  if (!ip) {
    end_op();
    printk("restore: file not found: %s\n", path);
    return -1;
  }
  ilock(ip);

  if (chk_read(ip, &off, &hdr, sizeof(hdr)) < 0) {
    printk("restore: failed to read header\n");
    goto err;
  }

  if (hdr.magic != XV6_CHK_MAGIC) {
    printk("restore: not a valid checkpoint file (bad magic)\n");
    goto err;
  }

  if (hdr.version != XV6_CHK_VERSION) {
    printk("restore: unsupported snapshot version %d\n", hdr.version);
    goto err;
  }

  if (chk_read(ip, &off, &saved_tf, sizeof(saved_tf)) < 0) {
    printk("restore: failed to read trapframe\n");
    goto err;
  }

  // Free the current address space before rebuilding it from the snapshot.
  if (p->sz > 0)
    uvmunmap(p->pagetable, 0, p->sz / PGSIZE, 1);
  p->sz = 0;

  // Allocate a fresh physical page for each saved page and map it in.
  for (i = 0; i < hdr.num_pages; i++) {
    if (chk_read(ip, &off, &pg, sizeof(pg)) < 0) {
      printk("restore: failed reading page %lld\n", i);
      goto err_no_as;
    }

    mem = kalloc();
    if (!mem) {
      printk("restore: out of memory at page %lld\n", i);
      goto err_no_as;
    }
    memmove(mem, pg.data, PGSIZE);

    if (mappages(p->pagetable, pg.va, PGSIZE, (uint64)mem,
                 PTE_R | PTE_W | PTE_X | PTE_U) < 0) {
      kfree(mem);
      goto err_no_as;
    }
  }

  p->sz = hdr.sz;

  // Restore registers but keep the live kernel-side trapframe fields.
  // Those four fields belong to the current kernel execution context,
  // not the saved user process, so we must not overwrite them.
  uint64 k_satp   = p->trapframe->kernel_satp;
  uint64 k_sp     = p->trapframe->kernel_sp;
  uint64 k_trap   = p->trapframe->kernel_trap;
  uint64 k_hartid = p->trapframe->kernel_hartid;

  memmove(p->trapframe, &saved_tf, sizeof(struct trapframe));

  p->trapframe->kernel_satp   = k_satp;
  p->trapframe->kernel_sp     = k_sp;
  p->trapframe->kernel_trap   = k_trap;
  p->trapframe->kernel_hartid = k_hartid;

  // Make checkpoint() appear to return 0 in the restored process.
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
  // The address space is gone so we can't safely continue.
  iunlockput(ip);
  end_op();
  printk("restore: fatal error, killing process\n");
  p->killed = 1;
  return -1;
}
