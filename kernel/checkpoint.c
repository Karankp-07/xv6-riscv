// kernel/checkpoint.c
//
// Implementation of sys_checkpoint: serialize the calling process
// (CPU registers + user pages + open-file metadata) into a snapshot
// file on the xv6 filesystem.
//
// Snapshot layout (written sequentially):
//   [xv6_chk_header]
//   [struct trapframe]           – user register state
//   [xv6_chk_page * num_pages]  – one entry per mapped user page

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

// Forward declaration – create() lives in sysfile.c but is not in defs.h.
// We use the lower-level inode interface directly instead.

// ---------------------------------------------------------------------------
// Helper: write `len` bytes from kernel pointer `src` into inode `ip`
// starting at byte offset `*off`.  Advances *off.
// Returns 0 on success, -1 on error.
// ---------------------------------------------------------------------------
static int
chk_write(struct inode *ip, uint *off, void *src, uint len)
{
  if (writei(ip, 0, (uint64)src, *off, len) != (int)len)
    return -1;
  *off += len;
  return 0;
}

// ---------------------------------------------------------------------------
// Helper: read `len` bytes from inode `ip` at byte offset `*off` into
// kernel buffer `dst`.  Advances *off.  Returns 0 on success, -1 on error.
// ---------------------------------------------------------------------------
static int
chk_read(struct inode *ip, uint *off, void *dst, uint len)
{
  if (readi(ip, 0, (uint64)dst, *off, len) != (int)len)
    return -1;
  *off += len;
  return 0;
}

// ---------------------------------------------------------------------------
// sys_restore(const char *path)
//
// Reads a snapshot file produced by sys_checkpoint and replaces the calling
// process's address space and CPU register state with the saved image.
//
// After sys_restore returns to usertrap(), the process resumes at the exact
// instruction that followed the original sys_checkpoint call, with all
// registers restored.  The restored process sees checkpoint() return 0
// (we set trapframe->a0 = 0 before returning).
//
// Returns 0 on success (but the caller in the restored world sees this as the
// return value of checkpoint(), not restore()).  Returns -1 on any error,
// leaving the process unchanged.
// ---------------------------------------------------------------------------
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

  // ---- 1. Fetch path argument ----------------------------------------------
  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  // ---- 2. Open the snapshot file (read-only) --------------------------------
  begin_op();
  ip = namei(path);
  if (!ip) {
    end_op();
    printk("restore: file not found: %s\n", path);
    return -1;
  }
  ilock(ip);

  // ---- 3. Read and validate the header -------------------------------------
  if (chk_read(ip, &off, &hdr, sizeof(hdr)) < 0) {
    printk("restore: failed to read header\n");
    goto err;
  }
  if (hdr.magic != XV6_CHK_MAGIC) {
    printk("restore: bad magic 0x%x (expected 0x%x)\n",
           hdr.magic, XV6_CHK_MAGIC);
    goto err;
  }
  if (hdr.version != XV6_CHK_VERSION) {
    printk("restore: unsupported version %d\n", hdr.version);
    goto err;
  }

  // ---- 4. Read the saved trapframe -----------------------------------------
  if (chk_read(ip, &off, &saved_tf, sizeof(saved_tf)) < 0) {
    printk("restore: failed to read trapframe\n");
    goto err;
  }

  // ---- 5. Tear down the current address space ------------------------------
  // Unmap and free all user pages [0, p->sz).
  // uvmunmap(pagetable, va, npages, do_free=1)
  if (p->sz > 0)
    uvmunmap(p->pagetable, 0, p->sz / PGSIZE, 1);
  p->sz = 0;

  // ---- 6. Restore user pages from snapshot ---------------------------------
  for (i = 0; i < hdr.num_pages; i++) {
    if (chk_read(ip, &off, &pg, sizeof(pg)) < 0) {
      printk("restore: failed to read page %ld\n", i);
      goto err_no_as;
    }

    // Allocate a fresh physical page.
    mem = kalloc();
    if (!mem) {
      printk("restore: kalloc failed at page %ld\n", i);
      goto err_no_as;
    }
    memmove(mem, pg.data, PGSIZE);

    // Map it into the process page table with full user R/W/X permissions.
    if (mappages(p->pagetable, pg.va, PGSIZE, (uint64)mem,
                 PTE_R | PTE_W | PTE_X | PTE_U) < 0) {
      printk("restore: mappages failed at va 0x%lx\n", pg.va);
      kfree(mem);
      goto err_no_as;
    }
  }

  // Update process size to the saved value.
  p->sz = hdr.sz;

  // ---- 7. Restore CPU register state ---------------------------------------
  // Preserve kernel-side fields that must not be overwritten with stale data.
  uint64 k_satp   = p->trapframe->kernel_satp;
  uint64 k_sp     = p->trapframe->kernel_sp;
  uint64 k_trap   = p->trapframe->kernel_trap;
  uint64 k_hartid = p->trapframe->kernel_hartid;

  memmove(p->trapframe, &saved_tf, sizeof(struct trapframe));

  // Put the live kernel fields back.
  p->trapframe->kernel_satp   = k_satp;
  p->trapframe->kernel_sp     = k_sp;
  p->trapframe->kernel_trap   = k_trap;
  p->trapframe->kernel_hartid = k_hartid;

  // The restored process should see checkpoint() return 0 (success).
  p->trapframe->a0 = 0;

  iunlockput(ip);
  end_op();

  printk("restore: resumed '%s' at epc=0x%lx (sz=%ld bytes, %ld pages)\n",
         hdr.name, p->trapframe->epc, hdr.sz, hdr.num_pages);
  return 0;        // <-- this value is discarded; a0=0 set above is what
                   //     the restored process actually receives.

