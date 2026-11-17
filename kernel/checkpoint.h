// kernel/checkpoint.h
// On-disk format for xv6 process snapshots (checkpoint/restore).
//
// A snapshot file looks like this on disk:
//   [ xv6_chk_header  ]        fixed-size header
//   [ struct trapframe ]        saved user register state
//   [ xv6_chk_page x N ]       one entry per mapped user page
//
// Each page entry is stored sequentially on disk as:
//   8 bytes: virtual address (page-aligned)
//   4096 bytes: raw page content
// We avoid a 4KB struct definition here so nothing large is ever placed
// on the kernel stack, which is limited to a single 4096-byte page.

#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include "types.h"

#define XV6_CHK_MAGIC   0x43365658   // Magic identifier for XV6 checkpoint files
#define XV6_CHK_VERSION 1

// Saved state for an open file descriptor slot.
struct xv6_chk_fd {
  int  valid;
  int  type;
  int  readable;
  int  writable;
  uint off;
};

// Fixed-size header placed at offset 0 in every snapshot file.
struct xv6_chk_header {
  uint32 magic;
  uint32 version;
  int    pid;
  char   name[16];
  uint64 sz;
  uint64 num_pages;
  struct xv6_chk_fd fds[16];   // File descriptor table entries (NOFILE = 16)
};

#endif // CHECKPOINT_H
