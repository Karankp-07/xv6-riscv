// kernel/checkpoint.h
// On-disk format for xv6 process snapshots (checkpoint/restore).
//
// A snapshot file looks like this on disk:
//   [ xv6_chk_header  ]        fixed-size header
//   [ struct trapframe ]        saved user register state
//   [ xv6_chk_page x N ]       one entry per mapped user page

#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include "types.h"

// Magic bytes that identify a valid XV6 checkpoint file ("XV6C" in ASCII).
#define XV6_CHK_MAGIC   0x43365658
#define XV6_CHK_VERSION 1

// Metadata for a single open file descriptor saved in the snapshot.
// We save enough to describe the fd type, permissions, and offset.
// Full path recovery is left as a future improvement.
struct xv6_chk_fd {
  int  valid;       // 1 if this slot was open at checkpoint time
  int  type;        // FD_DEVICE or FD_INODE (from file.h)
  int  readable;
  int  writable;
  uint off;         // file offset at the time of checkpoint
  char path[128];   // best-effort inode path (empty for device fds)
};

// Fixed-size header at the start of every snapshot file.
struct xv6_chk_header {
  uint32 magic;          // XV6_CHK_MAGIC — sanity check on restore
  uint32 version;        // format version for forward compatibility
  int    pid;            // original PID (informational only)
  char   name[16];       // process name
  uint64 sz;             // process address-space size in bytes
  uint64 num_pages;      // how many xv6_chk_page records follow the trapframe
  struct xv6_chk_fd fds[16]; // open fds (NOFILE == 16)
};

// One saved user page. We store the virtual address alongside the raw
// content so restore can map it back to exactly the right VA.
struct xv6_chk_page {
  uint64 va;           // virtual address (page-aligned)
  char   data[4096];   // raw page bytes (PGSIZE == 4096)
};

#endif // CHECKPOINT_H
