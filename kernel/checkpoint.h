// kernel/checkpoint.h
// On-disk format for xv6 process snapshots (checkpoint/restore).
//
// A snapshot file looks like this on disk:
//   [ xv6_chk_header  ]        fixed-size header
//   [ struct trapframe ]        saved user register state
//   [ xv6_chk_page x N ]       one entry per mapped user page
//
// Each xv6_chk_page is stored as:
//   8 bytes  – virtual address (page-aligned)
//   4096 bytes – raw page content
// We don't use a struct for this in the kernel to avoid putting 4KB on the
// kernel stack (which is only one page).

#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include "types.h"

#define XV6_CHK_MAGIC   0x43365658   // "XV6C"
#define XV6_CHK_VERSION 1

// Open file descriptor state saved per slot.
// path is left empty for now — full path recovery is a future improvement.
struct xv6_chk_fd {
  int  valid;
  int  type;
  int  readable;
  int  writable;
  uint off;
};

// Header written at byte 0 of every snapshot file.
struct xv6_chk_header {
  uint32 magic;
  uint32 version;
  int    pid;
  char   name[16];
  uint64 sz;
  uint64 num_pages;
  struct xv6_chk_fd fds[16];   // one slot per NOFILE (16)
};

#endif // CHECKPOINT_H