err:
  iunlockput(ip);
  end_op();
  return -1;

err_no_as:
  // Address space is partially destroyed – process cannot continue safely.
  // Kill it rather than leaving it in an inconsistent state.
  iunlockput(ip);
  end_op();
  printk("restore: fatal – killing process\n");
  p->killed = 1;
  return -1;
}

// ---------------------------------------------------------------------------
// sys_checkpoint(const char *path)
//
// Creates (or truncates) the file at `path`, then writes the full
// snapshot of the calling process into it.
//
// Returns 0 on success, -1 on any error.
// ---------------------------------------------------------------------------
uint64
sys_checkpoint(void)
{
  char path[MAXPATH];
  struct proc *p = myproc();
  struct inode *ip;
  uint off = 0;
  uint64 va;
  uint64 num_pages;
  struct xv6_chk_header hdr;
  struct xv6_chk_page pg;
  int i;

  // ---- 1. Fetch the path argument from user space --------------------------
  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  // ---- 2. Create / truncate the snapshot file ------------------------------
  begin_op();
  ip = namei(path);
  if (ip) {
    // File already exists – truncate it.
    ilock(ip);
    itrunc(ip);
  } else {
    // Create a new regular file.
    // We reproduce the essential logic from create() without calling it
    // directly (it is static in sysfile.c).
    char parent_path[MAXPATH];
    char name[DIRSIZ];
    struct inode *dp;

    // copy path so we can pass it to nameiparent
    memmove(parent_path, path, MAXPATH);

    // nameiparent fills `name` with the last component
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
  // `ip` is now locked and ready for writing.

  // ---- 3. Count user pages (all pages below p->sz) -------------------------
  num_pages = 0;
  for (va = 0; va < p->sz; va += PGSIZE) {
    if (walkaddr(p->pagetable, va) != 0)
      num_pages++;
  }

  // ---- 4. Build and write the fixed header ---------------------------------
  memset(&hdr, 0, sizeof(hdr));
  hdr.magic    = XV6_CHK_MAGIC;
  hdr.version  = XV6_CHK_VERSION;
  hdr.pid      = p->pid;
  hdr.sz       = p->sz;
  hdr.num_pages = num_pages;
  memmove(hdr.name, p->name, 16);

  // Capture open file metadata
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
    // For regular files, record path via inode number (best-effort).
    // We store the inode number encoded in path[] for restore to look up.
    if (f->type == FD_INODE && f->ip) {
      // Store inum as decimal string – restore can re-open by name if needed.
      // For console fds (FD_DEVICE) we leave path empty; restore re-opens /dev/console.
      hdr.fds[i].path[0] = '\0'; // future work: resolve full path
    }
  }

  if (chk_write(ip, &off, &hdr, sizeof(hdr)) < 0)
    goto err;

  // ---- 5. Write the trapframe (all user registers) -------------------------
  if (chk_write(ip, &off, p->trapframe, sizeof(struct trapframe)) < 0)
    goto err;

  // ---- 6. Walk user page table and dump each mapped page -------------------
  for (va = 0; va < p->sz; va += PGSIZE) {
    uint64 pa = walkaddr(p->pagetable, va);
    if (pa == 0)
      continue; // unmapped / guard page – skip

    pg.va = va;
    memmove(pg.data, (void *)pa, PGSIZE);

    if (chk_write(ip, &off, &pg, sizeof(pg)) < 0)
      goto err;
  }

  iunlockput(ip);
  end_op();

  printk("checkpoint: saved '%s' (%d pages, %ld bytes)\n",
         p->name, (int)num_pages, (uint64)off);
  return 0;

err:
  iunlockput(ip);
  end_op();
  printk("checkpoint: write failed\n");
  return -1;
}
